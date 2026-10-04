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

#include "Recipients.h"
#include "Actors.h"
#include "Config.h"
#include "MarkedText.h"
#include "SkyrimNet.h"
#include "Strings.h"

#include <map>
#include <set>

namespace PhysicalLetters::Recipients {

    namespace {
        using MarkedText::Trim;
        using MarkedText::TrimLeft;
        using MarkedText::WithoutBlood;
        using Names = std::map<std::string, std::vector<RE::Actor*>>;  // actors by lowercase name

        constexpr RE::FormID kLocTypeHold = 0x016771;
        // Letters typed before any suggestion: one letter starts too many names to check.
        constexpr std::size_t kMinTyped = 2;
        // More people of one name than this aren't told apart by address.
        constexpr std::size_t kMaxPeople = 10;
        constexpr std::size_t kMaxCompletions = 10;  // Tab cycles through at most these
        constexpr std::size_t kNumberDigits = 4;     // the end of the UUID an address shows, longer if needed
        constexpr int kMaxLocationDepth = 16;

        // This session's answers: SkyrimNet and the locations are asked about each actor once.
        std::unordered_map<RE::FormID, bool> g_hasMemories;
        std::unordered_map<RE::FormID, std::string> g_uuids;
        std::unordered_map<RE::FormID, std::string> g_names;
        std::unordered_map<RE::FormID, std::string> g_places;
        // This change's scan of the form map (NewText): later lookups of longer prefixes filter it.
        std::optional<std::pair<std::string, Names>> g_scan;

