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

#include "Api.h"
#include "Config.h"
#include "DmLetter.h"
#include "LetterDB.h"
#include "Letters.h"
#include "LlmJson.h"
#include "NpcToNpc.h"
#include "Package.h"
#include "PhysicalLettersAPI.h"
#include "SkyrimNet.h"
#include "Transit.h"


namespace PhysicalLetters::Api {

    namespace {
        using json = nlohmann::json;

        constexpr auto kScript = "PhysicalLetters";
        constexpr std::size_t kMaxActions = 20;
        constexpr std::size_t kMaxParams = 8;
        constexpr std::size_t kMaxNameLength = 40;
        constexpr std::size_t kMaxDescriptionLength = 300;
        constexpr std::size_t kMaxDisplayNameLength = 60;
        constexpr int kDefaultMaxLetters = 50;  // a query's, as SkyrimNet's

        struct Param {
            std::string name;
            std::string description;
        };

        // A letter action (docs/API.md#actions): a mod's, with its callback if it came through the C API.
        struct Action {
            std::string name;
            std::string displayName;         // the MCM's
            std::string displayDescription;  // the MCM's tooltip
            std::string description;         // the model's
            std::vector<Param> params;
            int letters = PL_LETTERS_ALL;
            PL_ActionCallback callback = nullptr;
            void* user = nullptr;
        };

        struct Listener {
            std::uint64_t id = 0;
            PL_EventCallback callback = nullptr;
            void* user = nullptr;
        };

        // The registry is read by Reading's worker threads (ActionsForPrompt) and written on the game thread.
        std::mutex g_mutex;
        std::vector<Action> g_actions;
        std::vector<Listener> g_listeners;
        std::uint64_t g_lastListener = 0;
        // Each letter's chosen action's values, for Papyrus's GetActionValue (this session only).
        std::unordered_map<std::string, std::vector<std::pair<std::string, std::string>>> g_values;

        // The strings GetLetter hands out, valid until the next call.
        PL_Letter g_letterOut{};
        std::string g_outSender, g_outText, g_outReply;
        std::string g_outQuery;  // QueryLetters' answer, valid until the next call

        void SendModEvent(const char* name, const std::string& letterId, float number, RE::FormID actorId)
        {
            auto* source = SKSE::GetModCallbackEventSource();
            if (!source) return;
            auto* actor = actorId ? RE::TESForm::LookupByID(actorId) : nullptr;
            SKSE::ModCallbackEvent event{ RE::BSFixedString(name), RE::BSFixedString(letterId), number, actor };
            source->SendEvent(&event);
        }

        void Notify(PL_Event kind, const std::string& letterId, RE::FormID actor, bool replied)
        {
            std::vector<Listener> listeners;
            {
                std::lock_guard lock{ g_mutex };
                listeners = g_listeners;
            }
            for (const auto& listener : listeners) {
                try {
                    listener.callback(listener.user, kind, letterId.c_str(), actor, replied);
                } catch (...) {
                    SKSE::log::error("[Api] A listener's callback threw (letter {})", letterId);
                }
            }
        }

        // An event, in a task of its own: never inside our own loops, so a listener may call the API (SendLetter).
        void Post(const char* modEvent, PL_Event kind, std::string letterId, RE::FormID actor, bool replied)
        {
            SKSE::GetTaskInterface()->AddTask([=]() {
                try {
                    SendModEvent(modEvent, letterId, replied ? 1.0f : 0.0f, actor);
                    Notify(kind, letterId, actor, replied);
                } catch (const std::exception& e) {
                    SKSE::log::error("[Api] An event for letter {} failed: {}", letterId, e.what());
                }
            });
        }

        // At most `max` bytes, never half a UTF-8 character (a broken one would fail the reading's prompt).
        void Cut(std::string& text, std::size_t max)
        {
            if (text.size() <= max) return;
            while (max > 0 && (static_cast<unsigned char>(text[max]) & 0xC0) == 0x80) --max;
            text.resize(max);
        }

