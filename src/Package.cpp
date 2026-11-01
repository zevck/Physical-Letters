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

#include "Package.h"
#include "CoSave.h"
#include "Letters.h"
#include "Parchment.h"
#include "Session.h"
#include "Strings.h"

namespace PhysicalLetters::Package {

    namespace {
        constexpr RE::FormID kPackingChest = 0x8BE;  // PhysicalLettersPackingChestRef, in WICourierCell
        constexpr RE::FormID kStore = 0x8BF;         // PhysicalLettersPackageStoreRef
        constexpr std::uint32_t kRecordVersion = 3;  // 1 and 2 were dev builds' only (2026-10-31): skipped
        constexpr RE::FormID kPackingChestBase = 0x8BC;  // PhysicalLettersPackingChest: its name is the menu's title

        struct Package {
            std::string letterId;
            RE::FormID holder = kStore;  // where its items are now; 0 delivered (kept: the reading says what came)
            Manifest items;
            bool playerPacked = false;  // the player's items (posting, the DM Letter): back to them if it's lost
        };

        // Read by Reading's worker threads (Has, Describe), written on the game thread.
        std::mutex g_mutex;
        std::vector<Package> g_packages;

        // Questions waiting their turn (one message box at a time), and the one whose chest is open.
        struct Question {
            std::string text;
            std::function<void(Manifest)> done;
            std::uint32_t generation = 0;
        };
        std::deque<Question> g_questions;
        std::optional<Question> g_packing;
        bool g_asking = false;
        // Yes was answered: the chest opens (ContainerMenu::OpenMenu) once the menus in the way have closed, within kOpenTimeout.
        bool g_openPending = false;
        bool g_opened = false;
        std::chrono::steady_clock::time_point g_packingSince;
        constexpr auto kOpenTimeout = std::chrono::seconds(5);

        RE::TESObjectREFR* Ref(RE::FormID id)
        {
            auto* data = RE::TESDataHandler::GetSingleton();
            return data ? data->LookupForm<RE::TESObjectREFR>(id, Parchment::kPlugin) : nullptr;
        }

        std::string Shown(std::int32_t count, std::string_view name)
        {
            return count > 1 ? std::format("{} {}", count, name) : std::string{ name };
        }

        // What makes this stack unique: its enchantment, tempering, custom name (the rest of the Item empty).
        Item Extras(RE::ExtraDataList* extra)
        {
            Item item;
            if (!extra) return item;
            if (const auto* e = extra->GetByType<RE::ExtraEnchantment>(); e && e->enchantment) item.enchantment = e->enchantment->GetFormID();
            if (const auto* h = extra->GetByType<RE::ExtraHealth>(); h && h->health != 1.0f) item.health = h->health;
            if (const auto* t = extra->GetByType<RE::ExtraTextDisplayData>(); t && t->displayName.c_str()) item.customName = t->displayName.c_str();
            return item;
        }

        // Where a package's items are: the store, or the courier carrying them.
        RE::TESObjectREFR* HolderRef(RE::FormID holder)
        {
            return holder == kStore ? Ref(kStore) : RE::TESForm::LookupByID<RE::TESObjectREFR>(holder);
        }

        // "300 Gold, Steel Sword".
        std::string Joined(const Manifest& manifest)
        {
            std::string shown;
            for (const auto& item : manifest) shown += (shown.empty() ? "" : ", ") + item.shownName;
            return shown;
        }

        // How the item is named, without its count: its custom name, or the base item's.
        std::string NameOf(const Item& item)
        {
            if (!item.customName.empty()) return item.customName;
            const auto* form = RE::TESForm::LookupByID(item.form);
            return form ? form->GetName() : std::string{};
        }

        Item Fingerprint(RE::TESBoundObject* object, RE::ExtraDataList* extra, std::int32_t count)
        {
            Item item = Extras(extra);
            item.form = object->GetFormID();
            item.count = count;
            item.shownName = Shown(count, NameOf(item));
            return item;
        }

        bool Matches(const Item& item, RE::ExtraDataList* extra)
        {
            const Item found = Extras(extra);
            return found.enchantment == item.enchantment && found.health == item.health && found.customName == item.customName;
        }

