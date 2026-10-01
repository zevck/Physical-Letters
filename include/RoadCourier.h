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

// The vanilla courier met on the road with the letters passing there (docs/ROAD_COURIER.md).
// The quest PLRoadCourier runs the encounter; Transit owns the letters.  Game thread.
namespace PhysicalLetters::RoadCourier {

    // Heartbeat: whether a letter passes the player now (the Story Manager node's global).
    void Tick();

    // An encounter is under way in this session (the courier carries letters on the road).
    bool IsLive();

    // Registers the quest script's natives.
    bool RegisterFunctions(RE::BSScript::IVirtualMachine* vm);

    void Save(SKSE::SerializationInterface* a_intfc, std::uint32_t a_type);
    void Load(SKSE::SerializationInterface* a_intfc, std::uint32_t a_version);
    void Revert();

} // namespace PhysicalLetters::RoadCourier
