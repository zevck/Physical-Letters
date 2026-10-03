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

#include "Letters.h"
#include "DynamicForms.h"
#include "MarkedText.h"
#include "SkyrimNet.h"
#include "Strings.h"

#include <random>

namespace PhysicalLetters::Letters {

    namespace {
        // WIDBAssassinLetter: a plain letter (Note01 model), no script.
        constexpr RE::FormID kTemplateId = 0x10596A;
        constexpr std::string_view kTemplatePlugin = "Skyrim.esm";
        constexpr RE::FormID kOutgoingKeyword = 0x000800;  // PhysicalLettersOutgoingLetter
        constexpr RE::FormID kHandInKeyword = 0x0008B3;    // PhysicalLettersHandInLetter
        constexpr std::string_view kPlugin = "Physical Letters.esp";
        constexpr RE::FormID kPlayer = 0x14;

        constexpr std::string_view kPageBreak = "[pagebreak]";
        constexpr std::string_view kFont = "<font face='$HandwrittenFont'>";
        using MarkedText::kBloodClose;
        using MarkedText::kBloodOpen;
        using MarkedText::kLockClose;
        using MarkedText::kLockOpen;
        constexpr std::string_view kBloodFont = "<font color='#2B0202'>";  // blood's colour as Ink & Quill's editor shows it

        struct Entry {
            std::string id;
            std::string text;  // rendered book markup
            std::string card;  // the item card's description; "" until LetterDB is open
            const RE::TESDescription* description = nullptr;
            const RE::TESDescription* cardDescription = nullptr;
        };

        std::mutex g_mutex;
        std::unordered_map<RE::FormID, Entry> g_letters;
        std::unordered_map<std::string, Returned> g_returned;  // by letter id

        RE::TESObjectBOOK* Template()
        {
            auto* data = RE::TESDataHandler::GetSingleton();
            return data ? data->LookupForm<RE::TESObjectBOOK>(kTemplateId, kTemplatePlugin) : nullptr;
        }

        void ReplaceAll(std::string& text, std::string_view from, std::string_view to)
        {
            for (std::size_t at = 0; (at = text.find(from, at)) != std::string::npos; at += to.size()) {
                text.replace(at, from.size(), to);
            }
        }

        std::string Escape(std::string_view text)
        {
            std::string out;
            out.reserve(text.size());
            for (const char c : text) {
                switch (c) {
                case '&': out += "&amp;"; break;
                case '<': out += "&lt;"; break;
                case '>': out += "&gt;"; break;
                case '\r': break;
                default: out += c; break;
                }
            }
            return out;
        }

        // The handwriting fonts lack these (as SNPD's SanitizeBookText); plain forms also keep
        // Cyrillic text single-byte for the book menu (TextHook).  No page break or lock.
        std::string Plain(std::string_view body)
        {
            static constexpr std::pair<std::string_view, std::string_view> kPlainForms[] = {
                { "\xE2\x80\x94", "-" },    // em dash
                { "\xE2\x80\x93", "-" },    // en dash
                { "\xE2\x80\x9C", "\"" },   // left double quote
                { "\xE2\x80\x9D", "\"" },   // right double quote
                { "\xE2\x80\x98", "'" },    // left single quote
                { "\xE2\x80\x99", "'" },    // right single quote
                { "\xE2\x80\xA6", "..." },  // ellipsis
            };
            std::string plain{ body };
            for (const auto& [from, to] : kPlainForms) ReplaceAll(plain, from, to);
            // A literal page break would split the note.
            ReplaceAll(plain, kPageBreak, "[page break]");
            ReplaceAll(plain, kLockOpen, "");
            ReplaceAll(plain, kLockClose, "");
            return plain;
        }