        std::string Lower(std::string text)
        {
            std::ranges::transform(text, text.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            return text;
        }

        // ASCII case-insensitive.
        bool StartsWith(std::string_view text, std::string_view prefix)
        {
            return text.size() >= prefix.size() && _strnicmp(text.data(), prefix.data(), prefix.size()) == 0;
        }

        bool IsUnique(RE::Actor* actor)
        {
            auto* base = actor->GetActorBase();
            return base && base->IsUnique();
        }

        // Whether SkyrimNet has memories of the actor.  `ask`: it may be asked (once per actor and
        // session); else only an answer already given counts.
        bool Known(RE::Actor* actor, bool ask)
        {
            const auto formId = actor->GetFormID();
            if (const auto it = g_hasMemories.find(formId); it != g_hasMemories.end()) return it->second;
            return ask && (g_hasMemories[formId] = SkyrimNet::HasMemories(formId));
        }

        // By GenericRecipients (docs/WRITING.md#the-recipient); `ask` as Known.
        bool CanReceive(RE::Actor* actor, int generic, bool ask)
        {
            if (IsUnique(actor) || generic == Config::kGenericAnyone) return true;
            return generic == Config::kGenericKnown && Known(actor, ask);
        }

        // Every actor in memory (persistent ones anywhere, the rest in loaded cells) of an NPC race,
        // not the player, deleted, disabled or dead, whose name starts with `prefix`.
        Names ActorsStarting(std::string_view prefix)
        {
            const std::string key = Lower(std::string{ prefix });
            if (g_scan && key.starts_with(g_scan->first)) {
                Names found;
                for (const auto& [name, actors] : g_scan->second) {
                    if (name.starts_with(key)) found.emplace(name, actors);
                }
                return found;
            }
            Names found;
            auto* npcType = Actors::NpcType();  // before the lock: a lookup takes it too
            const auto& [forms, lock] = RE::TESForm::GetAllForms();
            const RE::BSReadLockGuard guard{ lock };
            if (!forms || !npcType) return found;
            for (const auto& [formId, form] : *forms) {
                if (!form || form->GetFormType() != RE::FormType::ActorCharacter) continue;
                auto* actor = static_cast<RE::Actor*>(form);
                if (actor->IsPlayerRef() || actor->IsDeleted() || actor->IsDisabled() || actor->IsDead()) continue;
                const char* name = actor->GetDisplayFullName();
                if (!name || !*name || !StartsWith(name, prefix)) continue;
                if (Actors::IsNpcRace(actor, npcType)) found[Lower(name)].push_back(actor);
            }
            g_scan.emplace(key, found);
            return found;
        }

        // Of two references of one unique NPC, the one that is them: loaded, then persistent.
        bool Better(RE::Actor* a, RE::Actor* b)
        {
            const auto rank = [](RE::Actor* actor) {
                return std::tuple{ actor->Is3DLoaded(), (actor->GetFormFlags() & RE::TESForm::RecordFlags::kPersistent) != 0 };
            };
            return rank(a) != rank(b) ? rank(a) > rank(b) : a->GetFormID() < b->GetFormID();
        }

        // The people one name stands for (not under the form map's lock: it may ask SkyrimNet), one
        // reference per unique NPC.
        std::vector<RE::Actor*> People(const std::vector<RE::Actor*>& actors, bool ask)
        {
            const int generic = Config::GetSingleton()->Get(Config::kGenericRecipients);
            std::vector<RE::Actor*> people;
            std::unordered_map<RE::TESNPC*, RE::Actor*> byBase;
            for (auto* actor : actors) {
                if (!CanReceive(actor, generic, ask)) continue;
                if (!IsUnique(actor)) {
                    people.push_back(actor);
                    continue;
                }
                auto*& kept = byBase[actor->GetActorBase()];
                if (!kept || Better(actor, kept)) kept = actor;
            }
            for (const auto& [base, actor] : byBase) people.push_back(actor);
            std::ranges::sort(people, {}, &RE::Actor::GetFormID);
            return people;
        }

        // The place a location is in, as an address names it: the location just below its hold (the
        // town), or the hold.  "" if it isn't under a hold (a holding cell) or has no name.
        std::string Place(RE::BGSLocation* location)
        {
            static auto* hold = RE::TESForm::LookupByID<RE::BGSKeyword>(kLocTypeHold);
            RE::BGSLocation* below = nullptr;
            for (int depth = 0; location && hold && depth < kMaxLocationDepth; ++depth) {
                if (location->HasKeyword(hold)) {
                    const char* name = (below ? below : location)->GetName();
                    return name && *name ? std::string{ name } : std::string{};
                }
                below = location;
                location = location->parentLoc;
            }
            return {};
        }

        // Where they belong (their reference's persistent location), else where they are, else Tamriel.
        std::string PlaceOf(RE::Actor* actor)
        {
            if (auto place = Place(actor->GetEditorLocation1()); !place.empty()) return place;
            if (auto place = Place(actor->GetCurrentLocation()); !place.empty()) return place;
            return std::string{ Strings::kNoPlace };
        }

        // A name's people as recipients, registered with SkyrimNet, in the order offered, with their
        // addresses (docs/WRITING.md#the-recipient).
        std::vector<Recipient> Addressed(const std::vector<RE::Actor*>& people)
        {
            std::vector<Recipient> out;
            for (auto* actor : people) {
                const auto formId = actor->GetFormID();
                auto& uuid = g_uuids[formId];
                if (uuid.empty()) uuid = SkyrimNet::UuidForFormId(formId);
                if (uuid.empty()) {
                    SKSE::log::warn("[Recipients] No SkyrimNet UUID for {} (0x{:X})", actor->GetDisplayFullName(), formId);
                    continue;
                }
                auto& name = g_names[formId];
                if (name.empty()) name = SkyrimNet::ActorName(uuid);
                if (name.empty()) name = actor->GetDisplayFullName();
                out.push_back({ .actor = actor, .uuid = uuid, .name = name });
            }
            std::size_t digits = kNumberDigits;
            const auto tail = [&digits](const std::string& uuid) {
                return uuid.size() > digits ? uuid.substr(uuid.size() - digits) : uuid;
            };
            for (;; ++digits) {
                std::set<std::string> tails;
                for (const auto& r : out) tails.insert(tail(r.uuid));
                if (tails.size() == out.size() || std::ranges::all_of(out, [&](const Recipient& r) { return r.uuid.size() <= digits; })) {
                    break;
                }
            }
            for (auto& r : out) {
                auto& place = g_places[r.actor->GetFormID()];
                if (place.empty()) place = PlaceOf(r.actor);
                r.address = std::format("{} {}", tail(r.uuid), place);
            }
            // Those SkyrimNet has memories of first: the one you know is the one you mean.
            std::ranges::stable_sort(out, std::greater{}, [](const Recipient& r) { return Known(r.actor, true); });
            return out;
        }

        std::string Listed(const std::vector<Recipient>& people)
        {
            std::string out;
            for (const auto& r : people) out += (out.empty() ? "" : "; ") + r.address;
            return out;
        }

        struct Named {
            std::vector<Recipient> people;
            bool tooMany = false;  // more than kMaxPeople: none addressed
        };

        // The people of exactly this name, addressed.
        Named Lookup(const std::string& name)
        {
            const auto actors = ActorsStarting(name);
            const auto it = actors.find(Lower(name));
            if (it == actors.end()) return {};
            const auto people = People(it->second, true);
            if (people.size() > kMaxPeople) {
                SKSE::log::debug("[Recipients] {} people named \"{}\": too many to address", people.size(), name);
                return { .tooMany = true };
            }
            return { .people = Addressed(people) };
        }
    }

