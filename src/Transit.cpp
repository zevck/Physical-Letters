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
#include "Letters.h"
#include "Reading.h"
#include "Session.h"
#include "SkyrimNet.h"
#include "Strings.h"

namespace PhysicalLetters::Transit {

    namespace {
        using Clock = std::chrono::steady_clock;

        enum class State : std::uint8_t {
            kInTransit = 0,
            kAwaitingReading = 1,  // in the recipient's inventory, not yet read
        };

        struct Parcel {
            // Saved.
            std::string letterId;
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

        // v1 (dev builds only) had no state: every parcel was in transit.
        constexpr std::uint32_t kRecordVersion = 2;

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

        // The recipient, or nullptr while they can't be reached: a persistent NPC is in
        // memory wherever they are, anyone else only while their cell is loaded.  The
        // FormID must map back to the same UUID: a runtime FormID can belong to another
        // actor by now.
        RE::Actor* FindRecipient(const Parcel& parcel)
        {
            const auto formId = SkyrimNet::FormIdForUuid(parcel.recipientUuid);
            auto* actor = formId ? RE::TESForm::LookupByID<RE::Actor>(formId) : nullptr;
            if (!actor) {
                ReportWaiting(parcel, "not loaded");
                return nullptr;
            }
            if (const auto uuid = SkyrimNet::UuidForFormId(formId); uuid != parcel.recipientUuid) {
                ReportWaiting(parcel, std::format("0x{:X} is now UUID {}", formId, uuid));
                return nullptr;
            }
            return actor;
        }

        enum class Delivery { kDelivered, kReturned, kWaiting, kLost };

        Delivery Deliver(const Parcel& parcel)
        {
            auto* book = RE::TESForm::LookupByID<RE::TESObjectBOOK>(Letters::FormFor(parcel.letterId));
            if (!book) {
                SKSE::log::error("[Transit] Letter {} has no form in this save: dropped from the queue", parcel.letterId);
                return Delivery::kLost;
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

        void OnReadingDone(const std::string& letterId, std::uint64_t attempt, std::uint32_t generation, Reading::Result result)
        {
            if (generation != Session::Generation()) return;  // the new session has its own queue
            const auto it = std::ranges::find_if(g_parcels, [&](const Parcel& p) { return p.letterId == letterId; });
            if (it == g_parcels.end() || it->attempt != attempt) return;  // a timed-out attempt answering late

            if (result == Reading::Result::kRetry) {
                it->reading = false;
                ++it->failures;
                if (it->failures >= kMaxFailures) {
                    SKSE::log::error("[Transit] Letter {} couldn't be read {} times: it's tried again after the next load",
                                     letterId, it->failures);
                } else {
                    it->retryAt = Clock::now() + kFirstRetryDelay * (1 << (it->failures - 1));
                }
                return;
            }
            g_parcels.erase(it);  // read, or it never can be
        }

        void StartReading(Parcel& parcel)
        {
            auto* recipient = FindRecipient(parcel);
            if (!recipient) return;
            parcel.reading = true;
            parcel.attempt = ++g_lastAttempt;
            parcel.startedAt = Clock::now();
            Reading::Read(parcel.letterId, recipient->GetFormID(),
                          [letterId = parcel.letterId, attempt = parcel.attempt,
                           generation = Session::Generation()](Reading::Result result) {
                              OnReadingDone(letterId, attempt, generation, result);
                          });
        }
    }

    bool Send(RE::TESObjectBOOK* book, const Letter& letter, double delayHours)
    {
        auto* player = RE::PlayerCharacter::GetSingleton();
        if (!book || !player || player->GetItemCount(book) <= 0) return false;
        player->RemoveItem(book, 1, RE::ITEM_REMOVE_REASON::kRemove, nullptr, nullptr);
        g_parcels.push_back({ .letterId = letter.id,
                              .recipientUuid = letter.recipientUuid,
                              .recipientName = letter.recipientName,
                              .dueAt = Now() + delayHours / 24.0 });
        SKSE::log::info("[Transit] Sent letter {} to {}, due in {:.1f} game hours", letter.id, letter.recipientName, delayHours);
        return true;
    }

    void Tick()
    {
        if (!Session::IsReady() || g_parcels.empty()) return;
        const double now = Now();
        const auto clock = Clock::now();

        std::erase_if(g_parcels, [now](Parcel& parcel) {
            if (parcel.state != State::kInTransit || parcel.dueAt > now) return false;
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
                    OnReadingDone(parcel.letterId, parcel.attempt, Session::Generation(), Reading::Result::kRetry);
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
        }
    }

    void Load(SKSE::SerializationInterface* a_intfc, std::uint32_t a_version)
    {
        if (a_version != 1 && a_version != kRecordVersion) {
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
                (a_version >= 2 && a_intfc->ReadRecordData(state) != sizeof(state))) {
                SKSE::log::error("[Transit] Co-save record is truncated after {} of {} letter(s)", i, count);
                break;
            }
            p.state = state == 1 ? State::kAwaitingReading : State::kInTransit;
            g_parcels.push_back(std::move(p));
        }
        const auto awaiting = std::ranges::count_if(g_parcels, [](const Parcel& p) { return p.state == State::kAwaitingReading; });
        SKSE::log::info("[Transit] {} letter(s) in transit, {} awaiting reading", g_parcels.size() - awaiting, awaiting);
    }

    void Revert()
    {
        g_parcels.clear();
        g_reportedWaiting.clear();
    }

} // namespace PhysicalLetters::Transit
