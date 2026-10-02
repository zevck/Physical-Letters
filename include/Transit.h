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

    // A letter's way, straight from where it set out to where it goes, on one worldspace's
    // map (docs/ROAD_COURIER.md); world 0 when it can't be placed.
    struct Route {
        RE::FormID world = 0;
        float fromX = 0, fromY = 0, toX = 0, toY = 0;
        double departAt = 0;  // game days: when it set out (a reply, once written)
    };

    // A letter on its way now, to an NPC or to the courier for the player.
    struct OnTheRoad {
        std::string deliveryId;
        Route route;
        double dueAt = 0;
    };

    // Takes the letter from whoever holds it (the player, or the innkeeper it was handed to);
    // it reaches its recipient after the travel time from there (Travel::Hours).  Returns that
    // time in game hours, nothing if the holder hasn't the letter.
    std::optional<double> Send(RE::TESObjectBOOK* book, const Letter& letter, RE::TESObjectREFR* holder);

    // The player handed `reader` the letter; to its recipient it's delivered now.  Read there, aloud,
    // near the player (HandIn::ReadThere), then the reading: a memory, maybe a reply (docs/HAND_IN.md).
    void HandIn(const Letter& letter, RE::Actor* reader);

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

    // The letters on their way now, with a route.
    std::vector<OnTheRoad> LettersOnTheRoad();

    // The road courier (docs/ROAD_COURIER.md): these letters go into his inventory; how many.
    int TakeForRoad(RE::Actor* courier, const std::vector<std::string>& deliveryIds);

    // The recipient of a letter he carries to an NPC (his destination), or nullptr.
    RE::Actor* RoadRecipient();

    // The route of a letter he carries, or nothing.
    std::optional<Route> RoadRoute();

    // He gives his letters to the player: they're the player's now.  The forms to move to them.
    std::vector<RE::TESForm*> RoadHandOver(RE::Actor* courier);

    // He reached his destination town without its recipient at hand: his letters go on, due
    // now (delivered in town as any other).
    void RoadOnward(RE::Actor* courier);

    // He hands his letter for `recipient` over: delivered, then read.  False if he hasn't one.
    bool RoadDeliver(RE::Actor* courier, RE::Actor* recipient);

    // The encounter ended: letters he still has go on their way; missing ones the player took.
    void RoadDone(RE::Actor* courier);

    // Delivers what is due, starts (or retries) the readings owed, and hands replies to the
    // courier.  Runs every heartbeat once the session is ready.
    void Tick();

    // Debug: makes every letter in transit due now.
    void MakeAllDue();

    void Save(SKSE::SerializationInterface* a_intfc, std::uint32_t a_type);
    void Load(SKSE::SerializationInterface* a_intfc, std::uint32_t a_version);
    void Revert();

} // namespace PhysicalLetters::Transit
