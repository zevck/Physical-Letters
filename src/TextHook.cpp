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
#include "Letters.h"

#include <MinHook.h>

// ---------------------------------------------------------------------------
// TESDescription::GetDescription hook (the helpers below are copied unchanged from
// SkyrimNet Physical Diaries' src/BookTextHook.cpp as of its commit dd20705; see its
// docs/BOOK_TEXT.md).
//
// Everything that shows a book's text asks the form for its DESC field, so one hook
// covers every reader of our letters:
//
//  - The book menu passes the book's description component with no parent: the
//    styled text (Win-1251 for Cyrillic).  The component is matched by identity.
//  - Everything else (SkyrimNet's book-read event, Immersive Reading on VR) passes the
//    book as the parent: UTF-8, font tags stripped.
//
// The item card asks for CNAM, not DESC: a letter's item card gets "A letter to X from Y."
// RELOCATION_ID(14399, 14552) is (SE id, AE id); VR reuses the SE id through the VR
// Address Library.
// SkyrimNet Physical Diaries hooks the same function; each answers only its own forms.
// ---------------------------------------------------------------------------

namespace
{
    // ── UTF-8 → Windows-1251 for Cyrillic ────────────────────────────
    // Scaleform's book pagination mixes byte and character offsets, so 2-byte
    // UTF-8 Cyrillic overlaps progressively; Win-1251 is one byte per character.
    // See SkyrimNet Physical Diaries' docs/BOOK_TEXT.md.

    // True if the text contains Cyrillic letters (U+0400–U+04FF: UTF-8 lead bytes
    // 0xD0–0xD3).  Only such text is converted to Win-1251; converting other text
    // turned French guillemets into bytes that aren't valid UTF-8.
    static bool HasCyrillic(const std::string& text) {
        for (std::size_t i = 0; i + 1 < text.size(); ++i) {
            const auto b = static_cast<unsigned char>(text[i]);
            if (b >= 0xD0 && b <= 0xD3 && (static_cast<unsigned char>(text[i + 1]) & 0xC0) == 0x80) return true;
        }
        return false;
    }

    // Converts Cyrillic and the Win-1251 punctuation to Win-1251; anything else
    // stays UTF-8 (best effort).
    static std::string Utf8ToWin1251(const std::string& utf8) {
        std::string out;
        out.reserve(utf8.size());  // Will be smaller (2-byte → 1-byte)

        const unsigned char* p = reinterpret_cast<const unsigned char*>(utf8.c_str());
        const unsigned char* end = p + utf8.size();

        while (p < end) {
            if (*p < 0x80) {
                // ASCII pass-through (includes HTML tags, [pagebreak], \n)
                out += static_cast<char>(*p++);
            } else if ((*p & 0xE0) == 0xC0 && p + 1 < end && (*(p+1) & 0xC0) == 0x80) {
                // 2-byte UTF-8 sequence → decode codepoint
                uint32_t cp = (static_cast<uint32_t>(*p & 0x1F) << 6)
                            | static_cast<uint32_t>(*(p+1) & 0x3F);
                p += 2;

                // А-я (U+0410-U+044F) are contiguous in Win-1251 (0xC0-0xFF); the rest
                // are Ё/ё, Ukrainian, Belarusian, Serbian and Macedonian letters and « ».
                char mapped = 0;
                if (cp >= 0x0410 && cp <= 0x044F) {
                    mapped = static_cast<char>(cp - 0x0410 + 0xC0);
                } else {
                    switch (cp) {
                    case 0x0401: mapped = static_cast<char>(0xA8); break;  // Ё
                    case 0x0451: mapped = static_cast<char>(0xB8); break;  // ё
                    case 0x0404: mapped = static_cast<char>(0xAA); break;  // Є
                    case 0x0406: mapped = static_cast<char>(0xB2); break;  // І
                    case 0x0407: mapped = static_cast<char>(0xAF); break;  // Ї
                    case 0x0454: mapped = static_cast<char>(0xBA); break;  // є
                    case 0x0456: mapped = static_cast<char>(0xB3); break;  // і
                    case 0x0457: mapped = static_cast<char>(0xBF); break;  // ї
                    case 0x0490: mapped = static_cast<char>(0xA5); break;  // Ґ
                    case 0x0491: mapped = static_cast<char>(0xB4); break;  // ґ
                    case 0x040E: mapped = static_cast<char>(0xA1); break;  // Ў (Belarusian)
                    case 0x045E: mapped = static_cast<char>(0xA2); break;  // ў (Belarusian)
                    case 0x0402: mapped = static_cast<char>(0x80); break;  // Ђ (Serbian)
                    case 0x0452: mapped = static_cast<char>(0x90); break;  // ђ (Serbian)
                    case 0x0409: mapped = static_cast<char>(0x8A); break;  // Љ (Serbian, Macedonian)
                    case 0x0459: mapped = static_cast<char>(0x9A); break;  // љ (Serbian, Macedonian)
                    case 0x040A: mapped = static_cast<char>(0x8C); break;  // Њ (Serbian, Macedonian)
                    case 0x045A: mapped = static_cast<char>(0x9C); break;  // њ (Serbian, Macedonian)
                    case 0x040B: mapped = static_cast<char>(0x8D); break;  // Ћ (Serbian)
                    case 0x045B: mapped = static_cast<char>(0x9D); break;  // ћ (Serbian)
                    case 0x040F: mapped = static_cast<char>(0x8F); break;  // Џ (Serbian, Macedonian)
                    case 0x045F: mapped = static_cast<char>(0x9F); break;  // џ (Serbian, Macedonian)
                    case 0x0403: mapped = static_cast<char>(0x81); break;  // Ѓ (Macedonian)
                    case 0x0453: mapped = static_cast<char>(0x83); break;  // ѓ (Macedonian)
                    case 0x040C: mapped = static_cast<char>(0x8E); break;  // Ќ (Macedonian)
                    case 0x045C: mapped = static_cast<char>(0x9E); break;  // ќ (Macedonian)
                    case 0x0405: mapped = static_cast<char>(0xBD); break;  // Ѕ (Macedonian)
                    case 0x0455: mapped = static_cast<char>(0xBE); break;  // ѕ (Macedonian)
                    case 0x0408: mapped = static_cast<char>(0xA3); break;  // Ј (Serbian, Macedonian)
                    case 0x0458: mapped = static_cast<char>(0xBC); break;  // ј (Serbian, Macedonian)
                    case 0x00AB: mapped = static_cast<char>(0xAB); break;  // «
                    case 0x00BB: mapped = static_cast<char>(0xBB); break;  // »
                    default: break;
                    }
                }
                if (mapped) {
                    out += mapped;
                } else {
                    // Unmapped 2-byte char: pass through as UTF-8 bytes
                    out += static_cast<char>(*(p-2));
                    out += static_cast<char>(*(p-1));
                }
            } else if ((*p & 0xF0) == 0xE0 && p + 2 < end) {
                // 3-byte UTF-8: the Win-1251 punctuation Letters::Render doesn't
                // replace, else pass through unchanged (multi-byte text desyncs
                // pagination, so map what Win-1251 has).
                const uint32_t cp = (static_cast<uint32_t>(*p & 0x0F) << 12)
                                  | (static_cast<uint32_t>(*(p+1) & 0x3F) << 6)
                                  | static_cast<uint32_t>(*(p+2) & 0x3F);
                char mapped = 0;
                switch (cp) {
                case 0x201E: mapped = static_cast<char>(0x84); break;  // „
                case 0x201A: mapped = static_cast<char>(0x82); break;  // ‚
                case 0x2022: mapped = static_cast<char>(0x95); break;  // •
                case 0x2039: mapped = static_cast<char>(0x8B); break;  // ‹
                case 0x203A: mapped = static_cast<char>(0x9B); break;  // ›
                case 0x2116: mapped = static_cast<char>(0xB9); break;  // №
                default: break;
                }
                if (mapped) {
                    out += mapped;
                    p += 3;
                } else {
                    out += static_cast<char>(*p++);
                    out += static_cast<char>(*p++);
                    out += static_cast<char>(*p++);
                }
            } else if ((*p & 0xF8) == 0xF0 && p + 3 < end) {
                // 4-byte UTF-8: pass through unchanged
                out += static_cast<char>(*p++);
                out += static_cast<char>(*p++);
                out += static_cast<char>(*p++);
                out += static_cast<char>(*p++);
            } else {
                // Invalid sequence: skip byte
                out += '?';
                p++;
            }
        }
        return out;
    }

