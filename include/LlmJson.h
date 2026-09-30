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

#include <nlohmann/json.hpp>

// Reading the JSON an LLM answers with (docs/READING.md#reading-the-answer).
namespace PhysicalLetters::LlmJson {

    using json = nlohmann::json;

    // Fixes the two slips LLMs make most in otherwise valid JSON: a trailing comma
    // before } or ] (seen in game), and raw line breaks or tabs inside a string.
    inline std::string RepairJson(std::string_view text)
    {
        std::string out;
        out.reserve(text.size());
        bool inString = false, escaped = false;
        for (std::size_t i = 0; i < text.size(); ++i) {
            const char c = text[i];
            if (inString) {
                if (escaped) escaped = false;
                else if (c == '\\') escaped = true;
                else if (c == '"') inString = false;
                if (c == '\n') { out += "\\n"; continue; }
                if (c == '\r') continue;
                if (c == '\t') { out += "\\t"; continue; }
                out += c;
                continue;
            }
            if (c == '"') inString = true;
            if (c == ',') {
                auto next = text.find_first_not_of(" \t\r\n", i + 1);
                if (next != std::string_view::npos && (text[next] == '}' || text[next] == ']')) continue;
            }
            out += c;
        }
        return out;
    }

    // The LLM's JSON object, without any text or code fence around it; discarded
    // (not an object) if it can't be read even after RepairJson.
    inline json ParseResponse(const std::string& response)
    {
        const auto first = response.find('{');
        const auto last = response.rfind('}');
        if (first == std::string::npos || last == std::string::npos || last < first) return json{};
        const auto text = std::string_view{ response }.substr(first, last - first + 1);
        auto parsed = json::parse(text, nullptr, false);
        if (parsed.is_discarded()) parsed = json::parse(RepairJson(text), nullptr, false);
        return parsed;
    }

    // Field readers that accept what LLMs write instead of the type asked for (null,
    // "0.8", "yes").  json::value would throw on a present key of another type.
    inline std::string GetString(const json& j, const char* key)
    {
        const auto it = j.find(key);
        return it != j.end() && it->is_string() ? it->get<std::string>() : std::string{};
    }

    inline float GetFloat(const json& j, const char* key, float fallback)
    {
        const auto it = j.find(key);
        if (it == j.end()) return fallback;
        if (it->is_number()) return it->get<float>();
        if (it->is_string()) {
            const auto& text = it->get_ref<const std::string&>();
            float value = fallback;
            std::from_chars(text.data(), text.data() + text.size(), value);
            return value;
        }
        return fallback;
    }

    inline bool GetBool(const json& j, const char* key)
    {
        const auto it = j.find(key);
        if (it == j.end()) return false;
        if (it->is_boolean()) return it->get<bool>();
        if (it->is_number()) return it->get<double>() != 0.0;
        if (it->is_string()) {
            auto text = it->get<std::string>();
            std::ranges::transform(text, text.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            return text == "true" || text == "yes";
        }
        return false;
    }

} // namespace PhysicalLetters::LlmJson
