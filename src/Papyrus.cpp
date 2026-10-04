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

#include "Papyrus.h"
#include "Config.h"
#include "CourierErrand.h"
#include "HandIn.h"
#include "Letters.h"
#include "RoadCourier.h"
#include "Session.h"
#include "Postage.h"

namespace PhysicalLetters::Papyrus {

    namespace {
        constexpr auto kScript = "PhysicalLetters_MCM";

        // Settings are named "Section.Key" (Config::kSettings); an unknown name reads 0.
        const Config::Setting* Find(const RE::BSFixedString& name)
        {
            const auto* s = Config::Find(name.c_str());
            if (!s) SKSE::log::error("[Papyrus] Unknown setting '{}'", name.c_str());
            return s;
        }

        std::int32_t GetSetting(RE::StaticFunctionTag*, RE::BSFixedString name)
        {
            const auto* s = Find(name);
            return s ? Config::GetSingleton()->Get(*s) : 0;
        }

        std::int32_t GetSettingDefault(RE::StaticFunctionTag*, RE::BSFixedString name)
        {
            const auto* s = Find(name);
            return s ? s->defaultValue : 0;
        }

        std::int32_t GetSettingMin(RE::StaticFunctionTag*, RE::BSFixedString name)
        {
            const auto* s = Find(name);
            return s ? s->min : 0;
        }

        std::int32_t GetSettingMax(RE::StaticFunctionTag*, RE::BSFixedString name)
        {
            const auto* s = Find(name);
            return s ? s->max : 0;
        }

        void SetSetting(RE::StaticFunctionTag*, RE::BSFixedString name, std::int32_t value)
        {
            const auto* s = Find(name);
            if (!s) return;
            auto* config = Config::GetSingleton();
            config->Set(*s, value);
            config->Save();
            SKSE::log::info("[Config] {}.{} = {}", s->section, s->key, config->Get(*s));
            // kSettings holds copies: compare by key.
            const std::string_view key = s->key;
            if (key == Config::kDebugLog.key) {
                const auto level = config->Get(*s) ? spdlog::level::debug : spdlog::level::info;
                spdlog::default_logger()->set_level(level);
                spdlog::default_logger()->flush_on(level);
            } else if (key == Config::kPostage.key) {
                Postage::ApplyPrice();
            } else if (key == Config::kHandInDialogue.key) {
                HandIn::ApplyDialogue();
            } else if (key == Config::kFontSize.key && Session::IsReady()) {
                Letters::AttachTexts();  // every letter's text again, at the new size
            }
        }

        bool RegisterFunctions(RE::BSScript::IVirtualMachine* vm)
        {
            vm->RegisterFunction("GetSetting", kScript, GetSetting);
            vm->RegisterFunction("SetSetting", kScript, SetSetting);
            vm->RegisterFunction("GetSettingDefault", kScript, GetSettingDefault);
            vm->RegisterFunction("GetSettingMin", kScript, GetSettingMin);
            vm->RegisterFunction("GetSettingMax", kScript, GetSettingMax);
            return CourierErrand::RegisterFunctions(vm) && RoadCourier::RegisterFunctions(vm) && HandIn::RegisterFunctions(vm);
        }
    }

    void Register()
    {
        if (auto* papyrus = SKSE::GetPapyrusInterface(); !papyrus || !papyrus->Register(RegisterFunctions)) {
            SKSE::log::error("[Papyrus] Registering the natives failed");
        }
    }

} // namespace PhysicalLetters::Papyrus