    // The rendered text without its <font> tags.  Other readers (SkyrimNet's prompt,
    // Immersive Reading) get markup close to a vanilla book's; the book menu still
    // gets the styled text.
    std::string StripFontTags(const std::string& text) {
        std::string out;
        out.reserve(text.size());
        for (std::size_t i = 0; i < text.size();) {
            if (text.compare(i, 6, "<font ") == 0 || text.compare(i, 7, "</font>") == 0) {
                const std::size_t close = text.find('>', i);
                if (close == std::string::npos) break;
                i = close + 1;
            } else {
                out += text[i++];
            }
        }
        return out;
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
            // CNAM: a letter's item card ("A letter to X from Y.").
            if (a_fieldType == 'MANC') {
                try {
                    if (const auto card = PhysicalLetters::Letters::CardFor(a_self); !card.empty()) {
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
                                a_out = (HasCyrillic(text) ? Utf8ToWin1251(text) : text).c_str();
                                return;
                            }
                        }
                    } else if (a_parent->GetFormType() == RE::FormType::Book) {
                        if (const auto text = PhysicalLetters::Letters::TextFor(a_parent->GetFormID()); !text.empty()) {
                            a_out = StripFontTags(text).c_str();
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

        // MinHook copes with other plugins hooking the same function.
        static void Install()
        {
            REL::Relocation<std::uintptr_t> target{ RELOCATION_ID(14399, 14552) };
            auto* address = reinterpret_cast<void*>(target.address());
            auto status = MH_Initialize();
            if (status == MH_OK || status == MH_ERROR_ALREADY_INITIALIZED) {
                status = MH_CreateHook(address, reinterpret_cast<void*>(&thunk), reinterpret_cast<void**>(&original));
            }
            if (status == MH_OK) status = MH_EnableHook(address);
            if (status != MH_OK) {
                SKSE::log::error("GetDescription hook failed ({}): letters will show the template's text",
                                 MH_StatusToString(status));
                return;
            }
            SKSE::log::info("Installed GetDescription hook (RELOCATION_ID 14399/14552)");
        }
    };

} // anonymous namespace

void PhysicalLetters::TextHook::Install()
{
    GetDescriptionHook::Install();
}
