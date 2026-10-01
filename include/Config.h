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

#include <fstream>
#include <sstream>
#include <unordered_map>

// Settings: Data/SKSE/Plugins/PhysicalLetters.ini (docs/SETTINGS.md).  Adapted from
// SNPD's Config.h: every setting is an integer row in kSettings, clamped on read.
namespace PhysicalLetters {

    class Config {
    public:
        struct Setting { const char* section; const char* key; int defaultValue; int min; int max; };
        static constexpr Setting kDebugLog      { "General",  "DebugLog",      0,   0, 1    };
        static constexpr Setting kPostage       { "Delivery", "Postage",       20,  0, 1000 };
        static constexpr Setting kWritingHours  { "Delivery", "WritingHours",  12,  0, 168  };
        static constexpr Setting kMinHours      { "Delivery", "MinHours",      2,   0, 48   };
        static constexpr Setting kFallbackHours { "Delivery", "FallbackHours", 48,  1, 336  };
        // Game days a letter waits, once due, for a recipient who can't be found.
        static constexpr Setting kReturnAfterDays { "Delivery", "ReturnAfterDays", 3, 1, 30 };
        // NPCs writing to the player first (docs/NPC_LETTERS.md).
        static constexpr Setting kNpcLetters     { "NpcLetters", "Enabled",      1,  0, 1   };
        static constexpr Setting kNpcInterval    { "NpcLetters", "IntervalDays", 7,  1, 60  };
        static constexpr Setting kNpcCooldown    { "NpcLetters", "CooldownDays", 14, 0, 120 };
        static constexpr Setting kNpcMinEvents   { "NpcLetters", "MinEvents",    5,  1, 200 };
        // Closer than this is around the player (game units), besides the same area.
        static constexpr Setting kNpcNearDistance  { "NpcLetters", "NearDistance",    8192, 0, 65536 };
        // Weight grows from RecentWeight (percent) to full over MissedAfterDays since the last
        // exchange; MissedAfterDays 0 ignores recency.
        static constexpr Setting kNpcMissedAfter   { "NpcLetters", "MissedAfterDays", 3,    0, 30 };
        static constexpr Setting kNpcRecentWeight  { "NpcLetters", "RecentWeight",    10,   0, 100 };
        // Spoke to the player within this many days: not drawn at all (0 = off).
        static constexpr Setting kNpcMinDaysApart  { "NpcLetters", "MinDaysApart",    1,    0, 30 };
        // NPCs drawn each attempt; with more than one, a cheap call picks who writes.
        static constexpr Setting kNpcCandidates    { "NpcLetters", "CandidatesPerAttempt", 3, 1, 10 };
        // Letters between NPCs (docs/NPC_TO_NPC.md).  KnownOnly: only NPCs the player knows
        // (NpcLetters.MinEvents) write; else anyone SkyrimNet has registered.
        static constexpr Setting kN2nEnabled     { "NpcToNpc", "Enabled",          1,  0, 1   };
        static constexpr Setting kN2nInterval    { "NpcToNpc", "IntervalDays",     5,  1, 60  };
        static constexpr Setting kN2nKnownOnly   { "NpcToNpc", "KnownOnly",        0,  0, 1   };
        static constexpr Setting kN2nMaxThreads  { "NpcToNpc", "MaxOpenThreads",   3,  1, 10  };
        static constexpr Setting kN2nMaxLetters  { "NpcToNpc", "MaxLettersPerThread", 3, 1, 10 };
        static constexpr Setting kN2nPairCooldown { "NpcToNpc", "PairCooldownDays", 21, 0, 120 };
        // An attempt: writers drawn, names each may propose, memories each is shown, and how far
        // apart writer and recipient must be (game units) besides living in different places.
        static constexpr Setting kN2nWriters     { "NpcToNpc", "WritersPerAttempt", 4,  1, 10 };
        static constexpr Setting kN2nNames       { "NpcToNpc", "NamesPerWriter",    3,  1, 5  };
        static constexpr Setting kN2nMemories    { "NpcToNpc", "MemoriesPerWriter", 3,  0, 10 };
        static constexpr Setting kN2nMinDistance { "NpcToNpc", "MinDistance",       16384, 0, 131072 };
        // The courier carrying letters to NPCs in the player's town (docs/COURIER.md); a letter
        // waits for him at most WaitHours (game hours), then goes in off-screen.
        static constexpr Setting kCourierEnabled   { "Courier", "Enabled",   1, 0, 1  };
        static constexpr Setting kCourierWaitHours { "Courier", "WaitHours", 2, 0, 24 };
        // The courier met on the road with letters passing there (docs/ROAD_COURIER.md).
        static constexpr Setting kRoadEncounters   { "Courier", "RoadEncounters",   1, 0, 1  };
        static constexpr Setting kRoadCooldown     { "Courier", "RoadCooldownDays", 3, 0, 30 };
        // Speech needed to threaten him (or the Intimidation perk), besides the engine's check.
        static constexpr Setting kRoadIntimidate   { "Courier", "IntimidateSpeech", 40, 0, 100 };
        // Bounty (gold, non-violent) he reports when he was threatened or beaten into handing over.
        static constexpr Setting kRobberyBounty    { "Courier", "RobberyBounty",    40, 0, 1000 };
        // INI order.
        static constexpr Setting kSettings[] = {
            kDebugLog, kPostage, kWritingHours, kMinHours, kFallbackHours, kReturnAfterDays,
            kNpcLetters, kNpcInterval, kNpcCooldown, kNpcMinEvents, kNpcNearDistance, kNpcMissedAfter, kNpcRecentWeight,
            kNpcMinDaysApart, kNpcCandidates, kN2nEnabled, kN2nInterval, kN2nKnownOnly, kN2nMaxThreads, kN2nMaxLetters, kN2nPairCooldown,
            kN2nWriters, kN2nNames, kN2nMemories, kN2nMinDistance, kCourierEnabled, kCourierWaitHours,
            kRoadEncounters, kRoadCooldown, kRoadIntimidate, kRobberyBounty,
        };

