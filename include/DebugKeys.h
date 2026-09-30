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

// DEV HARNESS until the editor exists.  Outside menus:
//   F6  gives the player an example letter to the NPC under the crosshair
//   F7  sends the newest letter the player wrote and carries (arrives after Travel::Hours)
//   F8  makes every letter in transit due now, replies included (they go to the courier)
namespace PhysicalLetters::DebugKeys {

    // kDataLoaded.
    void Register();

    // Session ready: a letter to a recipient who is never found, unless the player carries
    // one already (tests docs/DELIVERY.md#undeliverable-letters).  Dev only.
    void GiveUndeliverableLetter();

} // namespace PhysicalLetters::DebugKeys