        bool ValidName(std::string_view name)
        {
            if (name.empty() || name.size() > kMaxNameLength) return false;
            return std::ranges::all_of(name, [](unsigned char c) { return std::isalnum(c) || c == '_'; });
        }

        bool Register(Action action)
        {
            if (!ValidName(action.name) || action.description.empty() || action.displayName.empty() ||
                action.displayDescription.empty() || action.params.size() > kMaxParams || action.letters < PL_LETTERS_ALL ||
                action.letters > PL_LETTERS_NPC) {
                SKSE::log::warn("[Api] Action '{}' refused: a name is letters, digits and _ (at most {}), with a display name, a "
                                "display description, a description, at most {} parameters and letters 0-2", action.name,
                                kMaxNameLength, kMaxParams);
                return false;
            }
            Cut(action.description, kMaxDescriptionLength);
            Cut(action.displayName, kMaxDisplayNameLength);
            Cut(action.displayDescription, kMaxDescriptionLength);
            for (auto& param : action.params) {
                if (!ValidName(param.name)) {
                    SKSE::log::warn("[Api] Action '{}' refused: parameter '{}' isn't a name", action.name, param.name);
                    return false;
                }
                Cut(param.description, kMaxDescriptionLength);
            }
            const std::string name = action.name;
            std::lock_guard lock{ g_mutex };
            const auto it = std::ranges::find(g_actions, name, &Action::name);
            if (it != g_actions.end()) {
                *it = std::move(action);
            } else if (g_actions.size() >= kMaxActions) {
                SKSE::log::warn("[Api] Action '{}' refused: {} are registered already", name, kMaxActions);
                return false;
            } else {
                g_actions.push_back(std::move(action));
            }
            SKSE::log::info("[Api] Action registered: {} ({} registered)", name, g_actions.size());
            return true;
        }

        void Unregister(std::string_view name)
        {
            std::lock_guard lock{ g_mutex };
            if (std::erase_if(g_actions, [&](const Action& a) { return a.name == name; })) {
                SKSE::log::info("[Api] Action unregistered: {}", name);
            }
        }

        // Offered to this letter's reader: actions on, this one on, and for the player's letters or NPCs' as registered.
        bool Offered(const Action& action, bool fromPlayer)
        {
            const auto* config = Config::GetSingleton();
            if (!config->Get(Config::kLetterActions) || !config->ActionEnabled(action.name)) return false;
            return action.letters == PL_LETTERS_ALL || (action.letters == PL_LETTERS_PLAYER) == fromPlayer;
        }

        // The newest reply to the letter that this save has: after a reload, one written in the abandoned timeline isn't.
        std::string ReplyIdTo(const std::string& letterId)
        {
            for (const auto& id : LetterDB::GetSingleton()->RepliesTo(letterId)) {
                if (Letters::FormFor(id)) return id;
            }
            return {};
        }

        RE::FormID FormOf(const std::string& uuid)
        {
            if (uuid.empty()) return 0;
            const auto formId = SkyrimNet::FormIdForUuid(uuid);
            return formId && SkyrimNet::UuidForFormId(formId) == uuid ? formId : 0;
        }

        std::string ReplyOf(const std::string& letterId)
        {
            const auto reading = json::parse(LetterDB::GetSingleton()->GetReading(letterId), nullptr, false);
            if (!reading.is_object() || !LlmJson::GetBool(reading, "reply")) return {};
            return LlmJson::GetString(reading, "reply_text");
        }

