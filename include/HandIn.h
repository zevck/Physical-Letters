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

#include "LetterDB.h"

// Handing a letter to an NPC in person (docs/HAND_IN.md): the topic "I have a letter for you."
// (every letter carries its gift menu's keyword) and the letters given in its gift menu.  Game thread.
namespace PhysicalLetters::HandIn {

    // Whether `holder` is the letter's recipient.
    bool IsRecipient(RE::Actor* holder, const Letter& letter);

    // Whether `actor` is with the player (alive, loaded, near, in their interior cell or
    // worldspace) and SkyrimNet takes private events: they read a letter handed over on the spot.
    bool NearPlayer(RE::Actor* actor);

    // `reader` reads the letter on the spot: a direct narration only they perceive, which they
    // react to aloud (docs/HAND_IN.md#reading-it-there).
    void ReadThere(const Letter& letter, RE::Actor* reader);

    // Sets the global PhysicalLettersHandInDialogue (the topic's condition) from the setting.
    // kNewGame / kPostLoadGame (a save stores it) and when the MCM changes it.
    void ApplyDialogue();

    // kDataLoaded: watches letters leaving the player's inventory: posted, or handed over in the
    // hand-in topic's gift menu.
    void Register();

    // Registers the TIF's native (PhysicalLetters_HandInQuest.BeginHandIn).
    bool RegisterFunctions(RE::BSScript::IVirtualMachine* vm);

    void Revert();

} // namespace PhysicalLetters::HandIn