        // Escaped text with its blood markers as red font tags, closed at the paragraph's end;
        // `inBlood` carries over to the next paragraph.
        std::string Redden(std::string_view text, bool& inBlood)
        {
            std::string out{ inBlood ? kBloodFont : "" };
            for (std::size_t i = 0; i < text.size();) {
                if (text.substr(i, kBloodOpen.size()) == kBloodOpen) {
                    if (!std::exchange(inBlood, true)) out += kBloodFont;
                    i += kBloodOpen.size();
                } else if (text.substr(i, kBloodClose.size()) == kBloodClose) {
                    if (std::exchange(inBlood, false)) out += "</font>";
                    i += kBloodClose.size();
                } else {
                    out += text[i++];
                }
            }
            if (inBlood) out += "</font>";
            return out;
        }

        // Book markup in the vanilla letters' handwriting.  Skyrim resets the font after a blank
        // line, so every paragraph gets its own tag.  `marked`: blood markers kept, for the editor.
        std::string Paragraphs(std::string_view body, bool marked)
        {
            const std::string plain = Plain(body);
            const std::string_view text = plain;
            std::string out;
            bool inBlood = false;
            for (std::size_t start = 0; start <= text.size();) {
                auto end = text.find("\n\n", start);
                if (end == std::string_view::npos) end = text.size();
                if (!out.empty()) out += "\n\n";
                const std::string paragraph = Escape(text.substr(start, end - start));
                out += std::string{ kFont } + (marked ? paragraph : Redden(paragraph, inBlood)) + "</font>";
                start = end + 2;
            }
            return out;
        }

        bool IsPlayers(const Letter& letter)
        {
            return !letter.authorUuid.empty() && letter.authorUuid == SkyrimNet::UuidForFormId(kPlayer);
        }

        // The player's letters open with "To: <recipient>" (docs/WRITING.md#the-text).  `marked`: Ink & Quill's
        // marked text; `bodyLocked`: the body is locked too, so the "To:" line is the only run.
        std::string Render(const Letter& letter, bool marked, bool bodyLocked = false)
        {
            const auto lock = [marked](std::string_view text) {
                return marked ? std::format("{}{}{}", kLockOpen, text, kLockClose) : std::string{ text };
            };
            const std::string body = Paragraphs(letter.blood.empty() ? letter.body : letter.blood, marked);
            if (!IsPlayers(letter)) return body + (marked ? lock("") : std::string{});
            // The name run is the name and, on a saved letter, its address.  Typed text takes the format of
            // the text before it (an empty run's too): the break before the body is in the handwriting.
            const std::string to = letter.address.empty() ? letter.recipientName
                                                          : std::format("{}, {}", letter.recipientName, letter.address);
            std::string out = std::string{ kFont } + lock(Strings::kToLabel) + Escape(Plain(to));
            if (marked && bodyLocked) return out + lock("\n\n</font>" + body);
            // An empty last lock: an empty body is still a run (text after the last lock is a run
            // only if there is any).
            return out + lock("\n\n</font>") + body + (marked ? lock("") : std::string{});
        }

        // A factory-made form has no look: without the template's world model and bounds
        // a dropped letter has no 3D and vanishes.  Its script, item card and text aren't copied.
        void Configure(RE::TESObjectBOOK* book, const RE::TESObjectBOOK* templateBook, const std::string& name)
        {
            if (templateBook) {
                book->data.type = templateBook->data.type;
                book->inventoryModel = templateBook->inventoryModel;
                if (const char* model = templateBook->GetModel(); model && *model) book->SetModel(model);
                book->boundData = templateBook->boundData;
                book->pickupSound = templateBook->pickupSound;
                book->putdownSound = templateBook->putdownSound;
                book->weight = templateBook->weight;
                templateBook->ForEachKeyword([book](RE::BGSKeyword* keyword) {
                    if (keyword && !book->HasKeyword(keyword)) book->AddKeyword(keyword);
                    return RE::BSContainer::ForEachResult::kContinue;
                });
            }
            book->value = 0;
            book->data.flags = static_cast<RE::OBJ_BOOK::Flag>(0);
            book->SetFullName(name.c_str());
            // Any letter can be handed over: the hand-in topic's condition and gift menu look for it.
            auto* data = RE::TESDataHandler::GetSingleton();
            if (auto* keyword = data ? data->LookupForm<RE::BGSKeyword>(kHandInKeyword, kPlugin) : nullptr) {
                if (!book->HasKeyword(keyword)) book->AddKeyword(keyword);
            }
        }

