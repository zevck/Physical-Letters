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

#include "LetterDB.h"

#include <sqlite3.h>
#include <filesystem>
#include <variant>

namespace PhysicalLetters {

    namespace {
        // A prepared statement that finalizes itself (as in SNPD's DiaryDB).  A failed
        // prepare is logged and makes every call a no-op.
        class Statement {
        public:
            Statement(sqlite3* db, const char* sql, const char* tag) : db_(db), tag_(tag)
            {
                if (sqlite3_prepare_v2(db, sql, -1, &stmt_, nullptr) != SQLITE_OK) {
                    SKSE::log::error("[LetterDB] {} prepare: {}", tag, sqlite3_errmsg(db));
                    sqlite3_finalize(stmt_);
                    stmt_ = nullptr;
                }
            }
            ~Statement() { sqlite3_finalize(stmt_); }
            Statement(const Statement&) = delete;
            Statement& operator=(const Statement&) = delete;

            Statement& Bind(int i, const std::string& v) { sqlite3_bind_text(stmt_, i, v.c_str(), -1, SQLITE_TRANSIENT); return *this; }
            Statement& Bind(int i, int v) { sqlite3_bind_int(stmt_, i, v); return *this; }
            Statement& Bind(int i, double v) { sqlite3_bind_double(stmt_, i, v); return *this; }

            bool Run()
            {
                if (!stmt_) return false;
                const bool ok = sqlite3_step(stmt_) == SQLITE_DONE;
                if (!ok) SKSE::log::error("[LetterDB] {} step: {}", tag_, sqlite3_errmsg(db_));
                return ok;
            }
            bool Next() { return stmt_ && sqlite3_step(stmt_) == SQLITE_ROW; }

            int Int(int col) const { return sqlite3_column_int(stmt_, col); }
            double Double(int col) const { return sqlite3_column_double(stmt_, col); }
            std::string Text(int col) const
            {
                const auto* text = sqlite3_column_text(stmt_, col);
                return text ? reinterpret_cast<const char*>(text) : "";
            }

        private:
            sqlite3*      db_;
            const char*   tag_;
            sqlite3_stmt* stmt_ = nullptr;
        };
    }

    LetterDB* LetterDB::GetSingleton()
    {
        static LetterDB instance;
        return &instance;
    }

    bool LetterDB::Open(const std::string& saveId)
    {
        std::lock_guard lock{ mutex_ };
        if (db_ && openId_ == saveId) return true;
        if (db_) {
            sqlite3_close(db_);
            db_ = nullptr;
            openId_.clear();
        }

        // The id becomes a path component: digits and dashes only.
        const bool validId = !saveId.empty() &&
                             std::ranges::all_of(saveId, [](char c) { return (c >= '0' && c <= '9') || c == '-'; });
        if (!validId) {
            SKSE::log::error("[LetterDB] Refusing to open for invalid save id '{}'", saveId);
            return false;
        }

        const auto path = std::filesystem::current_path() / "Data" / "SKSE" / "Plugins" / "PhysicalLetters" /
                          ("SkyrimNet-" + saveId) / "letters.db";
        std::error_code ec;
        std::filesystem::create_directories(path.parent_path(), ec);

        // UTF-8, as SQLite expects: path.string() is the ANSI code page, which throws or
        // mangles a game folder with characters outside it.
        const auto u8 = path.u8string();
        const std::string pathUtf8{ reinterpret_cast<const char*>(u8.data()), u8.size() };
        if (sqlite3_open(pathUtf8.c_str(), &db_) != SQLITE_OK) {
            SKSE::log::error("[LetterDB] Failed to open '{}': {}", pathUtf8, sqlite3_errmsg(db_));
            sqlite3_close(db_);
            db_ = nullptr;
            return false;
        }
        Exec("PRAGMA journal_mode=WAL;");
        Exec("PRAGMA synchronous=NORMAL;");
        if (!EnsureSchema()) {
            sqlite3_close(db_);
            db_ = nullptr;
            return false;
        }
        openId_ = saveId;
        SKSE::log::info("[LetterDB] Opened '{}'", pathUtf8);
        return true;
    }

    void LetterDB::Close()
    {
        std::lock_guard lock{ mutex_ };
        if (db_) {
            sqlite3_close(db_);
            db_ = nullptr;
            openId_.clear();
        }
    }

