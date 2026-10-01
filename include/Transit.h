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

    // An NPC's letter to the player (already created): it goes to the courier after `hours`.
    void QueueToPlayer(const Letter& letter, double hours);

    // An NPC's letter to another NPC (already created): delivered after `hours`, then read.
    void QueueToNpc(const Letter& letter, double hours);

    // The letters on their way to an NPC or delivered and not yet read.
    std::vector<std::string> PendingLetterIds();

    // Whether a letter to this NPC is on its way or delivered and not yet read: they have one
    // to answer, so they don't write first (docs/NPC_LETTERS.md#who, docs/NPC_TO_NPC.md).
    bool IsLetterPendingFor(const std::string& uuid);

    // The courier's errand (docs/COURIER.md): puts a waiting letter whose recipient he can reach
    // now in his inventory and returns that recipient; nullptr if none.
    RE::Actor* TakeForCourier(RE::Actor* courier);

    // He hands it over: delivered, then read.  If he hasn't it any more, it's lost.
    void HandOver(RE::Actor* courier);

    // The errand ended: a letter not handed over goes in off-screen.
    void CourierDone(RE::Actor* courier);

    // Delivers what is due, starts (or retries) the readings owed, and hands replies to the
    // courier.  Runs every heartbeat once the session is ready.
    void Tick();

    // Debug: makes every letter in transit due now.
    void MakeAllDue();

    void Save(SKSE::SerializationInterface* a_intfc, std::uint32_t a_type);
    void Load(SKSE::SerializationInterface* a_intfc, std::uint32_t a_version);
    void Revert();

} // namespace PhysicalLetters::Transit
