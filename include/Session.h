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

// The play session: from a load or new game until the next one.  A session is ready
// once SkyrimNet has settled this save's history and LetterDB is open for its save
// folder.  Nothing that reads or writes letters or SkyrimNet runs before that.
namespace PhysicalLetters::Session {

    // kPreLoadGame, and kNewGame before Start: closes LetterDB.
    void End();
    // kPostLoadGame / kNewGame: from now on Poll may make the session ready.
    void Start();

    // Game thread, every heartbeat: becomes ready once SkyrimNet is (see above).
    void Poll();
    bool IsReady();

    // Changes at every End.  Work that finishes on another thread checks it's still in
    // the session it started in before writing anything.
    std::uint32_t Generation();

} // namespace PhysicalLetters::Session
