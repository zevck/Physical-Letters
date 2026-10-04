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

// Letters between NPCs (docs/NPC_TO_NPC.md): every few days an NPC may write to someone in
// their own life, chosen by the LLM; the recipient reads it and may reply, within a thread
// of at most MaxLettersPerThread letters.  Delivery and reading are Transit's and Reading's;
// this module starts threads and holds the schedule and the pair cooldowns (co-save).
// Game thread.
namespace PhysicalLetters::NpcToNpc {

    // Heartbeat, once the session is ready.
    void Tick();

    // Neither the author nor the recipient is the player.
    bool IsNpcLetter(const Letter& letter);

    // Whether the recipient of this letter between NPCs may still reply: its thread has room.
    bool CanReply(const std::string& letterId);

    // The thread this letter belongs to is over (no reply, no room, or undeliverable): the
    // pair doesn't write again for PairCooldownDays.
    void ThreadEnded(const Letter& letter);

    void Save(SKSE::SerializationInterface* a_intfc, std::uint32_t a_type);
    void Load(SKSE::SerializationInterface* a_intfc, std::uint32_t a_version);
    void Revert();

} // namespace PhysicalLetters::NpcToNpc
