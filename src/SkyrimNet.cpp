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
        if (!PublicSearchActors) {
            SKSE::log::warn("[SkyrimNet] No PublicSearchActors in this SkyrimNet: letters between NPCs are off");
        }
        if (!PublicRegisterEvent) {
            SKSE::log::warn("[SkyrimNet] No PublicRegisterEvent in this SkyrimNet: letters handed over are read, not reacted to");
        }
        return true;
    }

    int RegisterEvent(const std::string& type, const std::string& content, RE::FormID originator, RE::FormID target,
                      const std::vector<RE::FormID>& audience)
    {
        if (!CanRegisterEvents()) return 0;
        std::vector<std::uint32_t> ids(audience.begin(), audience.end());
        return PublicRegisterEvent(type.c_str(), content.c_str(), originator, target, ids.data(),
                                   static_cast<std::uint32_t>(ids.size()));
    }

    bool CanRegisterEvents()
    {
        return g_available && PublicRegisterEvent;
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

    std::uint64_t UuidNumber(const std::string& uuid)
    {
        return ParseUuid(uuid);
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

    bool CanSearchActors()
    {
        return g_available && PublicSearchActors;
    }

    std::string SearchActors(const std::string& name, int maxCount)
    {
        if (!CanSearchActors()) return "[]";
        return PublicSearchActors(name.c_str(), maxCount);
    }

    std::string RelatedActors(RE::FormID formId, int maxCount)
    {
        if (!g_available || !PublicGetRelatedActors) return "[]";
        constexpr double kDay = 86400.0;
        return PublicGetRelatedActors(formId, maxCount, kDay, 7 * kDay);
    }

    std::string RecentMemories(RE::FormID formId, int maxCount, const std::string& excludeTag)
    {
        if (!g_available) return "[]";
        MemoryQuery memoryQuery;
        memoryQuery.maxCount = maxCount;
        memoryQuery.excludeTags = { excludeTag };
        memoryQuery.orderBy = MemoryOrder::GameTimeDesc;
        return QueryMemoriesForActor(formId, memoryQuery);
    }

    bool HasMemories(RE::FormID formId)
    {
        if (!g_available) return false;
        MemoryQuery memoryQuery;
        memoryQuery.maxCount = 1;
        const auto found = QueryMemoriesForActor(formId, memoryQuery);
        return !found.empty() && found != "[]";
    }

    double ImportanceOfMemoriesWith(RE::FormID formId, std::uint64_t relatedUuid, int maxCount)
    {
        if (!g_available || relatedUuid == 0) return 0;
        MemoryQuery memoryQuery;
        memoryQuery.maxCount = maxCount;
        memoryQuery.orderBy = MemoryOrder::GameTimeDesc;
        const auto found = nlohmann::json::parse(QueryMemoriesForActor(formId, memoryQuery), nullptr, false);
        if (!found.is_array()) return 0;
        double total = 0;
        for (const auto& memory : found) {
            const auto related = memory.find("related_actors");
            if (related == memory.end() || !related->is_array()) continue;
            // SkyrimNet writes related actors as numbers (Models/Memory.h ToJson).
            const bool withThem = std::ranges::any_of(*related, [relatedUuid](const auto& uuid) {
                return uuid.is_number_unsigned() && uuid.template get<std::uint64_t>() == relatedUuid;
            });
            const auto score = memory.find("importance_score");
            if (withThem && score != memory.end() && score->is_number()) total += score->template get<double>();
        }
        return total;
    }

    std::string RecentEvents(RE::FormID formId, int maxCount, const std::string& types)
    {
        if (!g_available || !PublicGetRecentEvents) return "[]";
        return PublicGetRecentEvents(formId, maxCount, types.c_str());
    }

    int AddMemory(RE::FormID formId, const std::string& content, float importance, const std::string& type,
                  const std::string& emotion, const std::string& tagsJson, const std::string& relatedActorsJson)
    {
        if (!g_available) return 0;
        return PublicAddMemory(formId, content.c_str(), importance, type.c_str(), emotion.c_str(), "", tagsJson.c_str(),
                               relatedActorsJson.c_str());
    }

    bool SendPrompt(const std::string& promptName, const std::string& contextJson,
                    std::function<void(std::string response, bool success)> callback, const std::string& variant)
    {
        if (!g_available) return false;
        // Every LLM call of ours, one line out and one back: to tally a session's calls by prompt (docs/ARCHITECTURE.md).
        static std::atomic<std::uint32_t> calls{ 0 };
        const auto call = ++calls;
        const auto name = promptName.substr(promptName.find_last_of("\\/") + 1);
        SKSE::log::info("[LLM] #{} {} sent ({} variant, {} bytes of context)", call, name,
                        variant.empty() ? std::string{ "default" } : variant, contextJson.size());
        const bool queued = PublicSendCustomPromptToLLM(
            promptName.c_str(), variant.c_str(), contextJson.c_str(),
            [callback = std::move(callback), call, name](const char* response, int success) {
                SKSE::log::info("[LLM] #{} {} {}", call, name, success == 1 ? "answered" : "failed");
                callback(response ? response : "", success == 1);
            });
        if (!queued) SKSE::log::warn("[LLM] #{} {} wasn't queued: SkyrimNet refused it", call, name);
        return queued;
    }

} // namespace PhysicalLetters::SkyrimNet
