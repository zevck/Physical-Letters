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

// Letters on their way, and delivered letters their recipient hasn't read yet.  The
// queue lives in the co-save, not LetterDB: it must revert with the save, so loading a
// save from before a letter was sent never delivers it, and loading one made before the
// reading finished reads it again.  Game thread only.
namespace PhysicalLetters::Transit {

    // Takes the letter from the player and delivers it to its recipient `delayHours`
    // of game time from now.
    bool Send(RE::TESObjectBOOK* book, const Letter& letter, double delayHours);

    // Delivers what is due and starts (or retries) the readings owed.  Runs every
    // heartbeat once the session is ready.
    void Tick();

    // Debug: makes every letter in transit due now.
    void MakeAllDue();

    void Save(SKSE::SerializationInterface* a_intfc, std::uint32_t a_type);
    void Load(SKSE::SerializationInterface* a_intfc, std::uint32_t a_version);
    void Revert();

} // namespace PhysicalLetters::Transit
