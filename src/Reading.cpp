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
#include "Api.h"
#include "Package.h"
#include "LetterDB.h"
#include "Letters.h"
#include "NpcToNpc.h"
#include "Session.h"
#include "LlmJson.h"
#include "SkyrimNet.h"

#include <nlohmann/json.hpp>
#include <thread>

namespace PhysicalLetters::Reading {

    namespace {
        using json = nlohmann::json;
        using namespace LlmJson;

        constexpr auto kPrompt = "physical_letters\\read_letter";
        constexpr int kMaxMemories = 8;
        // Earlier letters passed to the prompt; the template shows as many as it wants of
        // the newest (max_earlier_letters).
        constexpr std::size_t kMaxCorrespondence = 20;
        constexpr RE::FormID kPlayer = 0x14;
        constexpr float kDefaultImportance = 0.7f;

        // A letter action chosen (docs/API.md#actions): its name and parameter values, as strings.
        void ReadAction(const json& reading, Outcome& outcome)
        {
            const auto action = GetString(reading, "action");
            if (action.empty() || action == "none") return;
            outcome.action = action;
            if (!reading.contains("action_params") || !reading["action_params"].is_object()) return;
            for (const auto& [key, value] : reading["action_params"].items()) {
                outcome.actionValues.emplace_back(key, value.is_string() ? value.get<std::string>() : value.dump());
            }
        }

        // What came with it, for the event and the memory: only what reached the reader (docs/PACKAGES.md).
        std::string PackageSentence(const std::string& letterId)
        {
            const auto package = Package::Arrived(letterId);
            return package.empty() ? std::string{} : "With it came: " + package + ".";
        }

        // A letter already remembered: its reply and action from the reading kept in LetterDB, again, since the save
        // may predate what they led to; none if it was never recorded (docs/READING.md#reading-it-again).
        Outcome Remembered(const Letter& letter)
        {
            SKSE::log::info("[Reading] {} already remembers letter {}", letter.recipientName, letter.id);
            const auto reading = json::parse(LetterDB::GetSingleton()->GetReading(letter.id), nullptr, false);
            if (!reading.is_object()) return { Result::kRead, {} };
            Outcome outcome{ Result::kRead, GetBool(reading, "reply") ? GetString(reading, "reply_text") : std::string{} };
            ReadAction(reading, outcome);
            return outcome;
        }

        // That the reader received it, with its text, perceived by them alone: their recent history then brings it up in
        // conversation, where a memory waits to be searched for (docs/READING.md#the-event).  Blocks.
        void Announce(const Letter& letter, RE::FormID readerFormId, const std::string& readerName)
        {
            const auto sender = Letters::SenderOf(letter);
            std::string line = std::format("{} received {}. It reads:\n{}", readerName,
                                           sender.name.empty() ? std::string{ "an unsigned letter" } : "a letter from " + sender.name,
                                           letter.body);
            if (const auto forged = Letters::ForgerySentence(letter); !forged.empty()) line += "\n\n" + forged;
            if (const auto blood = Letters::BloodSentence(letter); !blood.empty()) line += "\n\n" + blood;
            if (const auto package = PackageSentence(letter.id); !package.empty()) line += "\n\n" + package;
            const int id = SkyrimNet::RegisterEvent("persistent_generic", line, readerFormId, 0, { readerFormId });
            if (id > 0) SKSE::log::info("[Reading] {} received letter {}: event {}", readerName, letter.id, id);
            else SKSE::log::warn("[Reading] SkyrimNet didn't store the event that {} received letter {}", readerName, letter.id);
        }