        // The matching letters this save has (docs/API.md#finding-letters): one written in another save of the character
        // has no form here.  At most `maxCount` (<= 0: 50).
        std::vector<Letter> Find(RE::FormID actor, RE::FormID other, LetterQuery query, int maxCount)
        {
            if (actor && (query.actorUuid = SkyrimNet::UuidForFormId(actor)).empty()) return {};
            if (actor && other && (query.otherUuid = SkyrimNet::UuidForFormId(other)).empty()) return {};
            const auto limit = static_cast<std::size_t>(maxCount > 0 ? maxCount : kDefaultMaxLetters);
            std::vector<Letter> found;
            for (auto& letter : LetterDB::GetSingleton()->Query(query)) {
                if (found.size() >= limit) break;
                if (Letters::FormFor(letter.id)) found.push_back(std::move(letter));
            }
            return found;
        }

        json ToJson(const Letter& letter)
        {
            auto package = json::array();
            for (const auto& item : Package::Contents(letter.id)) package.push_back({ { "form", item.form }, { "count", item.count } });
            return { { "id", letter.id },
                     { "author", FormOf(letter.authorUuid) },
                     { "recipient", FormOf(letter.recipientUuid) },
                     { "sender_name", Letters::SenderOf(letter).name },
                     { "text", letter.body },
                     { "written_at", letter.writtenAt },
                     { "in_reply_to", letter.inReplyTo },
                     { "tag", letter.tag },
                     { "package", package } };
        }

        // `count` of `form` with the letter, until a courier has it (docs/API.md#packages).
        bool AddItems(const std::string& letterId, RE::TESForm* form, std::int32_t count)
        {
            const auto fail = [&](std::string_view why) {
                SKSE::log::warn("[Api] Nothing added to letter {}'s package: {}", letterId, why);
                return false;
            };
            // A leveled list isn't rolled by AddObjectToContainer: refused, as anything else that isn't one item.
            const bool item = form && form->IsInventoryObject() && !form->Is(RE::FormType::LeveledItem);
            auto* object = item ? form->As<RE::TESBoundObject>() : nullptr;
            if (!object) return fail(std::format("0x{:X} isn't an item", form ? form->GetFormID() : 0));
            if (count <= 0) return fail(std::format("a count of {}", count));
            if (!Transit::NotYetCarried(letterId) && !DmLetter::IsBeingWritten(letterId)) {
                return fail("a courier has it already, it was delivered, or it's unknown");
            }
            return Package::Add(letterId, object, count) || fail("its package has left the store");
        }

        // ---- The C API ----

        bool SendLetter(const PL_Send* send, char* idOut)
        {
            if (!send || !idOut || send->size < sizeof(PL_Send)) return false;
            idOut[0] = '\0';
            if (send->writer && !RE::TESForm::LookupByID<RE::Actor>(send->writer)) {
                SKSE::log::warn("[Api] A letter from the C API wasn't sent: writer 0x{:X} isn't an actor (a base NPC?)", send->writer);
                return false;
            }
            DmLetter::Request request{ .writer = send->writer ? RE::TESForm::LookupByID<RE::Actor>(send->writer) : nullptr,
                                       .signature = send->signature ? send->signature : "",
                                       .recipient = send->recipient ? RE::TESForm::LookupByID<RE::Actor>(send->recipient) : nullptr,
                                       .text = send->text ? send->text : "",
                                       .transform = send->transform,
                                       .forgery = send->forgery,
                                       .hours = send->hours,
                                       .tag = send->tag ? send->tag : "" };
            const auto id = DmLetter::Send(request);
            if (id.empty()) return false;
            std::snprintf(idOut, PL_ID_SIZE, "%s", id.c_str());
            return true;
        }

        bool GetLetter(const char* letterId, PL_Letter* out)
        {
            if (!letterId || !out || out->size < sizeof(PL_Letter)) return false;
            const auto letter = LetterDB::GetSingleton()->Get(letterId);
            if (!letter) return false;
            g_outSender = Letters::SenderOf(*letter).name;
            g_outText = letter->body;
            g_outReply = ReplyOf(letter->id);
            *out = PL_Letter{ .size = sizeof(PL_Letter),
                              .author = FormOf(letter->authorUuid),
                              .recipient = FormOf(letter->recipientUuid),
                              .senderName = g_outSender.c_str(),
                              .text = g_outText.c_str(),
                              .reply = g_outReply.c_str() };
            return true;
        }

