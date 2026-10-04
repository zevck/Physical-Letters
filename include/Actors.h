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

// Who counts as a person for letters: SkyrimNet registers creatures and animals too.  Game thread.
namespace PhysicalLetters::Actors {

    // ActorTypeNPC, the keyword every NPC race has.  Look it up outside the form map's lock.
    inline RE::BGSKeyword* NpcType()
    {
        static auto* keyword = RE::TESForm::LookupByID<RE::BGSKeyword>(0x013794);
        return keyword;
    }

    // Of an NPC race: not a creature or an animal.
    inline bool IsNpcRace(RE::Actor* actor, RE::BGSKeyword* npcType = NpcType())
    {
        auto* race = actor ? actor->GetRace() : nullptr;
        return race && npcType && race->HasKeyword(npcType);
    }

    // Someone who can write: an NPC race with a voice.
    inline bool IsPerson(RE::Actor* actor)
    {
        auto* base = actor ? actor->GetActorBase() : nullptr;
        return IsNpcRace(actor) && base && base->voiceType;
    }

} // namespace PhysicalLetters::Actors
