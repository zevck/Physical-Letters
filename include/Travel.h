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

// How long a letter takes to travel: the engine's fast-travel time between the two
// places, along the navmesh as fast travel measures it (docs/DELIVERY.md).  Game thread.
namespace PhysicalLetters::Travel {

    // Game hours for a letter to go from one reference to the other.
    double Hours(RE::TESObjectREFR* a_from, RE::TESObjectREFR* a_to);

    // Straight-line game units between the two places, as Hours resolves them (an interior
    // is its location's exterior marker); nothing if they share no root worldspace or a
    // place can't be found.
    std::optional<double> Distance(RE::TESObjectREFR* a_from, RE::TESObjectREFR* a_to);

    // The area the reference is in, like a postcode: its settlement (the nearest location up
    // the parents with LocTypeHabitation: the Bannered Mare and the street outside are both
    // Whiterun), else the named place below the hold (a dungeon, a camp); nullptr in the
    // open wilderness (the hold itself) or when it has no location.
    const RE::BGSLocation* Area(RE::TESObjectREFR* a_ref);

    // A settlement: the location has LocTypeHabitation.
    bool IsTown(const RE::BGSLocation* a_location);

} // namespace PhysicalLetters::Travel
