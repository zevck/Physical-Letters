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

#include "LetterDB.h"

// Handing a letter to an innkeeper or the courier (docs/DELIVERY.md#the-hand-over).  The
// ESP's dialogue opens the gift menu, filtered to the player's own letters; when one is
// given (HandIn watches every transfer), this takes the postage, sends it and closes the menu.
namespace PhysicalLetters::Postage {

    // Who the postage topic's conditions allow: an innkeeper or the courier.
    bool TakesPost(RE::Actor* a_holder);

    // Game thread.  The letter is in the holder's inventory, given in a gift menu: posted from
    // there, or given back if it can't be (no letter record: `a_letter` empty).
    void Post(RE::TESObjectBOOK* a_book, RE::Actor* a_holder, const std::optional<Letter>& a_letter);

    // Sets the postage global (the topic's price and gold conditions) from the settings.
    // kNewGame / kPostLoadGame (a save stores the global's value) and when the MCM changes it.
    void ApplyPrice();

} // namespace PhysicalLetters::Postage
