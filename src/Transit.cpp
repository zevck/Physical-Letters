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
#include "GameTime.h"
#include "HandIn.h"
#include "Letters.h"
#include "NpcLetters.h"
#include "NpcToNpc.h"
#include "Reading.h"
#include "RoadCourier.h"
#include "Session.h"
#include "Courier.h"
#include "CourierErrand.h"
#include "SkyrimNet.h"
#include "Strings.h"
#include "Travel.h"

namespace PhysicalLetters::Transit {

    namespace {
        using Clock = std::chrono::steady_clock;

        enum class State : std::uint8_t {
            kInTransit = 0,        // a letter to an NPC, on its way
            kAwaitingReading = 1,  // in the recipient's inventory, not yet read
            kToPlayer = 2,         // on its way to the courier: an NPC's letter to the player, or
                                   // the player's own coming back undelivered
            kAwaitingCourier = 3,  // due, its recipient in the player's town: waits for the courier
            kOnCourier = 4,        // the courier carries it to the recipient (docs/COURIER.md)
            kOnRoad = 5,           // the courier carries it on the road, from kInTransit (docs/ROAD_COURIER.md)
            kOnRoadToPlayer = 6,   // the same, from kToPlayer
        };

        struct Parcel {
            // Saved.
            std::string letterId;
            std::string deliveryId;  // this sending of the letter; a letter can be sent again
            std::string recipientUuid;
            std::string recipientName;
            double      dueAt = 0;  // game days
            State       state = State::kInTransit;
            Route       route;

            // This session only: the reading in progress and its retries.
            bool              reading = false;
            std::uint64_t     attempt = 0;  // matches a reading's result to the attempt that started it
            int               failures = 0;
            Clock::time_point startedAt{};
            Clock::time_point retryAt{};
            bool              offScreen = false;  // no courier: delivered straight into their inventory
        };

        std::vector<Parcel> g_parcels;
        std::uint64_t g_lastAttempt = 0;
        // Letters already logged as waiting for an unreachable recipient, so the log isn't flooded.
        std::vector<std::string> g_reportedWaiting;

        // v1 (dev builds only) had no state: every parcel was in transit.  v2 had no delivery id,
        // v3 no returned letters, v4 no courier states, v5 no routes.
        constexpr std::uint32_t kRecordVersion = 6;

        // A reading SkyrimNet never answers (it drops cancelled LLM tasks) counts as failed.
        constexpr auto kReadingTimeout = std::chrono::minutes(5);
        constexpr auto kFirstRetryDelay = std::chrono::seconds(30);  // doubled after each failure
        // After this many failures the letter waits for the next load.
        constexpr int kMaxFailures = 5;

        using GameTime::Now;

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

        enum class Delivery { kDelivered, kReturning, kWaiting, kLost, kToCourier, kAwaitCourier };

        // The straight way between the two references, if both are on the same map.
        Route MakeRoute(RE::TESObjectREFR* from, RE::TESObjectREFR* to, double departAt)
        {
            const auto a = Travel::PointOf(from);
            const auto b = Travel::PointOf(to);
            if (!a || !b || a->world != b->world) {
                SKSE::log::debug("[Transit] No route from {} to {}: not on one map", from ? from->GetName() : "nobody",
                                 to ? to->GetName() : "nobody");
                return {};
            }
            SKSE::log::debug("[Transit] Route from {} ({:.0f}, {:.0f}) to {} ({:.0f}, {:.0f}) in 0x{:X}", from->GetName(), a->x,
                             a->y, to->GetName(), b->x, b->y, a->world);
            return { a->world, a->x, a->y, b->x, b->y, departAt };
        }

        // The letter goes back to the player through the courier, after the travel time from
        // the recipient; if they can't be found, at once (the wait was the delay).
        void TurnBack(Parcel& parcel, RE::Actor* recipient, Letters::Returned reason, std::string_view why)
        {
            Letters::SetReturned(parcel.letterId, reason);
            auto* player = RE::PlayerCharacter::GetSingleton();
            const double hours = recipient ? Travel::Hours(recipient, player) : 0.0;
            SKSE::log::info("[Transit] {} {}: letter {} comes back through the courier in {:.1f} game hours",
                            parcel.recipientName, why, parcel.letterId, hours);
            parcel.recipientUuid = SkyrimNet::UuidForFormId(player->GetFormID());
            parcel.recipientName = player->GetName();
            parcel.dueAt = Now() + hours / 24.0;
            parcel.state = State::kToPlayer;
            parcel.route = MakeRoute(recipient, player, Now());
        }

