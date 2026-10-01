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

#include "CourierErrand.h"
#include "Config.h"
#include "Session.h"
#include "Transit.h"
#include "Travel.h"

namespace PhysicalLetters::CourierErrand {

    namespace {
        constexpr auto kScript = "PhysicalLetters_CourierQuest";
        constexpr std::string_view kPlugin = "Physical Letters.esp";
        constexpr RE::FormID kPendingGlobal = 0x809;
        constexpr RE::FormID kQuest = 0x80A;
        constexpr RE::FormID kCourierRef = 0x039FB7;  // Skyrim.esm: the vanilla courier, WICourierNPC

        bool g_live = false;  // this session: the courier carries a letter

        RE::TESQuest* OurQuest()
        {
            auto* data = RE::TESDataHandler::GetSingleton();
            return data ? data->LookupForm<RE::TESQuest>(kQuest, kPlugin) : nullptr;
        }

        // The other quest holding the courier in an alias (vanilla's mail quest, or a mod's:
        // Courier Delivers to NPCs borrows him too), or nullptr.  He's theirs then.
        const RE::TESQuest* HeldBy(RE::Actor* courier)
        {
            const auto* aliases = courier ? courier->extraList.GetByType<RE::ExtraAliasInstanceArray>() : nullptr;
            if (!aliases) return nullptr;
            const auto* ours = OurQuest();
            RE::BSReadLockGuard lock{ aliases->lock };
            for (const auto* a : aliases->aliases) {
                // IsStopped, not IsRunning: CommonLib's IsRunning is true for a stopped quest too.
                if (a && a->quest && a->quest != ours && !a->quest->IsStopped()) return a->quest;
            }
            return nullptr;
        }

        std::string Name(const RE::TESQuest* quest)
        {
            const char* editorId = quest->GetFormEditorID();
            return std::format("{} (0x{:08X})", editorId && *editorId ? editorId : "a quest", quest->GetFormID());
        }

        bool IsOutdoors(RE::TESObjectREFR* ref)
        {
            const auto* cell = ref ? ref->GetParentCell() : nullptr;
            return cell && !cell->IsInteriorCell();
        }

        RE::Actor* TakeTarget(RE::StaticFunctionTag*)
        {
            auto* courier = Courier();
            const RE::TESQuest* holder = courier ? HeldBy(courier) : nullptr;
            std::string why;
            if (!Session::IsReady()) why = "the session isn't ready";
            else if (g_live) why = "an errand is already under way";
            else if (!courier) why = "the vanilla courier (Skyrim.esm 0x039FB7) wasn't found";
            else if (holder) why = std::format("{} holds him", Name(holder));
            if (!why.empty()) {
                SKSE::log::info("[Courier] No errand: {}", why);
                return nullptr;
            }
            auto* recipient = Transit::TakeForCourier(courier);
            if (!recipient) SKSE::log::info("[Courier] No errand: no waiting letter's recipient is outdoors in this town");
            g_live = recipient != nullptr;
            return recipient;
        }

        void HandOver(RE::StaticFunctionTag*)
        {
            if (g_live) Transit::HandOver(Courier());
        }

        bool IsErrandCurrent(RE::StaticFunctionTag*)
        {
            if (!g_live) return false;
            if (const auto* holder = HeldBy(Courier())) {
                SKSE::log::info("[Courier] {} took the courier: the errand ends", Name(holder));
                return false;
            }
            return true;
        }

        bool ErrandEnded(RE::StaticFunctionTag*)
        {
            auto* courier = Courier();
            Transit::CourierDone(courier);
            g_live = false;
            return courier && !HeldBy(courier);
        }
    }

    bool ShouldWait(RE::Actor* recipient)
    {
        if (!Config::GetSingleton()->Get(Config::kCourierEnabled) || !OurQuest() || !recipient || recipient->IsDead()) return false;
        const auto* area = Travel::Area(recipient);
        return Travel::IsTown(area) && area == Travel::Area(RE::PlayerCharacter::GetSingleton());
    }

    bool IsHere(RE::Actor* recipient)
    {
        auto* player = RE::PlayerCharacter::GetSingleton();
        if (!recipient || recipient->IsDead() || !recipient->Is3DLoaded() || !IsOutdoors(recipient) || !IsOutdoors(player)) {
            return false;
        }
        const auto* area = Travel::Area(recipient);
        return Travel::IsTown(area) && area == Travel::Area(player);
    }

    RE::Actor* Courier()
    {
        return RE::TESForm::LookupByID<RE::Actor>(kCourierRef);
    }

    bool IsLive()
    {
        // Our quest stopped without ErrandEnded (the console, a mod resetting quests).
        if (const auto* quest = OurQuest(); g_live && (!quest || quest->IsStopped())) {
            SKSE::log::warn("[Courier] The courier quest stopped mid-errand: the errand is over");
            g_live = false;
        }
        return g_live;
    }

    void SetPending(int count)
    {
        auto* data = RE::TESDataHandler::GetSingleton();
        if (auto* global = data ? data->LookupForm<RE::TESGlobal>(kPendingGlobal, kPlugin) : nullptr) {
            global->value = static_cast<float>(count);
        }
    }

    bool RegisterFunctions(RE::BSScript::IVirtualMachine* vm)
    {
        vm->RegisterFunction("TakeTarget", kScript, TakeTarget);
        vm->RegisterFunction("HandOver", kScript, HandOver);
        vm->RegisterFunction("IsErrandCurrent", kScript, IsErrandCurrent);
        vm->RegisterFunction("ErrandEnded", kScript, ErrandEnded);
        return true;
    }

    void Revert()
    {
        g_live = false;
    }

} // namespace PhysicalLetters::CourierErrand