        std::uint64_t AddListener(PL_EventCallback callback, void* user)
        {
            if (!callback) return 0;
            std::lock_guard lock{ g_mutex };
            g_listeners.push_back({ .id = ++g_lastListener, .callback = callback, .user = user });
            return g_lastListener;
        }

        void RemoveListener(std::uint64_t listener)
        {
            std::lock_guard lock{ g_mutex };
            std::erase_if(g_listeners, [listener](const Listener& l) { return l.id == listener; });
        }

        bool RegisterActionC(const PL_Action* a)
        {
            // The count first: a wrong one must be refused, not read past the caller's array.
            if (!a || a->size < sizeof(PL_Action) || a->count < 0 || a->count > static_cast<std::int32_t>(kMaxParams) ||
                (a->count > 0 && !a->params)) {
                SKSE::log::warn("[Api] An action from the C API refused: no action, an old PL_Action, or a parameter count "
                                "outside 0-{}", kMaxParams);
                return false;
            }
            const auto text = [](const char* s) { return std::string{ s ? s : "" }; };
            Action action{ .name = text(a->name),
                           .displayName = text(a->displayName),
                           .displayDescription = text(a->displayDescription),
                           .description = text(a->description),
                           .letters = a->letters,
                           .callback = a->callback,
                           .user = a->user };
            for (std::int32_t i = 0; i < a->count; ++i) action.params.push_back({ text(a->params[i].name), text(a->params[i].description) });
            return Register(std::move(action));
        }

        void UnregisterActionC(const char* name)
        {
            if (name) Unregister(name);
        }

        bool GetReplyIdC(const char* letterId, char* idOut)
        {
            if (!letterId || !idOut) return false;
            idOut[0] = '\0';
            const auto id = ReplyIdTo(letterId);
            if (id.empty()) return false;
            std::snprintf(idOut, PL_ID_SIZE, "%s", id.c_str());
            return true;
        }

        bool AddToPackageC(const char* letterId, std::uint32_t form, std::int32_t count)
        {
            return letterId && AddItems(letterId, RE::TESForm::LookupByID(form), count);
        }

        std::int32_t GetPackageC(const char* letterId, PL_Item* out, std::int32_t max)
        {
            if (!letterId) return 0;
            const auto items = Package::Contents(letterId);
            for (std::int32_t i = 0; out && i < max && i < static_cast<std::int32_t>(items.size()); ++i) {
                out[i] = PL_Item{ .form = items[i].form, .count = items[i].count };
            }
            return static_cast<std::int32_t>(items.size());
        }

        const char* QueryLettersC(std::uint32_t actor, const char* filterJson)
        {
            g_outQuery = "[]";
            try {
                const auto filter = json::parse(filterJson && *filterJson ? filterJson : "{}");
                if (!filter.is_object()) throw std::invalid_argument("not an object");
                LetterQuery query;
                const auto role = filter.value("role", std::string{ "either" });
                query.role = role == "writer" ? LetterQuery::Role::kWriter : role == "recipient" ? LetterQuery::Role::kRecipient
                                                                                                   : LetterQuery::Role::kEither;
                query.includeTags = filter.value("includeTags", std::vector<std::string>{});
                query.excludeTags = filter.value("excludeTags", std::vector<std::string>{});
                if (filter.contains("writtenAfter")) query.writtenAfter = filter["writtenAfter"].get<double>();
                if (filter.contains("writtenBefore")) query.writtenBefore = filter["writtenBefore"].get<double>();
                query.newestFirst = filter.value("orderBy", std::string{ "WrittenDesc" }) != "WrittenAsc";
                auto letters = json::array();
                for (const auto& letter : Find(actor, filter.value("other", 0u), query, filter.value("maxCount", kDefaultMaxLetters))) {
                    letters.push_back(ToJson(letter));
                }
                g_outQuery = letters.dump(-1, ' ', false, json::error_handler_t::replace);
            } catch (const std::exception& e) {
                SKSE::log::warn("[Api] QueryLetters: a bad filter ({}): {}", e.what(), filterJson ? filterJson : "");
            }
            return g_outQuery.c_str();
        }

