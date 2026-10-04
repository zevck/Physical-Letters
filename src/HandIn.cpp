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

#include "HandIn.h"
#include "Config.h"
#include "LetterDB.h"
#include "Letters.h"
#include "Postage.h"
#include "Session.h"
#include "SkyrimNet.h"
#include "Transit.h"

namespace PhysicalLetters::HandIn {

    namespace {
        constexpr auto kScript = "PhysicalLetters_HandInQuest";
        constexpr std::string_view kPlugin = "Physical Letters.esp";
        constexpr RE::FormID kPlayer = 0x14;
        constexpr RE::FormID kDialogueGlobal = 0x8B4;  // PhysicalLettersHandInDialogue, in the topic's conditions
        // Further than this from the player, a letter handed over isn't read on the spot.
        constexpr float kNearDistance = 2048.0f;

        // The NPC whose hand-in gift menu is open (the topic's TIF), 0 when none: only a letter given
        // there is handed over.  Game thread.
        RE::FormID g_handInTo = 0;

        void BeginHandIn(RE::StaticFunctionTag*, RE::Actor* recipient)
        {
            g_handInTo = recipient ? recipient->GetFormID() : 0;
            SKSE::log::info("[HandIn] The player offers a letter to {}", recipient ? recipient->GetName() : "nobody");
        }

        // What the player was doing when the letter left their inventory.
        struct Transfer {
            bool gift = false;    // a gift menu: our postage or hand-in topic's, or anyone else's
            bool barter = false;  // selling it
        };

        // Game thread.  The letter is in `holder`'s inventory now (docs/HAND_IN.md#what-counts).
        void Transferred(RE::FormID bookId, RE::FormID holderId, Transfer how)
        {
            auto* book = RE::TESForm::LookupByID<RE::TESObjectBOOK>(bookId);
            auto* holder = RE::TESForm::LookupByID<RE::Actor>(holderId);
            if (!book || !holder || holder->IsDead() || how.barter || !how.gift) return;
            const auto letter = Session::IsReady() ? LetterDB::GetSingleton()->Get(Letters::IdFor(bookId)) : std::nullopt;
            // The hand-in topic's menu, or the postage menu of the letter's own recipient.
            const bool post = Postage::TakesPost(holder);
            const bool handIn = holderId == g_handInTo || (post && letter && IsRecipient(holder, *letter));
            const bool own = !letter || letter->authorUuid == SkyrimNet::UuidForFormId(kPlayer);
            if (!handIn && own && post) {
                Postage::Post(book, holder, letter);
                return;
            }
            if (!letter || !handIn) return;  // anything else is just a gift
            Transit::HandIn(*letter, holder);
            // One letter per hand-over, as postage: the menu closes.
            g_handInTo = 0;
            if (auto* queue = RE::UIMessageQueue::GetSingleton()) {
                queue->AddMessage(RE::GiftMenu::MENU_NAME, RE::UI_MESSAGE_TYPE::kHide, nullptr);
            }
        }

        class TransferSink : public RE::BSTEventSink<RE::TESContainerChangedEvent> {
        public:
            static TransferSink* GetSingleton()
            {
                static TransferSink singleton;
                return &singleton;
            }

            RE::BSEventNotifyControl ProcessEvent(const RE::TESContainerChangedEvent* a_event,
                                                  RE::BSTEventSource<RE::TESContainerChangedEvent>*) override
            {
                if (!a_event || a_event->oldContainer != kPlayer || a_event->newContainer == 0 ||
                    a_event->newContainer == kPlayer || Letters::IdFor(a_event->baseObj).empty()) {
                    return RE::BSEventNotifyControl::kContinue;
                }
                // The menus as they are now: by the task, the menu may have closed.
                auto* ui = RE::UI::GetSingleton();
                const auto open = [ui](std::string_view menu) { return ui && ui->IsMenuOpen(menu); };
                const Transfer how{ .gift = open(RE::GiftMenu::MENU_NAME), .barter = open(RE::BarterMenu::MENU_NAME) };
                SKSE::GetTaskInterface()->AddTask([bookId = a_event->baseObj, holderId = a_event->newContainer, how]() {
                    // An exception must not cross into the engine.
                    try {
                        Transferred(bookId, holderId, how);
                    } catch (const std::exception& e) {
                        SKSE::log::error("[HandIn] Handing over letter 0x{:X} failed: {}", bookId, e.what());
                    }
                });
                return RE::BSEventNotifyControl::kContinue;
            }
        };

