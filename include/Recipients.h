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

// Whom the player's letter is for, from its "To:" line: "Name, <number> <place>" (docs/WRITING.md#the-recipient).
// Game thread only.
namespace PhysicalLetters::Recipients {

    struct Recipient {
        RE::Actor* actor = nullptr;  // nullptr for an edit's recipient as it was stored
        std::string uuid;     // registered with SkyrimNet
        std::string name;     // SkyrimNet's name for them
        std::string address;  // "6391 Dawnstar": the end of their UUID and their place
    };

    // A writing session begins: SkyrimNet's answers from the last one are forgotten.
    void Reset();

    // The line changed (each onChange and save): the form map is scanned again, once, for what follows.
    void NewText();

    // What could follow the line as typed, best first, for Ink & Quill's suggestions (docs/WRITING.md#the-name-as-you-type):
    // the rest of a name, or a complete name's addresses.  A complete name registers its people with SkyrimNet.
    std::vector<std::string> Completions(const std::string& line);

    // The one person the line is for ("Name, <address>", the address whole), or why there's none (a
    // message for the player).
    std::variant<Recipient, std::string> Resolve(const std::string& line);

} // namespace PhysicalLetters::Recipients
