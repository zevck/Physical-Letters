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

#pragma once

// Packages sent with letters (docs/PACKAGES.md): packed in a chest in the vanilla courier's cell, kept in another
// there, each package a manifest of what it holds, moved by its manifest wherever its letter goes.  Game thread.
namespace PhysicalLetters::Package {

    // One kind of item in a package: the base item, how many, and what made it unique when packed.
    struct Item {
        RE::FormID form = 0;
        std::int32_t count = 0;
        RE::FormID enchantment = 0;  // ExtraEnchantment: a player-enchanted item's
        float health = 0.0f;         // ExtraHealth: tempering; 0 none
        std::string customName;      // ExtraTextDisplayData
        std::string shownName;       // as packed, for prompts and logs (read on any thread)
    };
    using Manifest = std::vector<Item>;

    // Asks `question` at once, over whatever is open (one box at a time), and on yes opens the packing chest; `done`
    // gets what was put in it, now in the package store (empty: none).  Dropped by a load.
    void Ask(const std::string& question, std::function<void(Manifest)> done);

    // As Ask's yes, without the question (the DM Letter's Package box): the menus in the way close, the chest opens.
    void Pack(std::function<void(Manifest)> done);

    // The manifest is `letterId`'s package, in the store, from now on; the letter lists it ("Enclosed: …").  Nothing
    // for an empty manifest.
    void Attach(const std::string& letterId, Manifest manifest);

    // A courier takes the letter: its package goes onto him (`to`), or back to the store (nullptr) when he gives it up.
    void Carry(const std::string& letterId, RE::TESObjectREFR* to);

    // The package goes with its letter to `to` (a recipient, the courier's container, the player): what's still where
    // it was, by its manifest.  Nothing if the letter has none.
    void Deliver(const std::string& letterId, RE::TESObjectREFR* to);

    // Another mod's items with a letter no courier has yet (docs/API.md#packages): made in the store.  False if its
    // package has left the store.
    bool Add(const std::string& letterId, RE::TESBoundObject* object, std::int32_t count);

    // A letter that won't arrive (lost, never written): the player's items go back to them, a mod's are discarded.
    void Return(const std::string& letterId);

    // The letter and its package part (sent again, handed over in person, taken from a courier who keeps it): the
    // items stay where they are.  `keepDelivered`: only if they hadn't arrived (a delivered record stays).
    void Forget(const std::string& letterId, bool keepDelivered = false);

    // What came with the letter once it was delivered ("": nothing, or not yet).  Any thread.
    std::string Arrived(const std::string& letterId);

    // What the package holds now, or what arrived once delivered (empty: none).  Any thread.
    Manifest Contents(const std::string& letterId);

    // Whether the letter has a package, and what it holds ("300 Gold, Steel Sword"; "" none).  Any thread.
    bool Has(const std::string& letterId);
    std::string Describe(const std::string& letterId);

    // Heartbeat: a packing chest that didn't open in time lets its letter go without a package; waiting questions.
    void Tick();

    // kDataLoaded: the menu watcher.
    void Register();

    void Save(SKSE::SerializationInterface* a_intfc, std::uint32_t a_type);
    void Load(SKSE::SerializationInterface* a_intfc, std::uint32_t a_version);
    void Revert();

} // namespace PhysicalLetters::Package