        // Every stack in `ref`: the item, its extra data (nullptr: the plain ones), how many.
        struct Stack {
            RE::TESBoundObject* object = nullptr;
            RE::ExtraDataList* extra = nullptr;
            std::int32_t count = 0;
        };
        std::vector<Stack> Stacks(RE::TESObjectREFR* ref)
        {
            std::vector<Stack> stacks;
            for (auto& [object, data] : ref->GetInventory()) {
                const auto& [total, entry] = data;
                if (!object || total <= 0) continue;
                std::int32_t inLists = 0;
                if (entry && entry->extraLists) {
                    for (auto* extra : *entry->extraLists) {
                        if (!extra) continue;
                        const auto count = std::min(extra->GetCount(), total - inLists);
                        if (count <= 0) continue;
                        stacks.push_back({ object, extra, count });
                        inLists += count;
                    }
                }
                if (total > inLists) stacks.push_back({ object, nullptr, total - inLists });
            }
            return stacks;
        }

        // Moves the manifest's items from `from` to `to` (nullptr: deleted): each item's own stack first, then any of the
        // same kind.  What was moved, and the shortfall logged.
        Manifest Move(const Manifest& manifest, RE::TESObjectREFR* from, RE::TESObjectREFR* to, std::string_view why)
        {
            Manifest moved;
            for (const auto& item : manifest) {
                std::int32_t left = item.count;
                for (const bool exact : { true, false }) {
                    for (const auto& stack : Stacks(from)) {
                        if (left <= 0) break;
                        if (stack.object->GetFormID() != item.form || (exact && !Matches(item, stack.extra))) continue;
                        const auto count = std::min(left, stack.count);
                        from->RemoveItem(stack.object, count, to ? RE::ITEM_REMOVE_REASON::kStoreInContainer : RE::ITEM_REMOVE_REASON::kRemove,
                                         stack.extra, to);
                        left -= count;
                    }
                }
                if (left > 0) {
                    SKSE::log::warn("[Package] {}: {} of {} '{}' (0x{:X}) missing", why, left, item.count, item.shownName, item.form);
                }
                if (item.count - left > 0) {
                    auto kept = item;
                    kept.count = item.count - left;
                    if (left > 0) kept.shownName = Shown(kept.count, NameOf(kept));  // "200 Gold", not the 300 packed
                    moved.push_back(std::move(kept));
                }
            }
            return moved;
        }

        // ---- Asking and packing ----

        void Finish(Question question, Manifest manifest)
        {
            if (question.generation != Session::Generation()) return;  // a load meanwhile
            try {
                question.done(std::move(manifest));
            } catch (const std::exception& e) {
                SKSE::log::error("[Package] Handling a package failed: {}", e.what());
            }
        }

        void Next();

        // The packing chest closed: everything in it is the package, now in the store.
        void Packed()
        {
            auto question = std::move(*g_packing);
            g_packing.reset();
            g_opened = false;
            auto* chest = Ref(kPackingChest);
            auto* store = Ref(kStore);
            Manifest manifest;
            if (chest && store) {
                for (const auto& stack : Stacks(chest)) manifest.push_back(Fingerprint(stack.object, stack.extra, stack.count));
                manifest = Move(manifest, chest, store, "Packing");
            }
            const auto shown = Joined(manifest);
            SKSE::log::info("[Package] Packed: {}", shown.empty() ? "nothing" : shown);
            g_asking = false;
            Finish(std::move(question), std::move(manifest));
            Next();
        }

        // Nothing open, the game running: in a menu the container menu doesn't open.
        bool InWorld()
        {
            auto* ui = RE::UI::GetSingleton();
            return ui && !ui->GameIsPaused() && !ui->IsMenuOpen(RE::MessageBoxMenu::MENU_NAME) && !ui->IsMenuOpen(RE::DialogueMenu::MENU_NAME);
        }

