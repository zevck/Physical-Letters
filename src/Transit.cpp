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

#include "Transit.h"
#include "CoSave.h"
#include "Config.h"
#include "Letters.h"
#include "Reading.h"
#include "Session.h"
#include "Courier.h"
#include "SkyrimNet.h"
#include "Strings.h"
#include "Travel.h"

namespace PhysicalLetters::Transit {

    namespace {
        using Clock = std::chrono::steady_clock;

        enum class State : std::uint8_t {
            kInTransit = 0,        // a letter to an NPC, on its way
            kAwaitingReading = 1,  // in the recipient's inventory, not yet read
            kToPlayer = 2,         // an NPC's letter to the player, on its way to the courier
        };

        struct Parcel {
            // Saved.
            std::string letterId;
            std::string deliveryId;  // this sending of the letter; a letter can be sent again
            std::string recipientUuid;
            std::string recipientName;
            double      dueAt = 0;  // game days
            State       state = State::kInTransit;

            // This session only: the reading in progress and its retries.
            bool              reading = false;
            std::uint64_t     attempt = 0;  // matches a reading's result to the attempt that started it
            int               failures = 0;
            Clock::time_point startedAt{};
            Clock::time_point retryAt{};
        };

        std::vector<Parcel> g_parcels;
        std::uint64_t g_lastAttempt = 0;
        // Letters already logged as waiting for an unreachable recipient, so the log isn't flooded.
        std::vector<std::string> g_reportedWaiting;

        // v1 (dev builds only) had no state: every parcel was in transit.  v2 had no delivery id.
        constexpr std::uint32_t kRecordVersion = 3;

        // A reading SkyrimNet never answers (it drops cancelled LLM tasks) counts as failed.
        constexpr auto kReadingTimeout = std::chrono::minutes(5);
        constexpr auto kFirstRetryDelay = std::chrono::seconds(30);  // doubled after each failure
        // After this many failures the letter waits for the next load.
        constexpr int kMaxFailures = 5;

        double Now()
        {
            auto* calendar = RE::Calendar::GetSingleton();
            return calendar ? calendar->GetDaysPassed() : 0.0;
        }

        void ReportWaiting(const Parcel& parcel, std::string_view why)
        {
            if (std::ranges::find(g_reportedWaiting, parcel.letterId) != g_reportedWaiting.end()) return;
            SKSE::log::info("[Transit] {} ({}): letter {} waits", parcel.recipientName, why, parcel.letterId);
            g_reportedWaiting.push_back(parcel.letterId);
        }

        // The actor with this UUID, or nullptr while they can't be reached: a persistent NPC
        // is in memory wherever they are, anyone else only while their cell is loaded.  The
        // FormID must map back to the same UUID: a runtime FormID can belong to another
        // actor by now.
        RE::Actor* FindActor(const std::string& uuid, std::string* why = nullptr)
        {
            const auto formId = SkyrimNet::FormIdForUuid(uuid);
            auto* actor = formId ? RE::TESForm::LookupByID<RE::Actor>(formId) : nullptr;
            if (!actor) {
                if (why) *why = "not loaded";
                return nullptr;
            }
            if (const auto mapped = SkyrimNet::UuidForFormId(formId); mapped != uuid) {
                if (why) *why = std::format("0x{:X} is now UUID {}", formId, mapped);
                return nullptr;
            }
            return actor;
        }

        RE::Actor* FindRecipient(const Parcel& parcel)
        {
            std::string why;
            auto* actor = FindActor(parcel.recipientUuid, &why);
            if (!actor) ReportWaiting(parcel, why);
            return actor;
        }

        enum class Delivery { kDelivered, kReturned, kWaiting, kLost, kToCourier };

        Delivery Deliver(const Parcel& parcel)
        {
            auto* book = RE::TESForm::LookupByID<RE::TESObjectBOOK>(Letters::FormFor(parcel.letterId));
            if (!book) {
                SKSE::log::error("[Transit] Letter {} has no form in this save: dropped from the queue", parcel.letterId);
                return Delivery::kLost;
            }
            if (parcel.state == State::kToPlayer) {
                // From here the courier and his container hold it; the engine saves both.
                if (!Courier::Give(book)) {
                    ReportWaiting(parcel, "the courier can't be reached");
                    return Delivery::kWaiting;
                }
                SKSE::log::info("[Transit] Letter {} to {} is with the courier", parcel.letterId, parcel.recipientName);
                return Delivery::kToCourier;
            }
            auto* recipient = FindRecipient(parcel);
            if (!recipient) return Delivery::kWaiting;

            if (recipient->IsDead()) {
                RE::PlayerCharacter::GetSingleton()->AddObjectToContainer(book, nullptr, 1, nullptr);
                RE::SendHUDMessage::ShowHUDMessage(Strings::LetterReturned(parcel.recipientName).c_str());
                SKSE::log::info("[Transit] {} is dead: letter {} returned to the player", parcel.recipientName, parcel.letterId);
                return Delivery::kReturned;
            }

            recipient->AddObjectToContainer(book, nullptr, 1, nullptr);
            LetterDB::GetSingleton()->MarkDelivered(parcel.letterId, Now());
            SKSE::log::info("[Transit] Delivered letter {} to {} (0x{:X})", parcel.letterId, parcel.recipientName,
                            recipient->GetFormID());
            // Testing aid while there is no other sign; remove once replies exist.
            RE::SendHUDMessage::ShowHUDMessage(Strings::LetterDelivered(parcel.recipientName).c_str());
            return Delivery::kDelivered;
        }

