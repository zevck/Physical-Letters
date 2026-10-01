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

#include "RoadCourier.h"
#include "Config.h"
#include "CourierErrand.h"
#include "GameTime.h"
#include "Session.h"
#include "Transit.h"
#include "Travel.h"

namespace PhysicalLetters::RoadCourier {

    namespace {
        constexpr auto kScript = "PhysicalLetters_RoadCourierQuest";
        constexpr std::string_view kPlugin = "Physical Letters.esp";
        constexpr RE::FormID kReadyGlobal = 0x83F;
        constexpr RE::FormID kQuest = 0x840;
        constexpr RE::FormID kSpeechGlobal = 0x84C;
        constexpr std::uint32_t kRecordVersion = 1;

        // A letter on its way passes the player when its straight route runs within kCorridor of
        // them: about four exterior cells.  No timing: the player outruns letters threefold.
        constexpr float kCorridor = 16384.0f;
        constexpr float kMinRoute = 16384.0f;  // shorter: within one town, no road to meet it on
        constexpr float kSameWay = 0.866f;     // cos 30°: letters the courier carries together

        double g_lastAt = -1;  // saved: game days of the last encounter; -1 none
        bool g_live = false;   // this session: the courier carries letters on the road
        int g_ready = -1;
        std::string g_blocked;  // why a passing letter can't bring him now, logged when it changes      // the global as last set (-1 unknown, after a load), so it's set on changes

        using GameTime::Now;

        RE::TESQuest* OurQuest()
        {
            auto* data = RE::TESDataHandler::GetSingleton();
            return data ? data->LookupForm<RE::TESQuest>(kQuest, kPlugin) : nullptr;
        }

        struct Passing {
            std::string deliveryId;
            float dirX = 0, dirY = 0;  // the route's direction
            float distance = 0;        // from the player to the route
        };

        // The letters passing the player now, nearest first, and only those going the same
        // way as the nearest (docs/ROAD_COURIER.md#which-letters).
        std::vector<Passing> LettersPassing(double now)
        {
            auto* player = RE::PlayerCharacter::GetSingleton();
            const auto* cell = player->GetParentCell();
            if (!cell || cell->IsInteriorCell()) return {};
            const auto here = Travel::PointOf(player);
            if (!here) return {};
            // Debug, every 15 s: how the nearest letter's route lies, to tune the checks by.
            static auto lastTrace = std::chrono::steady_clock::time_point{};
            const bool trace = std::chrono::steady_clock::now() - lastTrace > std::chrono::seconds(15);
            if (trace) lastTrace = std::chrono::steady_clock::now();
            // No town check: a town's location reaches far past its walls (Whiterun's covers most of
            // the road to Riverwood), and vanilla's road triggers are only on roads anyway.
            const auto* area = Travel::Area(player);

            std::vector<Passing> passing;
            for (const auto& letter : Transit::LettersOnTheRoad()) {
                const auto& r = letter.route;
                if (r.world != here->world || now < r.departAt || now >= letter.dueAt) continue;
                const float vx = r.toX - r.fromX, vy = r.toY - r.fromY;
                const float length2 = vx * vx + vy * vy;
                if (length2 < kMinRoute * kMinRoute) continue;
                const float t = std::clamp(((here->x - r.fromX) * vx + (here->y - r.fromY) * vy) / length2, 0.0f, 1.0f);
                const float distance = std::hypot(r.fromX + t * vx - here->x, r.fromY + t * vy - here->y);
                if (trace) {
                    SKSE::log::debug("[RoadCourier] Letter {}: {:.0f} units from its route, {:.2f} along it ({})", letter.deliveryId,
                                     distance, t, area ? area->GetName() : "the wilds");
                }
                if (distance > kCorridor) continue;
                const float length = std::sqrt(length2);
                passing.push_back({ letter.deliveryId, vx / length, vy / length, distance });
            }
            std::ranges::sort(passing, {}, &Passing::distance);
            if (!passing.empty()) {
                const auto first = passing.front();
                std::erase_if(passing, [&](const Passing& p) { return p.dirX * first.dirX + p.dirY * first.dirY < kSameWay; });
            }
            return passing;
        }

        bool CoolingDown(double now)
        {
            return g_lastAt >= 0 && now - g_lastAt < Config::GetSingleton()->Get(Config::kRoadCooldown);
        }

