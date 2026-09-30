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

// Handing a letter to an innkeeper or the courier (docs/DELIVERY.md#the-hand-over).  The
// ESP's dialogue opens the gift menu, filtered to the player's own letters; when one is
// given, this takes the postage, sends the letter and closes the menu.
namespace PhysicalLetters::Postage {

    // kDataLoaded.
    void Register();

    // kPostLoadGame / kNewGame: (re)start the postage quest.  Its topic only reaches the
    // dialogue menu when the quest starts while the game runs: start-game-enabled, it ran
    // but its topic never showed (tested on AE, with and without flag 0x10).  A quest
    // already running (an older save) is stopped first.
    void RestartDialogue();
    // Heartbeat: finishes a restart once the stopped quest has stopped.
    void Tick();

} // namespace PhysicalLetters::Postage
