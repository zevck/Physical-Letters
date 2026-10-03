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

// Ink & Quill's markers in marked text and runs (its docs/API.md#marked-text), and the string
// helpers that read the runs it hands back.
namespace PhysicalLetters::MarkedText {

    inline constexpr std::string_view kBloodOpen = "\xEE\x80\x80";   // U+E000
    inline constexpr std::string_view kBloodClose = "\xEE\x80\x81";  // U+E001
    inline constexpr std::string_view kLockOpen = "\xEE\x80\x82";    // U+E002
    inline constexpr std::string_view kLockClose = "\xEE\x80\x83";   // U+E003

    inline std::string Without(std::string text, std::string_view marker)
    {
        for (std::size_t at; (at = text.find(marker)) != std::string::npos;) text.erase(at, marker.size());
        return text;
    }

    // The text without its blood markers: what it says, as stored and matched.
    inline std::string WithoutBlood(std::string text)
    {
        return Without(Without(std::move(text), kBloodOpen), kBloodClose);
    }

    inline std::string TrimLeft(std::string_view text)
    {
        const auto first = text.find_first_not_of(" \t\r\n");
        return first == std::string_view::npos ? std::string{} : std::string{ text.substr(first) };
    }

    inline std::string Trim(std::string_view text)
    {
        std::string out = TrimLeft(text);
        out.erase(out.find_last_not_of(" \t\r\n") + 1);
        return out;
    }

} // namespace PhysicalLetters::MarkedText
