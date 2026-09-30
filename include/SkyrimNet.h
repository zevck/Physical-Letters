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

// SkyrimNet's public API, resolved from its DLL at runtime (include/SkyrimNet/PublicAPI.h,
// included by SkyrimNet.cpp only: the header defines the function pointers).  Every call
// is safe without SkyrimNet: it returns an empty result.
namespace PhysicalLetters::SkyrimNet {

    // Outcome of SkyrimNet's keep/clear check after the last load (PublicGetTimelineState).
    enum class TimelineState : int { kNone = 0, kPending = 1, kKept = 2, kCleared = 3 };

    // kDataLoaded: resolves the API.  Needs v11 (the timeline state; filtered memory
    // queries are v10); false without it, and letters are never read.
    bool Init();

    // Why IsReady is false, for the log ("" when it's true).  Game thread.
    std::string NotReadyReason();

    // Game thread.  True once SkyrimNet's database is ready and its keep/clear timeline
    // check isn't pending, so what we read and write belongs to this save's history.
    bool IsReady();

    TimelineState GetTimelineState();

    // SkyrimNet's ID for the loaded save ("" in the main menu).  Game thread, and only
    // once IsReady: SkyrimNet makes up a new ID if it's asked before it has one.
    std::string SaveId();

    // "" / 0 when SkyrimNet doesn't know the actor.
    std::string UuidForFormId(RE::FormID formId);
    RE::FormID FormIdForUuid(const std::string& uuid);
    std::string ActorName(const std::string& uuid);

    // JSON array of the actor's memories most relevant to `query`, without those tagged
    // `excludeTag` ("[]" on error).  Blocks: not on the game thread.
    std::string Memories(RE::FormID formId, int maxCount, const std::string& query, const std::string& excludeTag);

    // Whether the actor has an active memory carrying `tag`.  Blocks: not on the game thread.
    bool HasMemoryWithTag(RE::FormID formId, const std::string& tag);

    // Every NPC with events involving the player, with their counts (PublicGetActorEngagement,
    // player events only; "[]" on error).  SkyrimNet aggregates by actor NAME: entries of
    // same-named actors are merged, so callers check each FormID against its UUID.  Scans
    // SkyrimNet's whole history: not on the game thread.
    std::string Engagement();

    // The actor's newest events of the given types (comma-separated), oldest first
    // (PublicGetRecentEvents; "[]" on error): { type, data, originatingActor, targetActor
    // (UUIDs), gameTime (game seconds), ... }.  Blocks: not on the game thread.
    std::string RecentEvents(RE::FormID formId, int maxCount, const std::string& types);

    // Stores a memory for the actor.  Blocks while it is embedded: not on the game thread.
    // Returns the memory id, 0 on error.
    int AddMemory(RE::FormID formId, const std::string& content, float importance, const std::string& type,
                  const std::string& emotion, const std::string& tagsJson, const std::string& relatedActorsJson);

    // Renders the prompt template `promptName` with the context variables and sends it to
    // the LLM.  The callback runs on a SkyrimNet worker thread, never the game thread, and
    // not at all if SkyrimNet cancels the task.
    bool SendPrompt(const std::string& promptName, const std::string& contextJson,
                    std::function<void(std::string response, bool success)> callback);

} // namespace PhysicalLetters::SkyrimNet
