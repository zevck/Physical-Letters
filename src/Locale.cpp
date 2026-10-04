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

#include "Locale.h"

#include "Config.h"
#include "MarkedText.h"

#include <fstream>

namespace PhysicalLetters::Locale {

    namespace {
        using MarkedText::Trim;

        std::map<std::string, std::string, std::less<>> g_letters;  // the locale file's [Letters]

        std::string Upper(std::string text)
        {
            std::ranges::transform(text, text.begin(), [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
            return text;
        }

        // [General] Language in PhysicalLetters.ini if set, else the game's sLanguage ("ENGLISH" if unset).
        std::string Language()
        {
            if (auto chosen = Config::GetSingleton()->GetLanguage(); !chosen.empty()) return Upper(chosen);
            auto* settings = RE::INISettingCollection::GetSingleton();
            auto* setting = settings ? settings->GetSetting("sLanguage:General") : nullptr;
            const char* value = setting && setting->GetType() == RE::Setting::Type::kString ? setting->GetString() : nullptr;
            return Upper(value && *value ? value : "ENGLISH");
        }

        // Every key of [Letters] in a UTF-8 ini file.
        void ReadLetters(const std::filesystem::path& path)
        {
            std::ifstream file(path);
            std::string line;
            bool inSection = false;
            while (std::getline(file, line)) {
                if (line.starts_with("\xEF\xBB\xBF")) line.erase(0, 3);  // a BOM
                const auto trimmed = Trim(line);
                if (trimmed.empty() || trimmed[0] == ';') continue;
                if (trimmed[0] == '[') {
                    inSection = trimmed == "[Letters]";
                    continue;
                }
                const auto eq = trimmed.find('=');
                if (!inSection || eq == std::string::npos) continue;
                if (auto value = Trim(std::string_view{ trimmed }.substr(eq + 1)); !value.empty()) {
                    g_letters[Trim(std::string_view{ trimmed }.substr(0, eq))] = std::move(value);
                }
            }
        }
    }

    void Load()
    {
        const auto language = Language();
        const auto path = std::filesystem::current_path() / "Data" / "SKSE" / "Plugins" / "PhysicalLetters" / "Locales" /
                          (language + ".ini");
        if (!std::filesystem::exists(path)) {
            SKSE::log::info("[Locale] No locale file for {} ({}): letters use English", language, path.string());
            return;
        }
        ReadLetters(path);
        SKSE::log::info("[Locale] {}: {} letter text(s) from {}", language, g_letters.size(), path.string());
    }

    std::string Text(std::string_view key, std::string_view english)
    {
        const auto it = g_letters.find(key);
        return it != g_letters.end() ? it->second : std::string{ english };
    }

} // namespace PhysicalLetters::Locale
