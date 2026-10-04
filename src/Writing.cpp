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

#include "Writing.h"
#include "InkAndQuillAPI.h"
#include "LetterDB.h"
#include "Letters.h"
#include "MarkedText.h"
#include "Recipients.h"
#include "Session.h"
#include "SkyrimNet.h"
#include "Strings.h"
#include "TextHook.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

namespace PhysicalLetters::Writing {

    namespace {
        using MarkedText::Trim;
        using MarkedText::WithoutBlood;

        constexpr std::string_view kPlugin = "Physical Letters.esp";
        constexpr RE::FormID kParchment = 0x8B6;  // PhysicalLettersParchment
        constexpr RE::FormID kPlayer = 0x14;
        constexpr std::string_view kParchmentText = "<font face='$HandwrittenFont'></font>";  // its record's text

        const IQ_API* g_api = nullptr;

        // Ink & Quill holds at most one of our sessions; its `user` is the session's number, so a
        // callback from an older one is ignored.  0: none.
        std::uint32_t g_session = 0;
        std::uint32_t g_sessions = 0;
        RE::FormID g_book = 0;           // the letter written in; 0: parchment
        std::optional<Letter> g_letter;  // that letter, as it was
        bool g_bodyLocked = false;       // the "To:" line names nobody: only it can be written in
        std::string g_lockedBody;        // the body as written when it was locked, given back when it opens
        bool g_bodyLockedAtStart = false;  // a new letter: its body had no run when the session began
        bool g_bloodHeading = false;       // the letter's "To:" line is red (begun in blood)

        void* UserOf(std::uint32_t session) { return reinterpret_cast<void*>(static_cast<std::uintptr_t>(session)); }
        bool IsCurrent(void* user) { return g_session != 0 && user == UserOf(g_session); }

        void Notify(std::string_view text) { RE::SendHUDMessage::ShowHUDMessage(std::string{ text }.c_str()); }

        // The "To:" line's recipient.  An edit's line as it was loaded is its letter's recipient, by UUID,
        // whatever the game or SkyrimNet calls them now (docs/WRITING.md#the-recipient).
        std::variant<Recipients::Recipient, std::string> ResolveLine(const std::string& line)
        {
            if (g_letter) {
                const auto& l = *g_letter;
                const std::string loaded = l.address.empty() ? l.recipientName : l.recipientName + ", " + l.address;
                if (_stricmp(Trim(WithoutBlood(line)).c_str(), loaded.c_str()) == 0) {
                    return Recipients::Recipient{ .uuid = l.recipientUuid, .name = l.recipientName, .address = l.address };
                }
            }
            return Recipients::Resolve(line);
        }

        // ---- Ink & Quill's callbacks ----

        void Refuse(IQ_SaveReply* reply, std::string_view message)
        {
            g_api->ReplySave(reply, false, std::string{ message }.c_str(), nullptr);
        }

        // Run 0: the recipient's name (its first line); run 1: the body.  A save makes a new letter
        // record, the parchment's or the edited letter's (docs/WRITING.md#saving).
        void OnSave(void* user, const char* const* runs, std::int32_t count, IQ_SaveReply* reply)
        {
            if (!IsCurrent(user)) return;  // no answer: refused
            // One run: the body is still locked, waiting for a recipient.
            if ((count != 1 && count != 2) || !runs[0] || (count == 2 && !runs[1])) {
                SKSE::log::error("[Writing] {} runs to save, not 1 or 2", count);
                return Refuse(reply, Strings::kWriteFailed);
            }
            if (!Session::IsReady()) return Refuse(reply, Strings::kWriteNotReady);
            // Lines typed after the name (Enter keeps the caret in its run) begin the body.
            const std::string_view nameRun{ runs[0] };
            const auto lineEnd = nameRun.find('\n');
            const std::string line{ nameRun.substr(0, lineEnd) };
            // The recipient before the body: an unaddressed letter says so, not that it's empty.
            Recipients::NewText();
            auto found = ResolveLine(line);
            if (const auto* message = std::get_if<std::string>(&found)) return Refuse(reply, *message);
            const auto& recipient = std::get<Recipients::Recipient>(found);
            const std::string rest = lineEnd == std::string_view::npos ? std::string{} : Trim(nameRun.substr(lineEnd + 1));
            const std::string after = count == 2 ? Trim(runs[1]) : std::string{};
            const std::string marked = rest.empty() ? after : after.empty() ? rest : rest + "\n\n" + after;
            const std::string body = Trim(WithoutBlood(marked));
            SKSE::log::info("[Writing] Saving: \"{}\", body {} bytes ({} typed after the name)", line, body.size(), rest.size());
            if (body.empty()) return Refuse(reply, Strings::kWriteEmpty);
            const auto playerUuid = SkyrimNet::UuidForFormId(kPlayer);
            if (playerUuid.empty()) {
                SKSE::log::error("[Writing] No SkyrimNet UUID for the player");
                return Refuse(reply, Strings::kWriteFailed);
            }
            const std::string& recipientName = recipient.name;
            const std::string blood = marked != body ? marked : std::string{};

            if (g_letter && g_letter->recipientUuid == recipient.uuid && g_letter->address == recipient.address &&
                g_letter->body == body && g_letter->blood == blood) {
                // Only the name's spelling or spacing changed: the letter is as it was.
                g_api->ReplySave(reply, true, "", TextHook::ForBookMenu(Letters::Reading(*g_letter)).c_str());
                return;
            }
            auto* player = RE::PlayerCharacter::GetSingleton();
            const Letter letter{ .id = Letters::NewId(),
                                 .authorUuid = playerUuid,
                                 .authorName = player->GetName(),
                                 .recipientUuid = recipient.uuid,
                                 .recipientName = recipientName,
                                 .body = body,
                                 .writtenAt = RE::Calendar::GetSingleton()->GetDaysPassed(),
                                 .blood = blood,
                                 .address = recipient.address,
                                 .bloodHeading = g_bloodHeading };
            const std::string reading = TextHook::ForBookMenu(Letters::Reading(letter));
            if (g_book) {
                if (!Letters::Rewrite(RE::TESForm::LookupByID<RE::TESObjectBOOK>(g_book), letter)) {
                    return Refuse(reply, Strings::kWriteFailed);
                }
                g_letter = letter;
                SKSE::log::info("[Writing] Letter to {} rewritten", recipientName);
                g_api->ReplySave(reply, true, "", reading.c_str());
                return;
            }
            // Parchment: Ink & Quill takes one and shows the new letter in the open menu.
            auto* book = Letters::Create(letter);
            if (!book) return Refuse(reply, Strings::kWriteFailed);
            player->AddObjectToContainer(book, nullptr, 1, nullptr);
            g_book = book->GetFormID();
            g_letter = letter;
            SKSE::log::info("[Writing] Letter to {} written on parchment (0x{:X})", recipientName, g_book);
            g_api->ReplySaveAsBook(reply, g_book, reading.c_str());
        }

