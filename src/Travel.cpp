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

#include "Travel.h"

namespace PhysicalLetters::Travel {

    namespace {
        // Fast travel measures the path; roads wind, so the straight line is stretched.
        constexpr double kPathFactor = 1.3;
        // Between worldspaces, or when a place can't be found: a straight line means nothing.
        constexpr double kFallbackHours = 48.0;
        // Someone still has to carry it: same room, or same town (both indoors resolve to
        // the town's marker, 0 units apart).
        constexpr double kMinHours = 2.0;

        struct Place {
            const RE::TESWorldSpace* world = nullptr;
            RE::NiPoint3 position;
        };

        // The place's exterior marker, or its parent location's.
        std::optional<Place> MarkerOf(const RE::BGSLocation* a_location)
        {
            for (auto* location = a_location; location; location = location->parentLoc) {
                const auto marker = location->worldLocMarker.get();
                if (marker && marker->GetWorldspace()) return Place{ marker->GetWorldspace(), marker->GetPosition() };
            }
            return std::nullopt;
        }

        // Where the reference is in the exterior world.  Interior coordinates don't compare
        // with exterior ones, so an interior counts as its location's exterior marker.
        std::optional<Place> WorldPlace(RE::TESObjectREFR* a_ref)
        {
            if (!a_ref) return std::nullopt;
            if (const auto* cell = a_ref->GetParentCell(); cell && cell->IsExteriorCell() && a_ref->GetWorldspace()) {
                return Place{ a_ref->GetWorldspace(), a_ref->GetPosition() };
            }
            if (auto place = MarkerOf(a_ref->GetCurrentLocation())) return place;
            if (auto place = MarkerOf(a_ref->GetEditorLocation())) return place;
            return std::nullopt;
        }

        float GameSetting(const char* a_name, float a_default)
        {
            auto* settings = RE::GameSettingCollection::GetSingleton();
            auto* setting = settings ? settings->GetSetting(a_name) : nullptr;
            return setting ? setting->GetFloat() : a_default;
        }
    }

    double Hours(RE::TESObjectREFR* a_from, RE::TESObjectREFR* a_to)
    {
        const auto from = WorldPlace(a_from);
        const auto to = WorldPlace(a_to);
        if (!from || !to || from->world != to->world) {
            SKSE::log::info("[Travel] No common worldspace: {:.0f} game hours", kFallbackHours);
            return kFallbackHours;
        }

        // The engine's fast-travel time (AE 1.6.1170, docs/DELIVERY.md): path length /
        // (fFastTravelSpeedMult * the traveller's walk speed) real seconds, which the
        // calendar turns into game time at TimeScale.
        auto* player = RE::PlayerCharacter::GetSingleton();
        const double walkSpeed = player ? player->GetWalkSpeed() : 0.0;
        const double speedMult = GameSetting("fFastTravelSpeedMult", 1.0f);
        const auto* calendar = RE::Calendar::GetSingleton();
        const double timeScale = calendar ? calendar->GetTimescale() : 20.0;
        if (walkSpeed <= 0.0 || speedMult <= 0.0) return kFallbackHours;

        const double distance = from->position.GetDistance(to->position) * kPathFactor;
        const double hours = std::max(distance / (speedMult * walkSpeed) * timeScale / 3600.0, kMinHours);
        SKSE::log::info("[Travel] {:.0f} units at walk speed {:.1f}, timescale {:.0f}: {:.1f} game hours", distance, walkSpeed,
                        timeScale, hours);
        return hours;
    }

} // namespace PhysicalLetters::Travel