        // The recipient's reply to the player's letter: a new letter form, and a parcel that
        // goes to the courier once the NPC has written it and it has travelled.
        std::optional<Parcel> MakeReply(const Parcel& original, const std::string& text)
        {
            const auto sent = LetterDB::GetSingleton()->Get(original.letterId);
            auto* player = RE::PlayerCharacter::GetSingleton();
            const Letter reply{ .id = Letters::NewId(),
                                .authorUuid = original.recipientUuid,
                                .authorName = original.recipientName,
                                .recipientUuid = sent ? sent->authorUuid : SkyrimNet::UuidForFormId(0x14),
                                .recipientName = sent ? sent->authorName : std::string{ player->GetName() },
                                .body = text,
                                .writtenAt = Now(),
                                .inReplyTo = original.letterId };
            if (!Letters::Create(reply)) return std::nullopt;

            // Writing the reply, then the travel.
            const double hours = Config::GetSingleton()->Get(Config::kWritingHours) +
                                 Travel::Hours(FindActor(original.recipientUuid), player);
            SKSE::log::info("[Transit] {} replies to letter {} with letter {}, due at the courier in {:.1f} game hours",
                            reply.authorName, original.letterId, reply.id, hours);
            return Parcel{ .letterId = reply.id,
                           .deliveryId = Letters::NewId(),
                           .recipientUuid = reply.recipientUuid,
                           .recipientName = reply.recipientName,
                           .dueAt = Now() + hours / 24.0,
                           .state = State::kToPlayer };
        }

        void OnReadingDone(const std::string& deliveryId, std::uint64_t attempt, std::uint32_t generation,
                           const Reading::Outcome& outcome)
        {
            if (generation != Session::Generation()) return;  // the new session has its own queue
            const auto it = std::ranges::find_if(g_parcels, [&](const Parcel& p) { return p.deliveryId == deliveryId; });
            if (it == g_parcels.end() || it->attempt != attempt) return;  // a timed-out attempt answering late

            if (outcome.result == Reading::Result::kRetry) {
                it->reading = false;
                ++it->failures;
                if (it->failures >= kMaxFailures) {
                    SKSE::log::error("[Transit] Letter {} couldn't be read {} times: it's tried again after the next load",
                                     it->letterId, it->failures);
                } else {
                    it->retryAt = Clock::now() + kFirstRetryDelay * (1 << (it->failures - 1));
                }
                return;
            }
            // Read, or it never can be.  The reply is queued in the same step, so no save
            // holds a finished reading without its reply.
            std::optional<Parcel> reply;
            if (outcome.result == Reading::Result::kRead && !outcome.reply.empty()) reply = MakeReply(*it, outcome.reply);
            g_parcels.erase(it);
            if (reply) g_parcels.push_back(std::move(*reply));
        }

        void StartReading(Parcel& parcel)
        {
            auto* recipient = FindRecipient(parcel);
            if (!recipient) return;
            parcel.reading = true;
            parcel.attempt = ++g_lastAttempt;
            parcel.startedAt = Clock::now();
            Reading::Read(parcel.letterId, parcel.deliveryId, recipient->GetFormID(),
                          [deliveryId = parcel.deliveryId, attempt = parcel.attempt,
                           generation = Session::Generation()](const Reading::Outcome& outcome) {
                              OnReadingDone(deliveryId, attempt, generation, outcome);
                          });
        }
    }

    std::optional<double> Send(RE::TESObjectBOOK* book, const Letter& letter, RE::TESObjectREFR* holder)
    {
        if (!book || !holder || holder->GetInventoryCounts([book](RE::TESBoundObject& item) { return &item == book; })[book] <= 0) {
            return std::nullopt;
        }
        const double hours = Travel::Hours(holder, FindActor(letter.recipientUuid));
        holder->RemoveItem(book, 1, RE::ITEM_REMOVE_REASON::kRemove, nullptr, nullptr);
        g_parcels.push_back({ .letterId = letter.id,
                              .deliveryId = Letters::NewId(),
                              .recipientUuid = letter.recipientUuid,
                              .recipientName = letter.recipientName,
                              .dueAt = Now() + hours / 24.0 });
        SKSE::log::info("[Transit] Sent letter {} to {}, due in {:.1f} game hours", letter.id, letter.recipientName, hours);
        return hours;
    }

