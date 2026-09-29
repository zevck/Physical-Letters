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

// DEV HARNESS until the editor and the hand-over exist.  Outside menus:
//   F6  gives the player an example letter to the NPC under the crosshair
//   F7  sends the newest letter the player carries (due in kDelayHours)
//   F8  makes every letter in transit due now
namespace PhysicalLetters::DebugKeys {

    // kDataLoaded.
    void Register();

} // namespace PhysicalLetters::DebugKeys