        void OnEnd(void* user)
        {
            if (!IsCurrent(user)) return;
            g_session = 0;
            g_book = 0;
            g_letter.reset();
        }

        // UTF-16 units, as the editor counts the caret's offset.
        int Utf16Length(const std::string& text)
        {
            return MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
        }

        // The player changed a run, in the key's own UI task: with the caret at the end of the "To:" line,
        // Ink & Quill's suggestions for it (Tab cycles, Right accepts).
        void OnChange(void* user, std::int32_t run, std::int32_t caretOffset)
        {
            if (!IsCurrent(user) || run != 0) return;
            std::vector<std::string> runs;
            const auto count = g_api->CurrentRuns(
                [](void* user, std::int32_t, const char* text) { static_cast<std::vector<std::string>*>(user)->emplace_back(text); },
                &runs);
            if ((count != 1 && count != 2) || runs.size() != static_cast<std::size_t>(count)) {
                SKSE::log::error("[Writing] {} runs for the name's suggestions, not 1 or 2", count);
                return;
            }
            // One line: a line break typed (Enter) or pasted is taken out before it's drawn; Enter on a line
            // that names someone goes on to the body.
            std::string line = runs[0];
            const bool broke = line.find_first_of("\r\n") != std::string::npos;
            std::erase(line, '\n');
            std::erase(line, '\r');
            // The body is open only while the line names one person: locked, its text is kept here.
            Recipients::NewText();
            const bool named = std::holds_alternative<Recipients::Recipient>(ResolveLine(line));
            const bool toBody = broke && named;
            if (broke || named == g_bodyLocked) {
                if (runs.size() == 2) g_lockedBody = runs[1];
                const Letter shown{ .authorUuid = SkyrimNet::UuidForFormId(kPlayer),
                                    .recipientName = line,
                                    .blood = g_lockedBody,
                                    .bloodHeading = g_bloodHeading };
                const std::string marked = Letters::Marked(shown, !named);
                const std::string reading = g_letter ? TextHook::ForBookMenu(Letters::Reading(*g_letter)) : std::string{ kParchmentText };
                // A reopened body counts as saved with its text at the session's start, if it had one: unchanged,
                // it's no save and no ink (Ink & Quill's from = -2 - k).
                const std::int32_t from[] = { 0, runs.size() == 2 ? 1 : g_bodyLockedAtStart ? -1 : -2 - 1 };
                if (!g_api->Reload(marked.c_str(), reading.c_str(), from, named ? 2 : 1, toBody ? 1 : 0,
                                   toBody ? 0 : (broke ? -1 : caretOffset))) {
                    SKSE::log::error("[Writing] Ink & Quill couldn't reload the letter's \"To:\" line");
                    return;
                }
                if (named == g_bodyLocked) {
                    SKSE::log::info("[Writing] \"{}\" {}: the body is {}", line, named ? "names a recipient" : "names nobody",
                                    named ? "open" : "locked");
                }
                g_bodyLocked = !named;
                if (toBody) return;
            }
            // The caret at the line's end (counted as Ink & Quill does, without blood markers): a completion
            // goes after it.
            std::vector<std::string> completions;
            if (broke || caretOffset == Utf16Length(WithoutBlood(line))) completions = Recipients::Completions(line);
            std::vector<const char*> texts;
            for (const auto& text : completions) texts.push_back(text.c_str());
            g_api->Suggest(texts.data(), static_cast<std::int32_t>(texts.size()));
        }

