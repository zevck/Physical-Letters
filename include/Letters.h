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

// Letter items: one runtime book form per letter (DynamicForms), keyed by the letter id.
// The save keeps the forms; the co-save record says which letter each one is; LetterDB
// holds the text.  The text the game shows comes from a thread-safe snapshot here,
// served by the GetDescription hook (TextHook).
namespace PhysicalLetters::Letters {

    // kDataLoaded: whether the vanilla letter every letter takes its look from is there
    // (logs an error if not).
    bool CheckTemplate();

    // A new letter id (random 128-bit, hex).
    std::string NewId();

    // Game thread.  Creates the letter's form, records it in LetterDB and the snapshot.
    // Nobody holds it yet.  nullptr if the form couldn't be made.
    RE::TESObjectBOOK* Create(const Letter& letter);

    // Load callback: gives this save's letter forms their look and name from their
    // records.  Their text shows a placeholder until AttachTexts.
    void ConfigureLoaded();
    // Once LetterDB is open: the text of every letter form in this save.
    void AttachTexts();
    // A new game or a load.
    void Revert();

    // Thread-safe lookups for the text hook and other code.
    RE::FormID FindByDescription(const RE::TESDescription* description);
    std::string TextFor(RE::FormID formId);
    // The item card's text ("A letter to X from Y.") if `description` is a letter's item
    // card, "" otherwise.
    std::string CardFor(const RE::TESDescription* description);
    std::string IdFor(RE::FormID formId);
    RE::FormID FormFor(const std::string& letterId);

} // namespace PhysicalLetters::Letters