        void SetReady(bool ready)
        {
            if (static_cast<int>(ready) == g_ready) return;
            g_ready = ready ? 1 : 0;
            auto* data = RE::TESDataHandler::GetSingleton();
            if (auto* global = data ? data->LookupForm<RE::TESGlobal>(kReadyGlobal, kPlugin) : nullptr) {
                global->value = ready ? 1.0f : 0.0f;
            }
            SKSE::log::debug("[RoadCourier] {}", ready ? "A letter passes the player: the courier may be met at a road trigger"
                                                      : "No letter passes the player now");
        }

        std::int32_t TakeLetters(RE::StaticFunctionTag*)
        {
            auto* courier = CourierErrand::Courier();
            if (!Session::IsReady() || g_live || !courier) {
                SKSE::log::info("[RoadCourier] No encounter: {}", !courier ? "the vanilla courier wasn't found"
                                                                 : g_live ? "one is under way" : "the session isn't ready");
                return 0;
            }
            std::vector<std::string> ids;
            for (const auto& p : LettersPassing(Now())) ids.push_back(p.deliveryId);
            const int taken = Transit::TakeForRoad(courier, ids);
            if (taken > 0) {
                g_live = true;
                g_lastAt = Now();
                SetReady(false);
            } else {
                SKSE::log::info("[RoadCourier] No encounter: no letter passes here now");
            }
            return taken;
        }

        RE::TESObjectREFR* Destination(RE::StaticFunctionTag*)
        {
            return Travel::MarkerFor(Travel::Area(Transit::RoadRecipient()));
        }

        // Of the two references, the one further along his letters' route: where he heads.
        RE::TESObjectREFR* Downstream(RE::StaticFunctionTag*, RE::TESObjectREFR* a, RE::TESObjectREFR* b)
        {
            const auto route = Transit::RoadRoute();
            if (!route || !a || !b) return b;
            const auto along = [&](RE::TESObjectREFR* ref) {
                const auto& p = ref->GetPosition();
                return (p.x - route->fromX) * (route->toX - route->fromX) + (p.y - route->fromY) * (route->toY - route->fromY);
            };
            return along(a) > along(b) ? a : b;
        }

        // He's at his destination's map marker: the recipient, wherever they are (he walks in
        // through doors), if they can be found alive; else his letters go on, due now, and nullptr.
        RE::Actor* ArriveInTown(RE::StaticFunctionTag*)
        {
            auto* recipient = Transit::RoadRecipient();
            if (recipient && !recipient->IsDead()) {
                SKSE::log::info("[RoadCourier] He reached {}'s town and goes to them", recipient->GetName());
                return recipient;
            }
            Transit::RoadOnward(CourierErrand::Courier());
            return nullptr;
        }

        bool DeliverToRecipient(RE::StaticFunctionTag*, RE::Actor* recipient)
        {
            return Transit::RoadDeliver(CourierErrand::Courier(), recipient);
        }

        // He couldn't reach them (a locked house): his letters go on, due now.
        void GiveUpDelivery(RE::StaticFunctionTag*)
        {
            SKSE::log::info("[RoadCourier] He couldn't reach the recipient in time: his letters go on");
            Transit::RoadOnward(CourierErrand::Courier());
        }

        // The hold's crime faction (Skyrim.esm), for the hold a road trigger names; nullptr if none.
        RE::TESFaction* CrimeFactionFor(RE::StaticFunctionTag*, RE::BGSLocation* hold)
        {
            static constexpr std::pair<RE::FormID, RE::FormID> kHolds[] = {
                { 0x01676A, 0x0267E3 },  // Eastmarch
                { 0x01676F, 0x028170 },  // Falkreath
                { 0x016770, 0x029DB0 },  // Haafingar
                { 0x01676E, 0x02816D },  // Hjaalmarch
                { 0x01676D, 0x02816E },  // the Pale
                { 0x016769, 0x02816C },  // the Reach
                { 0x01676C, 0x02816B },  // the Rift
                { 0x016772, 0x0267EA },  // Whiterun
                { 0x01676B, 0x02816F },  // Winterhold
            };
            for (const auto& [location, faction] : kHolds) {
                if (hold && hold->GetFormID() == location) return RE::TESForm::LookupByID<RE::TESFaction>(faction);
            }
            return nullptr;
        }

        std::int32_t RobberyBounty(RE::StaticFunctionTag*)
        {
            return Config::GetSingleton()->Get(Config::kRobberyBounty);
        }

