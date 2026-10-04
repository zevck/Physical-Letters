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

#include "NpcToNpc.h"
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

#include <array>
#include <map>
#include <random>
#include <regex>
#include <thread>
#include <unordered_set>
#include <variant>

namespace PhysicalLetters::NpcToNpc {

    namespace {
        using json = nlohmann::json;
        using GameTime::Now;

        // The cheap pass that proposes recipients, and the letter itself (docs/NPC_TO_NPC.md#an-attempt).
        constexpr auto kProposePrompt = "physical_letters_npc_propose";
        constexpr auto kWritePrompt = "physical_letters_npc_letter";
        // SkyrimNet's OpenRouter variant for the proposals: a fast, cheap model in its default
        // config.  A config without it uses its default model.
        constexpr auto kProposeVariant = "meta";
        constexpr std::uint32_t kRecordVersion = 1;
        // Actors SkyrimNet lists at most (PublicSearchActors' cap).
        constexpr int kMaxActors = 500;
        // A writer's letters with other NPCs shown to the proposals.
        constexpr int kMaxLetterLines = 5;
        constexpr RE::FormID kPlayer = 0x14;
        constexpr float kDefaultImportance = 0.5f;
        // SkyrimNet drops cancelled LLM tasks without calling back: a step this old failed.
        constexpr auto kStepTimeout = std::chrono::minutes(5);

        // Saved.
        double g_nextAt = 0;  // game days; 0 = not scheduled yet
        std::unordered_map<std::string, double> g_pairUntil;  // PairKey -> game day

        bool g_busy = false;
        std::chrono::steady_clock::time_point g_stepSince;
        std::uint64_t g_attempt = 0;

        // This play session's attempts and what came of them, logged after each attempt: to
        // tune the prompts and the settings by.  Not saved.
        struct Tally {
            int attempts = 0;
            int noWriters = 0;   // nobody could write: no LLM call
            int proposeCalls = 0;
            int proposals = 0;   // names proposed
            int noValid = 0;     // attempts where no name resolved
            int writes = 0;      // letter calls
            int written = 0;
            int declined = 0;
            int llmFailed = 0;
            int timedOut = 0;
            std::map<std::string, int> refused;  // why a proposed name was refused
        } g_tally;

        void LogTally()
        {
            std::string refused;
            for (const auto& [why, n] : g_tally.refused) refused += std::format("{}{} {}", refused.empty() ? "" : ", ", n, why);
            SKSE::log::info("[NpcToNpc] This session: {} attempt(s) ({} with no writer: no LLM call); {} proposal call(s), {} "
                            "name(s) proposed{}{}; {} attempt(s) with no name usable; {} letter call(s): {} written, {} "
                            "declined; {} LLM failure(s), {} timed out",
                            g_tally.attempts, g_tally.noWriters, g_tally.proposeCalls, g_tally.proposals,
                            refused.empty() ? "" : ", refused: ", refused, g_tally.noValid, g_tally.writes, g_tally.written,
                            g_tally.declined, g_tally.llmFailed, g_tally.timedOut);
        }

        // Which attempt, in which session, a result belongs to: anything else answers late.
        struct Token {
            std::uint32_t generation = 0;
            std::uint64_t attempt = 0;

            bool Current() const { return generation == Session::Generation() && attempt == g_attempt; }
        };

        struct Writer {
            std::string uuid;
            RE::FormID  formId = 0;
            std::string name;
            std::string place;  // set when drawn
        };

        struct Resolved {
            std::string uuid;
            RE::FormID  formId = 0;
            std::string name;
            RE::Actor*  actor = nullptr;  // set by Check, on the game thread
        };

        // Why a proposed name can't receive this writer's letter (the log and the tally).
        struct Refusal {
            std::string key;
        };

        // A writer and someone their letter will reach.
        struct Pair {
            Writer      writer;
            Resolved    recipient;
            std::string recipientPlace;
        };

        std::mt19937_64& Rng()
        {
            static std::mt19937_64 rng{ std::random_device{}() };
            return rng;
        }

        std::string PlayerUuid()
        {
            return SkyrimNet::UuidForFormId(kPlayer);
        }

        std::string PairKey(const std::string& a, const std::string& b)
        {
            return a < b ? a + "|" + b : b + "|" + a;
        }