    bool LetterDB::IsOpen() const
    {
        std::lock_guard lock{ mutex_ };
        return db_ != nullptr;
    }

    bool LetterDB::Exec(const char* sql)
    {
        char* err = nullptr;
        if (sqlite3_exec(db_, sql, nullptr, nullptr, &err) != SQLITE_OK) {
            SKSE::log::error("[LetterDB] SQL error: {}", err ? err : "unknown");
            sqlite3_free(err);
            return false;
        }
        return true;
    }

    bool LetterDB::EnsureSchema()
    {
        // New columns: ALTER TABLE ... ADD COLUMN ... DEFAULT, ignoring "duplicate column".
        return Exec(R"(
            CREATE TABLE IF NOT EXISTS letters (
                letter_id       TEXT PRIMARY KEY,
                author_uuid     TEXT NOT NULL DEFAULT '',
                author_name     TEXT NOT NULL DEFAULT '',
                recipient_uuid  TEXT NOT NULL DEFAULT '',
                recipient_name  TEXT NOT NULL DEFAULT '',
                body            TEXT NOT NULL DEFAULT '',
                written_at      REAL NOT NULL DEFAULT 0,
                delivered_at    REAL NOT NULL DEFAULT 0,
                reading         TEXT NOT NULL DEFAULT '',
                memory_id       INTEGER NOT NULL DEFAULT 0,
                in_reply_to     TEXT NOT NULL DEFAULT '',
                blood           TEXT NOT NULL DEFAULT '',
                address         TEXT NOT NULL DEFAULT '',
                blood_heading   INTEGER NOT NULL DEFAULT 0,
                signed_uuid     TEXT NOT NULL DEFAULT '',
                signed_name     TEXT NOT NULL DEFAULT '',
                signed_address  TEXT NOT NULL DEFAULT '',
                forgery         INTEGER NOT NULL DEFAULT 0,
                anonymous       INTEGER NOT NULL DEFAULT 0,
                dispatched      INTEGER NOT NULL DEFAULT 0,
                tag             TEXT NOT NULL DEFAULT ''
            );
        )") && AddColumn("ALTER TABLE letters ADD COLUMN in_reply_to TEXT NOT NULL DEFAULT '';") &&
               AddColumn("ALTER TABLE letters ADD COLUMN blood TEXT NOT NULL DEFAULT '';") &&
               AddColumn("ALTER TABLE letters ADD COLUMN address TEXT NOT NULL DEFAULT '';") &&
               AddColumn("ALTER TABLE letters ADD COLUMN blood_heading INTEGER NOT NULL DEFAULT 0;") &&
               AddColumn("ALTER TABLE letters ADD COLUMN signed_uuid TEXT NOT NULL DEFAULT '';") &&
               AddColumn("ALTER TABLE letters ADD COLUMN signed_name TEXT NOT NULL DEFAULT '';") &&
               AddColumn("ALTER TABLE letters ADD COLUMN signed_address TEXT NOT NULL DEFAULT '';") &&
               AddColumn("ALTER TABLE letters ADD COLUMN forgery INTEGER NOT NULL DEFAULT 0;") &&
               AddColumn("ALTER TABLE letters ADD COLUMN anonymous INTEGER NOT NULL DEFAULT 0;") &&
               AddColumn("ALTER TABLE letters ADD COLUMN dispatched INTEGER NOT NULL DEFAULT 0;") &&
               AddColumn("ALTER TABLE letters ADD COLUMN tag TEXT NOT NULL DEFAULT '';");
    }

    bool LetterDB::AddColumn(const char* sql)
    {
        // Databases that already have the column answer "duplicate column name".
        char* err = nullptr;
        if (sqlite3_exec(db_, sql, nullptr, nullptr, &err) != SQLITE_OK) {
            const bool duplicate = err && std::string_view{ err }.starts_with("duplicate column name");
            if (!duplicate) SKSE::log::error("[LetterDB] SQL error: {}", err ? err : "unknown");
            sqlite3_free(err);
            return duplicate;
        }
        return true;
    }

