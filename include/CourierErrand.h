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

// The vanilla courier carrying a letter to an NPC near the player (docs/COURIER.md).  The
// quest PhysicalLettersCourierQuest runs the errand; Transit owns the letters.  Game thread.
namespace PhysicalLetters::CourierErrand {

    // Whether a letter due to `recipient` waits for the courier: errands are on, and the
    // recipient is in the player's town.
    bool ShouldWait(RE::Actor* recipient);

    // Whether the courier can reach `recipient` now: outdoors, loaded, in the town the player
    // is outdoors in.
    bool IsHere(RE::Actor* recipient);

    // The vanilla courier (Skyrim.esm 0x039FB7).
    RE::Actor* Courier();

    // An errand is under way in this session (the courier carries a letter).
    bool IsLive();

    // How many letters wait for the courier: the Story Manager node's global.
    void SetPending(int count);

    // Registers the quest script's natives.
    bool RegisterFunctions(RE::BSScript::IVirtualMachine* vm);

    void Revert();

} // namespace PhysicalLetters::CourierErrand