        std::string Lower(std::string text)
        {
            std::ranges::transform(text, text.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            return text;
        }

        std::uint64_t ToUuid(const std::string& text)
        {
            std::uint64_t uuid = 0;
            std::from_chars(text.data(), text.data() + text.size(), uuid);
            return uuid;
        }

        // A person who can write and be written to: a unique NPC (not a generic guard or
        // bandit) of an NPC race (not a creature or an animal: SkyrimNet registers rabbits too),
        // with a voice, not a child.
        bool IsPerson(RE::Actor* actor)
        {
            static auto* npcType = RE::TESForm::LookupByID<RE::BGSKeyword>(0x013794);  // ActorTypeNPC
            auto* race = actor ? actor->GetRace() : nullptr;
            auto* base = actor ? actor->GetActorBase() : nullptr;
            return race && base && base->IsUnique() && base->voiceType && npcType && race->HasKeyword(npcType) &&
                   !actor->IsChild();
        }

        // Where the actor lives: their area's name, else their location's.
        std::string PlaceName(RE::Actor* actor)
        {
            if (const auto* area = Travel::Area(actor); area && area->GetName() && *area->GetName()) return area->GetName();
            if (const auto* location = actor ? actor->GetCurrentLocation() : nullptr;
                location && location->GetName() && *location->GetName()) {
                return location->GetName();
            }
            return "somewhere in Skyrim";
        }

        void Schedule()
        {
            const double days = Config::GetSingleton()->Get(Config::kN2nInterval) *
                                std::uniform_real_distribution<double>(0.5, 1.5)(Rng());
            g_nextAt = Now() + days;
            SKSE::log::info("[NpcToNpc] The next letter between NPCs is due in {:.1f} game days", days);
        }

        void Finish()
        {
            Schedule();
            g_busy = false;
            LogTally();
        }

        void FinishLater(Token token)
        {
            SKSE::GetTaskInterface()->AddTask([token]() {
                // An exception must not cross into the engine.
                try {
                    if (token.Current()) Finish();
                } catch (const std::exception& e) {
                    SKSE::log::error("[NpcToNpc] Ending the attempt failed: {}", e.what());
                }
            });
        }

        // The letter's thread: the root letter's id, and how many letters it holds up to this one.
        std::pair<std::string, int> Thread(const std::string& letterId)
        {
            auto* db = LetterDB::GetSingleton();
            std::string id = letterId;
            int count = 0;
            for (; count < 50; ++count) {  // bounded: in_reply_to can't loop, but a bad DB could
                const auto letter = db->Get(id);
                if (!letter || letter->inReplyTo.empty()) return { id, count + 1 };
                id = letter->inReplyTo;
            }
            return { id, count };
        }

        struct Open {
            std::unordered_set<std::string> threads;  // root letter ids
            std::unordered_set<std::string> npcs;     // UUIDs writing or awaited in them
        };

        // The threads between NPCs with a letter on its way or unread (Transit's queue): no
        // state of our own to drift.
        Open OpenThreads()
        {
            Open open;
            auto* db = LetterDB::GetSingleton();
            for (const auto& id : Transit::PendingLetterIds()) {
                const auto letter = db->Get(id);
                if (!letter || !IsNpcLetter(*letter)) continue;
                open.threads.insert(Thread(id).first);
                open.npcs.insert(letter->authorUuid);
                open.npcs.insert(letter->recipientUuid);
            }
            return open;
        }

        bool PairOnCooldown(const std::string& a, const std::string& b)
        {
            const auto it = g_pairUntil.find(PairKey(a, b));
            return it != g_pairUntil.end() && it->second > Now();
        }

        // Worker thread.  Who may write: everyone SkyrimNet has registered, or (KnownOnly) the
        // NPCs with enough events involving the player.
        std::vector<Writer> Pool(bool knownOnly, int minEvents)
        {
            std::vector<Writer> pool;
            if (knownOnly) {
                const auto engagement = json::parse(SkyrimNet::Engagement(), nullptr, false);
                if (!engagement.is_array()) return pool;
                for (const auto& entry : engagement) {
                    const auto formId = entry.value("formId", 0u);
                    if (formId == 0 || formId == kPlayer || entry.value("eventCount", 0) < minEvents) continue;
                    auto uuid = SkyrimNet::UuidForFormId(formId);
                    if (uuid.empty()) continue;
                    auto name = SkyrimNet::ActorName(uuid);
                    pool.push_back({ std::move(uuid), formId, name.empty() ? entry.value("name", std::string{}) : name, {} });
                }
                return pool;
            }
            const auto actors = json::parse(SkyrimNet::SearchActors("", kMaxActors), nullptr, false);
            if (!actors.is_array()) return pool;
            for (const auto& actor : actors) {
                if (actor.value("isPlayer", false)) continue;
                const auto formId = actor.value("formId", 0u);
                const auto uuid = actor.value("uuid", std::uint64_t{ 0 });
                if (formId == 0 || uuid == 0) continue;
                pool.push_back({ std::to_string(uuid), formId, actor.value("name", std::string{}), {} });
            }
            return pool;
        }

        // The writer's actor if they can write now: the same actor as their UUID, alive, a
        // unique person (IsPerson), and not already in a thread.
        RE::Actor* CanWrite(const Writer& writer, const Open& open, std::string& why)
        {
            auto* actor = RE::TESForm::LookupByID<RE::Actor>(writer.formId);
            if (!actor || SkyrimNet::FormIdForUuid(writer.uuid) != writer.formId) {
                why = "not found";
                return nullptr;
            }
            if (actor->IsDead()) {
                why = "dead";
                return nullptr;
            }
            if (!IsPerson(actor)) {
                why = "not a unique person (generic, a creature, a child, or without a voice)";
                return nullptr;
            }
            if (open.npcs.contains(writer.uuid)) {
                why = "already corresponding";
                return nullptr;
            }
            return actor;
        }

        // The jarls, from Skyrim.esm's JobJarlFaction (0x050920): every hold's jarl in both
        // outcomes of the civil war.  Erikur, Hrongar and Bryling are in the faction but never
        // rule (cut content), so they aren't here.  SkyrimNet names them
        // "Jarl Balgruuf the Greater", the game "Balgruuf the Greater" (short name "Balgruuf"),
        // and letters use any of these; each name below, with or without "Jarl ", means the
        // first.  Skald's "the Elder" is from his EditorID (SkaldtheElder): what the game calls him.
        struct Jarl {
            std::string_view name;
            std::array<std::string_view, 2> aliases;
        };
        constexpr Jarl kJarls[] = {
            { "Balgruuf the Greater", { "Balgruuf", "" } },   { "Brina Merilis", { "Brina", "" } },
            { "Brunwulf Free-Winter", { "Brunwulf", "" } },
            { "Dengeir of Stuhn", { "Dengeir", "" } },        { "Elisif the Fair", { "Elisif", "" } },
            { "Idgrod Ravencrone", { "Idgrod", "" } },        { "Igmund", { "", "" } },
            { "Korir", { "", "" } },                          { "Kraldar", { "", "" } },
            { "Laila Law-Giver", { "Laila", "" } },           { "Maven Black-Briar", { "Maven", "" } },
            { "Siddgeir", { "", "" } },                       { "Skald", { "Skald the Elder", "" } },
            { "Sorli the Builder", { "Sorli", "" } },         { "Thongvor Silver-Blood", { "Thongvor", "" } },
            { "Ulfric Stormcloak", { "Ulfric", "" } },        { "Vignar Gray-Mane", { "Vignar", "" } },
        };

        std::string StripJarl(const std::string& name)
        {
            return Lower(name).starts_with("jarl ") ? name.substr(5) : name;
        }

        // The jarl `name` means, by any of their names with or without "Jarl ", or nullptr.
        const Jarl* FindJarl(const std::string& name)
        {
            const auto wanted = Lower(StripJarl(name));
            for (const auto& jarl : kJarls) {
                if (wanted == Lower(std::string{ jarl.name })) return &jarl;
                for (const auto alias : jarl.aliases) {
                    if (!alias.empty() && wanted == Lower(std::string{ alias })) return &jarl;
                }
            }
            return nullptr;
        }

        // Any thread.  The one actor SkyrimNet has registered under exactly this name
        // (case-insensitive), not the player or the writer (docs/NPC_TO_NPC.md#the-recipient).
        // A jarl matches by any of their names (kJarls).
        std::variant<Resolved, Refusal> Lookup(const std::string& name, const Writer& writer, const std::string& playerName)
        {
            if (Lower(name) == Lower(playerName)) return Refusal{ "the player" };
            const auto* jarl = FindJarl(name);
            const std::string query = jarl ? std::string{ jarl->name } : name;
            const auto matchesName = [&](const std::string& actorName) {
                return jarl ? Lower(StripJarl(actorName)) == Lower(query) : Lower(actorName) == Lower(name);
            };
            const auto found = json::parse(SkyrimNet::SearchActors(query, 25), nullptr, false);
            std::vector<json> matches;
            if (found.is_array()) {
                for (const auto& actor : found) {
                    if (!matchesName(actor.value("name", std::string{})) || actor.value("isPlayer", false)) continue;
                    if (std::to_string(actor.value("uuid", std::uint64_t{ 0 })) == writer.uuid) continue;
                    matches.push_back(actor);
                }
            }
            if (matches.empty()) return Refusal{ "not resolved by SkyrimNet" };
            if (matches.size() > 1) return Refusal{ "several by that name" };
            return Resolved{ std::to_string(matches[0].value("uuid", std::uint64_t{ 0 })), matches[0].value("formId", 0u),
                             matches[0].value("name", name), nullptr };
        }

        // Game thread.  Whether the looked-up recipient can receive this writer's letter: the same
        // actor as their UUID, alive, a unique person, living elsewhere (another area, and at
        // least MinDistance away: the farm outside town is a neighbour), and free to correspond.
        // Sets `r.actor`.
        std::optional<Refusal> Check(Resolved& r, const Writer& writer, RE::Actor* writerActor, const Open& open)
        {
            r.actor = RE::TESForm::LookupByID<RE::Actor>(r.formId);
            if (!r.actor || SkyrimNet::FormIdForUuid(r.uuid) != r.formId) return Refusal{ "unreachable" };
            if (r.actor->IsDead()) return Refusal{ "dead" };
            if (!IsPerson(r.actor)) return Refusal{ "not a unique person" };
            if (const auto* area = Travel::Area(writerActor); area && area == Travel::Area(r.actor)) {
                return Refusal{ "same place" };
            }
            if (const auto distance = Travel::Distance(writerActor, r.actor);
                distance && *distance < Config::GetSingleton()->Get(Config::kN2nMinDistance)) {
                return Refusal{ "too close" };
            }
            if (PairOnCooldown(writer.uuid, r.uuid)) return Refusal{ "pair on cooldown" };
            if (open.npcs.contains(r.uuid)) return Refusal{ "busy" };
            return std::nullopt;
        }

        // Worker thread.  The actor's newest memories (letters excluded: they come as letters).
        json Memories(RE::FormID formId, int count)
        {
            auto memories = json::array();
            if (count <= 0) return memories;
            const auto found = json::parse(SkyrimNet::RecentMemories(formId, count, "physical_letters"), nullptr, false);
            if (found.is_array()) {
                for (const auto& m : found) {
                    if (m.contains("content") && m["content"].is_string()) memories.push_back(m["content"]);
                }
            }
            return memories;
        }

        // Worker thread.  The letters the writer exchanged with other NPCs and remembers, newest
        // first, one line each ("wrote to Nilsine Shatter-Shield, 5 days ago").
        json LetterLines(const Writer& writer, double now)
        {
            auto lines = json::array();
            for (const auto& letter : LetterDB::GetSingleton()->Involving(writer.uuid, 4 * kMaxLetterLines)) {
                if (lines.size() >= static_cast<std::size_t>(kMaxLetterLines)) break;
                if (!IsNpcLetter(letter) || !SkyrimNet::HasMemoryWithTag(writer.formId, Reading::LetterTag(letter.id))) continue;
                const int days = static_cast<int>(std::max(0.0, now - letter.writtenAt));
                const auto when = days == 0 ? std::string{ "today" } : days == 1 ? std::string{ "a day ago" }
                                                                                  : std::format("{} days ago", days);
                lines.push_back(letter.authorUuid == writer.uuid ? std::format("wrote to {}, {}", letter.recipientName, when)
                                                                 : std::format("had a letter from {}, {}", letter.authorName, when));
            }
            return lines;
        }

        // The writer's memory of the letter, tagged with it: their later readings count it
        // as correspondence.
        void Remember(const Pair& pair, const Letter& letter, const json& answer, std::uint32_t generation)
        {
            auto memory = LlmJson::GetString(answer, "memory");
            if (memory.empty()) memory = std::format("I wrote a letter to {}.", pair.recipient.name);
            const auto content = std::format("{}\n\nMy letter to {}:\n{}", memory, pair.recipient.name, letter.body);
            const float importance = std::clamp(LlmJson::GetFloat(answer, "importance", kDefaultImportance), 0.0f, 1.0f);
            const auto emotion = LlmJson::GetString(answer, "emotion");
            const auto tags = json::array({ "physical_letters", "letter_sent", Reading::LetterTag(letter.id) }).dump();
            std::thread([formId = pair.writer.formId, related = pair.recipient.formId, content, importance, emotion, tags,
                         name = pair.writer.name, generation]() {
                try {
                    if (generation != Session::Generation()) return;
                    const int id = SkyrimNet::AddMemory(formId, content, importance, "RELATIONSHIP", emotion, tags,
                                                        std::format("[{}]", related));
                    if (id == 0) SKSE::log::error("[NpcToNpc] SkyrimNet didn't store {}'s memory of writing", name);
                } catch (const std::exception& e) {
                    SKSE::log::error("[NpcToNpc] Storing {}'s memory failed: {}", name, e.what());
                }
            }).detach();
        }

        // Game thread.  The letter call's answer: a letter to send, or none.
        void OnLetter(Pair pair, Token token, const std::string& response, bool success)
        {
            if (!token.Current()) {
                SKSE::log::info("[NpcToNpc] {}'s letter came after the attempt ended: dropped", pair.writer.name);
                return;
            }
            if (!success) {
                ++g_tally.llmFailed;
                SKSE::log::error("[NpcToNpc] The letter call for {} failed: {}", pair.writer.name, response.substr(0, 300));
                Finish();
                return;
            }
            SKSE::log::debug("[NpcToNpc] {}'s letter: {}", pair.writer.name, response.substr(0, 2000));
            const auto answer = LlmJson::ParseResponse(response);
            const auto text = answer.is_object() ? LlmJson::GetString(answer, "letter") : std::string{};
            if (!answer.is_object() || !LlmJson::GetBool(answer, "write") || text.empty()) {
                ++g_tally.declined;
                SKSE::log::info("[NpcToNpc] {} doesn't write to {} after all{}", pair.writer.name, pair.recipient.name,
                                answer.is_object() ? std::format(" (tie: {})", LlmJson::GetString(answer, "tie"))
                                                   : std::format(" (unreadable answer: {})", response.substr(0, 300)));
                Finish();
                return;
            }
            // The world may have changed during the calls.
            std::string why;
            const auto open = OpenThreads();
            auto* writerActor = CanWrite(pair.writer, open, why);
            if (!writerActor) {
                SKSE::log::info("[NpcToNpc] {} can't write any more ({})", pair.writer.name, why);
                Finish();
                return;
            }
            if (const auto refusal = Check(pair.recipient, pair.writer, writerActor, open)) {
                SKSE::log::info("[NpcToNpc] {} can't receive the letter any more ({})", pair.recipient.name, refusal->key);
                Finish();
                return;
            }
            const Letter letter{ .id = Letters::NewId(),
                                 .authorUuid = pair.writer.uuid,
                                 .authorName = pair.writer.name,
                                 .recipientUuid = pair.recipient.uuid,
                                 .recipientName = pair.recipient.name,
                                 .body = text,
                                 .writtenAt = Now() };
            if (!Letters::Create(letter)) {
                Finish();
                return;
            }
            const double hours = Travel::Hours(writerActor, pair.recipient.actor);
            Transit::QueueToNpc(letter, hours);
            ++g_tally.written;
            SKSE::log::info("[NpcToNpc] {} wrote to {} (tie: {}; purpose: {}): letter {}, due in {:.1f} game hours",
                            pair.writer.name, pair.recipient.name, LlmJson::GetString(answer, "tie"),
                            LlmJson::GetString(answer, "purpose"), letter.id, hours);
            Finish();
            Remember(pair, letter, answer, token.generation);
        }

        // Worker thread.  The letter call on the default model: it judges the tie and the purpose
        // from both profiles, writes, or declines.
        void Write(Pair pair, Token token, std::string playerName, int memoryCount, double now)
        {
            try {
                const json context = {
                    { "npc", { { "UUID", ToUuid(pair.writer.uuid) }, { "name", pair.writer.name } } },
                    { "place", pair.writer.place },
                    { "recipient",
                      { { "UUID", ToUuid(pair.recipient.uuid) }, { "name", pair.recipient.name }, { "place", pair.recipientPlace } } },
                    { "player", playerName },
                    { "memories", Memories(pair.writer.formId, memoryCount) },
                    { "correspondence", Reading::Correspondence(pair.writer.uuid, pair.recipient.uuid, pair.writer.formId, now) },
                };
                const bool queued = SkyrimNet::SendPrompt(kWritePrompt, context.dump(), [pair, token](std::string response, bool success) {
                    SKSE::GetTaskInterface()->AddTask([pair, token, response = std::move(response), success]() {
                        // An exception must not cross into the engine.
                        try {
                            OnLetter(pair, token, response, success);
                        } catch (const std::exception& e) {
                            SKSE::log::error("[NpcToNpc] Handling the letter failed: {}", e.what());
                            if (token.Current()) Finish();
                        }
                    });
                });
                if (!queued) {
                    SKSE::log::error("[NpcToNpc] SkyrimNet refused the letter prompt");
                    FinishLater(token);
                    return;
                }
                SKSE::log::info("[NpcToNpc] Asking {} to write to {}", pair.writer.name, pair.recipient.name);
            } catch (const std::exception& e) {
                SKSE::log::error("[NpcToNpc] Asking for the letter failed: {}", e.what());
                FinishLater(token);
            }
        }

        // The names-only proposals, {"1": ["Name", ...], ...}, read tolerantly: the cheap
        // variant's output may be cut short (SkyrimNet's default "meta" stops at 100 tokens), so
        // every writer's list that closed before the cut counts, whether or not the whole parses.
        std::map<int, std::vector<std::string>> ReadProposals(const std::string& response)
        {
            std::map<int, std::vector<std::string>> proposals;
            static const std::regex list{ R"re("(\d+)"\s*:\s*\[([^\]]*)\])re" };
            static const std::regex name{ R"re("((?:[^"\\]|\\.)*)")re" };
            for (auto it = std::sregex_iterator(response.begin(), response.end(), list); it != std::sregex_iterator(); ++it) {
                const int number = std::stoi((*it)[1].str());
                const auto names = (*it)[2].str();
                auto& out = proposals[number];
                for (auto n = std::sregex_iterator(names.begin(), names.end(), name); n != std::sregex_iterator(); ++n) {
                    if (const auto text = (*n)[1].str(); !text.empty()) out.push_back(text);
                }
            }
            return proposals;
        }

        // Game thread.  The proposals' answer: for each writer, the first proposed name that
        // resolves and passes; one of those pairs, drawn at random, gets written.
        void OnProposals(std::vector<Writer> writers, Token token, const std::string& response, bool success)
        {
            if (!token.Current()) {
                SKSE::log::info("[NpcToNpc] The proposals came after the attempt ended: dropped");
                return;
            }
            if (!success) {
                ++g_tally.llmFailed;
                SKSE::log::error("[NpcToNpc] The proposal call failed: {}", response.substr(0, 300));
                Finish();
                return;
            }
            SKSE::log::debug("[NpcToNpc] The proposals: {}", response.substr(0, 3000));
            const auto proposals = ReadProposals(response);
            const int maxNames = Config::GetSingleton()->Get(Config::kN2nNames);
            const std::string playerName = RE::PlayerCharacter::GetSingleton()->GetName();
            const auto open = OpenThreads();
            std::vector<Pair> pairs;
            if (proposals.empty()) SKSE::log::info("[NpcToNpc] No proposals could be read: {}", response.substr(0, 300));
            for (const auto& [number, names] : proposals) {
                if (number < 1 || number > static_cast<int>(writers.size())) continue;
                const auto& writer = writers[number - 1];
                std::string why;
                auto* writerActor = CanWrite(writer, open, why);
                if (!writerActor) continue;
                int tried = 0;
                for (const auto& name : names) {
                    if (tried++ >= maxNames) break;
                    ++g_tally.proposals;
                    auto found = Lookup(name, writer, playerName);
                    std::optional<Refusal> refusal;
                    if (auto* r = std::get_if<Refusal>(&found)) refusal = *r;
                    else refusal = Check(std::get<Resolved>(found), writer, writerActor, open);
                    if (refusal) {
                        ++g_tally.refused[refusal->key];
                        SKSE::log::info("[NpcToNpc] {} -> {}: {}", writer.name, name, refusal->key);
                        continue;
                    }
                    auto& recipient = std::get<Resolved>(found);
                    SKSE::log::info("[NpcToNpc] {} -> {}: usable", writer.name, recipient.name);
                    pairs.push_back({ writer, recipient, PlaceName(recipient.actor) });
                    break;
                }
            }
            if (pairs.empty()) {
                ++g_tally.noValid;
                SKSE::log::info("[NpcToNpc] No proposed recipient can be written to this time");
                Finish();
                return;
            }
            auto pair = std::move(pairs[std::uniform_int_distribution<std::size_t>(0, pairs.size() - 1)(Rng())]);
            g_stepSince = std::chrono::steady_clock::now();  // the timeout is per LLM call
            ++g_tally.writes;
            std::thread([pair = std::move(pair), token, playerName,
                         memoryCount = Config::GetSingleton()->Get(Config::kN2nMemories), now = Now()]() mutable {
                Write(std::move(pair), token, std::move(playerName), memoryCount, now);
            }).detach();
        }

        // Worker thread.  The proposal call on the cheap variant: each writer's profile (by the
        // template), their newest memories and their letters with other NPCs.
        void Propose(std::vector<Writer> writers, Token token, std::string playerName, int names, int memoryCount, double now)
        {
            try {
                auto list = json::array();
                for (std::size_t i = 0; i < writers.size(); ++i) {
                    const auto& w = writers[i];
                    list.push_back({ { "number", i + 1 },
                                     { "UUID", ToUuid(w.uuid) },
                                     { "name", w.name },
                                     { "place", w.place },
                                     { "memories", Memories(w.formId, memoryCount) },
                                     { "letters", LetterLines(w, now) } });
                }
                const json context = { { "writers", list }, { "player", playerName }, { "names", names } };
                const bool queued = SkyrimNet::SendPrompt(
                    kProposePrompt, context.dump(),
                    [writers, token](std::string response, bool success) {
                        SKSE::GetTaskInterface()->AddTask([writers, token, response = std::move(response), success]() {
                            // An exception must not cross into the engine.
                            try {
                                OnProposals(writers, token, response, success);
                            } catch (const std::exception& e) {
                                SKSE::log::error("[NpcToNpc] Handling the proposals failed: {}", e.what());
                                if (token.Current()) Finish();
                            }
                        });
                    },
                    kProposeVariant);
                if (!queued) {
                    SKSE::log::error("[NpcToNpc] SkyrimNet refused the proposal prompt");
                    FinishLater(token);
                    return;
                }
                SKSE::log::info("[NpcToNpc] Asking whom {} writer(s) would write to", writers.size());
            } catch (const std::exception& e) {
                SKSE::log::error("[NpcToNpc] Asking for proposals failed: {}", e.what());
                FinishLater(token);
            }
        }

        // Game thread.  WritersPerAttempt writers drawn at random among those who can write.
        void OnPool(std::vector<Writer> pool, Token token)
        {
            if (!token.Current()) return;
            const auto* config = Config::GetSingleton();
            const auto count = static_cast<std::size_t>(config->Get(Config::kN2nWriters));
            const auto open = OpenThreads();
            std::vector<Writer> writers;
            std::ranges::shuffle(pool, Rng());
            for (auto& writer : pool) {
                if (writers.size() >= count) break;
                std::string why;
                auto* actor = CanWrite(writer, open, why);
                if (!actor) continue;
                writer.place = PlaceName(actor);
                writers.push_back(std::move(writer));
            }
            std::string names;
            for (const auto& w : writers) names += std::format("{}{} ({})", names.empty() ? "" : ", ", w.name, w.place);
            SKSE::log::info("[NpcToNpc] {} actor(s) in SkyrimNet; writers drawn: {}", pool.size(), names.empty() ? "none" : names);
            if (writers.empty()) {
                ++g_tally.noWriters;
                Finish();
                return;
            }
            g_stepSince = std::chrono::steady_clock::now();  // the timeout is per LLM call
            ++g_tally.proposeCalls;
            std::thread([writers = std::move(writers), token, playerName = std::string{ RE::PlayerCharacter::GetSingleton()->GetName() },
                         names = config->Get(Config::kN2nNames), memoryCount = config->Get(Config::kN2nMemories),
                         now = Now()]() mutable { Propose(std::move(writers), token, std::move(playerName), names, memoryCount, now); })
                .detach();
        }
    }

    bool IsNpcLetter(const Letter& letter)
    {
        const auto player = PlayerUuid();
        return !player.empty() && letter.authorUuid != player && letter.recipientUuid != player;
    }

    bool CanReply(const std::string& letterId)
    {
        return Thread(letterId).second < Config::GetSingleton()->Get(Config::kN2nMaxLetters);
    }

    void ThreadEnded(const Letter& letter)
    {
        const int days = Config::GetSingleton()->Get(Config::kN2nPairCooldown);
        g_pairUntil[PairKey(letter.authorUuid, letter.recipientUuid)] = Now() + days;
        SKSE::log::info("[NpcToNpc] The letters between {} and {} are over for now ({} letter(s)); they don't write again for "
                        "{} days",
                        letter.authorName, letter.recipientName, Thread(letter.id).second, days);
    }

    void Tick()
    {
        if (g_busy && std::chrono::steady_clock::now() - g_stepSince > kStepTimeout) {
            SKSE::log::warn("[NpcToNpc] No answer after {} minutes: the attempt is given up",
                            std::chrono::duration_cast<std::chrono::minutes>(kStepTimeout).count());
            ++g_attempt;  // whatever it answers later is dropped
            ++g_tally.timedOut;
            Finish();
            return;
        }
        const auto* config = Config::GetSingleton();
        if (!Session::IsReady() || g_busy || !config->Get(Config::kN2nEnabled) || !SkyrimNet::CanSearchActors()) return;
        if (g_nextAt <= 0) {
            Schedule();
            return;
        }
        if (Now() < g_nextAt) return;

        if (const auto open = OpenThreads().threads.size(); open >= static_cast<std::size_t>(config->Get(Config::kN2nMaxThreads))) {
            SKSE::log::info("[NpcToNpc] {} thread(s) between NPCs open: no new one this time", open);
            Schedule();
            return;
        }

        g_busy = true;
        g_stepSince = std::chrono::steady_clock::now();
        ++g_tally.attempts;
        const Token token{ Session::Generation(), ++g_attempt };
        std::thread([token, knownOnly = config->Get(Config::kN2nKnownOnly) != 0,
                     minEvents = config->Get(Config::kNpcMinEvents)]() {
            std::vector<Writer> pool;
            try {
                pool = Pool(knownOnly, minEvents);
            } catch (const std::exception& e) {
                SKSE::log::error("[NpcToNpc] Listing writers failed: {}", e.what());
            }
            SKSE::GetTaskInterface()->AddTask([pool = std::move(pool), token]() mutable {
                // An exception must not cross into the engine.
                try {
                    OnPool(std::move(pool), token);
                } catch (const std::exception& e) {
                    SKSE::log::error("[NpcToNpc] Drawing writers failed: {}", e.what());
                    if (token.Current()) Finish();
                }
            });
        }).detach();
    }

    void Save(SKSE::SerializationInterface* a_intfc, std::uint32_t a_type)
    {
        if (!a_intfc->OpenRecord(a_type, kRecordVersion)) {
            SKSE::log::error("[NpcToNpc] Couldn't open the co-save record");
            return;
        }
        a_intfc->WriteRecordData(g_nextAt);
        const double now = Now();
        std::uint32_t count = 0;
        for (const auto& [pair, until] : g_pairUntil) count += until > now ? 1 : 0;
        a_intfc->WriteRecordData(count);
        for (const auto& [pair, until] : g_pairUntil) {
            if (until <= now) continue;
            CoSave::WriteString(a_intfc, pair);
            a_intfc->WriteRecordData(until);
        }
    }

    void Load(SKSE::SerializationInterface* a_intfc, std::uint32_t a_version)
    {
        if (a_version != kRecordVersion) {
            SKSE::log::error("[NpcToNpc] Co-save record version {} is unknown — skipped", a_version);
            return;
        }
        std::uint32_t count = 0;
        if (a_intfc->ReadRecordData(g_nextAt) != sizeof(g_nextAt) || a_intfc->ReadRecordData(count) != sizeof(count)) return;
        for (std::uint32_t i = 0; i < count; ++i) {
            std::string pair;
            double until = 0;
            if (!CoSave::ReadString(a_intfc, pair) || a_intfc->ReadRecordData(until) != sizeof(until)) break;
            g_pairUntil[pair] = until;
        }
        SKSE::log::info("[NpcToNpc] Next letter at day {:.1f}; {} pair(s) on cooldown", g_nextAt, g_pairUntil.size());
    }

    void Revert()
    {
        g_nextAt = 0;
        g_pairUntil.clear();
        g_busy = false;
    }

} // namespace PhysicalLetters::NpcToNpc