        // The menus in the way of the chest's (the book, the inventory, the postage gift menu, the conversation): closed.
        void CloseMenus()
        {
            auto* ui = RE::UI::GetSingleton();
            auto* queue = RE::UIMessageQueue::GetSingleton();
            if (!ui || !queue) return;
            for (const auto menu : { RE::BookMenu::MENU_NAME, RE::GiftMenu::MENU_NAME, RE::InventoryMenu::MENU_NAME,
                                     RE::TweenMenu::MENU_NAME, RE::DialogueMenu::MENU_NAME }) {
                if (ui->IsMenuOpen(menu)) queue->AddMessage(menu, RE::UI_MESSAGE_TYPE::kHide, nullptr);
            }
        }

        // The engine's own opener (ContainerMenu::OpenMenu): no activation, so the chest's cell needn't be loaded.
        void OpenChest()
        {
            auto* chest = Ref(kPackingChest);
            if (!g_openPending || !chest || !InWorld()) return;
            g_openPending = false;
            RE::ContainerMenu::OpenMenu(chest, RE::ContainerMenu::ContainerMode::kLoot);
            SKSE::log::info("[Package] Opening the packing chest");
        }

        // Packing begins: the menus in the way close, the chest opens (now, or once they have), within kOpenTimeout.
        void StartPacking(Question question)
        {
            g_packing = std::move(question);
            g_asking = true;
            g_openPending = true;
            g_opened = false;
            g_packingSince = std::chrono::steady_clock::now();
            CloseMenus();
            OpenChest();
        }

        class Answer : public RE::IMessageBoxCallback {
        public:
            explicit Answer(Question question) : question_(std::move(question)) {}

            void Run(std::uint8_t a_button) override
            {
                auto* chest = Ref(kPackingChest);
                if (a_button != 0 || !chest || question_.generation != Session::Generation()) {
                    if (a_button == 0 && !chest) SKSE::log::error("[Package] The packing chest isn't in Physical Letters.esp");
                    g_asking = false;
                    Finish(std::move(question_), {});
                    Next();
                    return;
                }
                StartPacking(std::move(question_));
            }

        private:
            Question question_;
        };