    void Reset()
    {
        g_hasMemories.clear();
        g_uuids.clear();
        g_names.clear();
        g_places.clear();
        g_scan.reset();
    }

    void NewText()
    {
        g_scan.reset();
    }

    std::vector<std::string> Completions(const std::string& line)
    {
        std::vector<std::string> out;
        const std::string start = TrimLeft(WithoutBlood(line));
        if (Trim(start).size() < kMinTyped) return out;
        const auto comma = start.find(',');
        if (comma == std::string::npos) {
            const std::string typed = Trim(start);
            for (const auto& [key, actors] : ActorsStarting(start)) {
                const std::string name = actors.front()->GetDisplayFullName();
                if (_stricmp(name.c_str(), typed.c_str()) != 0) {
                    // An incomplete name: SkyrimNet isn't asked about generic NPCs yet (docs/WRITING.md).
                    const auto people = People(actors, false);
                    if (!people.empty() && people.size() <= kMaxPeople) out.push_back(name.substr(start.size()));
                } else if (typed == start) {
                    // A complete name: its people's addresses, first.
                    std::vector<std::string> addresses;
                    for (const auto& r : Lookup(typed).people) addresses.push_back(", " + r.address);
                    out.insert(out.begin(), addresses.begin(), addresses.end());
                }
            }
        } else {
            const std::string rest = TrimLeft(start.substr(comma + 1));
            for (const auto& r : Lookup(Trim(start.substr(0, comma))).people) {
                if (rest.empty()) {
                    out.push_back((start.ends_with(' ') ? "" : " ") + r.address);
                } else if (StartsWith(r.address, rest)) {
                    out.push_back(r.address.substr(rest.size()));
                }
            }
        }
        std::erase(out, std::string{});
        if (out.size() > kMaxCompletions) out.resize(kMaxCompletions);
        SKSE::log::debug("[Recipients] {} completion(s) for \"{}\"", out.size(), start);
        return out;
    }

    std::variant<Recipient, std::string> Resolve(const std::string& line)
    {
        const std::string start = TrimLeft(WithoutBlood(line));
        const auto comma = start.find(',');
        const std::string name = Trim(start.substr(0, comma));
        if (name.empty()) return Strings::WriteNoName();
        const auto found = Lookup(name);
        const auto& people = found.people;
        const std::string rest = comma == std::string::npos ? std::string{} : Trim(start.substr(comma + 1));
        SKSE::log::debug("[Recipients] \"{}\"{}: {} possible recipient(s) ({})", name, rest.empty() ? "" : ", \"" + rest + "\"",
                         people.size(), Listed(people));
        if (found.tooMany) return Strings::WriteTooMany(name);
        if (people.empty()) return Strings::WriteNobody(name);
        // Only the whole address names someone (a suggestion fills it in).
        for (const auto& r : people) {
            if (_stricmp(r.address.c_str(), rest.c_str()) == 0) return r;
        }
        if (people.size() > 1) return Strings::WriteSeveral(name, Listed(people), people.front().address);
        if (rest.empty()) return Strings::WriteAddress(name, people.front().address);
        return Strings::WriteNoneAt(name, rest, Listed(people));
    }

} // namespace PhysicalLetters::Recipients