    bool LetterDB::Insert(const Letter& letter)
    {
        std::lock_guard lock{ mutex_ };
        if (!db_) return false;
        Statement s{ db_,
                     "INSERT OR REPLACE INTO letters (letter_id, author_uuid, author_name, recipient_uuid, "
                     "recipient_name, body, written_at, in_reply_to, blood, address, blood_heading, signed_uuid, signed_name, "
                     "signed_address, forgery, anonymous, dispatched, tag) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?);",
                     "Insert" };
        return s.Bind(1, letter.id)
            .Bind(2, letter.authorUuid)
            .Bind(3, letter.authorName)
            .Bind(4, letter.recipientUuid)
            .Bind(5, letter.recipientName)
            .Bind(6, letter.body)
            .Bind(7, letter.writtenAt)
            .Bind(8, letter.inReplyTo)
            .Bind(9, letter.blood)
            .Bind(10, letter.address)
            .Bind(11, letter.bloodHeading ? 1 : 0)
            .Bind(12, letter.signedUuid)
            .Bind(13, letter.signedName)
            .Bind(14, letter.signedAddress)
            .Bind(15, letter.forgery)
            .Bind(16, letter.anonymous ? 1 : 0)
            .Bind(17, letter.dispatched ? 1 : 0)
            .Bind(18, letter.tag)
            .Run();
    }

    namespace {
        constexpr auto kLetterColumns =
            "letter_id, author_uuid, author_name, recipient_uuid, recipient_name, body, written_at, in_reply_to, blood, address, blood_heading, "
            "signed_uuid, signed_name, signed_address, forgery, anonymous, dispatched, tag";

        Letter ReadLetter(const Statement& s)
        {
            return Letter{ .id = s.Text(0),
                           .authorUuid = s.Text(1),
                           .authorName = s.Text(2),
                           .recipientUuid = s.Text(3),
                           .recipientName = s.Text(4),
                           .body = s.Text(5),
                           .writtenAt = s.Double(6),
                           .inReplyTo = s.Text(7),
                           .blood = s.Text(8),
                           .address = s.Text(9),
                           .bloodHeading = s.Int(10) != 0,
                           .signedUuid = s.Text(11),
                           .signedName = s.Text(12),
                           .signedAddress = s.Text(13),
                           .forgery = s.Int(14),
                           .anonymous = s.Int(15) != 0,
                           .dispatched = s.Int(16) != 0,
                           .tag = s.Text(17) };
        }
    }

    std::optional<Letter> LetterDB::Get(const std::string& id)
    {
        std::lock_guard lock{ mutex_ };
        if (!db_) return std::nullopt;
        const auto sql = std::format("SELECT {} FROM letters WHERE letter_id = ?;", kLetterColumns);
        Statement s{ db_, sql.c_str(), "Get" };
        s.Bind(1, id);
        if (!s.Next()) return std::nullopt;
        return ReadLetter(s);
    }

    std::vector<Letter> LetterDB::Between(const std::string& uuidA, const std::string& uuidB)
    {
        std::lock_guard lock{ mutex_ };
        if (!db_ || uuidA.empty() || uuidB.empty()) return {};
        const auto sql = std::format(
            "SELECT {} FROM letters WHERE (((author_uuid = ?1 AND recipient_uuid = ?2) OR (author_uuid = ?2 AND recipient_uuid = ?1)) "
            "AND signed_name = '' AND anonymous = 0) OR (signed_uuid = ?1 AND recipient_uuid = ?2) OR "
            "(signed_uuid = ?2 AND recipient_uuid = ?1) ORDER BY written_at, rowid;",
            kLetterColumns);
        Statement s{ db_, sql.c_str(), "Between" };
        s.Bind(1, uuidA).Bind(2, uuidB);
        std::vector<Letter> letters;
        while (s.Next()) letters.push_back(ReadLetter(s));
        return letters;
    }

    std::vector<Letter> LetterDB::Involving(const std::string& uuid, int limit)
    {
        std::lock_guard lock{ mutex_ };
        if (!db_) return {};
        const auto sql = std::format(
            "SELECT {} FROM letters WHERE author_uuid = ?1 OR recipient_uuid = ?1 ORDER BY written_at DESC, rowid DESC "
            "LIMIT ?2;",
            kLetterColumns);
        Statement s{ db_, sql.c_str(), "Involving" };
        s.Bind(1, uuid).Bind(2, limit);
        std::vector<Letter> letters;
        while (s.Next()) letters.push_back(ReadLetter(s));
        return letters;
    }