        constexpr PL_API kAPI{
            .size = sizeof(PL_API),
            .version = PL_API_VERSION,
            .SendLetter = SendLetter,
            .GetLetter = GetLetter,
            .AddListener = AddListener,
            .RemoveListener = RemoveListener,
            .RegisterAction = RegisterActionC,
            .UnregisterAction = UnregisterActionC,
            .GetReplyId = GetReplyIdC,
            .AddToPackage = AddToPackageC,
            .GetPackage = GetPackageC,
            .QueryLetters = QueryLettersC,
        };

        // ---- Papyrus (script PhysicalLetters, docs/API.md#papyrus) ----

        std::int32_t GetApiVersion(RE::StaticFunctionTag*) { return PL_API_VERSION; }

        RE::BSFixedString SendLetterP(RE::StaticFunctionTag*, RE::Actor* from, RE::BSFixedString fromName, RE::Actor* to,
                                      RE::BSFixedString text, bool transform, bool forgery, std::int32_t hours, RE::BSFixedString tag)
        {
            return DmLetter::Send({ .writer = from,
                                    .signature = fromName.c_str(),
                                    .recipient = to,
                                    .text = text.c_str(),
                                    .transform = transform,
                                    .forgery = forgery,
                                    .hours = hours,
                                    .tag = tag.c_str() })
                .c_str();
        }

        RE::Actor* GetAuthor(RE::StaticFunctionTag*, RE::BSFixedString id)
        {
            const auto letter = LetterDB::GetSingleton()->Get(id.c_str());
            return letter ? RE::TESForm::LookupByID<RE::Actor>(FormOf(letter->authorUuid)) : nullptr;
        }

        RE::Actor* GetRecipient(RE::StaticFunctionTag*, RE::BSFixedString id)
        {
            const auto letter = LetterDB::GetSingleton()->Get(id.c_str());
            return letter ? RE::TESForm::LookupByID<RE::Actor>(FormOf(letter->recipientUuid)) : nullptr;
        }

        RE::BSFixedString GetSenderName(RE::StaticFunctionTag*, RE::BSFixedString id)
        {
            const auto letter = LetterDB::GetSingleton()->Get(id.c_str());
            return letter ? Letters::SenderOf(*letter).name.c_str() : "";
        }

        RE::BSFixedString GetText(RE::StaticFunctionTag*, RE::BSFixedString id)
        {
            const auto letter = LetterDB::GetSingleton()->Get(id.c_str());
            return letter ? letter->body.c_str() : "";
        }

        RE::BSFixedString GetReplyText(RE::StaticFunctionTag*, RE::BSFixedString id)
        {
            return ReplyOf(id.c_str()).c_str();
        }

        bool RegisterActionP(RE::StaticFunctionTag*, RE::BSFixedString name, RE::BSFixedString displayName,
                             RE::BSFixedString displayDescription, RE::BSFixedString description,
                             std::vector<RE::BSFixedString> paramNames, std::vector<RE::BSFixedString> paramDescriptions,
                             std::int32_t letters)
        {
            Action action{ .name = name.c_str(),
                           .displayName = displayName.c_str(),
                           .displayDescription = displayDescription.c_str(),
                           .description = description.c_str(),
                           .letters = letters };
            for (std::size_t i = 0; i < paramNames.size(); ++i) {
                action.params.push_back({ paramNames[i].c_str(), i < paramDescriptions.size() ? paramDescriptions[i].c_str() : "" });
            }
            return Register(std::move(action));
        }

