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

namespace PhysicalLetters::Parchment {

    namespace {
        constexpr RE::FormID kList = 0x8B8;              // PhysicalLettersLItemParchment: 3 or 5 parchment
        constexpr RE::FormID kVendorMiscItems = 0x09AF0A;  // Skyrim.esm LItemMiscVendorMiscItems75

        // As SkyrimNet Physical Diaries' blank journals: the general-goods list that already sells
        // the Roll of Paper, changed in memory, so no vanilla record is overridden.
        void AddToMerchants(RE::TESLevItem* ours)
        {
            auto* vendor = RE::TESForm::LookupByID<RE::TESLevItem>(kVendorMiscItems);
            if (!vendor) {
                SKSE::log::warn("[Parchment] LItemMiscVendorMiscItems75 not found: merchants won't sell it");
                return;
            }
            auto& entries = vendor->entries;
            const std::size_t count = vendor->numEntries;
            if (entries.size() != count || count >= 255) {
                SKSE::log::warn("[Parchment] LItemMiscVendorMiscItems75 has {} entries ({} counted): not added", entries.size(),
                                count);
                return;
            }
            for (const auto& entry : entries) {
                if (entry.form == ours) return;
            }
            // Entries are kept in level order; ours goes after the other level-1 entries.
            std::size_t at = count;
            for (std::size_t i = 0; i < count; ++i) {
                if (entries[i].level > 1) {
                    at = i;
                    break;
                }
            }
            entries.resize(count + 1);
            for (std::size_t i = count; i > at; --i) entries[i] = entries[i - 1];
            entries[at] = RE::LEVELED_OBJECT{ .form = ours, .count = 1, .level = 1, .pad0C = 0, .itemExtra = nullptr };
            vendor->numEntries = static_cast<std::uint8_t>(count + 1);
            SKSE::log::info("[Parchment] Added to general-goods merchants' stock ({} entries)", count + 1);
        }
    }

    void OnDataLoaded()
    {
        auto* data = RE::TESDataHandler::GetSingleton();
        if (auto* parchment = Form()) {
            parchment->SetFullName(Strings::ParchmentName().c_str());  // the ESP's name is English
        }
        if (auto* list = data ? data->LookupForm<RE::TESLevItem>(kList, kPlugin) : nullptr) {
            AddToMerchants(list);
        } else {
            SKSE::log::warn("[Parchment] PhysicalLettersLItemParchment not found: merchants won't sell it");
        }
    }

} // namespace PhysicalLetters::Parchment