        RE::TESObjectBOOK* BookOf(const Parcel& parcel)
        {
            return RE::TESForm::LookupByID<RE::TESObjectBOOK>(Letters::FormFor(parcel.letterId));
        }

        // The letter the courier carries, if any (one errand at a time).
        std::vector<Parcel>::iterator OnCourier()
        {
            return std::ranges::find(g_parcels, State::kOnCourier, &Parcel::state);
        }

        bool CourierHas(RE::Actor* courier, RE::TESObjectBOOK* book)
        {
            return courier && book && courier->GetInventoryCounts([book](RE::TESBoundObject& item) { return &item == book; })[book] > 0;
        }

        // The courier no longer has the letter: the player took it.  Lost to delivery; a
        // letter between NPCs ends its thread (docs/COURIER.md).
        void Lost(const Parcel& parcel)
        {
            const auto letter = LetterDB::GetSingleton()->Get(parcel.letterId);
            SKSE::log::info("[Transit] The courier no longer has letter {} to {}: it's lost", parcel.letterId, parcel.recipientName);
            if (letter && NpcToNpc::IsNpcLetter(*letter)) NpcToNpc::ThreadEnded(*letter);
        }

        // The errand ends without a handover: the courier's copy goes, and the letter is
        // delivered off-screen.  False if he hadn't it any more (Lost): drop the parcel.
        bool TakeBack(Parcel& parcel, RE::Actor* courier)
        {
            auto* book = BookOf(parcel);
            if (!CourierHas(courier, book)) {
                Lost(parcel);
                return false;
            }
            courier->RemoveItem(book, 1, RE::ITEM_REMOVE_REASON::kRemove, nullptr, nullptr);
            parcel.state = State::kInTransit;
            parcel.offScreen = true;
            SKSE::log::info("[Transit] The courier's errand to {} ended without the handover: letter {} goes in off-screen",
                            parcel.recipientName, parcel.letterId);
            return true;
        }

        bool ToPlayer(const Parcel& parcel)
        {
            return parcel.state == State::kToPlayer || parcel.state == State::kOnRoadToPlayer;
        }

        // The player has the letter now, from the road courier.  A letter to them is delivered;
        // any other is lost to delivery (Lost: a thread between NPCs ends).
        void TakenByPlayer(const Parcel& parcel)
        {
            if (ToPlayer(parcel)) {
                LetterDB::GetSingleton()->MarkDelivered(parcel.letterId, Now());
                SKSE::log::info("[Transit] The player took letter {} to them from the courier on the road", parcel.letterId);
                return;
            }
            SKSE::log::info("[Transit] The player took letter {} to {} from the courier on the road: lost to delivery",
                            parcel.letterId, parcel.recipientName);
            const auto letter = LetterDB::GetSingleton()->Get(parcel.letterId);
            if (letter && NpcToNpc::IsNpcLetter(*letter)) NpcToNpc::ThreadEnded(*letter);
        }

        // The road encounter ended: a letter the courier still has goes on its way, as it was.
        // False if he hadn't it any more (TakenByPlayer): drop the parcel.
        bool FromRoad(Parcel& parcel, RE::Actor* courier)
        {
            auto* book = BookOf(parcel);
            if (!CourierHas(courier, book)) {
                TakenByPlayer(parcel);
                return false;
            }
            courier->RemoveItem(book, 1, RE::ITEM_REMOVE_REASON::kRemove, nullptr, nullptr);
            parcel.state = parcel.state == State::kOnRoadToPlayer ? State::kToPlayer : State::kInTransit;
            return true;
        }