        void UnregisterActionP(RE::StaticFunctionTag*, RE::BSFixedString name) { Unregister(name.c_str()); }

        std::vector<RE::BSFixedString> FindLetters(RE::StaticFunctionTag*, RE::Actor* who, std::int32_t role, RE::Actor* other,
                                                   RE::BSFixedString tag, std::int32_t maxCount)
        {
            LetterQuery query{ .role = role == 1 ? LetterQuery::Role::kWriter : role == 2 ? LetterQuery::Role::kRecipient
                                                                                           : LetterQuery::Role::kEither };
            if (tag.c_str() && *tag.c_str()) query.includeTags.push_back(tag.c_str());
            std::vector<RE::BSFixedString> ids;
            for (const auto& letter : Find(who ? who->GetFormID() : 0, other ? other->GetFormID() : 0, query, maxCount)) {
                ids.emplace_back(letter.id.c_str());
            }
            return ids;
        }

        RE::BSFixedString GetTag(RE::StaticFunctionTag*, RE::BSFixedString id)
        {
            const auto letter = LetterDB::GetSingleton()->Get(id.c_str());
            return letter ? letter->tag.c_str() : "";
        }

        float GetWrittenAt(RE::StaticFunctionTag*, RE::BSFixedString id)
        {
            const auto letter = LetterDB::GetSingleton()->Get(id.c_str());
            return letter ? static_cast<float>(letter->writtenAt) : 0.0f;
        }

        RE::BSFixedString GetInReplyTo(RE::StaticFunctionTag*, RE::BSFixedString id)
        {
            const auto letter = LetterDB::GetSingleton()->Get(id.c_str());
            return letter ? letter->inReplyTo.c_str() : "";
        }

        RE::BSFixedString GetReplyId(RE::StaticFunctionTag*, RE::BSFixedString id)
        {
            return ReplyIdTo(id.c_str()).c_str();
        }

        bool AddToPackageP(RE::StaticFunctionTag*, RE::BSFixedString id, RE::TESForm* item, std::int32_t count)
        {
            return AddItems(id.c_str(), item, count);
        }

        std::int32_t GetPackageCount(RE::StaticFunctionTag*, RE::BSFixedString id)
        {
            return static_cast<std::int32_t>(Package::Contents(id.c_str()).size());
        }

        RE::TESForm* GetPackageItem(RE::StaticFunctionTag*, RE::BSFixedString id, std::int32_t i)
        {
            const auto items = Package::Contents(id.c_str());
            return i >= 0 && i < static_cast<std::int32_t>(items.size()) ? RE::TESForm::LookupByID(items[i].form) : nullptr;
        }

        std::int32_t GetPackageItemCount(RE::StaticFunctionTag*, RE::BSFixedString id, std::int32_t i)
        {
            const auto items = Package::Contents(id.c_str());
            return i >= 0 && i < static_cast<std::int32_t>(items.size()) ? items[i].count : 0;
        }

        // ---- The MCM's list of actions (PhysicalLetters_MCM) ----

        std::int32_t GetActionCount(RE::StaticFunctionTag*)
        {
            std::lock_guard lock{ g_mutex };
            return static_cast<std::int32_t>(g_actions.size());
        }

        RE::BSFixedString GetActionName(RE::StaticFunctionTag*, std::int32_t i)
        {
            std::lock_guard lock{ g_mutex };
            return i >= 0 && i < static_cast<std::int32_t>(g_actions.size()) ? g_actions[i].name.c_str() : "";
        }

        RE::BSFixedString GetActionDisplayName(RE::StaticFunctionTag*, std::int32_t i)
        {
            std::lock_guard lock{ g_mutex };
            return i >= 0 && i < static_cast<std::int32_t>(g_actions.size()) ? g_actions[i].displayName.c_str() : "";
        }

