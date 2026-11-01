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

#include "InkAndQuillAPI.h"

// The player writes letters through Ink & Quill - Writing Framework, its client here
// (docs/WRITING.md): in parchment (a blank), and in their own letters until sent.
namespace PhysicalLetters::Writing {

    // kPostLoad: finds Ink & Quill's API.
    void Connect();

    // Whether letters can be written: Ink & Quill was found and its writing is on.  From kDataLoaded.
    bool Available();

    // kDataLoaded: registers parchment and the player's letters with Ink & Quill, if its writing is on.
    void Register();

    // Ink & Quill's API (the header's version, 3), or nullptr.
    const IQ_API* Api();

    // While writing: Ink & Quill's suggestions for a name line (docs/WRITING.md#the-name-as-you-type), with the caret at
    // its end (counted as Ink & Quill does, without blood markers) unless `force`: a completion goes after it.
    void Suggest(const std::string& line, std::int32_t caretOffset, bool force);

} // namespace PhysicalLetters::Writing
