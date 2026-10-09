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

#include "Session.h"
#include "HandIn.h"
#include "LetterDB.h"
#include "Letters.h"
#include "SkyrimNet.h"

namespace PhysicalLetters::Session {

    namespace {
        using Clock = std::chrono::steady_clock;

        // Past this, a session that isn't ready is logged once with the reason: until it
        // is, letters show a placeholder and nothing is delivered.
        constexpr auto kSlowStart = std::chrono::seconds(30);

        std::atomic<bool> g_started{ false };
        std::atomic<bool> g_ready{ false };
        std::atomic<std::uint32_t> g_generation{ 0 };
        Clock::time_point g_startedAt{};
        bool g_reportedSlow = false;
    }

    void End()
    {
        ++g_generation;
        g_started = false;
        g_ready = false;
        LetterDB::GetSingleton()->Close();
    }

    void Start()
    {
        g_started = true;
        g_startedAt = Clock::now();
        g_reportedSlow = false;
    }

    void Poll()
    {
        if (!g_started || g_ready) return;

        // Not before SkyrimNet is ready: asked earlier, it makes up a save id of its own.
        auto reason = SkyrimNet::NotReadyReason();
        const auto saveId = reason.empty() ? SkyrimNet::SaveId() : std::string{};
        if (reason.empty() && saveId.empty()) reason = "SkyrimNet has no save id yet";
        if (!reason.empty()) {
            if (!g_reportedSlow && Clock::now() - g_startedAt > kSlowStart) {
                SKSE::log::warn("[Session] Not ready after {}s: {}. Letters show '...' and nothing is delivered until then",
                                kSlowStart.count(), reason);
                g_reportedSlow = true;
            }
            return;
        }

        if (!LetterDB::GetSingleton()->Open(saveId)) {
            SKSE::log::error("[Session] LetterDB couldn't be opened: letters are paused until the next load");
            g_started = false;
            return;
        }
        Letters::AttachTexts();
        g_ready = true;
        HandIn::RefreshRecipients();  // who the hand-in topic shows to: needs LetterDB
        SKSE::log::info("[Session] Ready (save id {}, timeline state {})", saveId,
                        static_cast<int>(SkyrimNet::GetTimelineState()));
    }

    bool IsReady()
    {
        return g_ready;
    }

    std::uint32_t Generation()
    {
        return g_generation;
    }

} // namespace PhysicalLetters::Session
