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

#include "Parchment.h"

#include "Strings.h"
#include "Writing.h"


namespace PhysicalLetters::Parchment {

    namespace {
        constexpr RE::FormID kRecipe = 0x8B7;            // PhysicalLettersRecipeParchment, at a tanning rack
        constexpr RE::FormID kList = 0x8B8;             // PhysicalLettersLItemParchment: 3 or 5 parchment
        constexpr RE::FormID kVendorMiscItems = 0x09AF0A;  // Skyrim.esm LItemMiscVendorMiscItems75: tells a general store

        // Vanilla's generic world containers (hundreds in houses) stock the misc list too: a merchant using one would
        // put parchment in all of them.  Same walk as Ink & Quill's src/WritingTools.cpp: fix both.
        bool IsGenericContainer(const RE::TESObjectCONT* chest)
        {
            static constexpr std::pair<RE::FormID, std::string_view> kGeneric[] = { { 0x024CA4, "Skyrim.esm" }, { 0x09AF19, "Skyrim.esm" },
                                                                                   { 0x03C361, "Dragonborn.esm" } };
            auto* data = RE::TESDataHandler::GetSingleton();
            for (const auto& [id, plugin] : kGeneric) {
                if (data && data->LookupForm(id, plugin) == chest) return true;  // Cupboard01, PersonalChestSmall(_NoRespawn)
            }
            return false;
        }

        // Every general-goods merchant's chest (a vendor faction's, stocking vanilla's misc list) gets `ours` as its own entry,
        // in memory: every restock has it, and no record is overridden.
        void Stock(RE::TESLevItem* ours, RE::TESLevItem* generalGoods)
        {
            auto* data = RE::TESDataHandler::GetSingleton();
            int stocked = 0;
            for (auto* faction : data->GetFormArray<RE::TESFaction>()) {
                if (!faction || !faction->IsVendor() || !faction->vendorData.merchantContainer) continue;
                auto* base = faction->vendorData.merchantContainer->GetBaseObject();
                auto* chest = base ? base->As<RE::TESObjectCONT>() : nullptr;
                // Not a general store, or already stocked (several merchants can share a chest).
                if (!chest || chest->GetObjectCount(generalGoods) == 0 || chest->GetObjectCount(ours) > 0) continue;
                if (IsGenericContainer(chest)) {
                    SKSE::log::warn("[Parchment] Vendor faction {:08X}'s chest is a generic world container ({:08X}): not stocked there",
                                    faction->GetFormID(), chest->GetFormID());
                    continue;
                }
                if (!chest->AddObjectToContainer(ours, 1, nullptr)) continue;
                ++stocked;
                SKSE::log::info("[Parchment] Stocked chest {:08X} '{}' (vendor faction {:08X})", chest->GetFormID(),
                                chest->GetFormEditorID() ? chest->GetFormEditorID() : "", faction->GetFormID());
            }
            SKSE::log::info("[Parchment] Always stocked by {} general-goods merchant chest(s)", stocked);
        }

        void AddToMerchants(RE::TESLevItem* ours)
        {
            auto* generalGoods = RE::TESForm::LookupByID<RE::TESLevItem>(kVendorMiscItems);
            if (!generalGoods) {
                SKSE::log::warn("[Parchment] LItemMiscVendorMiscItems75 not found: merchants won't sell it");
                return;
            }
            Stock(ours, generalGoods);
        }
    }

    void OnDataLoaded()
    {
        auto* data = RE::TESDataHandler::GetSingleton();
        if (auto* parchment = Form()) {
            parchment->SetFullName(Strings::ParchmentName().c_str());  // the ESP's name is English
        }
        // Without Ink & Quill parchment can't be written on: nobody sells it and no bench makes it.
        if (!Writing::Available()) {
            if (auto* recipe = data ? data->LookupForm<RE::BGSConstructibleObject>(kRecipe, kPlugin) : nullptr) {
                recipe->benchKeyword = nullptr;
            }
            SKSE::log::info("[Parchment] Letters can't be written: parchment isn't sold or crafted");
            return;
        }
        if (auto* list = data ? data->LookupForm<RE::TESLevItem>(kList, kPlugin) : nullptr) {
            AddToMerchants(list);
        } else {
            SKSE::log::warn("[Parchment] PhysicalLettersLItemParchment not found: merchants won't sell it");
        }
    }

} // namespace PhysicalLetters::Parchment
