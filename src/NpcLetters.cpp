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

#include "NpcLetters.h"
#include "CoSave.h"
#include "Config.h"
#include "Letters.h"
#include "LlmJson.h"
#include "Reading.h"
#include "Session.h"
#include "SkyrimNet.h"
#include "Transit.h"
#include "Travel.h"

#include <random>
#include <thread>

namespace PhysicalLetters::NpcLetters {

    namespace {
        using json = nlohmann::json;

        constexpr auto kPrompt = "physical_letters_write_letter";
        constexpr std::uint32_t kRecordVersion = 1;
        // NPCs asked in turn when one has nothing to write.
        constexpr std::size_t kShortlist = 3;
        constexpr int kMaxMemories = 8;
        constexpr RE::FormID kPlayer = 0x14;
        constexpr float kDefaultImportance = 0.6f;

        // Saved.
        double g_nextAt = 0;  // game days; 0 = not scheduled yet
        std::unordered_map<std::string, double> g_cooldownUntil;  // UUID -> game day

        bool g_busy = false;  // a pick is running
        std::chrono::steady_clock::time_point g_busySince;
        // SkyrimNet drops cancelled LLM tasks without calling back: an attempt this old failed.
        constexpr auto kAttemptTimeout = std::chrono::minutes(5);

        struct Candidate {
            std::string uuid;
            RE::FormID  formId = 0;
            std::string name;
            double      daysSinceSeen = -1;  // -1 unknown
        };

        double Now()
        {
            auto* calendar = RE::Calendar::GetSingleton();
            return calendar ? calendar->GetDaysPassed() : 0.0;
        }

        std::mt19937_64& Rng()
        {
            static std::mt19937_64 rng{ std::random_device{}() };
            return rng;
        }

        // The next letter: IntervalDays from now, give or take half.
        void Schedule()
        {
            const double days = Config::GetSingleton()->Get(Config::kNpcInterval) *
                                std::uniform_real_distribution<double>(0.5, 1.5)(Rng());
            g_nextAt = Now() + days;
            SKSE::log::info("[NpcLetters] The next letter from an NPC is due in {:.1f} game days", days);
        }

        void Finish()
        {
            Schedule();
            g_busy = false;
        }

        // Worker thread.  NPCs with enough events involving the player, off cooldown, drawn
        // at random weighted by the square root of those events: someone the player sees a
        // lot writes more often, not every time.
        std::vector<Candidate> Shortlist(double now, const std::unordered_map<std::string, double>& cooldowns,
                                         int minEvents)
        {
            const auto engagement = json::parse(SkyrimNet::Engagement(), nullptr, false);
            if (!engagement.is_array()) return {};

            std::vector<Candidate> pool;
            std::vector<double> weights;
            for (const auto& entry : engagement) {
                const auto formId = entry.value("formId", 0u);
                const int events = entry.value("eventCount", 0);
                if (formId == 0 || formId == kPlayer || events < minEvents) continue;
                const auto uuid = SkyrimNet::UuidForFormId(formId);
                if (uuid.empty()) continue;
                if (const auto it = cooldowns.find(uuid); it != cooldowns.end() && it->second > now) continue;
                auto name = SkyrimNet::ActorName(uuid);
                if (name.empty()) name = entry.value("name", std::string{});
                // SkyrimNet's event times are game seconds.
                const double lastSeen = entry.value("lastEventTime", 0.0);
                pool.push_back({ uuid, formId, name, lastSeen > 0 ? std::max(0.0, now - lastSeen / 86400.0) : -1.0 });
                weights.push_back(std::sqrt(static_cast<double>(events)));
            }

            std::vector<Candidate> shortlist;
            while (shortlist.size() < kShortlist && !pool.empty()) {
                std::discrete_distribution<std::size_t> draw(weights.begin(), weights.end());
                const auto i = draw(Rng());
                shortlist.push_back(std::move(pool[i]));
                pool.erase(pool.begin() + static_cast<std::ptrdiff_t>(i));
                weights.erase(weights.begin() + static_cast<std::ptrdiff_t>(i));
            }
            SKSE::log::info("[NpcLetters] {} NPC(s) could write; shortlisted {}", pool.size() + shortlist.size(),
                            shortlist.size());
            return shortlist;
        }

        // The candidate's actor if they can write now: the same actor as their UUID
        // (SkyrimNet merges same-named actors), alive, and not around the player (a letter
        // from someone in the next room is odd).
        RE::Actor* Writer(const Candidate& candidate, std::string& why)
        {
            auto* actor = RE::TESForm::LookupByID<RE::Actor>(candidate.formId);
            if (!actor || SkyrimNet::FormIdForUuid(candidate.uuid) != candidate.formId) {
                why = "not found";
                return nullptr;
            }
            if (actor->IsDead()) {
                why = "dead";
                return nullptr;
            }
            if (actor->Is3DLoaded()) {
                why = "near the player";
                return nullptr;
            }
            return actor;
        }

        void TryFrom(std::vector<Candidate> shortlist, std::size_t index, std::uint32_t generation);