        // The player's own letters carry PhysicalLettersOutgoingLetter, which the postage
        // topic's gift menu filters on (docs/DELIVERY.md#the-hand-over).  Set every session:
        // the save keeps only a runtime form's flags, and a form can hold another letter
        // after loading another save.
        void MarkOutgoing(RE::TESObjectBOOK* book, const Letter& letter)
        {
            auto* data = RE::TESDataHandler::GetSingleton();
            auto* keyword = data ? data->LookupForm<RE::BGSKeyword>(kOutgoingKeyword, kPlugin) : nullptr;
            if (!keyword) return;
            if (letter.authorUuid == SkyrimNet::UuidForFormId(kPlayer)) {
                book->AddKeyword(keyword);
            } else {
                book->RemoveKeyword(keyword);
            }
        }

        void SetEntry(RE::TESObjectBOOK* book, std::string id, std::string text, std::string card = {})
        {
            std::lock_guard lock{ g_mutex };
            g_letters.insert_or_assign(book->GetFormID(), Entry{ .id = std::move(id),
                                                                 .text = std::move(text),
                                                                 .card = std::move(card),
                                                                 .description = static_cast<RE::TESDescription*>(book),
                                                                 .cardDescription = &book->itemCardDescription });
        }
    }

    bool CheckTemplate()
    {
        if (Template()) return true;
        SKSE::log::error("[Letters] The template letter {}:0x{:X} wasn't found: letters will have no model",
                         kTemplatePlugin, kTemplateId);
        return false;
    }

    std::string NewId()
    {
        static std::mt19937_64 rng{ std::random_device{}() };
        return std::format("{:016x}{:016x}", rng(), rng());
    }

    RE::TESObjectBOOK* Create(const Letter& letter)
    {
        auto* book = DynamicForms::Create<RE::TESObjectBOOK>();
        if (!book) {
            SKSE::log::error("[Letters] Couldn't create a form for letter {}", letter.id);
            return nullptr;
        }
        const bool toPlayer = letter.recipientUuid == SkyrimNet::UuidForFormId(0x14);
        const auto name = toPlayer ? Strings::LetterFromName(letter.authorName) : Strings::LetterName(letter.recipientName);
        Configure(book, Template(), name);
        DynamicForms::Track({ .formId = book->GetFormID(), .formType = RE::FormType::Book, .key = letter.id, .displayName = name });
        SetEntry(book, letter.id, Render(letter, false), Strings::LetterCard(letter.recipientName, letter.authorName));
        MarkOutgoing(book, letter);
        if (!LetterDB::GetSingleton()->Insert(letter)) {
            SKSE::log::error("[Letters] Letter {} wasn't stored: its text is lost after a reload", letter.id);
        }
        SKSE::log::info("[Letters] Created '{}' (0x{:X}, letter {})", name, book->GetFormID(), letter.id);
        return book;
    }

    std::string Reading(const Letter& letter)
    {
        return Render(letter, false);
    }

    std::string Marked(const Letter& letter, bool bodyLocked)
    {
        return Render(letter, true, bodyLocked);
    }

    bool Rewrite(RE::TESObjectBOOK* book, const Letter& letter)
    {
        if (!book || !LetterDB::GetSingleton()->Insert(letter)) {
            SKSE::log::error("[Letters] Letter {} wasn't stored: the letter is unchanged", letter.id);
            return false;
        }
        const auto name = Strings::LetterName(letter.recipientName);
        book->SetFullName(name.c_str());
        DynamicForms::Track({ .formId = book->GetFormID(), .formType = RE::FormType::Book, .key = letter.id, .displayName = name });
        const auto oldId = IdFor(book->GetFormID());
        SetEntry(book, letter.id, Render(letter, false), Strings::LetterCard(letter.recipientName, letter.authorName));
        MarkOutgoing(book, letter);
        SetReturned(oldId, std::nullopt);
        SKSE::log::info("[Letters] '{}' (0x{:X}) is letter {} now (was {})", name, book->GetFormID(), letter.id, oldId);
        return true;
    }

