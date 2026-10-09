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

#pragma once

// Parchment, the blank letter (docs/WRITING.md#parchment): crafted from a roll of paper at a
// tanning rack (the ESP's recipe), and always sold by general-goods merchants (added here, in memory).
namespace PhysicalLetters::Parchment {

    // PhysicalLettersParchment.  Letters copy their look from it (Letters::Configure).
    inline constexpr RE::FormID kFormId = 0x8B6;
    inline constexpr std::string_view kPlugin = "Physical Letters.esp";

    inline RE::TESObjectBOOK* Form()
    {
        auto* data = RE::TESDataHandler::GetSingleton();
        return data ? data->LookupForm<RE::TESObjectBOOK>(kFormId, kPlugin) : nullptr;
    }

    // kDataLoaded, after Writing::Connect: names it in the chosen language and stocks it in every general-goods
    // merchant's chest; without Ink & Quill's writing, takes its recipe off the tanning rack instead.
    void OnDataLoaded();

} // namespace PhysicalLetters::Parchment
