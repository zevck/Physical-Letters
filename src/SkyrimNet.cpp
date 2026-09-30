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

#include "SkyrimNet.h"
#include "SkyrimNet/PublicAPI.h"

#include <nlohmann/json.hpp>

namespace PhysicalLetters::SkyrimNet {

    namespace {
        // v11: PublicGetTimelineState, without which a letter could be read into history
        // that SkyrimNet's Clear then deletes.  (v10: PublicQueryMemoriesForActor.)
        constexpr int kMinVersion = 11;
        bool g_available = false;

        std::uint64_t ParseUuid(const std::string& uuid)
        {
            std::uint64_t value = 0;
            const auto [end, ec] = std::from_chars(uuid.data(), uuid.data() + uuid.size(), value);
            return ec == std::errc{} && end == uuid.data() + uuid.size() ? value : 0;
        }
    }

    bool Init()
    {
        if (g_available) return true;
        if (!FindFunctions() || !PublicGetVersion) {
            SKSE::log::error("[SkyrimNet] SkyrimNet.dll not found: letters won't be read");
            return false;
        }
        const int version = PublicGetVersion();
        if (version < kMinVersion || !PublicGetTimelineState || !PublicQueryMemoriesForActor || !PublicSendCustomPromptToLLM ||
            !PublicAddMemory || !PublicIsMemorySystemReady || !PublicGetSaveUniqueID || !PublicFormIDToUUID ||
            !PublicUUIDToFormID) {
            SKSE::log::error("[SkyrimNet] SkyrimNet public API v{}+ needed, found v{}: letters won't be read. "
                             "Update SkyrimNet.",
                             kMinVersion, version);
            return false;
        }
        g_available = true;
        SKSE::log::info("[SkyrimNet] Public API v{} ready", version);
        return true;
    }

    TimelineState GetTimelineState()
    {
        return g_available ? static_cast<TimelineState>(PublicGetTimelineState()) : TimelineState::kNone;
    }

    std::string NotReadyReason()
    {
        if (!g_available) return "SkyrimNet is missing or too old (see the start of this log)";
        if (!PublicIsMemorySystemReady()) return "SkyrimNet's database isn't ready";
        // SkyrimNet marks the check pending in its own kPreLoadGame, so there is no window
        // after a load where this reads a stale "settled".
        if (GetTimelineState() == TimelineState::kPending) return "SkyrimNet's keep/clear check is pending";
        return {};
    }

    bool IsReady()
    {
        return NotReadyReason().empty();
    }

    std::string SaveId()
    {
        return g_available ? PublicGetSaveUniqueID() : std::string{};
    }

    std::string UuidForFormId(RE::FormID formId)
    {
        if (!g_available) return {};
        const auto uuid = PublicFormIDToUUID(formId);
        return uuid == 0 ? std::string{} : std::to_string(uuid);
    }

    RE::FormID FormIdForUuid(const std::string& uuid)
    {
        const auto value = ParseUuid(uuid);
        return g_available && value != 0 ? PublicUUIDToFormID(value) : 0;
    }

    std::string ActorName(const std::string& uuid)
    {
        const auto value = ParseUuid(uuid);
        return g_available && value != 0 && PublicGetActorNameByUUID ? PublicGetActorNameByUUID(value) : std::string{};
    }

    std::string Memories(RE::FormID formId, int maxCount, const std::string& query, const std::string& excludeTag)
    {
        if (!g_available) return "[]";
        MemoryQuery memoryQuery;
        memoryQuery.maxCount = maxCount;
        memoryQuery.excludeTags = { excludeTag };
        memoryQuery.contextQuery = query;
        memoryQuery.orderBy = MemoryOrder::Relevance;
        return QueryMemoriesForActor(formId, memoryQuery);
    }

    bool HasMemoryWithTag(RE::FormID formId, const std::string& tag)
    {
        if (!g_available) return false;
        MemoryQuery query;
        query.maxCount = 1;
        query.includeTags = { tag };
        const auto found = nlohmann::json::parse(QueryMemoriesForActor(formId, query), nullptr, false);
        return found.is_array() && !found.empty();
    }

    std::string Engagement()
    {
        if (!g_available || !PublicGetActorEngagement) return "[]";
        constexpr double kDay = 86400.0;
        return PublicGetActorEngagement(0, true, true, kDay, 7 * kDay);
    }

    int AddMemory(RE::FormID formId, const std::string& content, float importance, const std::string& type,
                  const std::string& emotion, const std::string& tagsJson, const std::string& relatedActorsJson)
    {
        if (!g_available) return 0;
        return PublicAddMemory(formId, content.c_str(), importance, type.c_str(), emotion.c_str(), "", tagsJson.c_str(),
                               relatedActorsJson.c_str());
    }

    bool SendPrompt(const std::string& promptName, const std::string& contextJson,
                    std::function<void(std::string response, bool success)> callback)
    {
        if (!g_available) return false;
        return PublicSendCustomPromptToLLM(promptName.c_str(), "", contextJson.c_str(),
                                           [callback = std::move(callback)](const char* response, int success) {
                                               callback(response ? response : "", success == 1);
                                           });
    }

} // namespace PhysicalLetters::SkyrimNet
