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

// Every piece of text the player sees, in one place so localization can replace it.
// English only for now.  (Dev-key notifications stay in DebugKeys.)
namespace PhysicalLetters::Strings {

    // A letter's item name: the player's letters by their recipient, letters to the player
    // by their author.
    inline std::string LetterName(const std::string& recipientName)
    {
        return recipientName.empty() ? "Letter" : "Letter to " + recipientName;
    }

    inline std::string LetterFromName(const std::string& authorName)
    {
        return authorName.empty() ? "Letter" : "Letter from " + authorName;
    }

    // A letter's item card, under its model in the inventory.
    inline std::string LetterCard(const std::string& recipientName, const std::string& authorName)
    {
        return std::format("A letter to {} from {}.", recipientName, authorName);
    }

    // A letter's page before LetterDB is open (moments after a load).
    inline constexpr std::string_view kLetterPending = "...";
    // A letter LetterDB has no text for.
    inline constexpr std::string_view kLetterUnreadable = "The ink has run; the letter can't be read.";

    // The player's letter's first line, before the recipient's name (docs/WRITING.md#the-text).
    inline constexpr std::string_view kToLabel = "To: ";

    // A save of a letter the player writes, refused (docs/WRITING.md#saving).
    inline constexpr std::string_view kWriteNotReady = "Letters aren't ready yet. Try again in a moment.";
    inline constexpr std::string_view kWriteNoName = "The letter isn't addressed yet. Write who it's for after \"To:\".";
    inline constexpr std::string_view kWriteEmpty = "Nothing is written in the letter yet.";
    inline constexpr std::string_view kWriteFailed = "The letter couldn't be saved. See PhysicalLetters.log.";
    inline std::string WriteNobody(const std::string& name)
    {
        return std::format("Nobody named \"{}\" can receive a letter.", name);
    }
    inline std::string WriteSeveral(const std::string& name, const std::string& addresses, const std::string& example)
    {
        return std::format("Several people are named \"{}\" ({}). Write which after the name, as \"{}, {}\".", name, addresses,
                           name, example);
    }
    inline std::string WriteAddress(const std::string& name, const std::string& address)
    {
        return std::format("Write {}'s address after the name: \"{}, {}\".", name, name, address);
    }
    inline std::string WriteTooMany(const std::string& name)
    {
        return std::format("Too many people are named \"{}\" to address a letter to one of them.", name);
    }
    inline std::string WriteNoneAt(const std::string& name, const std::string& typed, const std::string& addresses)
    {
        return std::format("Nobody named \"{}\" fits \"{}\". The addresses for that name: {}.", name, typed, addresses);
    }

    // An address's place when the recipient's home can't be placed under a hold (docs/WRITING.md#the-recipient).
    inline constexpr std::string_view kNoPlace = "Tamriel";

    // Added to a returned letter's item card.
    inline std::string_view ReturnToSender(bool dead)
    {
        return dead ? "Return to sender (deceased)" : "Return to sender (not found)";
    }

} // namespace PhysicalLetters::Strings
