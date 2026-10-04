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

struct sqlite3;

namespace PhysicalLetters {

    // One letter.  Written once, so one row serves every save of the character; keyed by
    // the letter id, never by FormID (docs/PERSISTENCE.md).
    struct Letter {
        std::string id;             // random 128-bit hex; also the letter form's DynamicForms key
        std::string authorUuid;
        std::string authorName;
        std::string recipientUuid;
        std::string recipientName;
        std::string body;           // plain text, \n line breaks
        double      writtenAt = 0;  // game days
        std::string inReplyTo;      // a reply: the id of the letter it answers
        // The player's letter written partly in blood: the body with that text between U+E000
        // and U+E001; "" if none was.  The book shows it red; everything else reads `body`.
        std::string blood;
        // The player's letter: the recipient's address as the "To:" line shows it after their name
        // ("6391 Dawnstar"); "" for other letters and those written before addresses.
        std::string address;
        // The player's letter begun in blood: its "To:" line is red (decided when writing begins).
        bool bloodHeading = false;
    };

    // SQLite store for letters, one per SkyrimNet save folder:
    //   Data/SKSE/Plugins/PhysicalLetters/SkyrimNet-<save id>/letters.db
    // Thread-safe (NPC reading runs on worker threads).
    class LetterDB {
    public:
        static LetterDB* GetSingleton();

        // Opens (or creates) the DB for SkyrimNet's save id.  The same id again does
        // nothing; another id closes the old DB first.
        bool Open(const std::string& saveId);
        void Close();
        bool IsOpen() const;

        bool Insert(const Letter& letter);
        std::optional<Letter> Get(const std::string& id);

        // Every letter between the two, either way, oldest first.  From every save of the
        // character: the caller decides which belong to this one.
        std::vector<Letter> Between(const std::string& uuidA, const std::string& uuidB);
        // Every letter the actor wrote or received, newest first, at most `limit`.
        std::vector<Letter> Involving(const std::string& uuid, int limit);

        bool MarkDelivered(const std::string& id, double gameDays);

        // The recipient's reading: the LLM's answer (JSON) and the memory it became.  A
        // record only: whether a delivery was read is SkyrimNet's memory (Reading::DeliveryTag).
        bool SetReading(const std::string& id, const std::string& readingJson, int memoryId);
        // The stored reading's JSON, "" if there is none.
        std::string GetReading(const std::string& id);

    private:
        LetterDB() = default;
        bool EnsureSchema();
        bool Exec(const char* sql);
        bool AddColumn(const char* sql);

        mutable std::mutex mutex_;
        sqlite3*           db_ = nullptr;
        std::string        openId_;
    };

} // namespace PhysicalLetters
