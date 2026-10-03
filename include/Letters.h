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

    // Why a letter came back to the player (its item card says so).  Saved in the co-save.
    enum class Returned : std::uint8_t {
        kDead = 1,
        kNotFound = 2,
    };

    // kDataLoaded: whether the vanilla letter every letter takes its look from is there
    // (logs an error if not).
    bool CheckTemplate();

    // A new letter id (random 128-bit, hex).
    std::string NewId();

    // Game thread.  Creates the letter's form, records it in LetterDB and the snapshot.
    // Nobody holds it yet.  nullptr if the form couldn't be made.
    RE::TESObjectBOOK* Create(const Letter& letter);

    // The letter's text as the book menu reads it (UTF-8 book markup; TextHook converts it), and
    // as Ink & Quill edits it: the player's letters open with a "To:" line (docs/WRITING.md#the-text).
    std::string Reading(const Letter& letter);
    // `bodyLocked`: only the "To:" line can be written in (a new letter until it has a recipient).
    std::string Marked(const Letter& letter, bool bodyLocked = false);

    // Game thread.  An edit: the letter's form now holds `letter`, a new LetterDB record (the old one
    // stays, for other saves).  False if it couldn't be stored, and nothing changed.
    bool Rewrite(RE::TESObjectBOOK* book, const Letter& letter);

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

    // Marks a letter as returned (its card adds a line "Return to sender (…)"), or clears it when
    // it's sent again.  Thread-safe.
    void SetReturned(const std::string& letterId, std::optional<Returned> reason);
    // For the co-save.
    std::vector<std::pair<std::string, Returned>> ReturnedLetters();
    RE::FormID FormFor(const std::string& letterId);

} // namespace PhysicalLetters::Letters