        // A gift menu closed without a letter given: the hand-in is over.  As a task, after the
        // transfer tasks the menu queued (they run in order).
        class MenuSink : public RE::BSTEventSink<RE::MenuOpenCloseEvent> {
        public:
            static MenuSink* GetSingleton()
            {
                static MenuSink singleton;
                return &singleton;
            }

            RE::BSEventNotifyControl ProcessEvent(const RE::MenuOpenCloseEvent* a_event,
                                                  RE::BSTEventSource<RE::MenuOpenCloseEvent>*) override
            {
                if (a_event && !a_event->opening && a_event->menuName == RE::GiftMenu::MENU_NAME) {
                    SKSE::GetTaskInterface()->AddTask([]() { g_handInTo = 0; });
                }
                return RE::BSEventNotifyControl::kContinue;
            }
        };
    }

    bool IsRecipient(RE::Actor* holder, const Letter& letter)
    {
        return holder && !letter.recipientUuid.empty() && SkyrimNet::UuidForFormId(holder->GetFormID()) == letter.recipientUuid;
    }

    void Register()
    {
        if (auto* ui = RE::UI::GetSingleton()) ui->AddEventSink<RE::MenuOpenCloseEvent>(MenuSink::GetSingleton());
        if (auto* events = RE::ScriptEventSourceHolder::GetSingleton()) {
            events->AddEventSink<RE::TESContainerChangedEvent>(TransferSink::GetSingleton());
            SKSE::log::info("[HandIn] Watching for letters handed over");
        }
    }

    bool NearPlayer(RE::Actor* actor)
    {
        auto* player = RE::PlayerCharacter::GetSingleton();
        const auto* cell = actor ? actor->GetParentCell() : nullptr;
        // Coordinates compare only within one interior cell or one worldspace.
        const bool together = cell && (cell->IsInteriorCell() ? cell == player->GetParentCell()
                                                              : actor->GetWorldspace() == player->GetWorldspace());
        return together && !actor->IsDead() && actor->Is3DLoaded() &&
               actor->GetPosition().GetDistance(player->GetPosition()) <= kNearDistance && SkyrimNet::CanRegisterEvents();
    }

    void ReadThere(const Letter& letter, RE::Actor* reader)
    {
        // A direct narration perceived by the reader alone: they read it and react aloud; nobody
        // else learns the text (docs/HAND_IN.md#reading-it-there).
        auto text = std::format("{} hands {} a letter from {} to {}. It reads:\n{}", RE::PlayerCharacter::GetSingleton()->GetName(),
                                reader->GetName(), letter.authorName, letter.recipientName, letter.body);
        if (const auto blood = Letters::BloodSentence(letter); !blood.empty()) text += "\n\n" + blood;
        const auto readerId = reader->GetFormID();
        // SkyrimNet stores the event: off the game thread, as its API allows.
        std::thread([readerId, text, letterId = letter.id, name = std::string{ reader->GetName() }]() {
            try {
                const int id = SkyrimNet::RegisterEvent("direct_narration", text, readerId, 0, { readerId });
                if (id > 0) SKSE::log::info("[HandIn] {} reads letter {} on the spot: event {}", name, letterId, id);
                else SKSE::log::error("[HandIn] SkyrimNet didn't store {}'s reading of letter {}", name, letterId);
            } catch (const std::exception& e) {
                SKSE::log::error("[HandIn] Registering {}'s reading failed: {}", name, e.what());
            }
        }).detach();
    }

    void ApplyDialogue()
    {
        auto* data = RE::TESDataHandler::GetSingleton();
        auto* global = data ? data->LookupForm<RE::TESGlobal>(kDialogueGlobal, kPlugin) : nullptr;
        if (global) global->value = Config::GetSingleton()->Get(Config::kHandInDialogue) ? 1.0f : 0.0f;
    }

    bool RegisterFunctions(RE::BSScript::IVirtualMachine* vm)
    {
        vm->RegisterFunction("BeginHandIn", kScript, BeginHandIn);
        return true;
    }

    void Revert()
    {
        g_handInTo = 0;
    }

} // namespace PhysicalLetters::HandIn
