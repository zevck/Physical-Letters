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

// Strings in co-save records: a uint32 length, then the bytes.  (DynamicForms keeps its
// own copy so it stays identical to SkyrimNet Physical Diaries'.)
namespace PhysicalLetters::CoSave {

    inline constexpr std::uint32_t kMaxStringLength = 4096;  // a longer one is corruption

    inline void WriteString(SKSE::SerializationInterface* a_intfc, const std::string& a_text)
    {
        const auto length = static_cast<std::uint32_t>(a_text.size());
        a_intfc->WriteRecordData(length);
        if (length > 0) a_intfc->WriteRecordData(a_text.data(), length);
    }

    inline bool ReadString(SKSE::SerializationInterface* a_intfc, std::string& a_text)
    {
        std::uint32_t length = 0;
        if (a_intfc->ReadRecordData(length) != sizeof(length) || length > kMaxStringLength) return false;
        a_text.assign(length, '\0');
        return length == 0 || a_intfc->ReadRecordData(a_text.data(), length) == length;
    }

} // namespace PhysicalLetters::CoSave