        RE::BSFixedString GetActionDisplayDescription(RE::StaticFunctionTag*, std::int32_t i)
        {
            std::lock_guard lock{ g_mutex };
            return i >= 0 && i < static_cast<std::int32_t>(g_actions.size()) ? g_actions[i].displayDescription.c_str() : "";
        }

        bool IsActionEnabled(RE::StaticFunctionTag*, RE::BSFixedString name) { return Config::GetSingleton()->ActionEnabled(name.c_str()); }

        void SetActionEnabled(RE::StaticFunctionTag*, RE::BSFixedString name, bool enabled)
        {
            if (!ValidName(name.c_str())) return;  // any script can call it: nothing but a name goes into the INI
            auto* config = Config::GetSingleton();
            config->SetActionEnabled(name.c_str(), enabled);
            config->Save();
            SKSE::log::info("[Api] Action {} switched {}", name.c_str(), enabled ? "on" : "off");
        }

        RE::BSFixedString GetActionValue(RE::StaticFunctionTag*, RE::BSFixedString letterId, RE::BSFixedString param)
        {
            std::lock_guard lock{ g_mutex };
            const auto it = g_values.find(letterId.c_str());
            if (it == g_values.end()) return "";
            for (const auto& [name, value] : it->second) {
                if (_stricmp(name.c_str(), param.c_str()) == 0) return value.c_str();
            }
            return "";
        }
    }

    void Choose(const std::string& letterId, RE::FormID reader, const std::string& name,
                const std::vector<std::pair<std::string, std::string>>& values);

    void Delivered(const std::string& letterId, RE::FormID recipient)
    {
        Post("PhysicalLetters_Delivered", PL_EVENT_DELIVERED, letterId, recipient, false);
    }

    void Read(const std::string& letterId, RE::FormID reader, bool replied)
    {
        Post("PhysicalLetters_Read", PL_EVENT_READ, letterId, reader, replied);
    }

    void Failed(const std::string& letterId)
    {
        Post("PhysicalLetters_Failed", PL_EVENT_FAILED, letterId, 0, false);
    }

    json ActionsForPrompt(bool fromPlayer)
    {
        std::lock_guard lock{ g_mutex };
        auto actions = json::array();
        for (const auto& action : g_actions) {
            if (!Offered(action, fromPlayer)) continue;
            auto params = json::array();
            for (const auto& param : action.params) params.push_back({ { "name", param.name }, { "description", param.description } });
            actions.push_back({ { "name", action.name }, { "description", action.description }, { "params", params } });
        }
        return actions;
    }

    void ActionChosen(const std::string& letterId, RE::FormID reader, const std::string& name,
                      const std::vector<std::pair<std::string, std::string>>& values)
    {
        // A task of its own, after the Read event's (tasks run in order): a callback may call the API.
        SKSE::GetTaskInterface()->AddTask([=]() {
            try {
                Choose(letterId, reader, name, values);
            } catch (const std::exception& e) {
                SKSE::log::error("[Api] Letter {}: action '{}' failed: {}", letterId, name, e.what());
            }
        });
    }