        bool ReadRoute(SKSE::SerializationInterface* a_intfc, Route& route)
        {
            bool ok = a_intfc->ReadRecordData(route.world) == sizeof(route.world);
            for (float* v : { &route.fromX, &route.fromY, &route.toX, &route.toY }) {
                ok = ok && a_intfc->ReadRecordData(*v) == sizeof(*v);
            }
            return ok && a_intfc->ReadRecordData(route.departAt) == sizeof(route.departAt);
        }

        // This encounter's letters given to the player: the forms the script moves to them.
        std::vector<RE::TESObjectBOOK*> g_roadHanded;

        Delivery Deliver(Parcel& parcel)
        {
            auto* book = BookOf(parcel);
            if (!book) {
                SKSE::log::error("[Transit] Letter {} has no form in this save: dropped from the queue", parcel.letterId);
                return Delivery::kLost;
            }
            if (parcel.state == State::kToPlayer) {
                if (CourierErrand::IsLive() || RoadCourier::IsLive()) return Delivery::kWaiting;  // he's out
                // From here the courier and his container hold it; the engine saves both.
                if (!Courier::Give(book)) {
                    ReportWaiting(parcel, "the courier can't be reached");
                    return Delivery::kWaiting;
                }
                SKSE::log::info("[Transit] Letter {} to {} is with the courier", parcel.letterId, parcel.recipientName);
                return Delivery::kToCourier;
            }
            // A letter between NPCs that can't be delivered is dropped, and its thread ends;
            // the player's own comes back to them.
            const auto letter = LetterDB::GetSingleton()->Get(parcel.letterId);
            const bool betweenNpcs = letter && NpcToNpc::IsNpcLetter(*letter);
            auto* recipient = FindRecipient(parcel);
            if (!recipient) {
                // Someone not persistent is only found while their cell is loaded: wait, but
                // not forever (a recipient a mod removed never turns up).
                const int days = Config::GetSingleton()->Get(Config::kReturnAfterDays);
                if (Now() - parcel.dueAt < days) return Delivery::kWaiting;
                if (betweenNpcs) {
                    SKSE::log::info("[Transit] {} wasn't found in {} days: letter {} from {} is dropped", parcel.recipientName,
                                    days, parcel.letterId, letter->authorName);
                    NpcToNpc::ThreadEnded(*letter);
                    return Delivery::kLost;
                }
                TurnBack(parcel, nullptr, Letters::Returned::kNotFound, std::format("wasn't found in {} days", days));
                return Delivery::kReturning;
            }
            if (recipient->IsDead()) {
                if (betweenNpcs) {
                    SKSE::log::info("[Transit] {} is dead: letter {} from {} is dropped", parcel.recipientName, parcel.letterId,
                                    letter->authorName);
                    NpcToNpc::ThreadEnded(*letter);
                    return Delivery::kLost;
                }
                TurnBack(parcel, recipient, Letters::Returned::kDead, "is dead");
                return Delivery::kReturning;
            }

            if (!parcel.offScreen && CourierErrand::ShouldWait(recipient)) return Delivery::kAwaitCourier;

            recipient->AddObjectToContainer(book, nullptr, 1, nullptr);
            LetterDB::GetSingleton()->MarkDelivered(parcel.letterId, Now());
            SKSE::log::info("[Transit] Delivered letter {} to {} (0x{:X})", parcel.letterId, parcel.recipientName,
                            recipient->GetFormID());
            return Delivery::kDelivered;
        }

