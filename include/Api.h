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

#include <nlohmann/json.hpp>

// The API for other mods (docs/API.md): sending letters (DmLetter::Send), the events (mod events and C callbacks),
// and letter actions a reader may choose.  Game thread, except where said.
namespace PhysicalLetters::Api {

    // The events, fired where they happen (Transit, HandIn, DmLetter).
    void Delivered(const std::string& letterId, RE::FormID recipient);
    void Read(const std::string& letterId, RE::FormID reader, bool replied);
    void Failed(const std::string& letterId);

    // The registered actions for the reading prompt: [{ name, description, params: [{ name, description }] }].  Any thread.
    nlohmann::json ActionsForPrompt(bool fromPlayer);

    // The reader of the letter chose an action (Reading's answer): its event, if the action is still registered.
    void ActionChosen(const std::string& letterId, RE::FormID reader, const std::string& name,
                      const std::vector<std::pair<std::string, std::string>>& values);

    // The Papyrus natives (script PhysicalLetters).
    bool RegisterFunctions(RE::BSScript::IVirtualMachine* vm);

} // namespace PhysicalLetters::Api
