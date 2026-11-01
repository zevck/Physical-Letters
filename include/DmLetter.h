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

// The DM Letter (docs/DM_LETTER.md): the player, as the story's dungeon master, sends a letter in someone's name.
// An Ink & Quill blank that's never used up and needs no materials.  Game thread.
namespace PhysicalLetters::DmLetter {

    // PhysicalLettersDMLetter.
    inline constexpr RE::FormID kFormId = 0x8BB;

    RE::TESObjectBOOK* Form();

    // Any thread: its item card's text in the chosen language if `description` is its card, else "" (TextHook).
    std::string CardFor(const RE::TESDescription* description);

    // kDataLoaded, after Writing::Register: names it in the chosen language and registers it with Ink & Quill (version 3).
    void Register();

    // Heartbeat: a writer asked for their letter and silent for 5 minutes has failed (SkyrimNet drops such calls).
    void Tick();

    // The MCM's AddDmLetter.
    bool RegisterFunctions(RE::BSScript::IVirtualMachine* vm);

} // namespace PhysicalLetters::DmLetter