        // Game thread.  The LLM's answer for shortlist[index].
        void OnAnswer(std::vector<Candidate> shortlist, std::size_t index, std::uint32_t generation,
                      const std::string& response, bool success)
        {
            if (generation != Session::Generation()) return;  // a load happened; Revert reset the state
            const auto& candidate = shortlist[index];
            if (!success) {
                SKSE::log::error("[NpcLetters] The LLM call for {} failed: {}", candidate.name, response.substr(0, 300));
                Finish();
                return;
            }
            const auto answer = LlmJson::ParseResponse(response);
            const auto text = answer.is_object() ? LlmJson::GetString(answer, "letter") : std::string{};
            if (!answer.is_object() || !LlmJson::GetBool(answer, "write") || text.empty()) {
                SKSE::log::info("[NpcLetters] {} has nothing to write{}", candidate.name,
                                answer.is_object() ? "" : std::format(" (unreadable answer: {})", response.substr(0, 300)));
                TryFrom(std::move(shortlist), index + 1, generation);
                return;
            }

            auto* player = RE::PlayerCharacter::GetSingleton();
            auto* writer = RE::TESForm::LookupByID<RE::Actor>(candidate.formId);
            const Letter letter{ .id = Letters::NewId(),
                                 .authorUuid = candidate.uuid,
                                 .authorName = candidate.name,
                                 .recipientUuid = SkyrimNet::UuidForFormId(kPlayer),
                                 .recipientName = player->GetName(),
                                 .body = text,
                                 .writtenAt = Now() };
            if (!Letters::Create(letter)) {
                Finish();
                return;
            }
            const double hours = Travel::Hours(writer, player);
            Transit::QueueToPlayer(letter, hours);
            StartCooldown(candidate.uuid);
            SKSE::log::info("[NpcLetters] {} wrote letter {}, due at the courier in {:.1f} game hours", candidate.name,
                            letter.id, hours);
            Finish();

            // The writer's memory of it, tagged with the letter: later readings count it as
            // correspondence.  After the letter, so a load in between loses only the memory.
            const auto memory = LlmJson::GetString(answer, "memory");
            if (memory.empty()) return;
            const auto content = std::format("{}\n\nMy letter to {}:\n{}", memory, letter.recipientName, text);
            const float importance = std::clamp(LlmJson::GetFloat(answer, "importance", kDefaultImportance), 0.0f, 1.0f);
            const auto emotion = LlmJson::GetString(answer, "emotion");
            const auto tags = json::array({ "physical_letters", "letter_sent", Reading::LetterTag(letter.id) }).dump();
            std::thread([formId = candidate.formId, content, importance, emotion, tags, name = candidate.name, generation]() {
                try {
                    if (generation != Session::Generation()) return;
                    const int id = SkyrimNet::AddMemory(formId, content, importance, "RELATIONSHIP", emotion, tags,
                                                        std::format("[{}]", kPlayer));
                    if (id == 0) SKSE::log::error("[NpcLetters] SkyrimNet didn't store {}'s memory of writing", name);
                } catch (const std::exception& e) {
                    SKSE::log::error("[NpcLetters] Storing {}'s memory failed: {}", name, e.what());
                }
            }).detach();
        }

        // Game thread.  Asks the LLM whether shortlist[index] writes; the context is gathered
        // on a worker thread (memory queries block).
        void Ask(std::vector<Candidate> shortlist, std::size_t index, std::uint32_t generation)
        {
            const auto& candidate = shortlist[index];
            auto* player = RE::PlayerCharacter::GetSingleton();
            const auto playerUuid = SkyrimNet::UuidForFormId(kPlayer);
            const std::string playerName = player->GetName();
            const double now = Now();
            std::thread([shortlist = std::move(shortlist), index, generation, playerUuid, playerName, now]() mutable {
                try {
                    const auto& c = shortlist[index];
                    auto memories = json::array();
                    const auto found =
                        json::parse(SkyrimNet::Memories(c.formId, kMaxMemories, playerName, "physical_letters"), nullptr, false);
                    if (found.is_array()) {
                        for (const auto& m : found) {
                            if (m.contains("text") && m["text"].is_string()) memories.push_back(m["text"]);
                        }
                    }
                    std::uint64_t uuid = 0;
                    std::from_chars(c.uuid.data(), c.uuid.data() + c.uuid.size(), uuid);
                    const json context = {
                        { "npc", { { "UUID", uuid }, { "name", c.name } } },
                        { "recipient", playerName },
                        { "days_since_seen", static_cast<int>(c.daysSinceSeen) },
                        { "correspondence", Reading::Correspondence(c.uuid, playerUuid, c.formId, now) },
                        { "memories", memories },
                    };
                    const bool queued = SkyrimNet::SendPrompt(
                        kPrompt, context.dump(), [shortlist, index, generation](std::string response, bool success) {
                            SKSE::GetTaskInterface()->AddTask([shortlist, index, generation, response = std::move(response),
                                                               success]() {
                                try {
                                    OnAnswer(shortlist, index, generation, response, success);
                                } catch (const std::exception& e) {
                                    SKSE::log::error("[NpcLetters] Handling the answer failed: {}", e.what());
                                    if (generation == Session::Generation()) Finish();
                                }
                            });
                        });
                    if (!queued) {
                        SKSE::log::error("[NpcLetters] SkyrimNet refused the prompt");
                        SKSE::GetTaskInterface()->AddTask([generation]() {
                            if (generation == Session::Generation()) Finish();
                        });
                    } else {
                        SKSE::log::info("[NpcLetters] Asking whether {} writes", c.name);
                    }
                } catch (const std::exception& e) {
                    SKSE::log::error("[NpcLetters] Asking failed: {}", e.what());
                    SKSE::GetTaskInterface()->AddTask([generation]() {
                        if (generation == Session::Generation()) Finish();
                    });
                }
            }).detach();
        }