        // Ink & Quill's session over `shown`, the caret at the end of run `caretRun` (-1: the page being
        // read).
        bool Begin(const Letter& shown, int caretRun, RE::FormID book, std::optional<Letter> letter, bool bodyLocked = false)
        {
            Recipients::Reset();
            const std::string marked = Letters::Marked(shown, bodyLocked);
            g_bodyLocked = bodyLocked;
            g_bodyLockedAtStart = bodyLocked;
            g_bloodHeading = shown.bloodHeading;
            g_lockedBody.clear();
            const auto previous = std::exchange(g_session, ++g_sessions);
            const auto previousBook = std::exchange(g_book, book);
            auto previousLetter = std::exchange(g_letter, std::move(letter));
            IQ_Session session{};
            session.size = sizeof(IQ_Session);
            session.markedText = marked.c_str();
            session.caretRun = caretRun;
            session.user = UserOf(g_session);
            session.onSave = OnSave;
            session.onEnd = OnEnd;
            session.onChange = OnChange;
            const bool begun = g_api->BeginSession(&session);
            // Refused (its OnEnd has run): Ink & Quill still has the one before, if there was one.
            if (!begun && g_session == 0) {
                g_session = previous;
                g_book = previousBook;
                g_letter = std::move(previousLetter);
            }
            return begun;
        }

        // Parchment read from the player's inventory: a new letter, the caret after "To: ".
        bool OnParchmentOpen(void*, std::uint32_t)
        {
            if (!Session::IsReady()) {
                Notify(Strings::kWriteNotReady);
                return false;
            }
            // Blood is chosen once the session starts: the "To:" line is red if it will be (as Physical Diaries' headings).
            const Letter blank{ .authorUuid = SkyrimNet::UuidForFormId(kPlayer), .bloodHeading = g_api->WouldBeInBlood() };
            return Begin(blank, 0, 0, std::nullopt, true);
        }

        // The edit key on an open letter: ours if it's one the player wrote and carries (not sent).
        bool Owner(void*, std::uint32_t bookFormId)
        {
            const auto id = Letters::IdFor(bookFormId);
            if (id.empty() || !Session::IsReady()) return false;
            auto letter = LetterDB::GetSingleton()->Get(id);
            auto* player = RE::PlayerCharacter::GetSingleton();
            auto* book = RE::TESForm::LookupByID<RE::TESObjectBOOK>(bookFormId);
            const auto carried = book ? player->GetInventoryCounts([book](RE::TESBoundObject& item) { return &item == book; })
                                      : RE::TESObjectREFR::InventoryCountMap{};
            if (!letter || letter->authorUuid != SkyrimNet::UuidForFormId(kPlayer) || carried.empty()) {
                SKSE::log::info("[Writing] Letter {} isn't one the player wrote and carries: not editable", id);
                return false;
            }
            const Letter shown = *letter;
            Begin(shown, -1, bookFormId, std::move(letter));
            return true;
        }
    }

    void Connect()
    {
        auto* module = GetModuleHandleA("InkAndQuill.dll");
        const auto get = module ? reinterpret_cast<IQ_GetAPI_t>(GetProcAddress(module, "IQ_GetAPI")) : nullptr;
        g_api = get ? get(IQ_API_VERSION) : nullptr;
        if (g_api) {
            SKSE::log::info("[Writing] Ink & Quill found (API {})", g_api->version);
        } else {
            SKSE::log::info("[Writing] Ink & Quill {}: letters can't be written", module ? "is too old" : "isn't installed");
        }
    }

    void Register()
    {
        if (!g_api) return;
        if (!g_api->IsWritingOn()) {
            SKSE::log::info("[Writing] Ink & Quill's writing is off (its book.swf isn't loaded): letters can't be written");
            return;
        }
        g_api->SetClientName("Physical Letters");  // in Ink & Quill's MCM
        g_api->AddOwner(Owner, nullptr);
        auto* data = RE::TESDataHandler::GetSingleton();
        auto* parchment = data ? data->LookupForm<RE::TESObjectBOOK>(kParchment, kPlugin) : nullptr;
        if (!parchment || !g_api->RegisterBlank(parchment->GetFormID(), OnParchmentOpen, nullptr)) {
            SKSE::log::error("[Writing] Parchment couldn't be registered with Ink & Quill: no new letters can be written");
            return;
        }
        SKSE::log::info("[Writing] Registered with Ink & Quill: parchment and the player's letters can be written in");
    }

} // namespace PhysicalLetters::Writing
