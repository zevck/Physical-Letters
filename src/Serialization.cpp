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

#include "Serialization.h"
#include "DynamicForms.h"
#include "Letters.h"
#include "NpcLetters.h"
#include "Transit.h"

namespace PhysicalLetters::Serialization {

    namespace {
        constexpr std::uint32_t kSerializationId = 'SNPL';
        constexpr std::uint32_t kLetterFormsRecord = 'LFRM';
        constexpr std::uint32_t kTransitRecord = 'LTRN';
        constexpr std::uint32_t kNpcLettersRecord = 'LNPC';

        void SaveCallback(SKSE::SerializationInterface* a_intfc)
        {
            try {
                // First: without its record a letter in the save loads as an empty shell.
                DynamicForms::Save(a_intfc, kLetterFormsRecord);
                Transit::Save(a_intfc, kTransitRecord);
                NpcLetters::Save(a_intfc, kNpcLettersRecord);
            } catch (const std::exception& e) {
                SKSE::log::error("SaveCallback exception: {}", e.what());
            }
        }

        void RevertCallback(SKSE::SerializationInterface*)
        {
            DynamicForms::Revert();
            Letters::Revert();
            Transit::Revert();
            NpcLetters::Revert();
        }

        // Inside the load, after the engine has recreated this save's letter forms: their
        // look now, their text once LetterDB is open (Session::Poll).
        void LoadCallback(SKSE::SerializationInterface* a_intfc)
        {
            try {
                std::uint32_t type = 0, version = 0, length = 0;
                while (a_intfc->GetNextRecordInfo(type, version, length)) {
                    if (type == kLetterFormsRecord) DynamicForms::Load(a_intfc, version);
                    else if (type == kTransitRecord) Transit::Load(a_intfc, version);
                    else if (type == kNpcLettersRecord) NpcLetters::Load(a_intfc, version);
                }
                Letters::ConfigureLoaded();
            } catch (const std::exception& e) {
                SKSE::log::error("LoadCallback exception: {}", e.what());
            }
        }
    }

    void Register()
    {
        auto* serialization = SKSE::GetSerializationInterface();
        serialization->SetUniqueID(kSerializationId);
        serialization->SetSaveCallback(SaveCallback);
        serialization->SetRevertCallback(RevertCallback);
        serialization->SetLoadCallback(LoadCallback);
    }

} // namespace PhysicalLetters::Serialization
