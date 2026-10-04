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
#include "GameTime.h"
#include "Letters.h"
#include "LlmJson.h"
#include "Reading.h"
#include "Session.h"
#include "SkyrimNet.h"
#include "Transit.h"
#include "Travel.h"

#include <random>
#include <regex>
#include <thread>

namespace PhysicalLetters::NpcLetters {

    namespace {
        using json = nlohmann::json;
        using GameTime::Now;

        constexpr auto kPrompt = "physical_letters_write_letter";
        constexpr auto kPickPrompt = "physical_letters_who_writes";
        constexpr auto kPickVariant = "meta";
        constexpr std::uint32_t kRecordVersion = 1;
        constexpr int kMaxMemories = 8;
        // Lines of the latest conversation shown to the pick, per candidate.
        constexpr std::size_t kPickLines = 8;
        // Lines of the latest conversation passed to the prompt, and the events fetched to find
        // them (not every dialogue event of the NPC's is with the player).
        constexpr std::size_t kMaxLines = 20;
        constexpr int kDialogueEvents = 80;
        constexpr RE::FormID kPlayer = 0x14;
        constexpr float kDefaultImportance = 0.6f;
        // SkyrimNet drops cancelled LLM tasks without calling back: a step this old failed.
        constexpr auto kStepTimeout = std::chrono::minutes(5);

        // Saved.
        double g_nextAt = 0;  // game days; 0 = not scheduled yet
        std::unordered_map<std::string, double> g_cooldownUntil;  // UUID -> game day

        bool g_busy = false;  // an attempt is running
        std::chrono::steady_clock::time_point g_stepSince;  // the pool scan, or the current LLM call
        std::uint64_t g_attempt = 0;                        // the running attempt; bumped when it's given up

        // This play session's attempts and what came of them, logged after each attempt: to
        // tune the prompt and the settings by.  Not saved.
        struct Tally {
            int attempts = 0;
            int nobody = 0;     // attempts where nobody could write
            int pickCalls = 0;  // cheap calls choosing among the shortlist
            int noneHad = 0;    // picks where nobody had a reason
            int asked = 0;      // full calls writing a letter
            int written = 0;
            int declined = 0;
            int llmFailed = 0;
            int timedOut = 0;
        } g_tally;

        void LogTally()
        {
            SKSE::log::info("[NpcLetters] This session: {} attempt(s) ({} with nobody able to write), {} pick call(s) ({} "
                            "with no reason to write), {} letter call(s), {} letter(s) written, {} declined, {} LLM "
                            "failure(s), {} timed out",
                            g_tally.attempts, g_tally.nobody, g_tally.pickCalls, g_tally.noneHad, g_tally.asked,
                            g_tally.written, g_tally.declined, g_tally.llmFailed, g_tally.timedOut);
        }

        // Which attempt, in which session, a result belongs to: anything else answers late.
        struct Token {
            std::uint32_t generation = 0;
            std::uint64_t attempt = 0;

            bool Current() const { return generation == Session::Generation() && attempt == g_attempt; }
        };

        struct Candidate {
            std::string uuid;
            RE::FormID  formId = 0;
            std::string name;
            int         events = 0;          // with the player
            double      daysSinceSeen = -1;  // since their last exchange with the player; -1 unknown
            json        dialogue = json::array();  // their latest exchanges with the player
            json        memories = json::array();  // most relevant to the player; fetched once shortlisted
        };

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
            LogTally();
        }

        // From a worker thread: ends the attempt on the game thread, if it's still current.
        void FinishLater(Token token)
        {
            SKSE::GetTaskInterface()->AddTask([token]() {
                // An exception must not cross into the engine.
                try {
                    if (token.Current()) Finish();
                } catch (const std::exception& e) {
                    SKSE::log::error("[NpcLetters] Ending the attempt failed: {}", e.what());
                }
            });
        }

