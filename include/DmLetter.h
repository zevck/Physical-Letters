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

    // A letter sent by another mod (docs/API.md), as the page would send it.  `writer` nullptr: unsigned, or signed with
    // `signature`.  `hours` -1: the travel time.  Returns the letter id at once ("" and a log line if refused).
    struct Request {
        RE::Actor* writer = nullptr;
        std::string signature;
        RE::Actor* recipient = nullptr;  // may be the player
        std::string text;                // empty with a writer: they choose the subject
        bool transform = false;
        bool forgery = false;
        int hours = -1;
        std::string tag;  // the mod's label (docs/API.md#finding-letters), at most 64 bytes
    };
    std::string Send(const Request& request);

    // Whether the letter is still being written (Transform, or the writer's choice): its id is known, the letter isn't.
    bool IsBeingWritten(const std::string& letterId);

    // Any thread: its item card's text in the chosen language if `description` is its card, else "" (TextHook).
    std::string CardFor(const RE::TESDescription* description);

    // kDataLoaded, after Writing::Register: names it in the chosen language and registers it with Ink & Quill (API version 2).
    void Register();

    // Heartbeat: a writer asked for their letter and silent for 5 minutes has failed (SkyrimNet drops such calls).
    void Tick();

    // Letters still being written when the game is saved: after loading that save they never come, so Tick fails them
    // (the API's Failed event, the DM's notice, the package back).
    void Save(SKSE::SerializationInterface* a_intfc, std::uint32_t a_type);
    void Load(SKSE::SerializationInterface* a_intfc, std::uint32_t a_version);
    void Revert();

    // The MCM's AddDmLetter.
    bool RegisterFunctions(RE::BSScript::IVirtualMachine* vm);

} // namespace PhysicalLetters::DmLetter