    std::vector<Letter> LetterDB::Query(const LetterQuery& query)
    {
        std::lock_guard lock{ mutex_ };
        if (!db_) return {};
        // Built from fixed pieces, every value bound: nothing from the caller goes into the SQL text.
        std::vector<std::string> where;
        std::vector<std::variant<std::string, double>> values;
        const auto bind = [&](std::variant<std::string, double> value) {
            values.push_back(std::move(value));
            return std::format("?{}", values.size());
        };
        using Role = LetterQuery::Role;
        if (!query.actorUuid.empty()) {
            const auto me = bind(query.actorUuid);
            const auto other = query.otherUuid.empty() ? std::string{} : bind(query.otherUuid);
            const auto wrote = std::format("(author_uuid = {}{})", me, other.empty() ? "" : " AND recipient_uuid = " + other);
            const auto got = std::format("(recipient_uuid = {}{})", me, other.empty() ? "" : " AND author_uuid = " + other);
            where.push_back(query.role == Role::kWriter ? wrote : query.role == Role::kRecipient ? got : "(" + wrote + " OR " + got + ")");
        }
        const auto list = [&](const std::vector<std::string>& tags) {
            std::string out;
            for (const auto& tag : tags) out += (out.empty() ? "" : ", ") + bind(tag);
            return out;
        };
        if (!query.includeTags.empty()) where.push_back("tag IN (" + list(query.includeTags) + ")");
        if (!query.excludeTags.empty()) where.push_back("tag NOT IN (" + list(query.excludeTags) + ")");
        if (query.writtenAfter) where.push_back("written_at >= " + bind(*query.writtenAfter));
        if (query.writtenBefore) where.push_back("written_at <= " + bind(*query.writtenBefore));
        std::string sql = std::format("SELECT {} FROM letters", kLetterColumns);
        for (std::size_t i = 0; i < where.size(); ++i) sql += (i ? " AND " : " WHERE ") + where[i];
        sql += query.newestFirst ? " ORDER BY written_at DESC, rowid DESC;" : " ORDER BY written_at ASC, rowid ASC;";
        Statement s{ db_, sql.c_str(), "Query" };
        for (std::size_t i = 0; i < values.size(); ++i) {
            std::visit([&](const auto& value) { s.Bind(static_cast<int>(i + 1), value); }, values[i]);
        }
        std::vector<Letter> letters;
        while (s.Next()) letters.push_back(ReadLetter(s));
        return letters;
    }

    std::vector<std::string> LetterDB::RepliesTo(const std::string& id)
    {
        std::lock_guard lock{ mutex_ };
        if (!db_ || id.empty()) return {};
        Statement s{ db_, "SELECT letter_id FROM letters WHERE in_reply_to = ?1 ORDER BY written_at DESC, rowid DESC;", "RepliesTo" };
        s.Bind(1, id);
        std::vector<std::string> ids;
        while (s.Next()) ids.push_back(s.Text(0));
        return ids;
    }

    bool LetterDB::MarkDelivered(const std::string& id, double gameDays)
    {
        std::lock_guard lock{ mutex_ };
        if (!db_) return false;
        Statement s{ db_, "UPDATE letters SET delivered_at = ? WHERE letter_id = ?;", "MarkDelivered" };
        return s.Bind(1, gameDays).Bind(2, id).Run();
    }

    bool LetterDB::SetReading(const std::string& id, const std::string& readingJson, int memoryId)
    {
        std::lock_guard lock{ mutex_ };
        if (!db_) return false;
        Statement s{ db_, "UPDATE letters SET reading = ?, memory_id = ? WHERE letter_id = ?;", "SetReading" };
        return s.Bind(1, readingJson).Bind(2, memoryId).Bind(3, id).Run();
    }

    std::string LetterDB::GetReading(const std::string& id)
    {
        std::lock_guard lock{ mutex_ };
        if (!db_) return {};
        Statement s{ db_, "SELECT reading FROM letters WHERE letter_id = ?;", "GetReading" };
        s.Bind(1, id);
        return s.Next() ? s.Text(0) : std::string{};
    }

} // namespace PhysicalLetters