        // Game thread.  The first NPC from shortlist[index] on who can write now is asked;
        // none left, and the letter waits for the next interval.
        void TryFrom(std::vector<Candidate> shortlist, std::size_t index, std::uint32_t generation)
        {
            if (generation != Session::Generation()) return;
            for (; index < shortlist.size(); ++index) {
                std::string why;
                if (Writer(shortlist[index], why)) {
                    Ask(std::move(shortlist), index, generation);
                    return;
                }
                SKSE::log::info("[NpcLetters] {} can't write now ({})", shortlist[index].name, why);
            }
            SKSE::log::info("[NpcLetters] No NPC wrote this time");
            Finish();
        }
    }

    void Tick()
    {
        if (g_busy && std::chrono::steady_clock::now() - g_busySince > kAttemptTimeout) {
            SKSE::log::warn("[NpcLetters] No answer after 5 minutes: the attempt is given up");
            Finish();
            return;
        }
        if (!Session::IsReady() || g_busy || !Config::GetSingleton()->Get(Config::kNpcLetters)) return;
        if (g_nextAt <= 0) {
            Schedule();
            return;
        }
        const double now = Now();
        if (now < g_nextAt) return;

        g_busy = true;
        g_busySince = std::chrono::steady_clock::now();
        const auto generation = Session::Generation();
        const int minEvents = Config::GetSingleton()->Get(Config::kNpcMinEvents);
        std::thread([now, cooldowns = g_cooldownUntil, minEvents, generation]() {
            std::vector<Candidate> shortlist;
            try {
                shortlist = Shortlist(now, cooldowns, minEvents);
            } catch (const std::exception& e) {
                SKSE::log::error("[NpcLetters] Picking a writer failed: {}", e.what());
            }
            SKSE::GetTaskInterface()->AddTask([shortlist = std::move(shortlist), generation]() mutable {
                try {
                    TryFrom(std::move(shortlist), 0, generation);
                } catch (const std::exception& e) {
                    SKSE::log::error("[NpcLetters] Picking a writer failed: {}", e.what());
                    if (generation == Session::Generation()) Finish();
                }
            });
        }).detach();
    }

    void StartCooldown(const std::string& uuid)
    {
        g_cooldownUntil[uuid] = Now() + Config::GetSingleton()->Get(Config::kNpcCooldown);
    }

    void MakeDue()
    {
        g_nextAt = Now();
        SKSE::log::info("[NpcLetters] The next letter from an NPC is due now");
    }

    void Save(SKSE::SerializationInterface* a_intfc, std::uint32_t a_type)
    {
        if (!a_intfc->OpenRecord(a_type, kRecordVersion)) {
            SKSE::log::error("[NpcLetters] Couldn't open the co-save record");
            return;
        }
        a_intfc->WriteRecordData(g_nextAt);
        const double now = Now();
        std::uint32_t count = 0;
        for (const auto& [uuid, until] : g_cooldownUntil) count += until > now ? 1 : 0;
        a_intfc->WriteRecordData(count);
        for (const auto& [uuid, until] : g_cooldownUntil) {
            if (until <= now) continue;  // over: not worth saving
            CoSave::WriteString(a_intfc, uuid);
            a_intfc->WriteRecordData(until);
        }
    }

    void Load(SKSE::SerializationInterface* a_intfc, std::uint32_t a_version)
    {
        if (a_version != kRecordVersion) {
            SKSE::log::error("[NpcLetters] Co-save record version {} is unknown — skipped", a_version);
            return;
        }
        std::uint32_t count = 0;
        if (a_intfc->ReadRecordData(g_nextAt) != sizeof(g_nextAt) || a_intfc->ReadRecordData(count) != sizeof(count)) return;
        for (std::uint32_t i = 0; i < count; ++i) {
            std::string uuid;
            double until = 0;
            if (!CoSave::ReadString(a_intfc, uuid) || a_intfc->ReadRecordData(until) != sizeof(until)) break;
            g_cooldownUntil[uuid] = until;
        }
        SKSE::log::info("[NpcLetters] Next letter at day {:.1f}; {} NPC(s) on cooldown", g_nextAt, g_cooldownUntil.size());
    }

    void Revert()
    {
        g_nextAt = 0;
        g_cooldownUntil.clear();
        g_busy = false;
    }

} // namespace PhysicalLetters::NpcLetters