        std::uint64_t ToUuid(const std::string& text)
        {
            std::uint64_t uuid = 0;
            std::from_chars(text.data(), text.data() + text.size(), uuid);
            return uuid;
        }

        // Worker thread.  The NPC's latest spoken exchanges with the player, oldest first, as
        // { speaker, text }, and when the last one was (game days, -1 if none).  From their
        // dialogue events, matched by UUID; lines later than now belong to a timeline the
        // player left (an older save loaded with Keep) and are skipped.
        // PublicGetRecentDialogue isn't used: it keeps the oldest lines of what it fetches.
        std::pair<json, double> LatestExchanges(RE::FormID formId, std::uint64_t npcUuid, std::uint64_t playerUuid,
                                                const std::string& npcName, const std::string& playerName, double now)
        {
            const auto events = json::parse(SkyrimNet::RecentEvents(formId, kDialogueEvents, "dialogue,dialogue_player_text"),
                                            nullptr, false);
            std::vector<json> lines;
            double lastSeen = -1;
            if (!events.is_array()) return { json::array(), lastSeen };
            for (const auto& event : events) {
                const auto from = event.value("originatingActor", std::uint64_t{ 0 });
                const auto to = event.value("targetActor", std::uint64_t{ 0 });
                const bool fromNpc = from == npcUuid && to == playerUuid;
                if (!fromNpc && !(from == playerUuid && to == npcUuid)) continue;
                const double days = event.value("gameTime", 0.0) / 86400.0;
                if (days > now + 0.01) continue;
                const auto data = event.find("data");
                std::string text;
                if (data != event.end() && data->is_object()) text = LlmJson::GetString(*data, "dialogue");
                else if (data != event.end() && data->is_string()) text = data->get<std::string>();
                if (text.empty()) continue;
                lastSeen = std::max(lastSeen, days);
                lines.push_back({ { "speaker", fromNpc ? npcName : playerName }, { "text", text } });
            }
            if (lines.size() > kMaxLines) lines.erase(lines.begin(), lines.end() - kMaxLines);
            return { json(lines), lastSeen < 0 ? -1.0 : std::max(0.0, now - lastSeen) };
        }

        // Worker thread.  NPCs with enough events involving the player, off cooldown.  The
        // engagement list's own times are unusable (docs/NPC_LETTERS.md#who).
        std::vector<Candidate> Pool(double now, const std::unordered_map<std::string, double>& cooldowns, int minEvents,
                                    const std::string& playerName, std::uint64_t playerUuid)
        {
            const auto engagement = json::parse(SkyrimNet::Engagement(), nullptr, false);
            if (!engagement.is_array()) return {};

            std::vector<Candidate> pool;
            for (const auto& entry : engagement) {
                SKSE::log::debug("[NpcLetters] Engagement: {}", entry.dump());
                const auto formId = entry.value("formId", 0u);
                const int events = entry.value("eventCount", 0);
                if (formId == 0 || formId == kPlayer || events < minEvents) continue;
                const auto uuid = SkyrimNet::UuidForFormId(formId);
                if (uuid.empty()) continue;
                if (const auto it = cooldowns.find(uuid); it != cooldowns.end() && it->second > now) continue;
                auto name = SkyrimNet::ActorName(uuid);
                if (name.empty()) name = entry.value("name", std::string{});
                auto [dialogue, daysSinceSeen] = LatestExchanges(formId, ToUuid(uuid), playerUuid, name, playerName, now);
                SKSE::log::debug("[NpcLetters] {}'s latest exchanges ({} lines, last {:.1f} days ago): {}", name,
                                 dialogue.size(), daysSinceSeen, dialogue.dump().substr(0, 1500));
                pool.push_back({ uuid, formId, name, events, daysSinceSeen, std::move(dialogue) });
            }
            return pool;
        }