    void Choose(const std::string& letterId, RE::FormID reader, const std::string& name,
                const std::vector<std::pair<std::string, std::string>>& values)
    {
        Action action;
        {
            std::lock_guard lock{ g_mutex };
            const auto it = std::ranges::find_if(g_actions, [&](const Action& a) { return _stricmp(a.name.c_str(), name.c_str()) == 0; });
            if (it == g_actions.end()) {
                SKSE::log::info("[Api] Letter {}: the reader chose '{}', which no mod registers now: dropped", letterId, name);
                return;
            }
            // The model may name one it wasn't offered.
            const auto letter = LetterDB::GetSingleton()->Get(letterId);
            if (!letter || !Offered(*it, !NpcToNpc::IsNpcLetter(*letter))) {
                SKSE::log::info("[Api] Letter {}: the reader chose '{}', which wasn't offered for it: dropped", letterId, name);
                return;
            }
            action = *it;
            g_values[letterId] = values;
        }
        // The values in the registered order, "" for one the answer left out.
        std::vector<std::string> ordered;
        for (const auto& param : action.params) {
            const auto it = std::ranges::find_if(values, [&](const auto& v) { return _stricmp(v.first.c_str(), param.name.c_str()) == 0; });
            ordered.push_back(it != values.end() ? it->second : std::string{});
        }
        std::string shown;
        for (std::size_t i = 0; i < ordered.size(); ++i) shown += std::format("{}{}={}", i ? ", " : "", action.params[i].name, ordered[i]);
        SKSE::log::info("[Api] Letter {}: the reader (0x{:X}) chose '{}' ({})", letterId, reader, action.name, shown);
        SendModEvent(("PhysicalLetters_Action_" + action.name).c_str(), letterId, 0.0f, reader);
        if (action.callback) {
            std::vector<const char*> pointers;
            for (const auto& value : ordered) pointers.push_back(value.c_str());
            try {
                action.callback(action.user, letterId.c_str(), reader, pointers.data(), static_cast<std::int32_t>(pointers.size()));
            } catch (...) {
                SKSE::log::error("[Api] Action '{}'s callback threw (letter {})", action.name, letterId);
            }
        }
    }

    bool RegisterFunctions(RE::BSScript::IVirtualMachine* vm)
    {
        vm->RegisterFunction("GetApiVersion", kScript, GetApiVersion);
        vm->RegisterFunction("SendLetter", kScript, SendLetterP);
        vm->RegisterFunction("GetAuthor", kScript, GetAuthor);
        vm->RegisterFunction("GetRecipient", kScript, GetRecipient);
        vm->RegisterFunction("GetSenderName", kScript, GetSenderName);
        vm->RegisterFunction("GetText", kScript, GetText);
        vm->RegisterFunction("GetReplyText", kScript, GetReplyText);
        vm->RegisterFunction("RegisterAction", kScript, RegisterActionP);
        vm->RegisterFunction("UnregisterAction", kScript, UnregisterActionP);
        vm->RegisterFunction("GetActionValue", kScript, GetActionValue);
        vm->RegisterFunction("GetReplyId", kScript, GetReplyId);
        vm->RegisterFunction("FindLetters", kScript, FindLetters);
        vm->RegisterFunction("GetTag", kScript, GetTag);
        vm->RegisterFunction("GetWrittenAt", kScript, GetWrittenAt);
        vm->RegisterFunction("GetInReplyTo", kScript, GetInReplyTo);
        vm->RegisterFunction("AddToPackage", kScript, AddToPackageP);
        vm->RegisterFunction("GetPackageCount", kScript, GetPackageCount);
        vm->RegisterFunction("GetPackageItem", kScript, GetPackageItem);
        vm->RegisterFunction("GetPackageItemCount", kScript, GetPackageItemCount);
        constexpr auto kMcm = "PhysicalLetters_MCM";
        vm->RegisterFunction("GetActionCount", kMcm, GetActionCount);
        vm->RegisterFunction("GetActionName", kMcm, GetActionName);
        vm->RegisterFunction("GetActionDisplayName", kMcm, GetActionDisplayName);
        vm->RegisterFunction("GetActionDisplayDescription", kMcm, GetActionDisplayDescription);
        vm->RegisterFunction("IsActionEnabled", kMcm, IsActionEnabled);
        vm->RegisterFunction("SetActionEnabled", kMcm, SetActionEnabled);
        return true;
    }

} // namespace PhysicalLetters::Api

extern "C" __declspec(dllexport) const PL_API* PL_GetAPI(std::uint32_t version)
{
    if (version > PL_API_VERSION) {
        SKSE::log::warn("[Api] A plugin wants API version {}; this is {}", version, PL_API_VERSION);
        return nullptr;
    }
    return &PhysicalLetters::Api::kAPI;
}
