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
#include "Config.h"

namespace PhysicalLetters::Travel {

    namespace {
        // When the engine finds no path: the straight line, stretched for winding roads.
        constexpr double kStraightLineFactor = 1.3;

        // Fast travel's own path length (docs/DELIVERY.md#travel-time).  Not in CommonLib;
        // found in fast travel (AE 40445) on each runtime.  VR's Address Library doesn't list the
        // functions (older copies not even the singleton), so VR uses raw offsets.
        struct PathParams {
            alignas(8) std::byte data[0x50];  // built from the traveller's handle
        };
        struct PathLocation {
            alignas(8) std::byte data[sizeof(RE::BSPathingLocation)];
            RE::BSPathingLocation* get() { return reinterpret_cast<RE::BSPathingLocation*>(data); }
        };
        static_assert(sizeof(RE::BSPathingLocation) == 0x30);

        RE::Pathing* PathingSingleton()
        {
            static REL::Relocation<RE::Pathing**> singleton{ REL::VariantID(514893, 401037, 0x2FC4658) };
            return *singleton;
        }

        void MakeParams(PathParams& a_out, const std::uint32_t& a_travellerHandle)
        {
            using func_t = void* (*)(PathParams*, const std::uint32_t*, bool);
            static REL::Relocation<func_t> func{ REL::VariantID(30030, 30845, 0x48E670) };
            func(&a_out, &a_travellerHandle, true);
        }

        void DestroyParams(PathParams& a_params)
        {
            using func_t = void (*)(PathParams*);
            static REL::Relocation<func_t> func{ REL::VariantID(30031, 30846, 0x48E740) };
            func(&a_params);
        }

        // Constructs the location in place.
        void MakeLocation(PathLocation& a_out, RE::TESObjectREFR* a_ref)
        {
            using func_t = RE::BSPathingLocation* (*)(RE::Pathing*, RE::BSPathingLocation*, RE::TESObjectREFR*);
            static REL::Relocation<func_t> func{ REL::VariantID(29820, 30636, 0x4831E0) };
            func(PathingSingleton(), a_out.get(), a_ref);
        }

        // Game units along the navmesh, or FLT_MAX when there's no path.
        float PathLength(RE::BSPathingLocation* a_from, RE::BSPathingLocation* a_to, PathParams& a_params)
        {
            using func_t = float (*)(RE::Pathing*, RE::BSPathingLocation*, RE::BSPathingLocation*, std::uint32_t, PathParams*,
                                     std::uint32_t);
            static REL::Relocation<func_t> func{ REL::VariantID(29841, 30657, 0x485760) };
            return func(PathingSingleton(), a_from, a_to, 0, &a_params, 0);
        }

        // Fast travel's path length between the two, measured for the player; nullopt when
        // the engine finds no path.
        std::optional<double> RoadLength(RE::TESObjectREFR* a_from, RE::TESObjectREFR* a_to)
        {
            auto* player = RE::PlayerCharacter::GetSingleton();
            if (!player || !PathingSingleton()) return std::nullopt;
            const std::uint32_t handle = player->GetHandle().native_handle();
            PathParams params;
            MakeParams(params, handle);
            PathLocation from, to;
            MakeLocation(from, a_from);
            MakeLocation(to, a_to);
            const float length = PathLength(from.get(), to.get(), params);
            std::destroy_at(to.get());
            std::destroy_at(from.get());
            DestroyParams(params);
            if (length >= std::numeric_limits<float>::max()) return std::nullopt;
            return length;
        }

        struct Place {
            RE::NiPointer<RE::TESObjectREFR> ref;
            const RE::TESWorldSpace* world = nullptr;  // the root worldspace
        };

        // City worldspaces (WhiterunWorld, SolitudeWorld) are children of Tamriel and share its
        // coordinates: the same world for a letter.  Solstheim or Blackreach are not.
        const RE::TESWorldSpace* RootWorld(const RE::TESWorldSpace* a_world)
        {
            while (a_world && a_world->parentWorld) a_world = a_world->parentWorld;
            return a_world;
        }

        // The place's exterior marker, or its parent location's.
        std::optional<Place> MarkerOf(const RE::BGSLocation* a_location)
        {
            for (auto* location = a_location; location; location = location->parentLoc) {
                auto marker = location->worldLocMarker.get();
                if (marker && marker->GetWorldspace()) return Place{ marker, RootWorld(marker->GetWorldspace()) };
            }
            return std::nullopt;
        }

