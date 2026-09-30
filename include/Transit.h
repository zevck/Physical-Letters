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

// Letters on their way, delivered letters their recipient hasn't read yet, and replies on
// their way to the courier.  The queue lives in the co-save, not LetterDB: it must revert
// with the save, so loading a save from before a letter was sent never delivers it, and
// loading one made before the reading finished reads it again.  Game thread only.
namespace PhysicalLetters::Transit {

    // Takes the letter from whoever holds it (the player, or the innkeeper it was handed to);
    // it reaches its recipient after the travel time from there (Travel::Hours).  Returns that
    // time in game hours, nothing if the holder hasn't the letter.
    std::optional<double> Send(RE::TESObjectBOOK* book, const Letter& letter, RE::TESObjectREFR* holder);

    // Delivers what is due, starts (or retries) the readings owed, and hands replies to the
    // courier.  Runs every heartbeat once the session is ready.
    void Tick();

    // Debug: makes every letter in transit due now.
    void MakeAllDue();

    void Save(SKSE::SerializationInterface* a_intfc, std::uint32_t a_type);
    void Load(SKSE::SerializationInterface* a_intfc, std::uint32_t a_version);
    void Revert();

} // namespace PhysicalLetters::Transit
