/*
 * Physical Letters - a Skyrim SKSE plugin for writing letters to NPCs,
 * having them delivered, and receiving their replies through SkyrimNet.
 * Copyright (C) 2026 Zevick
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

#include "Reading.h"
#include "LetterDB.h"
#include "Session.h"
#include "SkyrimNet.h"

#include <nlohmann/json.hpp>
#include <thread>

namespace PhysicalLetters::Reading {

    namespace {
        using json = nlohmann::json;

        constexpr auto kPrompt = "physical_letters_read_letter";
        constexpr int kMaxMemories = 8;
        // Earlier letters passed to the prompt; the template shows as many as it wants of
        // the newest (max_earlier_letters).
        constexpr std::size_t kMaxCorrespondence = 20;
        constexpr RE::FormID kPlayer = 0x14;
        constexpr float kDefaultImportance = 0.7f;

        // Fixes the two slips LLMs make most in otherwise valid JSON: a trailing comma
        // before } or ] (seen in game), and raw line breaks or tabs inside a string.
        std::string RepairJson(std::string_view text)
        {
            std::string out;
            out.reserve(text.size());
            bool inString = false, escaped = false;
            for (std::size_t i = 0; i < text.size(); ++i) {
                const char c = text[i];
                if (inString) {
                    if (escaped) escaped = false;
                    else if (c == '\\') escaped = true;
                    else if (c == '"') inString = false;
                    if (c == '\n') { out += "\\n"; continue; }
                    if (c == '\r') continue;
                    if (c == '\t') { out += "\\t"; continue; }
                    out += c;
                    continue;
                }
                if (c == '"') inString = true;
                if (c == ',') {
                    auto next = text.find_first_not_of(" \t\r\n", i + 1);
                    if (next != std::string_view::npos && (text[next] == '}' || text[next] == ']')) continue;
                }
                out += c;
            }
            return out;
        }

        // The LLM's JSON object, without any text or code fence around it; discarded
        // (not an object) if it can't be read even after RepairJson.
        json ParseResponse(const std::string& response)
        {
            const auto first = response.find('{');
            const auto last = response.rfind('}');
            if (first == std::string::npos || last == std::string::npos || last < first) return json{};
            const auto text = std::string_view{ response }.substr(first, last - first + 1);
            auto parsed = json::parse(text, nullptr, false);
            if (parsed.is_discarded()) parsed = json::parse(RepairJson(text), nullptr, false);
            return parsed;
        }

        // Field readers that accept what LLMs write instead of the type asked for (null,
        // "0.8", "yes").  json::value would throw on a present key of another type.
        std::string GetString(const json& j, const char* key)
        {
            const auto it = j.find(key);
            return it != j.end() && it->is_string() ? it->get<std::string>() : std::string{};
        }

        float GetFloat(const json& j, const char* key, float fallback)
        {
            const auto it = j.find(key);
            if (it == j.end()) return fallback;
            if (it->is_number()) return it->get<float>();
            if (it->is_string()) {
                const auto& text = it->get_ref<const std::string&>();
                float value = fallback;
                std::from_chars(text.data(), text.data() + text.size(), value);
                return value;
            }
            return fallback;
        }

        bool GetBool(const json& j, const char* key)
        {
            const auto it = j.find(key);
            if (it == j.end()) return false;
            if (it->is_boolean()) return it->get<bool>();
            if (it->is_number()) return it->get<double>() != 0.0;
            if (it->is_string()) {
                auto text = it->get<std::string>();
                std::ranges::transform(text, text.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
                return text == "true" || text == "yes";
            }
            return false;
        }

        // A letter the recipient already remembers: its reply, from the reading kept in
        // LetterDB (the save may predate the reply letter).  No reply if that reading was
        // never recorded (a load while it was stored).
        Outcome Remembered(const Letter& letter)
        {
            SKSE::log::info("[Reading] {} already remembers letter {}", letter.recipientName, letter.id);
            const auto reading = json::parse(LetterDB::GetSingleton()->GetReading(letter.id), nullptr, false);
            if (!reading.is_object()) return { Result::kRead, {} };
            return { Result::kRead, GetBool(reading, "reply") ? GetString(reading, "reply_text") : std::string{} };
        }

        // Its own thread (AddMemory blocks while the memory is embedded): the memory, then
        // the reading kept in LetterDB.
        Outcome Store(const Letter& letter, RE::FormID recipientFormId, std::uint32_t generation, const std::string& response)
        {
            const auto reading = ParseResponse(response);
            if (!reading.is_object()) {
                SKSE::log::error("[Reading] {}'s reading of letter {} isn't valid JSON. Response: {}", letter.recipientName,
                                 letter.id, response.substr(0, 500));
                return { Result::kRetry };
            }
            const auto memory = GetString(reading, "memory");
            if (memory.empty()) {
                SKSE::log::error("[Reading] {}'s reading of letter {} has no memory. Response: {}", letter.recipientName,
                                 letter.id, response.substr(0, 500));
                return { Result::kRetry };
            }
            if (generation != Session::Generation()) {
                // The new session reads the letter again if its save still owes the reading.
                SKSE::log::info("[Reading] A load happened while {} read letter {}: not stored", letter.recipientName, letter.id);
                return { Result::kRetry };
            }
            // An earlier attempt that timed out may have finished after all.
            const auto tag = LetterTag(letter.id);
            if (SkyrimNet::HasMemoryWithTag(recipientFormId, tag)) return Remembered(letter);

            const float importance = std::clamp(GetFloat(reading, "importance", kDefaultImportance), 0.0f, 1.0f);
            const auto emotion = GetString(reading, "emotion");
            const bool replies = GetBool(reading, "reply");
            const auto replyText = GetString(reading, "reply_text");

            // The summary first: SkyrimNet embeds only the start of a long memory, so the
            // summary is what semantic search matches; the exact letters follow for recall.
            std::string content = std::format("{}\n\nThe letter from {}:\n{}", memory, letter.authorName, letter.body);
            if (replies && !replyText.empty()) content += std::format("\n\nMy reply:\n{}", replyText);

            const auto tags = json::array({ "physical_letters", "letter_received", tag }).dump();
            const int memoryId = SkyrimNet::AddMemory(recipientFormId, content, importance, "RELATIONSHIP", emotion, tags,
                                                      std::format("[{}]", kPlayer));
            if (memoryId == 0) {
                SKSE::log::error("[Reading] SkyrimNet didn't store {}'s memory of letter {}", letter.recipientName, letter.id);
                return { Result::kRetry };
            }
            SKSE::log::info("[Reading] {} read letter {} ({}): memory {}: {}", letter.recipientName, letter.id, emotion,
                            memoryId, memory);

            // AddMemory blocks for a while: LetterDB may belong to another session by now.
            if (generation == Session::Generation()) {
                LetterDB::GetSingleton()->SetReading(letter.id, reading.dump(), memoryId);
            } else {
                SKSE::log::info("[Reading] A load happened while memory {} was stored: LetterDB not updated", memoryId);
            }

            if (replies && !replyText.empty()) {
                SKSE::log::info("[Reading] {} replies: {}", letter.recipientName, replyText);
                return { Result::kRead, replyText };
            }
            SKSE::log::info("[Reading] {} doesn't reply", letter.recipientName);
            return { Result::kRead };
        }

        // The earlier letters between the reader and the writer that the reader knows of in
        // this timeline, oldest first, at most kMaxCorrespondence.  SkyrimNet's memory
        // decides, as everywhere: a letter to the reader counts if they remember it, and
        // their reply counts if they remember the letter it answers (that memory holds the
        // reply).  Letters from timelines the player left, or still on their way, drop out.
        // Blocks (a memory query per letter to the reader): not on the game thread.
        json Correspondence(const Letter& current, RE::FormID readerFormId, double now)
        {
            std::vector<std::string> remembered;
            auto entries = json::array();
            for (const auto& earlier : LetterDB::GetSingleton()->Between(current.recipientUuid, current.authorUuid)) {
                if (earlier.id == current.id) continue;
                bool known = false;
                if (earlier.recipientUuid == current.recipientUuid) {
                    known = SkyrimNet::HasMemoryWithTag(readerFormId, LetterTag(earlier.id));
                    if (known) remembered.push_back(earlier.id);
                } else {
                    known = std::ranges::find(remembered, earlier.inReplyTo) != remembered.end();
                }
                if (!known) continue;
                entries.push_back({ { "from", earlier.authorName },
                                    { "to", earlier.recipientName },
                                    { "days_ago", static_cast<int>(std::max(0.0, now - earlier.writtenAt)) },
                                    { "body", earlier.body } });
            }
            if (entries.size() > kMaxCorrespondence) entries.erase(entries.begin(), entries.end() - kMaxCorrespondence);
            return entries;
        }

        void Report(const std::function<void(Outcome)>& done, Outcome outcome)
        {
            SKSE::GetTaskInterface()->AddTask([done, outcome = std::move(outcome)]() {
                try {
                    done(outcome);
                } catch (const std::exception& e) {
                    SKSE::log::error("[Reading] Handling a reading's result failed: {}", e.what());
                }
            });
        }
    }

    std::string LetterTag(const std::string& letterId)
    {
        return "physical_letters_letter:" + letterId;
    }

    void Read(const std::string& letterId, RE::FormID recipientFormId, std::function<void(Outcome)> done)
    {
        auto letter = LetterDB::GetSingleton()->Get(letterId);
        if (!letter) {
            SKSE::log::error("[Reading] Letter {} isn't in LetterDB: it can't be read", letterId);
            Report(done, { Result::kAbandon });
            return;
        }

        const auto generation = Session::Generation();
        const double now = RE::Calendar::GetSingleton()->GetDaysPassed();
        // Memory queries and the LLM call block: off the game thread.
        std::thread([letter = std::move(*letter), recipientFormId, generation, now, done = std::move(done)]() {
            try {
                // Delivered again after loading an older save: after Keep the memory is
                // still there, after Clear it was deleted with the rest of that history.
                if (SkyrimNet::HasMemoryWithTag(recipientFormId, LetterTag(letter.id))) {
                    Report(done, Remembered(letter));
                    return;
                }

                // Other memories of the writer: the letters themselves are in the correspondence.
                auto memories = json::array();
                const auto found = json::parse(
                    SkyrimNet::Memories(recipientFormId, kMaxMemories, letter.authorName, "physical_letters"), nullptr, false);
                if (found.is_array()) {
                    for (const auto& m : found) {
                        if (m.contains("text") && m["text"].is_string()) memories.push_back(m["text"]);
                    }
                }

                std::uint64_t uuid = 0;
                std::from_chars(letter.recipientUuid.data(), letter.recipientUuid.data() + letter.recipientUuid.size(), uuid);
                const json context = {
                    { "npc", { { "UUID", uuid }, { "name", letter.recipientName } } },
                    { "letter", { { "author", letter.authorName }, { "recipient", letter.recipientName }, { "body", letter.body } } },
                    { "correspondence", Correspondence(letter, recipientFormId, now) },
                    { "memories", memories },
                };

                const bool queued = SkyrimNet::SendPrompt(
                    kPrompt, context.dump(), [letter, recipientFormId, generation, done](std::string response, bool success) {
                        if (!success) {
                            SKSE::log::error("[Reading] The LLM call for letter {} failed: {}", letter.id, response);
                            Report(done, { Result::kRetry });
                            return;
                        }
                        std::thread([letter, recipientFormId, generation, done, response = std::move(response)]() {
                            Outcome outcome;
                            try {
                                outcome = Store(letter, recipientFormId, generation, response);
                            } catch (const std::exception& e) {
                                SKSE::log::error("[Reading] Storing the reading of letter {} failed: {}", letter.id, e.what());
                            }
                            Report(done, std::move(outcome));
                        }).detach();
                    });
                if (!queued) {
                    SKSE::log::error("[Reading] SkyrimNet refused the prompt for letter {}", letter.id);
                    Report(done, { Result::kRetry });
                } else {
                    SKSE::log::info("[Reading] {} is reading letter {}", letter.recipientName, letter.id);
                }
            } catch (const std::exception& e) {
                SKSE::log::error("[Reading] Reading letter {} failed: {}", letter.id, e.what());
                Report(done, { Result::kRetry });
            }
        }).detach();
    }

} // namespace PhysicalLetters::Reading
