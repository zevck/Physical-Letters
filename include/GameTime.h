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

namespace PhysicalLetters::GameTime {

    // Game days passed: the clock every schedule here runs on.  SkyrimNet stores game time
    // as this times 86400 (game seconds).
    inline double Now()
    {
        auto* calendar = RE::Calendar::GetSingleton();
        return calendar ? calendar->GetDaysPassed() : 0.0;
    }

} // namespace PhysicalLetters::GameTime
