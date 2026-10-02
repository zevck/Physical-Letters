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
#include "Config.h"
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

        void GiveBack(RE::Actor* a_holder, RE::TESObjectBOOK* a_book)
        {
            a_holder->RemoveItem(a_book, 1, RE::ITEM_REMOVE_REASON::kStoreInContainer, nullptr,
                                 RE::PlayerCharacter::GetSingleton());
        }
    }

    bool TakesPost(RE::Actor* a_holder)
    {
        auto* innkeepers = RE::TESForm::LookupByID<RE::TESFaction>(kInnkeeperFaction);
        const auto* base = a_holder ? a_holder->GetActorBase() : nullptr;
        return a_holder && ((innkeepers && a_holder->IsInFaction(innkeepers)) || (base && base->GetFormID() == kCourier));
    }

    void Post(RE::TESObjectBOOK* a_book, RE::Actor* a_holder, const std::optional<Letter>& a_letter)
    {
        auto* player = RE::PlayerCharacter::GetSingleton();
        if (!a_letter) {
            SKSE::log::warn("[Postage] Letter 0x{:X} can't be sent now: given back", a_book->GetFormID());
            GiveBack(a_holder, a_book);
            return;
        }
        const auto& letter = *a_letter;
        // Only letters the player wrote are posted (the menu shows only those; this covers a
        // stale keyword).  Anything else stays a gift.
        if (letter.authorUuid != SkyrimNet::UuidForFormId(kPlayer)) return;

        auto* data = RE::TESDataHandler::GetSingleton();
        auto* postageGlobal = data->LookupForm<RE::TESGlobal>(kPostageGlobal, kPlugin);
        auto* gold = RE::TESForm::LookupByID<RE::TESBoundObject>(kGold);
        const auto postage = postageGlobal ? static_cast<std::int32_t>(postageGlobal->value) : 0;
        // The dialogue only offers this with the postage in hand; a mod could have taken it since.
        if (!gold || player->GetItemCount(gold) < postage) {
            SKSE::log::info("[Postage] The player can't pay {} gold: letter {} given back", postage, letter.id);
            GiveBack(a_holder, a_book);
            return;
        }

        if (!Transit::Send(a_book, letter, a_holder)) {
            GiveBack(a_holder, a_book);
            return;
        }
        player->RemoveItem(gold, postage, RE::ITEM_REMOVE_REASON::kRemove, nullptr, a_holder);
        RE::SendHUDMessage::ShowInventoryChangeMessage(gold, postage, false, true);
        SKSE::log::info("[Postage] {} took letter {} to {} for {} gold", a_holder->GetName(), letter.id, letter.recipientName,
                        postage);

        // One letter per postage: done.
        if (auto* queue = RE::UIMessageQueue::GetSingleton()) {
            queue->AddMessage(RE::GiftMenu::MENU_NAME, RE::UI_MESSAGE_TYPE::kHide, nullptr);
        }
    }

    void ApplyPrice()
    {
        auto* data = RE::TESDataHandler::GetSingleton();
        auto* global = data ? data->LookupForm<RE::TESGlobal>(kPostageGlobal, kPlugin) : nullptr;
        if (!global) return;
        global->value = static_cast<float>(Config::GetSingleton()->Get(Config::kPostage));
    }

} // namespace PhysicalLetters::Postage
