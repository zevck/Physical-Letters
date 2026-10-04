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

// The recipient reads a delivered letter: one LLM call (the prompt
// physical_letters/read_letter, shipped in the SkyrimNet plugin folder
// SKSE/Plugins/SkyrimNet/external/zevick.physical-letters) returns their memory of it,
// their emotion, and whether and what they reply.  The memory goes into SkyrimNet, tagged
// with the letter and the delivery, so the NPC knows about the letter from then on.
namespace PhysicalLetters::Reading {

    enum class Result {
        kRead,     // the recipient remembers the letter (now, or from an earlier reading)
        kRetry,    // it failed and may work another time (LLM, JSON, SkyrimNet, a load)
        kAbandon,  // it can never work (the letter isn't in LetterDB)
    };

    struct Outcome {
        Result      result = Result::kRetry;
        std::string reply;  // kRead: the recipient's reply letter, "" if they don't write back
    };

    // The tags on the memory of a reading.  The letter tag names the letter (the
    // correspondence history counts it).  The delivery tag names one delivery: its presence
    // in SkyrimNet, not our own records, says whether that delivery was read (it survives
    // Keep and goes with Clear).  A letter sent again is a new delivery, so it's read again.
    std::string LetterTag(const std::string& letterId);
    std::string DeliveryTag(const std::string& deliveryId);

    // The earlier letters between `readerUuid` and `otherUuid` that the reader knows of in
    // this timeline, oldest first (at most 20): { from, to, days_ago, body }.  SkyrimNet's
    // memory decides (docs/READING.md#the-prompt).  `skipId` is left out; if
    // `skipRemembered`, the reader's replies to it still count.  Blocks: not on the game thread.
    nlohmann::json Correspondence(const std::string& readerUuid, const std::string& otherUuid, RE::FormID readerFormId,
                                  double now, const std::string& skipId = {}, bool skipRemembered = false);

    // Who reads: the letter's recipient, or someone else the player handed it to
    // (docs/HAND_IN.md#someone-elses-letter).  Someone else never replies.
    enum class Reader { kRecipient, kHandedOther };

    // Game thread.  The work runs on other threads; `done` is then called on the game
    // thread, once, unless SkyrimNet drops the LLM task (the caller times out).
    // `canReply` false (a thread between NPCs at its limit, or [NpcLetters] Replies off): the recipient
    // is told not to reply, and no reply is returned.
    void Read(const std::string& letterId, const std::string& deliveryId, RE::FormID readerFormId, bool canReply, Reader reader,
              std::function<void(Outcome)> done);

} // namespace PhysicalLetters::Reading