        static Config* GetSingleton()
        {
            static Config singleton;
            return &singleton;
        }

        // "Section.Key" of a row in kSettings, or nullptr.
        static const Setting* Find(std::string_view name)
        {
            for (const auto& s : kSettings) {
                if (name == std::format("{}.{}", s.section, s.key)) return &s;
            }
            return nullptr;
        }

        int Get(const Setting& s) const
        {
            std::lock_guard lock(mutex_);
            const auto it = settings_.find(std::format("{}.{}", s.section, s.key));
            int value = s.defaultValue;
            if (it != settings_.end()) {
                try {
                    value = std::stoi(it->second);
                } catch (...) {
                    SKSE::log::warn("[Config] Invalid value for {}.{}: '{}', using {}", s.section, s.key, it->second,
                                    s.defaultValue);
                }
            }
            return std::clamp(value, s.min, s.max);
        }

        void Set(const Setting& s, int value)
        {
            std::lock_guard lock(mutex_);
            settings_[std::format("{}.{}", s.section, s.key)] = std::to_string(std::clamp(value, s.min, s.max));
        }

        bool Load(const std::filesystem::path& path)
        {
            path_ = path;
            std::ifstream file(path);
            if (!file.is_open()) {
                SKSE::log::info("[Config] No {}: defaults", path.string());
                return false;
            }
            std::string section, line;
            while (std::getline(file, line)) {
                Trim(line);
                if (line.empty() || line[0] == ';' || line[0] == '#') continue;
                if (line.front() == '[' && line.back() == ']') {
                    section = line.substr(1, line.size() - 2);
                    continue;
                }
                const auto eq = line.find('=');
                if (eq == std::string::npos) continue;
                auto key = line.substr(0, eq);
                auto value = line.substr(eq + 1);
                if (const auto comment = value.find(';'); comment != std::string::npos) value.erase(comment);
                Trim(key);
                Trim(value);
                settings_[section + "." + key] = value;
            }
            // Out-of-range or invalid values are replaced now, so the warning is logged once.
            for (const auto& s : kSettings) Set(s, Get(s));
            for (const auto& s : kSettings) SKSE::log::info("[Config] {}.{} = {}", s.section, s.key, Get(s));
            return true;
        }

        // Writes every known key (unknown ones are dropped).  Built in memory first, so a
        // failure can't leave the file half-written.
        bool Save() const
        {
            if (path_.empty()) return false;
            try {
                std::ostringstream out;
                std::string_view section;
                for (const auto& s : kSettings) {
                    if (section != s.section) {
                        if (!section.empty()) out << "\n";
                        out << "[" << s.section << "]\n";
                        section = s.section;
                    }
                    out << s.key << " = " << Get(s) << "\n";
                }
                std::filesystem::create_directories(path_.parent_path());
                std::ofstream file(path_, std::ios::trunc);
                if (!file.is_open()) {
                    SKSE::log::error("[Config] Can't write {}", path_.string());
                    return false;
                }
                file << out.str();
                return true;
            } catch (const std::exception& e) {
                SKSE::log::error("[Config] Saving failed: {}", e.what());
                return false;
            }
        }

    private:
        Config() = default;

        static void Trim(std::string& s)
        {
            s.erase(0, s.find_first_not_of(" \t\r\n"));
            s.erase(s.find_last_not_of(" \t\r\n") + 1);
        }

        mutable std::mutex mutex_;
        std::unordered_map<std::string, std::string> settings_;
        std::filesystem::path path_;
    };

} // namespace PhysicalLetters