        // The recipient's reply: a new letter form, and a parcel that travels once the NPC has
        // written it, to the courier for the player, or to the NPC who wrote the letter.
        std::optional<Parcel> MakeReply(const Parcel& original, const std::string& text)
        {
            const auto sent = LetterDB::GetSingleton()->Get(original.letterId);
            const bool betweenNpcs = sent && NpcToNpc::IsNpcLetter(*sent);
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
            RE::TESObjectREFR* destination = betweenNpcs ? FindActor(reply.recipientUuid) : player;
            const double hours = Config::GetSingleton()->Get(Config::kWritingHours) +
                                 Travel::Hours(FindActor(original.recipientUuid), destination);
            SKSE::log::info("[Transit] {} replies to letter {} with letter {}, due {} in {:.1f} game hours", reply.authorName,
                            original.letterId, reply.id, betweenNpcs ? "to " + reply.recipientName : std::string{ "at the courier" },
                            hours);
            if (!betweenNpcs) NpcLetters::StartCooldown(reply.authorUuid);
            const double writtenAt = Now() + Config::GetSingleton()->Get(Config::kWritingHours) / 24.0;
            return Parcel{ .letterId = reply.id,
                           .deliveryId = Letters::NewId(),
                           .recipientUuid = reply.recipientUuid,
                           .recipientName = reply.recipientName,
                           .dueAt = Now() + hours / 24.0,
                           .state = betweenNpcs ? State::kInTransit : State::kToPlayer,
                           .route = MakeRoute(FindActor(original.recipientUuid), destination, writtenAt) };
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
            const auto letter = LetterDB::GetSingleton()->Get(it->letterId);
            const bool betweenNpcs = letter && NpcToNpc::IsNpcLetter(*letter);
            // Someone else's letter (docs/HAND_IN.md#someone-elses-letter): no reply, its thread untouched.
            const bool addressed = !letter || it->recipientUuid == letter->recipientUuid;
            std::optional<Parcel> reply;
            if (addressed && outcome.result == Reading::Result::kRead && !outcome.reply.empty() &&
                (!betweenNpcs || NpcToNpc::CanReply(it->letterId))) {
                reply = MakeReply(*it, outcome.reply);
            }
            if (addressed && betweenNpcs && !reply) NpcToNpc::ThreadEnded(*letter);
            g_parcels.erase(it);
            if (reply) g_parcels.push_back(std::move(*reply));
        }

