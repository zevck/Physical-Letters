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

#include "Courier.h"

namespace PhysicalLetters::Courier {

    namespace {
        // WICourier, with WICourierScript attached.
        constexpr RE::FormID kCourierQuestId = 0x039F82;
        constexpr std::string_view kCourierQuestPlugin = "Skyrim.esm";
    }

    bool Give(RE::TESBoundObject* a_item)
    {
        auto* data = RE::TESDataHandler::GetSingleton();
        auto* quest = data ? data->LookupForm<RE::TESQuest>(kCourierQuestId, kCourierQuestPlugin) : nullptr;
        auto* vm = RE::BSScript::Internal::VirtualMachine::GetSingleton();
        if (!a_item || !quest || !vm) {
            SKSE::log::error("[Courier] The courier quest {}:0x{:X} wasn't found", kCourierQuestPlugin, kCourierQuestId);
            return false;
        }
        const auto handle = vm->GetObjectHandlePolicy()->GetHandleForObject(RE::TESQuest::FORMTYPE, quest);
        // The VM takes ownership of the arguments (as in CommonLib's RegistrationSet).
        auto* args = RE::MakeFunctionArguments(static_cast<RE::TESForm*>(a_item), static_cast<std::int32_t>(1));
        RE::BSTSmartPointer<RE::BSScript::IStackCallbackFunctor> callback;
        if (!vm->DispatchMethodCall(handle, "WICourierScript", "addItemToContainer", args, callback)) {
            SKSE::log::error("[Courier] WICourierScript.addItemToContainer couldn't be called for 0x{:X}", a_item->GetFormID());
            return false;
        }
        SKSE::log::info("[Courier] Gave 0x{:X} to the courier", a_item->GetFormID());
        return true;
    }

} // namespace PhysicalLetters::Courier