        std::vector<RE::TESForm*> HandOverLetters(RE::StaticFunctionTag*)
        {
            return g_live ? Transit::RoadHandOver(CourierErrand::Courier()) : std::vector<RE::TESForm*>{};
        }

        bool IsEncounterCurrent(RE::StaticFunctionTag*)
        {
            return IsLive();
        }

        void EncounterEnded(RE::StaticFunctionTag*, RE::BSFixedString why)
        {
            Transit::RoadDone(CourierErrand::Courier());
            if (g_live) SKSE::log::info("[RoadCourier] The encounter ended: {}", why.c_str());
            g_live = false;
        }
    }

    void Tick()
    {
        // The dialogue's Speech requirement, from the INI.
        static int speech = -1;
        if (const int wanted = Config::GetSingleton()->Get(Config::kRoadIntimidate); wanted != speech) {
            auto* data = RE::TESDataHandler::GetSingleton();
            if (auto* global = data ? data->LookupForm<RE::TESGlobal>(kSpeechGlobal, kPlugin) : nullptr) {
                global->value = static_cast<float>(wanted);
                speech = wanted;
            }
        }
        const double now = Now();
        if (!Session::IsReady() || g_live || !OurQuest()) {
            SetReady(false);
            return;
        }
        const bool passing = !LettersPassing(now).empty();
        std::string blocked;
        if (passing && !Config::GetSingleton()->Get(Config::kRoadEncounters)) blocked = "road encounters are off";
        else if (passing && CoolingDown(now)) blocked = std::format("he was met on the road {:.1f} days ago (cooldown)", now - g_lastAt);
        if (blocked != g_blocked) {
            if (!blocked.empty()) SKSE::log::info("[RoadCourier] A letter passes the player, but {}", blocked);
            g_blocked = blocked;
        }
        SetReady(passing && blocked.empty());
    }

    bool IsLive()
    {
        // Our quest stopped without EncounterEnded (the console, a mod resetting quests).
        if (const auto* quest = OurQuest(); g_live && (!quest || quest->IsStopped())) {
            SKSE::log::warn("[RoadCourier] The road courier quest stopped mid-encounter: the encounter is over");
            g_live = false;
        }
        return g_live;
    }

    bool RegisterFunctions(RE::BSScript::IVirtualMachine* vm)
    {
        vm->RegisterFunction("TakeLetters", kScript, TakeLetters);
        vm->RegisterFunction("Destination", kScript, Destination);
        vm->RegisterFunction("HandOverLetters", kScript, HandOverLetters);
        vm->RegisterFunction("Downstream", kScript, Downstream);
        vm->RegisterFunction("ArriveInTown", kScript, ArriveInTown);
        vm->RegisterFunction("CrimeFactionFor", kScript, CrimeFactionFor);
        vm->RegisterFunction("RobberyBounty", kScript, RobberyBounty);
        vm->RegisterFunction("DeliverToRecipient", kScript, DeliverToRecipient);
        vm->RegisterFunction("GiveUpDelivery", kScript, GiveUpDelivery);
        vm->RegisterFunction("IsEncounterCurrent", kScript, IsEncounterCurrent);
        vm->RegisterFunction("EncounterEnded", kScript, EncounterEnded);
        return true;
    }

    void Save(SKSE::SerializationInterface* a_intfc, std::uint32_t a_type)
    {
        if (!a_intfc->OpenRecord(a_type, kRecordVersion)) {
            SKSE::log::error("[RoadCourier] Couldn't open the co-save record");
            return;
        }
        a_intfc->WriteRecordData(g_lastAt);
    }

    void Load(SKSE::SerializationInterface* a_intfc, std::uint32_t a_version)
    {
        if (a_version != kRecordVersion || a_intfc->ReadRecordData(g_lastAt) != sizeof(g_lastAt)) {
            SKSE::log::error("[RoadCourier] Co-save record version {} unreadable — skipped", a_version);
            g_lastAt = -1;
        }
        SKSE::log::info("[RoadCourier] {}", g_lastAt < 0 ? std::string{ "Never met on the road in this save" }
                                                        : std::format("Last met on the road at day {:.1f}", g_lastAt));
    }

    void Revert()
    {
        g_lastAt = -1;
        g_live = false;
        g_ready = -1;
        g_blocked.clear();
    }

} // namespace PhysicalLetters::RoadCourier
