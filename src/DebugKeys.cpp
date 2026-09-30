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

#include "DebugKeys.h"
#include "LetterDB.h"
#include "Letters.h"
#include "Session.h"
#include "SkyrimNet.h"
#include "Transit.h"

namespace PhysicalLetters::DebugKeys {

    namespace {
        constexpr std::uint32_t kCreateKey = 0x40;  // F6
        constexpr std::uint32_t kSendKey = 0x41;    // F7
        constexpr std::uint32_t kDueKey = 0x42;     // F8

        void Notify(const std::string& text)
        {
            RE::SendHUDMessage::ShowHUDMessage(text.c_str());
            SKSE::log::info("[DebugKeys] {}", text);
        }

        std::string ExampleBody(const std::string& recipient, const std::string& author)
        {
            return std::format(
                "Dear {},\n\n"
                "I hope this finds you well. The road has kept me busy since we last spoke, but I've thought of you "
                "often. Skyrim feels larger with every mile, and it's good to know there are people in it I can count "
                "on.\n\n"
                "Write back if you get the chance. I'd like to hear how you've been.\n\n"
                "Yours,\n{}",
                recipient, author);
        }

        void CreateExampleLetter()
        {
            auto* pick = RE::CrosshairPickData::GetSingleton();
            const auto target = pick ? pick->GetActiveTarget().get() : RE::NiPointer<RE::TESObjectREFR>{};
            auto* actor = target ? target->As<RE::Actor>() : nullptr;
            if (!actor || actor->IsPlayerRef()) {
                Notify("Look at an NPC to address a letter to them.");
                return;
            }
            const auto recipientUuid = SkyrimNet::UuidForFormId(actor->GetFormID());
            if (recipientUuid.empty()) {
                Notify(std::format("SkyrimNet doesn't know {} yet.", actor->GetName()));
                return;
            }
            auto* player = RE::PlayerCharacter::GetSingleton();
            auto recipientName = SkyrimNet::ActorName(recipientUuid);
            if (recipientName.empty()) recipientName = actor->GetName();

            const Letter letter{ .id = Letters::NewId(),
                                 .authorUuid = SkyrimNet::UuidForFormId(player->GetFormID()),
                                 .authorName = player->GetName(),
                                 .recipientUuid = recipientUuid,
                                 .recipientName = recipientName,
                                 .body = ExampleBody(recipientName, player->GetName()),
                                 .writtenAt = RE::Calendar::GetSingleton()->GetDaysPassed() };
            if (auto* book = Letters::Create(letter)) {
                player->AddObjectToContainer(book, nullptr, 1, nullptr);
                Notify(std::format("Example letter to {} added.", recipientName));
            }
        }

        void SendNewestLetter()
        {
            auto* player = RE::PlayerCharacter::GetSingleton();
            auto* db = LetterDB::GetSingleton();
            const auto playerUuid = SkyrimNet::UuidForFormId(player->GetFormID());
            RE::TESObjectBOOK* newestBook = nullptr;
            std::optional<Letter> newest;
            const auto inventory = player->GetInventory([](RE::TESBoundObject& item) {
                return item.GetFormType() == RE::FormType::Book && !Letters::IdFor(item.GetFormID()).empty();
            });
            for (const auto& [item, data] : inventory) {
                if (data.first <= 0) continue;
                auto letter = db->Get(Letters::IdFor(item->GetFormID()));
                // Only the player's own letters: not replies they received.
                if (letter && letter->authorUuid == playerUuid && (!newest || letter->writtenAt > newest->writtenAt)) {
                    newest = std::move(letter);
                    newestBook = item->As<RE::TESObjectBOOK>();
                }
            }
            if (!newest) {
                Notify("You carry no letter to send.");
                return;
            }
            if (const auto hours = Transit::Send(newestBook, *newest, player)) {
                Notify(std::format("Letter to {} sent; it arrives in {:.0f} hours.", newest->recipientName, *hours));
            }
        }

        // A UUID SkyrimNet never gives out: the recipient is never found.
        constexpr std::string_view kNobodyUuid = "0";
        constexpr std::string_view kNobodyName = "Nobody (test)";
    }

    void GiveUndeliverableLetter()
    {
        auto* player = RE::PlayerCharacter::GetSingleton();
        auto* db = LetterDB::GetSingleton();
        const auto inventory = player->GetInventory([](RE::TESBoundObject& item) {
            return item.GetFormType() == RE::FormType::Book && !Letters::IdFor(item.GetFormID()).empty();
        });
        for (const auto& [item, data] : inventory) {
            const auto letter = db->Get(Letters::IdFor(item->GetFormID()));
            if (data.first > 0 && letter && letter->recipientUuid == kNobodyUuid) return;
        }
        const Letter letter{ .id = Letters::NewId(),
                             .authorUuid = SkyrimNet::UuidForFormId(player->GetFormID()),
                             .authorName = player->GetName(),
                             .recipientUuid = std::string{ kNobodyUuid },
                             .recipientName = std::string{ kNobodyName },
                             .body = ExampleBody(std::string{ kNobodyName }, player->GetName()),
                             .writtenAt = RE::Calendar::GetSingleton()->GetDaysPassed() };
        if (auto* book = Letters::Create(letter)) {
            player->AddObjectToContainer(book, nullptr, 1, nullptr);
            SKSE::log::info("[DebugKeys] Test letter to {} (never found) added", kNobodyName);
        }
    }

    namespace {
        class InputSink : public RE::BSTEventSink<RE::InputEvent*> {
        public:
            static InputSink* GetSingleton()
            {
                static InputSink singleton;
                return &singleton;
            }

            RE::BSEventNotifyControl ProcessEvent(RE::InputEvent* const* a_event, RE::BSTEventSource<RE::InputEvent*>*) override
            {
                auto* ui = RE::UI::GetSingleton();
                if (!a_event || !ui || ui->GameIsPaused() || ui->IsMenuOpen(RE::Console::MENU_NAME)) {
                    return RE::BSEventNotifyControl::kContinue;
                }
                for (auto* event = *a_event; event; event = event->next) {
                    const auto* button = event->AsButtonEvent();
                    if (!button || button->GetDevice() != RE::INPUT_DEVICE::kKeyboard || !button->IsDown()) continue;
                    const auto key = button->GetIDCode();
                    if (key != kCreateKey && key != kSendKey && key != kDueKey) continue;
                    SKSE::GetTaskInterface()->AddTask([key]() {
                        // An exception must not cross into the engine.
                        try {
                            if (!Session::IsReady()) {
                                Notify("Letters aren't ready yet (waiting for SkyrimNet).");
                            } else if (key == kCreateKey) {
                                CreateExampleLetter();
                            } else if (key == kSendKey) {
                                SendNewestLetter();
                            } else {
                                Transit::MakeAllDue();
                                Notify("Letters in transit are due now.");
                            }
                        } catch (const std::exception& e) {
                            SKSE::log::error("[DebugKeys] Key 0x{:X} failed: {}", key, e.what());
                        }
                    });
                }
                return RE::BSEventNotifyControl::kContinue;
            }
        };
    }

    void Register()
    {
        if (auto* input = RE::BSInputDeviceManager::GetSingleton()) {
            input->AddEventSink(InputSink::GetSingleton());
            SKSE::log::info("[DebugKeys] F6 example letter, F7 send, F8 make due");
        }
    }

} // namespace PhysicalLetters::DebugKeys
