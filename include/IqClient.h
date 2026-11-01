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

// What the two Ink & Quill clients here share (Writing: letters; DmLetter: the DM Letter).  Game thread.
namespace PhysicalLetters::IqClient {

    // A session's `user`: its number, so a callback from an older session is told apart.
    inline void* UserOf(std::uint32_t session) { return reinterpret_cast<void*>(static_cast<std::uintptr_t>(session)); }

    inline void Notify(std::string_view text) { RE::SendHUDMessage::ShowHUDMessage(std::string{ text }.c_str()); }

    inline void Refuse(const IQ_API* api, IQ_SaveReply* reply, std::string_view message)
    {
        api->ReplySave(reply, false, std::string{ message }.c_str(), nullptr);
    }

    // While writing: the runs as the player has them, unsaved text included (empty if not writing).
    inline std::vector<std::string> CurrentRuns(const IQ_API* api)
    {
        std::vector<std::string> runs;
        api->CurrentRuns(
            [](void* user, std::int32_t, const char* text) { static_cast<std::vector<std::string>*>(user)->emplace_back(text); }, &runs);
        return runs;
    }

    // A one-line run: a line break typed (Enter) or pasted is taken out.  Whether there was one.
    inline bool TakeLineBreaks(std::string& line)
    {
        const bool broke = line.find_first_of("\r\n") != std::string::npos;
        std::erase(line, '\n');
        std::erase(line, '\r');
        return broke;
    }

} // namespace PhysicalLetters::IqClient