    void Tick()
    {
        if (!Session::IsReady() || g_parcels.empty()) return;
        const double now = Now();
        const auto clock = Clock::now();

        std::erase_if(g_parcels, [now](Parcel& parcel) {
            if (parcel.state == State::kAwaitingReading || parcel.dueAt > now) return false;
            switch (Deliver(parcel)) {
            case Delivery::kDelivered:
                parcel.state = State::kAwaitingReading;
                return false;
            case Delivery::kWaiting:
                return false;
            default:
                return true;
            }
        });

        // Readings report back in a later task, so nothing here erases a parcel (a
        // timed-out reading is a kRetry, which keeps it).
        for (auto& parcel : g_parcels) {
            if (parcel.state != State::kAwaitingReading) continue;
            if (parcel.reading) {
                if (clock - parcel.startedAt > kReadingTimeout) {
                    SKSE::log::warn("[Transit] No answer for letter {} after {} minutes: counted as failed",
                                    parcel.letterId, std::chrono::duration_cast<std::chrono::minutes>(kReadingTimeout).count());
                    OnReadingDone(parcel.deliveryId, parcel.attempt, Session::Generation(), { Reading::Result::kRetry });
                    // Ignore it if it answers after all; the next attempt finds its memory by tag.
                    parcel.attempt = 0;
                }
            } else if (parcel.failures < kMaxFailures && parcel.retryAt <= clock) {
                StartReading(parcel);
            }
        }
    }

    void MakeAllDue()
    {
        for (auto& parcel : g_parcels) parcel.dueAt = 0;
        SKSE::log::info("[Transit] {} letter(s) made due now", g_parcels.size());
    }

    void Save(SKSE::SerializationInterface* a_intfc, std::uint32_t a_type)
    {
        if (!a_intfc->OpenRecord(a_type, kRecordVersion)) {
            SKSE::log::error("[Transit] Couldn't open the co-save record: {} letter(s) in transit are lost", g_parcels.size());
            return;
        }
        a_intfc->WriteRecordData(static_cast<std::uint32_t>(g_parcels.size()));
        for (const auto& p : g_parcels) {
            CoSave::WriteString(a_intfc, p.letterId);
            CoSave::WriteString(a_intfc, p.recipientUuid);
            CoSave::WriteString(a_intfc, p.recipientName);
            a_intfc->WriteRecordData(p.dueAt);
            a_intfc->WriteRecordData(static_cast<std::uint8_t>(p.state));
            CoSave::WriteString(a_intfc, p.deliveryId);
        }
    }

    void Load(SKSE::SerializationInterface* a_intfc, std::uint32_t a_version)
    {
        if (a_version < 1 || a_version > kRecordVersion) {
            SKSE::log::error("[Transit] Co-save record version {} is unknown — skipped", a_version);
            return;
        }
        std::uint32_t count = 0;
        a_intfc->ReadRecordData(count);
        for (std::uint32_t i = 0; i < count; ++i) {
            Parcel p;
            std::uint8_t state = 0;
            if (!CoSave::ReadString(a_intfc, p.letterId) || !CoSave::ReadString(a_intfc, p.recipientUuid) ||
                !CoSave::ReadString(a_intfc, p.recipientName) || a_intfc->ReadRecordData(p.dueAt) != sizeof(p.dueAt) ||
                (a_version >= 2 && a_intfc->ReadRecordData(state) != sizeof(state)) ||
                (a_version >= 3 && !CoSave::ReadString(a_intfc, p.deliveryId))) {
                SKSE::log::error("[Transit] Co-save record is truncated after {} of {} letter(s)", i, count);
                break;
            }
            p.state = state <= 2 ? static_cast<State>(state) : State::kInTransit;
            if (p.deliveryId.empty()) p.deliveryId = p.letterId;  // v1/v2: one delivery per letter
            g_parcels.push_back(std::move(p));
        }
        const auto inState = [](State state) {
            return std::ranges::count_if(g_parcels, [state](const Parcel& p) { return p.state == state; });
        };
        SKSE::log::info("[Transit] {} letter(s) in transit, {} awaiting reading, {} to the player", inState(State::kInTransit),
                        inState(State::kAwaitingReading), inState(State::kToPlayer));
    }

    void Revert()
    {
        g_parcels.clear();
        g_reportedWaiting.clear();
    }

} // namespace PhysicalLetters::Transit
