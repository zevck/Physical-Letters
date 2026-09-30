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

#include "Postage.h"
#include "LetterDB.h"
#include "Letters.h"
#include "Session.h"
#include "SkyrimNet.h"
#include "Transit.h"

namespace PhysicalLetters::Postage {

    namespace {
        constexpr RE::FormID kPlayer = 0x14;
        constexpr RE::FormID kGold = 0x00000F;
        constexpr RE::FormID kInnkeeperFaction = 0x05091B;  // JobInnkeeperFaction
        constexpr RE::FormID kCourier = 0x039F83;           // WICourierNPC
        constexpr RE::FormID kPostageGlobal = 0x000802;     // PhysicalLettersPostage
        constexpr std::string_view kPlugin = "Physical Letters.esp";

        // Who the postage topic's conditions allow; any other gift menu is just a gift.
        bool TakesPost(RE::Actor* a_holder)
        {
            auto* innkeepers = RE::TESForm::LookupByID<RE::TESFaction>(kInnkeeperFaction);
            const auto* base = a_holder->GetActorBase();
            return (innkeepers && a_holder->IsInFaction(innkeepers)) || (base && base->GetFormID() == kCourier);
        }

        void GiveBack(RE::Actor* a_holder, RE::TESObjectBOOK* a_book)
        {
            a_holder->RemoveItem(a_book, 1, RE::ITEM_REMOVE_REASON::kStoreInContainer, nullptr,
                                 RE::PlayerCharacter::GetSingleton());
        }

        // Game thread.  The letter is in the holder's inventory now: send it from there, or
        // give it back if it can't be sent.
        void HandOver(RE::FormID a_bookId, RE::FormID a_holderId)
        {
            auto* book = RE::TESForm::LookupByID<RE::TESObjectBOOK>(a_bookId);
            auto* holder = RE::TESForm::LookupByID<RE::Actor>(a_holderId);
            auto* player = RE::PlayerCharacter::GetSingleton();
            if (!book || !holder || !TakesPost(holder)) return;

            auto letter = LetterDB::GetSingleton()->Get(Letters::IdFor(a_bookId));
            if (!Session::IsReady() || !letter) {
                SKSE::log::warn("[Postage] Letter 0x{:X} can't be sent now: given back", a_bookId);
                GiveBack(holder, book);
                return;
            }
            // Only letters the player wrote are posted (the menu shows only those; this
            // covers a stale keyword).  Anything else stays a gift.
            if (letter->authorUuid != SkyrimNet::UuidForFormId(kPlayer)) return;

            auto* data = RE::TESDataHandler::GetSingleton();
            auto* postageGlobal = data->LookupForm<RE::TESGlobal>(kPostageGlobal, kPlugin);
            auto* gold = RE::TESForm::LookupByID<RE::TESBoundObject>(kGold);
            const auto postage = postageGlobal ? static_cast<std::int32_t>(postageGlobal->value) : 0;
            // The dialogue only offers this with the postage in hand; a mod could have taken it since.
            if (!gold || player->GetItemCount(gold) < postage) {
                SKSE::log::info("[Postage] The player can't pay {} gold: letter {} given back", postage, letter->id);
                GiveBack(holder, book);
                return;
            }

            if (!Transit::Send(book, *letter, holder)) {
                GiveBack(holder, book);
                return;
            }
            player->RemoveItem(gold, postage, RE::ITEM_REMOVE_REASON::kRemove, nullptr, holder);
            RE::SendHUDMessage::ShowInventoryChangeMessage(gold, postage, false, true);
            SKSE::log::info("[Postage] {} took letter {} to {} for {} gold", holder->GetName(), letter->id,
                            letter->recipientName, postage);

            // One letter per postage: done.
            if (auto* queue = RE::UIMessageQueue::GetSingleton()) {
                queue->AddMessage(RE::GiftMenu::MENU_NAME, RE::UI_MESSAGE_TYPE::kHide, nullptr);
            }
        }

        // A letter given away while a gift menu is open; HandOver decides whether it's post.
        class GiftSink : public RE::BSTEventSink<RE::TESContainerChangedEvent> {
        public:
            static GiftSink* GetSingleton()
            {
                static GiftSink singleton;
                return &singleton;
            }

            RE::BSEventNotifyControl ProcessEvent(const RE::TESContainerChangedEvent* a_event,
                                                  RE::BSTEventSource<RE::TESContainerChangedEvent>*) override
            {
                if (!a_event || a_event->oldContainer != kPlayer || a_event->newContainer == 0 ||
                    a_event->newContainer == kPlayer || Letters::IdFor(a_event->baseObj).empty()) {
                    return RE::BSEventNotifyControl::kContinue;
                }
                auto* ui = RE::UI::GetSingleton();
                if (!ui || !ui->IsMenuOpen(RE::GiftMenu::MENU_NAME)) return RE::BSEventNotifyControl::kContinue;

                SKSE::GetTaskInterface()->AddTask([bookId = a_event->baseObj, holderId = a_event->newContainer]() {
                    // An exception must not cross into the engine.
                    try {
                        HandOver(bookId, holderId);
                    } catch (const std::exception& e) {
                        SKSE::log::error("[Postage] Handing over letter 0x{:X} failed: {}", bookId, e.what());
                    }
                });
                return RE::BSEventNotifyControl::kContinue;
            }
        };
    }

    void Register()
    {
        if (auto* events = RE::ScriptEventSourceHolder::GetSingleton()) {
            events->AddEventSink<RE::TESContainerChangedEvent>(GiftSink::GetSingleton());
            SKSE::log::info("[Postage] Watching for letters handed over in the gift menu");
        }
    }

} // namespace PhysicalLetters::Postage