    void ConfigureLoaded()
    {
        auto* templateBook = Template();
        int count = 0;
        for (const auto& record : DynamicForms::Tracked()) {
            auto* book = RE::TESForm::LookupByID<RE::TESObjectBOOK>(record.formId);
            if (!book) continue;
            Configure(book, templateBook, record.displayName);
            SetEntry(book, record.key, Paragraphs(Strings::kLetterPending, false));
            ++count;
        }
        SKSE::log::info("[Letters] Configured {} letter(s) from this save", count);
    }

    void AttachTexts()
    {
        auto* db = LetterDB::GetSingleton();
        int attached = 0, missing = 0;
        for (const auto& record : DynamicForms::Tracked()) {
            auto* book = RE::TESForm::LookupByID<RE::TESObjectBOOK>(record.formId);
            if (!book) continue;
            if (const auto letter = db->Get(record.key)) {
                SetEntry(book, record.key, Render(*letter, false), Strings::LetterCard(letter->recipientName, letter->authorName));
                MarkOutgoing(book, *letter);
                ++attached;
            } else {
                SKSE::log::warn("[Letters] No stored text for letter {} (0x{:X})", record.key, record.formId);
                SetEntry(book, record.key, Paragraphs(Strings::kLetterUnreadable, false));
                ++missing;
            }
        }
        SKSE::log::info("[Letters] Attached the text of {} letter(s), {} missing", attached, missing);
    }

    void Revert()
    {
        std::lock_guard lock{ g_mutex };
        g_letters.clear();
        g_returned.clear();
    }

    RE::FormID FindByDescription(const RE::TESDescription* description)
    {
        std::lock_guard lock{ g_mutex };
        for (const auto& [formId, entry] : g_letters) {
            if (entry.description == description) return formId;
        }
        return 0;
    }

    std::string CardFor(const RE::TESDescription* description)
    {
        std::lock_guard lock{ g_mutex };
        for (const auto& [formId, entry] : g_letters) {
            if (entry.cardDescription != description) continue;
            const auto returned = g_returned.find(entry.id);
            if (entry.card.empty() || returned == g_returned.end()) return entry.card;
            return std::format("{}\n{}", entry.card, Strings::ReturnToSender(returned->second == Returned::kDead));
        }
        return {};
    }

    void SetReturned(const std::string& letterId, std::optional<Returned> reason)
    {
        std::lock_guard lock{ g_mutex };
        if (reason) {
            g_returned.insert_or_assign(letterId, *reason);
        } else {
            g_returned.erase(letterId);
        }
    }

    std::vector<std::pair<std::string, Returned>> ReturnedLetters()
    {
        std::lock_guard lock{ g_mutex };
        return { g_returned.begin(), g_returned.end() };
    }

    std::string TextFor(RE::FormID formId)
    {
        std::lock_guard lock{ g_mutex };
        const auto it = g_letters.find(formId);
        return it != g_letters.end() ? it->second.text : std::string{};
    }

    std::string IdFor(RE::FormID formId)
    {
        std::lock_guard lock{ g_mutex };
        const auto it = g_letters.find(formId);
        return it != g_letters.end() ? it->second.id : std::string{};
    }

    RE::FormID FormFor(const std::string& letterId)
    {
        std::lock_guard lock{ g_mutex };
        for (const auto& [formId, entry] : g_letters) {
            if (entry.id == letterId) return formId;
        }
        return 0;
    }

} // namespace PhysicalLetters::Letters