        void StartReading(Parcel& parcel)
        {
            auto* recipient = FindRecipient(parcel);
            if (!recipient) return;
            const auto letter = LetterDB::GetSingleton()->Get(parcel.letterId);
            const bool canReply = !letter || !NpcToNpc::IsNpcLetter(*letter) || NpcToNpc::CanReply(parcel.letterId);
            const auto reader = !letter || parcel.recipientUuid == letter->recipientUuid ? Reading::Reader::kRecipient
                                                                                         : Reading::Reader::kHandedOther;
            parcel.reading = true;
            parcel.attempt = ++g_lastAttempt;
            parcel.startedAt = Clock::now();
            Reading::Read(parcel.letterId, parcel.deliveryId, recipient->GetFormID(), canReply, reader,
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
        Letters::SetReturned(letter.id, std::nullopt);
        g_parcels.push_back({ .letterId = letter.id,
                              .deliveryId = Letters::NewId(),
                              .recipientUuid = letter.recipientUuid,
                              .recipientName = letter.recipientName,
                              .dueAt = Now() + hours / 24.0,
                              .route = MakeRoute(holder, FindActor(letter.recipientUuid), Now()) });
        SKSE::log::info("[Transit] Sent letter {} to {}, due in {:.1f} game hours", letter.id, letter.recipientName, hours);
        return hours;
    }

    void HandIn(const Letter& letter, RE::Actor* reader)
    {
        const auto readerUuid = SkyrimNet::UuidForFormId(reader->GetFormID());
        const bool addressed = readerUuid == letter.recipientUuid;
        if (addressed) {
            // The player held it, so it's on no way; a stale parcel would deliver it twice.
            std::erase_if(g_parcels, [&](const Parcel& p) { return p.letterId == letter.id && p.state != State::kAwaitingReading; });
            Letters::SetReturned(letter.id, std::nullopt);
            LetterDB::GetSingleton()->MarkDelivered(letter.id, Now());
        }
        SKSE::log::info("[Transit] The player handed letter {} from {} to {} {}(0x{:X})", letter.id, letter.authorName,
                        letter.recipientName, addressed ? "" : std::format("on {} ", reader->GetName()), reader->GetFormID());
        // There and then, aloud; then the reading, as for a letter delivered: their memory, and maybe a reply.
        if (HandIn::NearPlayer(reader)) HandIn::ReadThere(letter, reader);
        if (std::ranges::any_of(g_parcels, [&](const Parcel& p) {
                return p.letterId == letter.id && p.recipientUuid == readerUuid && p.state == State::kAwaitingReading;
            })) {
            SKSE::log::info("[Transit] {} still owed a reading of letter {}: it goes on", reader->GetName(), letter.id);
            return;
        }
        g_parcels.push_back({ .letterId = letter.id,
                              .deliveryId = Letters::NewId(),
                              .recipientUuid = readerUuid,
                              .recipientName = reader->GetName(),
                              .dueAt = Now(),
                              .state = State::kAwaitingReading });
    }

    bool IsLetterPendingFor(const std::string& uuid)
    {
        return std::ranges::any_of(g_parcels, [&](const Parcel& p) {
            return p.recipientUuid == uuid && p.state != State::kToPlayer && p.state != State::kOnRoadToPlayer;
        });
    }

    void QueueToPlayer(const Letter& letter, double hours)
    {
        g_parcels.push_back({ .letterId = letter.id,
                              .deliveryId = Letters::NewId(),
                              .recipientUuid = letter.recipientUuid,
                              .recipientName = letter.recipientName,
                              .dueAt = Now() + hours / 24.0,
                              .state = State::kToPlayer,
                              .route = MakeRoute(FindActor(letter.authorUuid), RE::PlayerCharacter::GetSingleton(), Now()) });
    }

    void QueueToNpc(const Letter& letter, double hours)
    {
        g_parcels.push_back({ .letterId = letter.id,
                              .deliveryId = Letters::NewId(),
                              .recipientUuid = letter.recipientUuid,
                              .recipientName = letter.recipientName,
                              .dueAt = Now() + hours / 24.0,
                              .route = MakeRoute(FindActor(letter.authorUuid), FindActor(letter.recipientUuid), Now()) });
    }

    std::vector<std::string> PendingLetterIds()
    {
        std::vector<std::string> ids;
        for (const auto& p : g_parcels) {
            if (p.state != State::kToPlayer && p.state != State::kOnRoadToPlayer) ids.push_back(p.letterId);
        }
        return ids;
    }

    RE::Actor* TakeForCourier(RE::Actor* courier)
    {
        for (auto& parcel : g_parcels) {
            if (parcel.state != State::kAwaitingCourier) continue;
            auto* recipient = FindActor(parcel.recipientUuid);
            auto* book = BookOf(parcel);
            if (!courier || !book || !CourierErrand::IsHere(recipient)) continue;
            courier->AddObjectToContainer(book, nullptr, 1, nullptr);
            parcel.state = State::kOnCourier;
            SKSE::log::info("[Transit] The courier takes letter {} to {}", parcel.letterId, parcel.recipientName);
            return recipient;
        }
        return nullptr;
    }

    void HandOver(RE::Actor* courier)
    {
        const auto it = OnCourier();
        if (it == g_parcels.end()) return;
        auto* recipient = FindActor(it->recipientUuid);
        auto* book = BookOf(*it);
        if (!recipient || recipient->IsDead() || !CourierHas(courier, book)) {
            if (!TakeBack(*it, courier)) g_parcels.erase(it);
            return;
        }
        courier->RemoveItem(book, 1, RE::ITEM_REMOVE_REASON::kStoreInContainer, nullptr, recipient);
        LetterDB::GetSingleton()->MarkDelivered(it->letterId, Now());
        it->state = State::kAwaitingReading;
        SKSE::log::info("[Transit] The courier handed letter {} to {} (0x{:X})", it->letterId, it->recipientName,
                        recipient->GetFormID());
    }

    void CourierDone(RE::Actor* courier)
    {
        const auto it = OnCourier();
        if (it != g_parcels.end() && !TakeBack(*it, courier)) g_parcels.erase(it);
    }

    std::vector<OnTheRoad> LettersOnTheRoad()
    {
        std::vector<OnTheRoad> letters;
        for (const auto& p : g_parcels) {
            if ((p.state == State::kInTransit || p.state == State::kToPlayer) && p.route.world) {
                letters.push_back({ p.deliveryId, p.route, p.dueAt });
            }
        }
        return letters;
    }

    int TakeForRoad(RE::Actor* courier, const std::vector<std::string>& deliveryIds)
    {
        int taken = 0;
        g_roadHanded.clear();
        for (auto& parcel : g_parcels) {
            if (!courier || std::ranges::find(deliveryIds, parcel.deliveryId) == deliveryIds.end()) continue;
            if (parcel.state != State::kInTransit && parcel.state != State::kToPlayer) continue;
            auto* book = BookOf(parcel);
            if (!book) continue;
            courier->AddObjectToContainer(book, nullptr, 1, nullptr);
            parcel.state = parcel.state == State::kToPlayer ? State::kOnRoadToPlayer : State::kOnRoad;
            ++taken;
            SKSE::log::info("[Transit] The courier on the road carries letter {} to {}", parcel.letterId, parcel.recipientName);
        }
        return taken;
    }

    RE::Actor* RoadRecipient()
    {
        const auto it = std::ranges::find(g_parcels, State::kOnRoad, &Parcel::state);
        return it == g_parcels.end() ? nullptr : FindActor(it->recipientUuid);
    }

    std::optional<Route> RoadRoute()
    {
        const auto it = std::ranges::find_if(g_parcels, [](const Parcel& p) {
            return p.state == State::kOnRoad || p.state == State::kOnRoadToPlayer;
        });
        if (it == g_parcels.end()) return std::nullopt;
        return it->route;
    }

    std::vector<RE::TESForm*> RoadHandOver(RE::Actor* courier)
    {
        std::vector<RE::TESForm*> forms;
        std::erase_if(g_parcels, [&](const Parcel& parcel) {
            if (parcel.state != State::kOnRoad && parcel.state != State::kOnRoadToPlayer) return false;
            auto* book = BookOf(parcel);
            if (CourierHas(courier, book)) {
                forms.push_back(book);
                g_roadHanded.push_back(book);
            }
            TakenByPlayer(parcel);
            return true;
        });
        SKSE::log::info("[Transit] The courier on the road gives the player {} letter(s)", forms.size());
        return forms;
    }

    void RoadOnward(RE::Actor* courier)
    {
        const double now = Now();
        std::erase_if(g_parcels, [&](Parcel& parcel) {
            if (parcel.state != State::kOnRoad && parcel.state != State::kOnRoadToPlayer) return false;
            if (!FromRoad(parcel, courier)) return true;
            parcel.dueAt = std::min(parcel.dueAt, now);
            SKSE::log::info("[Transit] The courier on the road reached {}'s town: letter {} is due now", parcel.recipientName,
                            parcel.letterId);
            return false;
        });
    }

    bool RoadDeliver(RE::Actor* courier, RE::Actor* recipient)
    {
        for (auto& parcel : g_parcels) {
            if (parcel.state != State::kOnRoad || !recipient || FindActor(parcel.recipientUuid) != recipient) continue;
            auto* book = BookOf(parcel);
            if (!CourierHas(courier, book)) continue;
            courier->RemoveItem(book, 1, RE::ITEM_REMOVE_REASON::kStoreInContainer, nullptr, recipient);
            LetterDB::GetSingleton()->MarkDelivered(parcel.letterId, Now());
            parcel.state = State::kAwaitingReading;
            SKSE::log::info("[Transit] The courier on the road handed letter {} to {}", parcel.letterId, parcel.recipientName);
            return true;
        }
        return false;
    }

    void RoadDone(RE::Actor* courier)
    {
        std::erase_if(g_parcels, [courier](Parcel& parcel) {
            return (parcel.state == State::kOnRoad || parcel.state == State::kOnRoadToPlayer) && !FromRoad(parcel, courier);
        });
        // Given to the player, but still on him (the script didn't get to move them).
        for (auto* book : g_roadHanded) {
            if (CourierHas(courier, book)) courier->RemoveItem(book, 1, RE::ITEM_REMOVE_REASON::kStoreInContainer, nullptr,
                                                             RE::PlayerCharacter::GetSingleton());
        }
        g_roadHanded.clear();
    }

    void Tick()
    {
        if (!Session::IsReady() || g_parcels.empty()) return;
        const double now = Now();
        const auto clock = Clock::now();

        std::erase_if(g_parcels, [now](Parcel& parcel) {
            if (parcel.state == State::kOnRoad || parcel.state == State::kOnRoadToPlayer) {
                // The encounter went with a load, or it ended without telling us.
                return !RoadCourier::IsLive() && !FromRoad(parcel, CourierErrand::Courier());
            }
            if (parcel.state == State::kOnCourier) {
                // The errand went with a load, or it ended without telling us.
                return !CourierErrand::IsLive() && !TakeBack(parcel, CourierErrand::Courier());
            }
            if (parcel.state == State::kAwaitingCourier) {
                const double wait = Config::GetSingleton()->Get(Config::kCourierWaitHours) / 24.0;
                if (now - parcel.dueAt < wait && CourierErrand::ShouldWait(FindActor(parcel.recipientUuid))) return false;
                SKSE::log::info("[Transit] No courier for letter {} to {}: it goes in off-screen", parcel.letterId,
                                parcel.recipientName);
                parcel.state = State::kInTransit;
                parcel.offScreen = true;
            }
            if (parcel.state == State::kAwaitingReading || parcel.dueAt > now) return false;
            switch (Deliver(parcel)) {
            case Delivery::kDelivered:
                parcel.state = State::kAwaitingReading;
                return false;
            case Delivery::kAwaitCourier:
                parcel.state = State::kAwaitingCourier;
                parcel.dueAt = now;  // the wait counts from here
                SKSE::log::info("[Transit] {} is in the player's town: letter {} waits for the courier", parcel.recipientName,
                                parcel.letterId);
                return false;
            case Delivery::kWaiting:
            case Delivery::kReturning:
                return false;
            default:
                return true;
            }
        });

        CourierErrand::SetPending(static_cast<int>(
            std::ranges::count_if(g_parcels, [](const Parcel& p) { return p.state == State::kAwaitingCourier; })));

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
            a_intfc->WriteRecordData(p.route.world);
            for (const float v : { p.route.fromX, p.route.fromY, p.route.toX, p.route.toY }) a_intfc->WriteRecordData(v);
            a_intfc->WriteRecordData(p.route.departAt);
        }
        const auto returned = Letters::ReturnedLetters();
        a_intfc->WriteRecordData(static_cast<std::uint32_t>(returned.size()));
        for (const auto& [letterId, reason] : returned) {
            CoSave::WriteString(a_intfc, letterId);
            a_intfc->WriteRecordData(static_cast<std::uint8_t>(reason));
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
                (a_version >= 3 && !CoSave::ReadString(a_intfc, p.deliveryId)) || (a_version >= 6 && !ReadRoute(a_intfc, p.route))) {
                SKSE::log::error("[Transit] Co-save record is truncated after {} of {} letter(s)", i, count);
                break;
            }
            p.state = state <= 6 ? static_cast<State>(state) : State::kInTransit;
            if (p.deliveryId.empty()) p.deliveryId = p.letterId;  // v1/v2: one delivery per letter
            g_parcels.push_back(std::move(p));
        }
        std::uint32_t returnedCount = 0;
        if (a_version >= 4 && a_intfc->ReadRecordData(returnedCount) == sizeof(returnedCount)) {
            for (std::uint32_t i = 0; i < returnedCount; ++i) {
                std::string letterId;
                std::uint8_t reason = 0;
                if (!CoSave::ReadString(a_intfc, letterId) || a_intfc->ReadRecordData(reason) != sizeof(reason)) break;
                if (reason == 1 || reason == 2) Letters::SetReturned(letterId, static_cast<Letters::Returned>(reason));
            }
        }
        const auto inState = [](State state) {
            return std::ranges::count_if(g_parcels, [state](const Parcel& p) { return p.state == state; });
        };
        SKSE::log::info("[Transit] {} letter(s) in transit, {} awaiting reading, {} to the player, {} with or waiting for "
                        "the courier", inState(State::kInTransit), inState(State::kAwaitingReading), inState(State::kToPlayer),
                        inState(State::kAwaitingCourier) + inState(State::kOnCourier) + inState(State::kOnRoad) +
                            inState(State::kOnRoadToPlayer));
    }

    void Revert()
    {
        g_parcels.clear();
        g_reportedWaiting.clear();
        g_roadHanded.clear();
    }

} // namespace PhysicalLetters::Transit
