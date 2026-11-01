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

#include "TextHook.h"
#include "DmLetter.h"
#include "Letters.h"

#include <MinHook.h>

// ---------------------------------------------------------------------------
// TESDescription::GetDescription hook (see SkyrimNet Physical Diaries' docs/BOOK_TEXT.md).
//
// Two hooks (docs/ARCHITECTURE.md).  Every reader asks the form for its DESC field:
//
//  - The book menu passes the book's description component with no parent: the
//    styled text.  The component is matched by identity.
//  - Other readers (SkyrimNet's book-read event, Immersive Reading) pass the book as
//    the parent: the same text in UTF-8, font tags kept.
//
// BookMenu::OpenBookMenu: the menu gets the styled text whoever opens it (Grid Inventory
// opens it itself, with the parent's text).
//
// The item card asks for CNAM, not DESC: a letter's item card gets "A letter to X from Y."
// RELOCATION_ID(14399, 14552) is (SE id, AE id); VR reuses the SE id through the VR
// Address Library.
// SkyrimNet Physical Diaries hooks the same function; each answers only its own forms.
// ---------------------------------------------------------------------------

namespace
{
    // MinHook copes with other plugins hooking the same function.
    template <class F>
    void Hook(REL::RelocationID id, F thunk, F* original, std::string_view name, std::string_view otherwise)
    {
        auto* address = reinterpret_cast<void*>(REL::Relocation<std::uintptr_t>{ id }.address());
        auto status = MH_Initialize();
        if (status == MH_OK || status == MH_ERROR_ALREADY_INITIALIZED) {
            status = MH_CreateHook(address, reinterpret_cast<void*>(thunk), reinterpret_cast<void**>(original));
        }
        if (status == MH_OK) status = MH_EnableHook(address);
        if (status != MH_OK) {
            SKSE::log::error("{} hook failed ({}): {}", name, MH_StatusToString(status), otherwise);
            return;
        }
        SKSE::log::info("Installed {} hook", name);
    }

    struct GetDescriptionHook
    {
        using func_t = void (*)(RE::TESDescription*, RE::BSString&, RE::TESForm*, std::uint32_t);
        static inline func_t original{ nullptr };

        static void thunk(RE::TESDescription* a_self, RE::BSString& a_out, RE::TESForm* a_parent,
                          std::uint32_t a_fieldType)
        {
            // Only our letters' CNAM (item card) and DESC (text).  Cheap checks first: this
            // runs for every description.
            // CNAM: a letter's item card ("A letter to X from Y."), or the DM Letter's in the chosen language.
            if (a_fieldType == 'MANC') {
                try {
                    auto card = PhysicalLetters::Letters::CardFor(a_self);
                    if (card.empty()) card = PhysicalLetters::DmLetter::CardFor(a_self);
                    if (!card.empty()) {
                        a_out = card.c_str();
                        return;
                    }
                } catch (const std::exception& e) {
                    SKSE::log::error("[TextHook] Preparing a letter's item card failed: {}", e.what());
                }
            }
            if (a_fieldType == 'CSED') {
                try {
                    if (!a_parent) {
                        if (const auto letterId = PhysicalLetters::Letters::FindByDescription(a_self)) {
                            if (const auto text = PhysicalLetters::Letters::TextFor(letterId); !text.empty()) {
                                a_out = text.c_str();
                                return;
                            }
                        }
                    } else if (a_parent->GetFormType() == RE::FormType::Book) {
                        if (const auto text = PhysicalLetters::Letters::TextFor(a_parent->GetFormID()); !text.empty()) {
                            a_out = text.c_str();  // UTF-8 with its font tags (docs/ARCHITECTURE.md)
                            return;
                        }
                    }
                } catch (const std::exception& e) {
                    SKSE::log::error("[TextHook] Preparing letter text failed: {} — showing the book's own text", e.what());
                } catch (...) {
                    SKSE::log::error("[TextHook] Preparing letter text failed — showing the book's own text");
                }
            }
            original(a_self, a_out, a_parent, a_fieldType);
        }

        static void Install()
        {
            Hook(RELOCATION_ID(14399, 14552), &thunk, &original, "GetDescription", "letters will show the template's text");
        }
    };

    // BookMenu::OpenBookMenu: the menu shows the text it's given.  Grid Inventory opens books itself, with text it
    // asked for with the book as parent: a letter's gets its book-menu text here.
    struct OpenBookMenuHook
    {
        // Plus VR's ninth argument, the reference's 3D (VR reuses the SE id): forwarded on every runtime, else VR
        // reads a junk pointer for every book (SNPD docs/BOOK_TEXT.md, "VR's ninth argument").
        using func_t = void (*)(const RE::BSString&, const RE::ExtraDataList*, RE::TESObjectREFR*, RE::TESObjectBOOK*,
                                const RE::NiPoint3&, const RE::NiMatrix3&, float, bool, RE::NiAVObject*);
        static inline func_t original{ nullptr };

        static void thunk(const RE::BSString& a_description, const RE::ExtraDataList* a_extraList, RE::TESObjectREFR* a_ref,
                          RE::TESObjectBOOK* a_book, const RE::NiPoint3& a_pos, const RE::NiMatrix3& a_rot, float a_scale,
                          bool a_useDefaultPos, RE::NiAVObject* a_vrNode)
        {
            std::string styled;
            try {
                if (const auto text = a_book ? PhysicalLetters::Letters::TextFor(a_book->GetFormID()) : std::string{};
                    !text.empty()) {
                    styled = text;
                    SKSE::log::info("[TextHook] Opening letter 0x{:X} ({} bytes)", a_book->GetFormID(), styled.size());
                }
            } catch (const std::exception& e) {
                SKSE::log::error("[TextHook] Opening a letter failed: {} — showing the text it was given", e.what());
            } catch (...) {
                SKSE::log::error("[TextHook] Opening a letter failed — showing the text it was given");
            }
            if (!styled.empty()) {
                const RE::BSString text{ styled.c_str() };
                original(text, a_extraList, a_ref, a_book, a_pos, a_rot, a_scale, a_useDefaultPos, a_vrNode);
                return;
            }
            original(a_description, a_extraList, a_ref, a_book, a_pos, a_rot, a_scale, a_useDefaultPos, a_vrNode);
        }

        static void Install()
        {
            Hook(RELOCATION_ID(50122, 51053), &thunk, &original, "OpenBookMenu",
                 "no \"Opening letter\" lines in the log");
        }
    };

} // anonymous namespace

void PhysicalLetters::TextHook::Install()
{
    GetDescriptionHook::Install();
    OpenBookMenuHook::Install();
}
