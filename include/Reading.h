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

// The recipient reads a delivered letter: one LLM call (the prompt
// physical_letters_read_letter, shipped in the SkyrimNet plugin folder
// SKSE/Plugins/SkyrimNet/external/zevick.physical-letters) returns their memory of it,
// their emotion, and whether and what they reply.  The memory goes into SkyrimNet, tagged
// with the letter (LetterTag), so the NPC knows about the letter from then on.
namespace PhysicalLetters::Reading {

    enum class Result {
        kRead,     // the recipient remembers the letter (now, or from an earlier reading)
        kRetry,    // it failed and may work another time (LLM, JSON, SkyrimNet, a load)
        kAbandon,  // it can never work (the letter isn't in LetterDB)
    };

    // The tag on the memory of a letter.  Its presence in SkyrimNet, not our own records,
    // says whether the letter was read: it survives Keep and goes with Clear.
    std::string LetterTag(const std::string& letterId);

    // Game thread.  The work runs on other threads; `done` is then called on the game
    // thread, once, unless SkyrimNet drops the LLM task (the caller times out).
    void Read(const std::string& letterId, RE::FormID recipientFormId, std::function<void(Result)> done);

} // namespace PhysicalLetters::Reading
