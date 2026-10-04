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

// NPCs writing to the player first (docs/NPC_LETTERS.md): every few days one NPC the player
// has dealt with, picked at random weighted by how much, may write; the LLM decides whether
// and what.  The schedule and the cooldowns live in the co-save.  Game thread.
namespace PhysicalLetters::NpcLetters {

    // Heartbeat, once the session is ready.
    void Tick();

    // An NPC wrote to the player (first, or a reply): they don't write first again for
    // CooldownDays.
    void StartCooldown(const std::string& uuid);

    void Save(SKSE::SerializationInterface* a_intfc, std::uint32_t a_type);
    void Load(SKSE::SerializationInterface* a_intfc, std::uint32_t a_version);
    void Revert();

} // namespace PhysicalLetters::NpcLetters
