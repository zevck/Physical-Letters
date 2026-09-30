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
#include "LlmJson.h"
#include "SkyrimNet.h"

#include <nlohmann/json.hpp>
#include <thread>

namespace PhysicalLetters::Reading {

    namespace {
        using json = nlohmann::json;
        using namespace LlmJson;

        constexpr auto kPrompt = "physical_letters_read_letter";
        constexpr int kMaxMemories = 8;
        // Earlier letters passed to the prompt; the template shows as many as it wants of
        // the newest (max_earlier_letters).
        constexpr std::size_t kMaxCorrespondence = 20;
        constexpr RE::FormID kPlayer = 0x14;
        constexpr float kDefaultImportance = 0.7f;

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
        Outcome Store(const Letter& letter, const std::string& deliveryId, RE::FormID recipientFormId, std::uint32_t generation,
                      const std::string& response)
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
            const auto tag = DeliveryTag(deliveryId);
            if (SkyrimNet::HasMemoryWithTag(recipientFormId, tag)) return Remembered(letter);

            const float importance = std::clamp(GetFloat(reading, "importance", kDefaultImportance), 0.0f, 1.0f);
            const auto emotion = GetString(reading, "emotion");
            const bool replies = GetBool(reading, "reply");
            const auto replyText = GetString(reading, "reply_text");

            // The summary first: SkyrimNet embeds only the start of a long memory, so the
            // summary is what semantic search matches; the exact letters follow for recall.
            std::string content = std::format("{}\n\nThe letter from {}:\n{}", memory, letter.authorName, letter.body);
            if (replies && !replyText.empty()) content += std::format("\n\nMy reply:\n{}", replyText);

            const auto tags = json::array({ "physical_letters", "letter_received", LetterTag(letter.id), tag }).dump();
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

    std::string DeliveryTag(const std::string& deliveryId)
    {
        return "physical_letters_delivery:" + deliveryId;
    }

    // SkyrimNet's memory decides, as everywhere.  A letter to the reader counts if they
    // remember it.  Their own letter counts if they remember writing it (a letter they wrote
    // first carries its tag on their memory of writing it), or, for a reply, if they remember
    // the letter it answers (that memory holds the reply).  Letters from timelines the player
    // left, or still on their way, drop out.  A memory query per letter.
    json Correspondence(const std::string& readerUuid, const std::string& otherUuid, RE::FormID readerFormId, double now,
                        const std::string& skipId, bool skipRemembered)
    {
        std::vector<std::string> remembered;
        if (skipRemembered) remembered.push_back(skipId);
        auto entries = json::array();
        for (const auto& earlier : LetterDB::GetSingleton()->Between(readerUuid, otherUuid)) {
            if (earlier.id == skipId) continue;
            bool known = false;
            if (earlier.recipientUuid == readerUuid) {
                known = SkyrimNet::HasMemoryWithTag(readerFormId, LetterTag(earlier.id));
                if (known) remembered.push_back(earlier.id);
            } else if (!earlier.inReplyTo.empty() && std::ranges::find(remembered, earlier.inReplyTo) != remembered.end()) {
                known = true;
            } else {
                known = SkyrimNet::HasMemoryWithTag(readerFormId, LetterTag(earlier.id));
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

    void Read(const std::string& letterId, const std::string& deliveryId, RE::FormID recipientFormId,
              std::function<void(Outcome)> done)
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
        std::thread([letter = std::move(*letter), deliveryId, recipientFormId, generation, now, done = std::move(done)]() {
            try {
                // This delivery again after loading an older save: after Keep the memory is
                // still there, after Clear it was deleted with the rest of that history.
                if (SkyrimNet::HasMemoryWithTag(recipientFormId, DeliveryTag(deliveryId))) {
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

                // The same letter from an earlier delivery (sent again).
                const bool readBefore = SkyrimNet::HasMemoryWithTag(recipientFormId, LetterTag(letter.id));

                std::uint64_t uuid = 0;
                std::from_chars(letter.recipientUuid.data(), letter.recipientUuid.data() + letter.recipientUuid.size(), uuid);
                const json context = {
                    { "npc", { { "UUID", uuid }, { "name", letter.recipientName } } },
                    { "letter",
                      { { "author", letter.authorName },
                        { "recipient", letter.recipientName },
                        { "body", letter.body },
                        { "read_before", readBefore } } },
                    { "correspondence", Correspondence(letter.recipientUuid, letter.authorUuid, recipientFormId, now, letter.id, readBefore) },
                    { "memories", memories },
                };

                const bool queued = SkyrimNet::SendPrompt(
                    kPrompt, context.dump(), [letter, deliveryId, recipientFormId, generation, done](std::string response, bool success) {
                        if (!success) {
                            SKSE::log::error("[Reading] The LLM call for letter {} failed: {}", letter.id, response);
                            Report(done, { Result::kRetry });
                            return;
                        }
                        std::thread([letter, deliveryId, recipientFormId, generation, done, response = std::move(response)]() {
                            Outcome outcome;
                            try {
                                outcome = Store(letter, deliveryId, recipientFormId, generation, response);
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