        // The next question, at once (over the book or the gift menu); one message box at a time.
        void Next()
        {
            auto* ui = RE::UI::GetSingleton();
            if (g_asking || g_questions.empty() || !ui || ui->IsMenuOpen(RE::MessageBoxMenu::MENU_NAME)) return;
            auto question = std::move(g_questions.front());
            g_questions.pop_front();
            if (question.generation != Session::Generation()) return Next();
            auto* data = RE::UIMessageDataFactory::Create<RE::MessageBoxData>();
            if (!data) return Finish(std::move(question), {});
            g_asking = true;
            data->bodyText = question.text.c_str();
            data->buttonText.push_back(std::string{ Strings::kPackageYes }.c_str());
            data->buttonText.push_back(std::string{ Strings::kPackageNo }.c_str());
            data->cancelButtonIndex = 1;
            data->callback = RE::BSTSmartPointer<RE::IMessageBoxCallback>(new Answer(std::move(question)));
            RE::MessageBoxMenu::QueueMessage(data);
        }

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
                if (!a_event) return RE::BSEventNotifyControl::kContinue;
                const bool container = a_event->menuName == RE::ContainerMenu::MENU_NAME;
                const bool opening = a_event->opening;
                if (opening && !container) return RE::BSEventNotifyControl::kContinue;
                SKSE::GetTaskInterface()->AddTask([container, opening]() {
                    // An exception must not cross into the engine.
                    try {
                        if (opening) {
                            // Ours, not a barrel the player opened meanwhile.
                            RE::NiPointer<RE::TESObjectREFR> target;
                            RE::TESObjectREFR::LookupByHandle(RE::ContainerMenu::GetTargetRefHandle(), target);
                            if (g_packing && !g_openPending && !g_opened && target.get() == Ref(kPackingChest)) {
                                g_opened = true;
                                SKSE::log::info("[Package] The packing chest is open");
                            }
                        } else if (container && g_packing && g_opened) {
                            Packed();
                        } else {
                            OpenChest();
                            Next();
                        }
                    } catch (const std::exception& e) {
                        SKSE::log::error("[Package] After a menu closed: {}", e.what());
                    }
                });
                return RE::BSEventNotifyControl::kContinue;
            }
        };
    }

    void Ask(const std::string& question, std::function<void(Manifest)> done)
    {
        g_questions.push_back({ .text = question, .done = std::move(done), .generation = Session::Generation() });
        Next();
    }

    void Pack(std::function<void(Manifest)> done)
    {
        if (g_packing) {
            SKSE::log::warn("[Package] Already packing: this letter goes without a package");
            done({});
            return;
        }
        StartPacking({ .done = std::move(done), .generation = Session::Generation() });
    }

    void Attach(const std::string& letterId, Manifest manifest)
    {
        if (manifest.empty()) return;
        {
            std::lock_guard lock{ g_mutex };
            std::erase_if(g_packages, [&](const Package& p) { return p.letterId == letterId; });  // sent again
            g_packages.push_back({ .letterId = letterId, .holder = kStore, .items = std::move(manifest), .playerPacked = true });
        }
        Letters::Refresh(letterId);  // its card and its "Enclosed:" line, if it's made already
    }

    void Carry(const std::string& letterId, RE::TESObjectREFR* to)
    {
        Package package;
        {
            std::lock_guard lock{ g_mutex };
            const auto it = std::ranges::find_if(g_packages, [&](const Package& p) { return p.letterId == letterId && p.holder; });
            if (it == g_packages.end()) return;
            package = *it;
        }
        auto* store = Ref(kStore);
        auto* from = HolderRef(package.holder);
        auto* target = to ? to : store;
        if (!from || !target || from == target) return;
        // What he no longer has stays missing: the package is what's left.
        const auto moved = Move(package.items, from, target, std::format("Letter {}'s package", letterId));
        {
            std::lock_guard lock{ g_mutex };
            const auto it = std::ranges::find_if(g_packages, [&](const Package& p) { return p.letterId == letterId && p.holder; });
            if (it == g_packages.end()) return;
            it->holder = target == store ? kStore : target->GetFormID();
            it->items = moved;
        }
        SKSE::log::info("[Package] Letter {}'s package is now with {} (0x{:X})", letterId, target->GetName(), target->GetFormID());
        Letters::Refresh(letterId);  // its "Enclosed:" line: what's left
    }

    void Deliver(const std::string& letterId, RE::TESObjectREFR* to)
    {
        Package package;
        {
            std::lock_guard lock{ g_mutex };
            const auto it = std::ranges::find_if(g_packages, [&](const Package& p) { return p.letterId == letterId && p.holder; });
            if (it == g_packages.end()) return;
            package = *it;
            it->holder = 0;
        }
        auto* from = HolderRef(package.holder);
        if (!from || !to) {
            SKSE::log::error("[Package] Letter {}'s package can't be moved (from 0x{:X}, to 0x{:X}): lost", letterId,
                             package.holder, to ? to->GetFormID() : 0);
            return;
        }
        const auto moved = Move(package.items, from, to, std::format("Letter {}'s package", letterId));
        {
            std::lock_guard lock{ g_mutex };
            const auto it = std::ranges::find(g_packages, letterId, &Package::letterId);
            if (it != g_packages.end()) it->items = moved;  // what came, for the reading
        }
        SKSE::log::info("[Package] Letter {}'s package went to {} (0x{:X}): {} kind(s) of item", letterId, to->GetName(),
                        to->GetFormID(), moved.size());
        Letters::Refresh(letterId);  // what arrived
    }

    bool Add(const std::string& letterId, RE::TESBoundObject* object, std::int32_t count)
    {
        {
            std::lock_guard lock{ g_mutex };
            const auto it = std::ranges::find(g_packages, letterId, &Package::letterId);
            if (it != g_packages.end() && it->holder != kStore) return false;  // with a courier, or delivered
        }
        auto* to = Ref(kStore);
        if (!to) {
            SKSE::log::error("[Package] The package store isn't in Physical Letters.esp: nothing added to letter {}", letterId);
            return false;
        }
        to->AddObjectToContainer(object, nullptr, count, nullptr);
        {
            std::lock_guard lock{ g_mutex };
            auto it = std::ranges::find(g_packages, letterId, &Package::letterId);
            if (it == g_packages.end()) it = g_packages.insert(g_packages.end(), Package{ .letterId = letterId });
            // Plain items of the same kind are one line ("500 Gold"); an enchanted or named one stays its own.
            auto item = std::ranges::find_if(it->items, [&](const Item& i) {
                return i.form == object->GetFormID() && !i.enchantment && !i.health && i.customName.empty();
            });
            if (item == it->items.end()) item = it->items.insert(it->items.end(), Item{ .form = object->GetFormID() });
            item->count = static_cast<std::int32_t>(std::min<std::int64_t>(std::int64_t{ item->count } + count, INT32_MAX));
            item->shownName = Shown(item->count, object->GetName());
        }
        SKSE::log::info("[Package] Letter {}: {} '{}' (0x{:X}) added by a mod", letterId, count, object->GetName(), object->GetFormID());
        Letters::Refresh(letterId);  // its card and its "Enclosed:" line
        return true;
    }

    namespace {
        // A mod's items with a letter that won't arrive: deleted.
        void Discard(const std::string& letterId)
        {
            Package package;
            {
                std::lock_guard lock{ g_mutex };
                const auto it = std::ranges::find(g_packages, letterId, &Package::letterId);
                if (it == g_packages.end()) return;
                package = std::move(*it);
                g_packages.erase(it);
            }
            if (!package.holder) return;
            auto* from = HolderRef(package.holder);
            if (from) Move(package.items, from, nullptr, std::format("Letter {}'s package, discarded", letterId));
            SKSE::log::info("[Package] Letter {}'s package is discarded", letterId);
        }
    }

    void Return(const std::string& letterId)
    {
        bool playerPacked = false;
        {
            std::lock_guard lock{ g_mutex };
            const auto it = std::ranges::find(g_packages, letterId, &Package::letterId);
            if (it == g_packages.end() || !it->holder) return;
            playerPacked = it->playerPacked;
        }
        SKSE::log::info("[Package] Letter {} won't arrive: its package goes {}", letterId, playerPacked ? "back to the player" : "nowhere");
        if (playerPacked) Deliver(letterId, RE::PlayerCharacter::GetSingleton());
        else Discard(letterId);
    }

    void Forget(const std::string& letterId, bool keepDelivered)
    {
        bool parted = false;
        {
            std::lock_guard lock{ g_mutex };
            parted = std::erase_if(g_packages, [&](const Package& p) { return p.letterId == letterId && (p.holder || !keepDelivered); });
        }
        if (!parted) return;
        SKSE::log::info("[Package] Letter {} and its package parted", letterId);
        Letters::Refresh(letterId);  // no "Enclosed:" line, no "With a package."
    }

    std::string Arrived(const std::string& letterId)
    {
        std::lock_guard lock{ g_mutex };
        const auto it = std::ranges::find(g_packages, letterId, &Package::letterId);
        return it == g_packages.end() || it->holder ? std::string{} : Joined(it->items);
    }

    Manifest Contents(const std::string& letterId)
    {
        std::lock_guard lock{ g_mutex };
        const auto it = std::ranges::find(g_packages, letterId, &Package::letterId);
        return it != g_packages.end() ? it->items : Manifest{};
    }

    bool Has(const std::string& letterId)
    {
        std::lock_guard lock{ g_mutex };
        const auto it = std::ranges::find(g_packages, letterId, &Package::letterId);
        return it != g_packages.end() && !it->items.empty();  // a courier robbed of all of it: no package left
    }

    std::string Describe(const std::string& letterId)
    {
        std::lock_guard lock{ g_mutex };
        const auto it = std::ranges::find(g_packages, letterId, &Package::letterId);
        return it == g_packages.end() ? std::string{} : Joined(it->items);
    }

    void Register()
    {
        if (auto* ui = RE::UI::GetSingleton()) ui->AddEventSink<RE::MenuOpenCloseEvent>(MenuSink::GetSingleton());
        if (!Ref(kPackingChest) || !Ref(kStore)) SKSE::log::error("[Package] The package chests aren't in Physical Letters.esp");
        // The container menu's title, in the chosen language (the ESP's is English).
        auto* data = RE::TESDataHandler::GetSingleton();
        if (auto* chest = data ? data->LookupForm<RE::TESObjectCONT>(kPackingChestBase, Parchment::kPlugin) : nullptr) {
            chest->SetFullName(Strings::PackingChestName().c_str());
        }
    }

    void Save(SKSE::SerializationInterface* a_intfc, std::uint32_t a_type)
    {
        std::lock_guard lock{ g_mutex };
        if (!a_intfc->OpenRecord(a_type, kRecordVersion)) {
            SKSE::log::error("[Package] Couldn't open the co-save record");
            return;
        }
        a_intfc->WriteRecordData(static_cast<std::uint32_t>(g_packages.size()));
        for (const auto& package : g_packages) {
            CoSave::WriteString(a_intfc, package.letterId);
            a_intfc->WriteRecordData(package.holder);
            a_intfc->WriteRecordData(static_cast<std::uint8_t>(package.playerPacked));
            a_intfc->WriteRecordData(static_cast<std::uint32_t>(package.items.size()));
            for (const auto& item : package.items) {
                a_intfc->WriteRecordData(item.form);
                a_intfc->WriteRecordData(item.count);
                a_intfc->WriteRecordData(item.enchantment);
                a_intfc->WriteRecordData(item.health);
                CoSave::WriteString(a_intfc, item.customName);
                CoSave::WriteString(a_intfc, item.shownName);
            }
        }
    }

    void Load(SKSE::SerializationInterface* a_intfc, std::uint32_t a_version)
    {
        if (a_version != kRecordVersion) {
            SKSE::log::error("[Package] Co-save record version {} is unknown — skipped", a_version);
            return;
        }
        std::lock_guard lock{ g_mutex };
        std::uint32_t count = 0;
        if (a_intfc->ReadRecordData(count) != sizeof(count)) return;
        for (std::uint32_t i = 0; i < count; ++i) {
            Package package;
            std::uint32_t items = 0;
            std::uint8_t playerPacked = 0;
            if (!CoSave::ReadString(a_intfc, package.letterId) || a_intfc->ReadRecordData(package.holder) != sizeof(package.holder) ||
                a_intfc->ReadRecordData(playerPacked) != sizeof(playerPacked) || a_intfc->ReadRecordData(items) != sizeof(items)) {
                break;
            }
            package.playerPacked = playerPacked != 0;
            // The store is ours (its FormID moves with the load order); anything else is resolved.
            RE::FormID holder = 0;
            if (package.holder != kStore && a_intfc->ResolveFormID(package.holder, holder)) package.holder = holder;
            for (std::uint32_t j = 0; j < items; ++j) {
                Item item;
                if (a_intfc->ReadRecordData(item.form) != sizeof(item.form) || a_intfc->ReadRecordData(item.count) != sizeof(item.count) ||
                    a_intfc->ReadRecordData(item.enchantment) != sizeof(item.enchantment) ||
                    a_intfc->ReadRecordData(item.health) != sizeof(item.health) || !CoSave::ReadString(a_intfc, item.customName) ||
                    !CoSave::ReadString(a_intfc, item.shownName)) {
                    return;
                }
                RE::FormID resolved = 0;
                if (!a_intfc->ResolveFormID(item.form, resolved)) continue;  // its plugin is gone
                item.form = resolved;
                if (item.enchantment && !a_intfc->ResolveFormID(item.enchantment, resolved)) item.enchantment = 0;
                else if (item.enchantment) item.enchantment = resolved;
                package.items.push_back(std::move(item));
            }
            g_packages.push_back(std::move(package));
        }
        SKSE::log::info("[Package] {} package(s) on their way", g_packages.size());
    }

    void Revert()
    {
        std::lock_guard lock{ g_mutex };
        g_packages.clear();
        g_questions.clear();
        g_packing.reset();
        g_asking = false;
        g_openPending = false;
        g_opened = false;
    }

    void Tick()
    {
        if (!g_packing || g_opened || std::chrono::steady_clock::now() - g_packingSince < kOpenTimeout) {
            Next();
            return;
        }
        SKSE::log::warn("[Package] The packing chest didn't open within {} seconds: the letter goes without a package",
                        std::chrono::duration_cast<std::chrono::seconds>(kOpenTimeout).count());
        auto question = std::move(*g_packing);
        g_packing.reset();
        g_openPending = false;
        g_asking = false;
        Finish(std::move(question), {});
        Next();
    }

} // namespace PhysicalLetters::Package