        // Its own thread (AddMemory blocks while the memory is embedded): the memory, then
        // the reading kept in LetterDB.
        Outcome Store(const Letter& letter, const std::string& deliveryId, RE::FormID recipientFormId, const std::string& readerName,
                      bool canReply, bool announce, std::uint32_t generation, const std::string& response)
        {
            const auto reading = ParseResponse(response);
            if (!reading.is_object()) {
                SKSE::log::error("[Reading] {}'s reading of letter {} isn't valid JSON. Response: {}", readerName,
                                 letter.id, response.substr(0, 500));
                return { Result::kRetry };
            }
            const auto memory = GetString(reading, "memory");
            if (memory.empty()) {
                SKSE::log::error("[Reading] {}'s reading of letter {} has no memory. Response: {}", readerName,
                                 letter.id, response.substr(0, 500));
                return { Result::kRetry };
            }
            if (generation != Session::Generation()) {
                // The new session reads the letter again if its save still owes the reading.
                SKSE::log::info("[Reading] A load happened while {} read letter {}: not stored", readerName, letter.id);
                return { Result::kRetry };
            }
            // An earlier attempt that timed out may have finished after all.
            const auto tag = DeliveryTag(deliveryId);
            if (SkyrimNet::HasMemoryWithTag(recipientFormId, tag)) return Remembered(letter);

            const float importance = std::clamp(GetFloat(reading, "importance", kDefaultImportance), 0.0f, 1.0f);
            const auto emotion = GetString(reading, "emotion");
            // A thread between NPCs at its limit: a reply would be dropped, so none is kept.
            const bool replies = canReply && GetBool(reading, "reply");
            const auto replyText = GetString(reading, "reply_text");

            // The summary first: SkyrimNet embeds only the start of a long memory, so the
            // summary is what semantic search matches; the exact letters follow for recall.
            const auto sender = Letters::SenderOf(letter);
            std::string content = sender.name.empty() ? std::format("{}\n\nThe unsigned letter:\n{}", memory, letter.body)
                                                      : std::format("{}\n\nThe letter from {}:\n{}", memory, sender.name, letter.body);
            if (const auto forged = Letters::ForgerySentence(letter); !forged.empty()) content += "\n\n" + forged;
            if (const auto blood = Letters::BloodSentence(letter); !blood.empty()) content += "\n\n" + blood;
            if (const auto package = PackageSentence(letter.id); !package.empty()) content += "\n\n" + package;
            if (replies && !replyText.empty()) content += std::format("\n\nMy reply:\n{}", replyText);

            const auto tags = json::array({ "physical_letters", "letter_received", LetterTag(letter.id), tag }).dump();
            // Whom it's from, to the reader: the player, an NPC, or the person a forgery names.  A signature or an
            // anonymous letter has nobody (never the player, who wrote it: docs/FORGERY.md#reading-it).
            RE::FormID author = sender.uuid.empty() ? 0 : SkyrimNet::FormIdForUuid(sender.uuid);
            if (author == 0 && !sender.uuid.empty() && sender.uuid == letter.authorUuid) author = kPlayer;
            const int memoryId = SkyrimNet::AddMemory(recipientFormId, content, importance, "RELATIONSHIP", emotion, tags,
                                                      author ? std::format("[{}]", author) : std::string{ "[]" });
            if (memoryId == 0) {
                SKSE::log::error("[Reading] SkyrimNet didn't store {}'s memory of letter {}", readerName, letter.id);
                return { Result::kRetry };
            }
            SKSE::log::info("[Reading] {} read letter {} ({}): memory {}: {}", readerName, letter.id, emotion,
                            memoryId, memory);

            // AddMemory blocks for a while: LetterDB may belong to another session by now.
            if (generation == Session::Generation()) {
                LetterDB::GetSingleton()->SetReading(letter.id, reading.dump(), memoryId);
                if (announce) Announce(letter, recipientFormId, readerName);
            } else {
                SKSE::log::info("[Reading] A load happened while memory {} was stored: LetterDB not updated", memoryId);
            }

            Outcome outcome{ Result::kRead };
            ReadAction(reading, outcome);
            if (replies && !replyText.empty()) {
                SKSE::log::info("[Reading] {} replies: {}", readerName, replyText);
                outcome.reply = replyText;
                return outcome;
            }
            SKSE::log::info("[Reading] {} doesn't reply", readerName);
            return outcome;
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

    // The rules: docs/READING.md#the-prompt.  A memory query per letter.
    json Correspondence(const std::string& readerUuid, const std::string& otherUuid, RE::FormID readerFormId, double now,
                        const std::string& skipId, bool skipRemembered)
    {
        std::vector<std::string> remembered;
        if (skipRemembered) remembered.push_back(skipId);
        auto entries = json::array();
        for (const auto& earlier : LetterDB::GetSingleton()->Between(readerUuid, otherUuid)) {
            if (earlier.id == skipId || earlier.forgery == kForgerySeen) continue;
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
            entries.push_back({ { "from", Letters::SenderOf(earlier).name },
                                { "to", earlier.recipientName },
                                { "days_ago", static_cast<int>(std::max(0.0, now - earlier.writtenAt)) },
                                { "body", earlier.body } });
        }
        if (entries.size() > kMaxCorrespondence) entries.erase(entries.begin(), entries.end() - kMaxCorrespondence);
        return entries;
    }

    void Read(const std::string& letterId, const std::string& deliveryId, RE::FormID readerFormId, bool canReply, bool announce,
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
        std::thread([letter = std::move(*letter), deliveryId, readerFormId, canReply, announce, generation, now,
                     done = std::move(done)]() {
            try {
                const std::string& readerName = letter.recipientName;
                // This delivery again after loading an older save: after Keep the memory is
                // still there, after Clear it was deleted with the rest of that history.
                if (SkyrimNet::HasMemoryWithTag(readerFormId, DeliveryTag(deliveryId))) {
                    Report(done, Remembered(letter));
                    return;
                }

                // Whom it's from, to the reader (docs/FORGERY.md#reading-it).  Other memories of them: the letters
                // themselves are in the correspondence.
                const auto sender = Letters::SenderOf(letter);
                auto memories = json::array();
                const auto found = json::parse(SkyrimNet::Memories(readerFormId, kMaxMemories, sender.name, "physical_letters"),
                                               nullptr, false);
                if (found.is_array()) {
                    for (const auto& m : found) {
                        if (m.contains("content") && m["content"].is_string()) memories.push_back(m["content"]);
                    }
                }

                // The same letter from an earlier delivery (sent again).
                const bool readBefore = SkyrimNet::HasMemoryWithTag(readerFormId, LetterTag(letter.id));

                const auto uuid = SkyrimNet::UuidNumber(letter.recipientUuid);
                json context = {
                    { "npc", { { "UUID", uuid }, { "name", readerName } } },
                    { "letter",
                      { { "author", sender.name },
                        { "recipient", letter.recipientName },
                        { "body", letter.body },
                        { "read_before", readBefore },
                        { "can_reply", canReply } } },
                    { "memories", memories },
                    { "actions", Api::ActionsForPrompt(!NpcToNpc::IsNpcLetter(letter)) },  // the player's letter, or an NPC's
                };
                // Written in blood: "all" or "part" (with the passages), else "".
                const auto blood = Letters::BloodOf(letter);
                context["letter"]["blood"] = blood.amount == Letters::Blood::kAll    ? "all"
                                             : blood.amount == Letters::Blood::kPart ? "part"
                                                                                     : "";
                context["letter"]["blood_text"] = blood.passages;
                context["letter"]["package"] = Package::Arrived(letter.id);  // what reached them (docs/PACKAGES.md)
                // Whom it's from, and to whom: their public profile for the prompt (docs/READING.md#the-prompt); a
                // forgery seen through says so; a signature or an anonymous letter has nobody behind it.
                context["letter"]["named"] = !sender.uuid.empty();
                context["letter"]["anonymous"] = letter.anonymous;
                context["letter"]["forged"] = letter.forgery == kForgerySeen;
                context["letter"]["author_UUID"] = sender.uuid.empty() ? 0 : SkyrimNet::UuidNumber(sender.uuid);
                context["letter"]["recipient_UUID"] = SkyrimNet::UuidNumber(letter.recipientUuid);
                context["correspondence"] = sender.uuid.empty() ? json::array()
                                                                : Correspondence(letter.recipientUuid, sender.uuid, readerFormId, now,
                                                                                 letter.id, readBefore);

                const bool queued = SkyrimNet::SendPrompt(
                    kPrompt, context.dump(-1, ' ', false, json::error_handler_t::replace),
                    [letter, deliveryId, readerFormId, canReply, announce, generation, done](std::string response, bool success) {
                        if (!success) {
                            SKSE::log::error("[Reading] The LLM call for letter {} failed: {}", letter.id, response);
                            Report(done, { Result::kRetry });
                            return;
                        }
                        std::thread([letter, deliveryId, readerFormId, canReply, announce, generation, done,
                                     response = std::move(response)]() {
                            Outcome outcome;
                            try {
                                outcome = Store(letter, deliveryId, readerFormId, letter.recipientName, canReply, announce,
                                                generation, response);
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
                    SKSE::log::info("[Reading] {} is reading letter {}", readerName, letter.id);
                }
            } catch (const std::exception& e) {
                SKSE::log::error("[Reading] Reading letter {} failed: {}", letter.id, e.what());
                Report(done, { Result::kRetry });
            }
        }).detach();
    }

} // namespace PhysicalLetters::Reading