        // Where the reference is in the exterior world.  Letters travel the roads, so an
        // interior counts as its location's exterior marker.
        std::optional<Place> WorldPlace(RE::TESObjectREFR* a_ref)
        {
            if (!a_ref) return std::nullopt;
            if (const auto* cell = a_ref->GetParentCell(); cell && cell->IsExteriorCell() && a_ref->GetWorldspace()) {
                return Place{ RE::NiPointer<RE::TESObjectREFR>(a_ref), RootWorld(a_ref->GetWorldspace()) };
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

    bool IsTown(const RE::BGSLocation* a_location)
    {
        static auto* habitation = RE::TESForm::LookupByID<RE::BGSKeyword>(0x039793);  // LocTypeHabitation
        return a_location && habitation && a_location->HasKeyword(habitation);
    }

    std::optional<MapPoint> PointOf(RE::TESObjectREFR* a_ref)
    {
        const auto place = WorldPlace(a_ref);
        if (!place || !place->world) return std::nullopt;
        const auto& position = place->ref->GetPosition();
        return MapPoint{ place->world->GetFormID(), position.x, position.y };
    }

    RE::TESObjectREFR* MarkerFor(const RE::BGSLocation* a_location)
    {
        const auto place = MarkerOf(a_location);
        return place ? place->ref.get() : nullptr;
    }

    const RE::BGSLocation* Area(RE::TESObjectREFR* a_ref)
    {
        if (!a_ref) return nullptr;
        static auto* hold = RE::TESForm::LookupByID<RE::BGSKeyword>(0x016771);        // LocTypeHold
        const RE::BGSLocation* start = a_ref->GetCurrentLocation();
        if (!start) start = a_ref->GetEditorLocation();
        const RE::BGSLocation* belowHold = nullptr;
        for (auto* location = start; location; location = location->parentLoc) {
            if (IsTown(location)) return location;
            if (hold && location->HasKeyword(hold)) break;
            belowHold = location;
        }
        return belowHold;
    }

    std::optional<double> Distance(RE::TESObjectREFR* a_from, RE::TESObjectREFR* a_to)
    {
        const auto from = WorldPlace(a_from);
        const auto to = WorldPlace(a_to);
        if (!from || !to || from->world != to->world) return std::nullopt;
        return from->ref->GetPosition().GetDistance(to->ref->GetPosition());
    }

    double Hours(RE::TESObjectREFR* a_from, RE::TESObjectREFR* a_to)
    {
        const auto* config = Config::GetSingleton();
        // Between worldspaces, or when a place can't be found: no road to measure.
        const double fallbackHours = config->Get(Config::kFallbackHours);
        // Someone still has to carry it: same room, or same town (both indoors resolve to
        // the town's marker, 0 units apart).
        const double minHours = config->Get(Config::kMinHours);

        const auto from = WorldPlace(a_from);
        const auto to = WorldPlace(a_to);
        if (!from || !to || from->world != to->world) {
            SKSE::log::info("[Travel] No common worldspace ({} and {}): {:.0f} game hours",
                            from && from->world ? from->world->GetFormEditorID() : "none",
                            to && to->world ? to->world->GetFormEditorID() : "none", fallbackHours);
            return fallbackHours;
        }

        // The engine's fast-travel time: path length / (fFastTravelSpeedMult * the
        // traveller's walk speed) real seconds, which the calendar turns into game time at
        // TimeScale.
        auto* player = RE::PlayerCharacter::GetSingleton();
        const double walkSpeed = player ? player->GetWalkSpeed() : 0.0;
        const double speedMult = GameSetting("fFastTravelSpeedMult", 1.0f);
        const auto* calendar = RE::Calendar::GetSingleton();
        const double timeScale = calendar ? calendar->GetTimescale() : 20.0;
        if (walkSpeed <= 0.0 || speedMult <= 0.0) return fallbackHours;

        const auto started = std::chrono::steady_clock::now();
        const auto road = RoadLength(from->ref.get(), to->ref.get());
        const auto ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count();
        const double straight = from->ref->GetPosition().GetDistance(to->ref->GetPosition());
        const double distance = road ? *road : straight * kStraightLineFactor;

        const double hours = std::max(distance / (speedMult * walkSpeed) * timeScale / 3600.0, minHours);
        SKSE::log::info("[Travel] {} {:.0f} units (straight line {:.0f}, pathing {:.1f} ms) at walk speed {:.1f}, "
                        "fFastTravelSpeedMult {:.2f}, timescale {:.0f}: {:.1f} game hours",
                        road ? "Road" : "No road found: straight line x1.3,", distance, straight, ms, walkSpeed, speedMult,
                        timeScale, hours);
        return hours;
    }

    std::string PlaceName(RE::TESObjectREFR* a_ref)
    {
        if (const auto* area = Area(a_ref); area && area->GetName() && *area->GetName()) return area->GetName();
        if (const auto* location = a_ref ? a_ref->GetCurrentLocation() : nullptr;
            location && location->GetName() && *location->GetName()) {
            return location->GetName();
        }
        return "somewhere in Skyrim";
    }

} // namespace PhysicalLetters::Travel