        // How likely the candidate is to be drawn: the square root of their events with the
        // player (someone seen a lot writes more often, not every time), less if they dealt
        // with the player lately.
        double Weight(const Candidate& candidate)
        {
            const auto* config = Config::GetSingleton();
            const double missedAfter = config->Get(Config::kNpcMissedAfter);
            const double floor = config->Get(Config::kNpcRecentWeight) / 100.0;
            const double recency = candidate.daysSinceSeen < 0 || missedAfter <= 0
                                        ? 1.0
                                        : std::clamp(candidate.daysSinceSeen / missedAfter, floor, 1.0);
            return std::sqrt(static_cast<double>(candidate.events)) * recency;
        }

        // The candidate's actor if they can write now (docs/NPC_LETTERS.md#who); else `why`
        // says why not.  Checked again before asking: the player may have moved meanwhile.
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
            if (Transit::IsLetterPendingFor(candidate.uuid)) {
                why = "has a letter to answer";
                return nullptr;
            }
            const auto* config = Config::GetSingleton();
            if (const int apart = config->Get(Config::kNpcMinDaysApart);
                apart > 0 && candidate.daysSinceSeen >= 0 && candidate.daysSinceSeen < apart) {
                why = std::format("spoke to the player {:.1f} days ago", candidate.daysSinceSeen);
                return nullptr;
            }
            auto* player = RE::PlayerCharacter::GetSingleton();
            if (const auto* area = Travel::Area(actor); area && area == Travel::Area(player)) {
                why = std::format("in the player's area, {}", area->GetName());
                return nullptr;
            }
            if (const auto distance = Travel::Distance(actor, player);
                distance && *distance < config->Get(Config::kNpcNearDistance)) {
                why = std::format("{:.0f} units from the player", *distance);
                return nullptr;
            }
            return actor;
        }

        // The writer's memory of the letter, tagged with it: later readings count the letter
        // as correspondence (docs/NPC_LETTERS.md#the-letter), so it's stored even when the
        // LLM gave no memory text.
        void Remember(const Candidate& writer, const Letter& letter, const json& answer, std::uint32_t generation)
        {
            auto memory = LlmJson::GetString(answer, "memory");
            if (memory.empty()) {
                SKSE::log::info("[NpcLetters] The LLM gave no memory for {}'s letter: a plain one is stored", writer.name);
                memory = std::format("I wrote a letter to {}.", letter.recipientName);
            }
            const auto content = std::format("{}\n\nMy letter to {}:\n{}", memory, letter.recipientName, letter.body);
            const float importance = std::clamp(LlmJson::GetFloat(answer, "importance", kDefaultImportance), 0.0f, 1.0f);
            const auto emotion = LlmJson::GetString(answer, "emotion");
            const auto tags = json::array({ "physical_letters", "letter_sent", Reading::LetterTag(letter.id) }).dump();
            std::thread([formId = writer.formId, content, importance, emotion, tags, name = writer.name, generation]() {
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

        // Game thread.  The LLM's letter, if `candidate` writes one.
        void OnLetter(const Candidate& candidate, Token token, const std::string& response, bool success)
        {
            if (!token.Current()) {
                // A load happened (Revert reset the state), or the attempt was given up.
                SKSE::log::info("[NpcLetters] {}'s answer came after the attempt ended: dropped", candidate.name);
                return;
            }
            if (!success) {
                ++g_tally.llmFailed;
                SKSE::log::error("[NpcLetters] The LLM call for {} failed: {}", candidate.name, response.substr(0, 300));
                Finish();
                return;
            }
            SKSE::log::debug("[NpcLetters] {}'s answer: {}", candidate.name, response.substr(0, 2000));
            const auto answer = LlmJson::ParseResponse(response);
            const auto text = answer.is_object() ? LlmJson::GetString(answer, "letter") : std::string{};
            if (!answer.is_object() || !LlmJson::GetBool(answer, "write") || text.empty()) {
                ++g_tally.declined;
                SKSE::log::info("[NpcLetters] {} has nothing to write after all{}", candidate.name,
                                answer.is_object() ? "" : std::format(" (unreadable answer: {})", response.substr(0, 300)));
                Finish();
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
            ++g_tally.written;
            SKSE::log::info("[NpcLetters] {} wrote letter {}, due at the courier in {:.1f} game hours", candidate.name,
                            letter.id, hours);
            Finish();
            // After the letter, so a load in between loses only the memory.
            Remember(candidate, letter, answer, token.generation);
        }

        // What the worker threads need of the player, read on the game thread.
        struct PlayerInfo {
            std::string uuid;
            std::string name;
            double now = 0;
        };

        PlayerInfo ReadPlayer()
        {
            return { SkyrimNet::UuidForFormId(kPlayer), RE::PlayerCharacter::GetSingleton()->GetName(), Now() };
        }

        // Worker thread.  The full call: `candidate` writes their letter, or declines.
        void Write(Candidate candidate, Token token, const PlayerInfo& player)
        {
            try {
                const json context = {
                    { "npc", { { "UUID", ToUuid(candidate.uuid) }, { "name", candidate.name } } },
                    { "recipient", player.name },
                    { "days_since_seen", static_cast<int>(candidate.daysSinceSeen) },
                    { "correspondence", Reading::Correspondence(candidate.uuid, player.uuid, candidate.formId, player.now) },
                    { "dialogue", candidate.dialogue },
                    { "memories", candidate.memories },
                };
                const auto name = candidate.name;
                const bool queued = SkyrimNet::SendPrompt(
                    kPrompt, context.dump(), [candidate = std::move(candidate), token](std::string response, bool success) {
                        SKSE::GetTaskInterface()->AddTask([candidate, token, response = std::move(response), success]() {
                            // An exception must not cross into the engine.
                            try {
                                OnLetter(candidate, token, response, success);
                            } catch (const std::exception& e) {
                                SKSE::log::error("[NpcLetters] Handling the letter failed: {}", e.what());
                                if (token.Current()) Finish();
                            }
                        });
                    });
                if (!queued) {
                    SKSE::log::error("[NpcLetters] SkyrimNet refused the prompt");
                    FinishLater(token);
                    return;
                }
                ++g_tally.asked;
                SKSE::log::info("[NpcLetters] Asking {} to write", name);
            } catch (const std::exception& e) {
                SKSE::log::error("[NpcLetters] Asking for the letter failed: {}", e.what());
                FinishLater(token);
            }
        }

        // The pick's answer: {"pick":N,"why":"..."}, read with a regex so an answer cut short
        // still counts.  0 for nobody, -1 unreadable.
        std::pair<int, std::string> ReadPick(const std::string& response)
        {
            static const std::regex pick{ R"re("pick"\s*:\s*"?(\d+))re" };
            static const std::regex why{ R"re("why"\s*:\s*"((?:[^"\\]|\\.)*))re" };
            std::smatch m;
            if (!std::regex_search(response, m, pick)) return { -1, {} };
            const int number = std::stoi(m[1].str());
            return { number, std::regex_search(response, m, why) ? m[1].str() : std::string{} };
        }

        // Game thread.  The cheap call's choice among the shortlist.
        void OnPick(std::vector<Candidate> shortlist, Token token, const std::string& response, bool success)
        {
            if (!token.Current()) {
                SKSE::log::info("[NpcLetters] The pick came after the attempt ended: dropped");
                return;
            }
            if (!success) {
                ++g_tally.llmFailed;
                SKSE::log::error("[NpcLetters] The pick call failed: {}", response.substr(0, 300));
                Finish();
                return;
            }
            SKSE::log::debug("[NpcLetters] The pick's answer: {}", response.substr(0, 500));
            const auto [number, why] = ReadPick(response);
            if (number <= 0 || static_cast<std::size_t>(number) > shortlist.size()) {
                ++g_tally.noneHad;
                SKSE::log::info("[NpcLetters] Nobody has a reason to write{}", number == 0 ? std::string{}
                                : std::format(" (unreadable answer: {})", response.substr(0, 300)));
                Finish();
                return;
            }
            auto& candidate = shortlist[static_cast<std::size_t>(number) - 1];
            SKSE::log::info("[NpcLetters] Picked {}: {}", candidate.name, why.empty() ? "no reason given" : why);
            std::string whyNot;
            if (!Writer(candidate, whyNot)) {
                SKSE::log::info("[NpcLetters] {} can't write now ({})", candidate.name, whyNot);
                Finish();
                return;
            }
            g_stepSince = std::chrono::steady_clock::now();  // the timeout is per LLM call
            std::thread([candidate = std::move(candidate), token, player = ReadPlayer()]() mutable {
                Write(std::move(candidate), token, player);
            }).detach();
        }

        // Worker thread.  The cheap call: which of the shortlist, if anyone, has a reason to
        // write now (docs/NPC_LETTERS.md#the-pick).
        void Pick(std::vector<Candidate> shortlist, Token token, const PlayerInfo& player)
        {
            try {
                auto list = json::array();
                for (std::size_t i = 0; i < shortlist.size(); ++i) {
                    const auto& c = shortlist[i];
                    auto lines = c.dialogue;
                    if (lines.size() > kPickLines) lines.erase(lines.begin(), lines.end() - kPickLines);
                    list.push_back({ { "number", i + 1 },
                                     { "UUID", ToUuid(c.uuid) },
                                     { "name", c.name },
                                     { "days_since_seen", static_cast<int>(c.daysSinceSeen) },
                                     { "dialogue", lines },
                                     { "memories", c.memories } });
                }
                const json context = { { "candidates", list }, { "player", player.name } };
                const bool queued = SkyrimNet::SendPrompt(
                    kPickPrompt, context.dump(),
                    [shortlist = std::move(shortlist), token](std::string response, bool success) {
                        SKSE::GetTaskInterface()->AddTask([shortlist, token, response = std::move(response), success]() {
                            // An exception must not cross into the engine.
                            try {
                                OnPick(shortlist, token, response, success);
                            } catch (const std::exception& e) {
                                SKSE::log::error("[NpcLetters] Handling the pick failed: {}", e.what());
                                if (token.Current()) Finish();
                            }
                        });
                    },
                    kPickVariant);
                if (!queued) {
                    SKSE::log::error("[NpcLetters] SkyrimNet refused the pick prompt");
                    FinishLater(token);
                    return;
                }
                ++g_tally.pickCalls;
                SKSE::log::info("[NpcLetters] Asking which of {} NPC(s) has a reason to write", list.size());
            } catch (const std::exception& e) {
                SKSE::log::error("[NpcLetters] Asking for the pick failed: {}", e.what());
                FinishLater(token);
            }
        }

        // Game thread.  Of the pool, those who can write now (Writer), drawn at random by
        // Weight without replacement, up to CandidatesPerAttempt.
        std::vector<Candidate> Shortlist(std::vector<Candidate> pool)
        {
            const auto count = static_cast<std::size_t>(Config::GetSingleton()->Get(Config::kNpcCandidates));
            const auto total = pool.size();
            std::vector<Candidate> eligible;
            std::vector<double> weights;
            for (auto& candidate : pool) {
                std::string why;
                if (!Writer(candidate, why)) {
                    SKSE::log::debug("[NpcLetters] {} can't write now ({})", candidate.name, why);
                    continue;
                }
                const double weight = Weight(candidate);
                if (weight <= 0) {
                    SKSE::log::debug("[NpcLetters] {} has weight 0", candidate.name);
                    continue;
                }
                weights.push_back(weight);
                eligible.push_back(std::move(candidate));
            }
            std::vector<Candidate> shortlist;
            while (shortlist.size() < count && !eligible.empty()) {
                std::discrete_distribution<std::size_t> draw(weights.begin(), weights.end());
                const auto i = draw(Rng());
                SKSE::log::info("[NpcLetters] Drew {} ({} events with the player, last seen {} days ago, weight {:.2f})",
                                eligible[i].name, eligible[i].events,
                                eligible[i].daysSinceSeen < 0 ? std::string{ "?" } : std::format("{:.1f}", eligible[i].daysSinceSeen),
                                weights[i]);
                shortlist.push_back(std::move(eligible[i]));
                eligible.erase(eligible.begin() + static_cast<std::ptrdiff_t>(i));
                weights.erase(weights.begin() + static_cast<std::ptrdiff_t>(i));
            }
            SKSE::log::info("[NpcLetters] {} NPC(s) in the pool, {} could write now; shortlisted {}", total,
                            eligible.size() + shortlist.size(), shortlist.size());
            return shortlist;
        }

        // Game thread.  Nobody shortlisted: no call.  One: straight to the letter.  More: the
        // cheap pick first.  Their memories are fetched on a worker thread (the query blocks).
        void Start(std::vector<Candidate> shortlist, Token token)
        {
            if (shortlist.empty()) {
                SKSE::log::info("[NpcLetters] Nobody can write this time");
                ++g_tally.nobody;
                Finish();
                return;
            }
            g_stepSince = std::chrono::steady_clock::now();  // the timeout is per LLM call
            std::thread([shortlist = std::move(shortlist), token, player = ReadPlayer()]() mutable {
                try {
                    for (auto& c : shortlist) {
                        const auto found =
                            json::parse(SkyrimNet::Memories(c.formId, kMaxMemories, player.name, "physical_letters"), nullptr, false);
                        if (!found.is_array()) continue;
                        for (const auto& m : found) {
                            if (m.contains("content") && m["content"].is_string()) c.memories.push_back(m["content"]);
                        }
                    }
                } catch (const std::exception& e) {
                    SKSE::log::error("[NpcLetters] Reading memories failed: {}", e.what());
                }
                if (shortlist.size() == 1) Write(std::move(shortlist.front()), token, player);
                else Pick(std::move(shortlist), token, player);
            }).detach();
        }
    }

    void Tick()
    {
        if (g_busy && std::chrono::steady_clock::now() - g_stepSince > kStepTimeout) {
            SKSE::log::warn("[NpcLetters] No answer after {} minutes: the attempt is given up",
                            std::chrono::duration_cast<std::chrono::minutes>(kStepTimeout).count());
            ++g_attempt;  // whatever it answers later is dropped
            ++g_tally.timedOut;
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
        g_stepSince = std::chrono::steady_clock::now();
        ++g_tally.attempts;
        const Token token{ Session::Generation(), ++g_attempt };
        const int minEvents = Config::GetSingleton()->Get(Config::kNpcMinEvents);
        std::thread([now, cooldowns = g_cooldownUntil, minEvents, token,
                     playerName = std::string{ RE::PlayerCharacter::GetSingleton()->GetName() },
                     playerUuid = ToUuid(SkyrimNet::UuidForFormId(kPlayer))]() {
            std::vector<Candidate> pool;
            try {
                pool = Pool(now, cooldowns, minEvents, playerName, playerUuid);
            } catch (const std::exception& e) {
                SKSE::log::error("[NpcLetters] Picking a writer failed: {}", e.what());
            }
            SKSE::GetTaskInterface()->AddTask([pool = std::move(pool), token]() mutable {
                // An exception must not cross into the engine.
                try {
                    if (!token.Current()) return;
                    Start(Shortlist(std::move(pool)), token);
                } catch (const std::exception& e) {
                    SKSE::log::error("[NpcLetters] Picking a writer failed: {}", e.what());
                    if (token.Current()) Finish();
                }
            });
        }).detach();
    }

    void StartCooldown(const std::string& uuid)
    {
        g_cooldownUntil[uuid] = Now() + Config::GetSingleton()->Get(Config::kNpcCooldown);
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
