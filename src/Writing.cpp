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
#include "Config.h"
#include "HandIn.h"
#include "InkAndQuillAPI.h"
#include "IqClient.h"
#include "LetterDB.h"
#include "Letters.h"
#include "MarkedText.h"
#include "Parchment.h"
#include "Recipients.h"
#include "Session.h"
#include "SkyrimNet.h"
#include "Strings.h"

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
        using MarkedText::TrimRight;
        using MarkedText::WithoutBlood;

        constexpr RE::FormID kPlayer = 0x14;
        constexpr std::string_view kParchmentText = "<font face='$HandwrittenFont'></font>";  // its record's text

        const IQ_API* g_api = nullptr;

        // Ink & Quill holds at most one of our sessions; its `user` is the session's number, so a
        // callback from an older one is ignored.  0: none.
        std::uint32_t g_session = 0;
        std::uint32_t g_sessions = 0;
        RE::FormID g_book = 0;           // the letter written in; 0: parchment
        std::optional<Letter> g_letter;  // that letter, as it was
        bool g_bodyLocked = false;       // the "To:" line names nobody: only the two lines can be written in
        std::string g_lockedBody;        // the body as written when it was locked, given back when it opens
        bool g_bodyLockedAtStart = false;  // a new letter: its body had no run when the session began
        bool g_bloodHeading = false;       // the letter's "From:" and "To:" lines are red (begun in blood)

        bool BloodAllowed() { return Config::GetSingleton()->Get(Config::kWritingBlood) != 0; }

        using IqClient::Notify;
        using IqClient::UserOf;
        bool IsCurrent(void* user) { return g_session != 0 && user == UserOf(g_session); }

        // An edit's name line as it was loaded: "name, address" (or the name), ignoring case and blood.
        bool AsLoaded(const std::string& line, const std::string& name, const std::string& address)
        {
            const std::string loaded = address.empty() ? name : name + ", " + address;
            return _stricmp(Trim(WithoutBlood(line)).c_str(), loaded.c_str()) == 0;
        }

        // The "To:" line's recipient.  An edit's line as it was loaded is its letter's recipient, by UUID,
        // whatever the game or SkyrimNet calls them now (docs/WRITING.md#the-recipient).
        std::variant<Recipients::Recipient, std::string> ResolveLine(const std::string& line)
        {
            if (g_letter && AsLoaded(line, g_letter->recipientName, g_letter->address)) {
                return Recipients::Recipient{ .uuid = g_letter->recipientUuid, .name = g_letter->recipientName, .address = g_letter->address };
            }
            return Recipients::Resolve(line);
        }

        // ---- Ink & Quill's callbacks ----

        void Refuse(IQ_SaveReply* reply, std::string_view message) { IqClient::Refuse(g_api, reply, message); }

        // The "From:" line's sender, besides the player.  An edit's line as it was loaded is its letter's sender, as the
        // "To:" line (docs/FORGERY.md#the-from-line).
        std::variant<Recipients::Recipient, Recipients::Signature, std::string> ResolveFrom(const std::string& line,
                                                                                            const std::string& recipientUuid)
        {
            if (g_letter && !g_letter->signedName.empty() && AsLoaded(line, g_letter->signedName, g_letter->signedAddress)) {
                const auto& l = *g_letter;
                if (l.signedUuid.empty()) return Recipients::Signature{ .name = l.signedName };
                if (l.signedUuid != recipientUuid) {
                    return Recipients::Recipient{ .uuid = l.signedUuid, .name = l.signedName, .address = l.signedAddress };
                }
            }
            return Recipients::ResolveSender(line, recipientUuid);
        }

        // A forgery passes for the named person's hand (docs/FORGERY.md#the-roll): 25% at Pickpocket 15, 90% at 100, in a
        // line between, held at the ends.  Always in Special Ink.
        int RollForgery(bool specialInk)
        {
            constexpr float kLowSkill = 15.0f, kHighSkill = 100.0f;
            constexpr float kLowChance = 25.0f, kHighChance = 90.0f;
            if (specialInk) {
                SKSE::log::info("[Writing] A forgery in Special Ink: it passes");
                return kForgeryPasses;
            }
            const float skill = RE::PlayerCharacter::GetSingleton()->AsActorValueOwner()->GetActorValue(RE::ActorValue::kPickpocket);
            const float chance = std::clamp(kLowChance + (skill - kLowSkill) * (kHighChance - kLowChance) / (kHighSkill - kLowSkill),
                                            kLowChance, kHighChance);
            static std::mt19937_64 rng{ std::random_device{}() };
            const bool passes = std::uniform_real_distribution<float>(0.0f, 100.0f)(rng) < chance;
            SKSE::log::info("[Writing] A forgery: Pickpocket {:.0f}, {:.0f}% to pass: {}", skill, chance, passes ? "passes" : "seen through");
            return passes ? kForgeryPasses : kForgerySeen;
        }

        // Runs: 0 the "From:" line, 1 the "To:" line, 2 the body (none while it's locked).  A save makes a new letter
        // record, the parchment's or the edited letter's (docs/WRITING.md#saving).
        void OnSave(void* user, const char* const* runs, std::int32_t count, IQ_SaveReply* reply)
        {
            if (!IsCurrent(user)) return;  // no answer: refused
            // Two runs: the body is still locked, waiting for a recipient.
            if ((count != 2 && count != 3) || !runs[0] || !runs[1] || (count == 3 && !runs[2])) {
                SKSE::log::error("[Writing] {} runs to save, not 2 or 3", count);
                return Refuse(reply, Strings::kWriteFailed);
            }
            if (!Session::IsReady()) return Refuse(reply, Strings::kWriteNotReady);
            // Lines typed after the name (Enter keeps the caret in its run) begin the body.
            const std::string_view nameRun{ runs[1] };
            const auto lineEnd = nameRun.find('\n');
            const std::string line{ nameRun.substr(0, lineEnd) };
            // The recipient before the body: an unaddressed letter says so, not that it's empty.
            Recipients::NewText();
            auto found = ResolveLine(line);
            if (const auto* message = std::get_if<std::string>(&found)) return Refuse(reply, *message);
            const auto& recipient = std::get<Recipients::Recipient>(found);
            // The sender: the player by their exact name, else whom the line names (docs/FORGERY.md#the-from-line).
            auto* player = RE::PlayerCharacter::GetSingleton();
            const std::string_view fromRun{ runs[0] };
            const std::string fromLine = Trim(WithoutBlood(std::string{ fromRun.substr(0, fromRun.find('\n')) }));
            std::string signedUuid, signedName, signedAddress;
            bool anonymous = false;
            // Their name as the line was filled in: a page writes typographic marks plain.
            if (fromLine != Trim(Letters::AsWritten(player->GetName()))) {
                auto sender = ResolveFrom(fromLine, recipient.uuid);
                if (const auto* message = std::get_if<std::string>(&sender)) return Refuse(reply, *message);
                if (const auto* person = std::get_if<Recipients::Recipient>(&sender)) {
                    signedUuid = person->uuid;
                    signedName = person->name;
                    signedAddress = person->address;
                } else {
                    signedName = std::get<Recipients::Signature>(sender).name;
                    anonymous = signedName.empty();
                }
            }
            const std::string rest = lineEnd == std::string_view::npos ? std::string{} : Trim(nameRun.substr(lineEnd + 1));
            // Only the end is trimmed: blank lines the player puts before the first paragraph are kept.
            const std::string after = count == 3 ? TrimRight(runs[2]) : std::string{};
            const std::string marked = rest.empty() ? after : after.empty() ? rest : rest + "\n\n" + after;
            const std::string body = TrimRight(WithoutBlood(marked));
            SKSE::log::info("[Writing] Saving: \"{}\" from \"{}\", body {} bytes ({} typed after the name)", line, fromLine,
                            body.size(), rest.size());
            if (Trim(body).empty()) return Refuse(reply, Strings::kWriteEmpty);
            const auto playerUuid = SkyrimNet::UuidForFormId(kPlayer);
            if (playerUuid.empty()) {
                SKSE::log::error("[Writing] No SkyrimNet UUID for the player");
                return Refuse(reply, Strings::kWriteFailed);
            }
            const std::string& recipientName = recipient.name;
            const std::string blood = marked != body ? marked : std::string{};
            const bool specialInk = g_api->InSpecialInk();

            if (g_letter && g_letter->recipientUuid == recipient.uuid && g_letter->address == recipient.address &&
                g_letter->body == body && g_letter->blood == blood && g_letter->signedUuid == signedUuid &&
                g_letter->signedName == signedName && g_letter->signedAddress == signedAddress && g_letter->anonymous == anonymous) {
                // Only the names' spelling or spacing changed: the letter is as it was.
                g_api->ReplySave(reply, true, "", Letters::Reading(*g_letter).c_str());
                return;
            }
            const Letter letter{ .id = Letters::NewId(),
                                 .authorUuid = playerUuid,
                                 .authorName = player->GetName(),
                                 .recipientUuid = recipient.uuid,
                                 .recipientName = recipientName,
                                 .body = body,
                                 .writtenAt = RE::Calendar::GetSingleton()->GetDaysPassed(),
                                 .blood = blood,
                                 .address = recipient.address,
                                 .bloodHeading = g_bloodHeading,
                                 .signedUuid = signedUuid,
                                 .signedName = signedName,
                                 .signedAddress = signedAddress,
                                 .forgery = signedUuid.empty() ? kForgeryNone : RollForgery(specialInk),
                                 .anonymous = anonymous };
            const std::string reading = Letters::Reading(letter);
            if (g_book) {
                if (!Letters::Rewrite(RE::TESForm::LookupByID<RE::TESObjectBOOK>(g_book), letter)) {
                    return Refuse(reply, Strings::kWriteFailed);
                }
                g_letter = letter;
                SKSE::log::info("[Writing] Letter to {} rewritten", recipientName);
                // Its recipient may have changed (the item didn't move: no container event).
                try {
                    HandIn::RefreshRecipients();
                } catch (const std::exception& e) {
                    SKSE::log::error("[Writing] Refreshing the hand-in recipients failed: {}", e.what());
                }
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

        // Ink & Quill's text again with these two lines, the body open or locked (its text kept here while locked), the
        // caret at `caretRun` and `caretOffset` (-1: its end).
        bool ReloadLines(const std::string& fromLine, const std::string& toLine, const std::vector<std::string>& runs, bool open,
                         std::int32_t caretRun, std::int32_t caretOffset)
        {
            if (runs.size() == 3) g_lockedBody = runs[2];
            const Letter shown{ .authorUuid = SkyrimNet::UuidForFormId(kPlayer),
                                .authorName = fromLine,
                                .recipientName = toLine,
                                .blood = g_lockedBody,
                                .bloodHeading = g_bloodHeading };
            const std::string marked = Letters::Marked(shown, !open);
            const std::string reading = g_letter ? Letters::Reading(*g_letter) : std::string{ kParchmentText };
            // A reopened body counts as saved with its text at the session's start, if it had one: unchanged, it's no save
            // and no ink (Ink & Quill's from = -2 - k).
            const std::int32_t from[] = { 0, 1, runs.size() == 3 ? 2 : g_bodyLockedAtStart ? -1 : -2 - 2 };
            if (!g_api->Reload(marked.c_str(), reading.c_str(), from, open ? 3 : 2, caretRun, caretOffset)) {
                SKSE::log::error("[Writing] Ink & Quill couldn't reload the letter's first lines");
                return false;
            }
            return true;
        }

        // The player changed a run, in the key's own UI task: the "From:" line (run 0) or the "To:" line (run 1), each one
        // line with Ink & Quill's suggestions (Tab cycles, Right accepts).
        void OnChange(void* user, std::int32_t run, std::int32_t caretOffset)
        {
            if (!IsCurrent(user) || (run != 0 && run != 1)) return;
            const auto runs = IqClient::CurrentRuns(g_api);
            if (runs.size() != 2 && runs.size() != 3) {
                SKSE::log::error("[Writing] {} runs for the names' suggestions, not 2 or 3", runs.size());
                return;
            }
            // One line: a line break typed (Enter) or pasted is taken out before it's drawn.
            std::string line = runs[run];
            const bool broke = IqClient::TakeLineBreaks(line);
            Recipients::NewText();
            if (run == 0) {
                // Enter on the "From:" line goes on to the "To:" line.
                if (broke) {
                    ReloadLines(line, runs[1], runs, !g_bodyLocked, 1, -1);
                    return;
                }
                Suggest(line, caretOffset, false);
                return;
            }
            // The body is open only while the "To:" line names one person; Enter on a line that does goes on to it.
            const bool named = std::holds_alternative<Recipients::Recipient>(ResolveLine(line));
            const bool toBody = broke && named;
            if (broke || named == g_bodyLocked) {
                if (!ReloadLines(runs[0], line, runs, named, toBody ? 2 : 1, toBody ? 0 : (broke ? -1 : caretOffset))) return;
                if (named == g_bodyLocked) {
                    SKSE::log::info("[Writing] \"{}\" {}: the body is {}", line, named ? "names a recipient" : "names nobody",
                                    named ? "open" : "locked");
                }
                g_bodyLocked = !named;
                if (toBody) return;
            }
            Suggest(line, caretOffset, broke);
        }

        // Ink & Quill's session over `shown`, the caret at the end of run `caretRun` (-1: the page being
        // read), written the `way` chosen (IQ_WRITE_ASK: Ink & Quill decides).
        bool Begin(const Letter& shown, int caretRun, RE::FormID book, std::optional<Letter> letter, bool bodyLocked = false,
                   std::int32_t way = IQ_WRITE_ASK)
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
            // Typed text's format, so a new paragraph gets the size the rendered page gives it.
            session.runFont = Letters::FontFace();
            session.runSize = Config::GetSingleton()->Get(Config::kFontSize);
            session.caretRun = caretRun;
            session.user = UserOf(g_session);
            session.onSave = OnSave;
            session.onEnd = OnEnd;
            session.onChange = OnChange;
            session.way = way;
            session.noBlood = !BloodAllowed();  // with ASK (an edit): no ink means Ink & Quill's notice, not its blood prompt
            const bool begun = g_api->BeginSession(&session);
            // Refused (its OnEnd has run): Ink & Quill still has the one before, if there was one.
            if (!begun && g_session == 0) {
                g_session = previous;
                g_book = previousBook;
                g_letter = std::move(previousLetter);
            }
            return begun;
        }

        // A new letter on parchment, written the way chosen.  A blood one's "From:" and "To:" lines are red
        // (as Physical Diaries' headings).
        bool BeginParchment(std::int32_t way)
        {
            // "From:" is the player's name, theirs to change (docs/FORGERY.md#the-from-line); the caret starts on "To:".
            const Letter blank{ .authorUuid = SkyrimNet::UuidForFormId(kPlayer),
                                .authorName = RE::PlayerCharacter::GetSingleton()->GetName(),
                                .bloodHeading = way == IQ_WRITE_BLOOD };
            return Begin(blank, 1, 0, std::nullopt, true, way);
        }

        // The way-of-writing box's answer: begins if the parchment is still open and nobody is writing.
        class WayChoice : public RE::IMessageBoxCallback {
        public:
            WayChoice(std::vector<std::int32_t> ways, RE::FormID parchment) : ways_(std::move(ways)), parchment_(parchment) {}

            void Run(std::uint8_t a_button) override
            {
                const auto button = static_cast<std::size_t>(a_button);
                if (button >= ways_.size()) {
                    SKSE::log::info("[Writing] The parchment's box: cancelled");
                    return;
                }
                auto* ui = RE::UI::GetSingleton();
                auto* open = ui && ui->IsMenuOpen(RE::BookMenu::MENU_NAME) ? RE::BookMenu::GetTargetForm() : nullptr;
                if (!open || open->GetFormID() != parchment_ || !Session::IsReady()) {
                    SKSE::log::info("[Writing] The parchment's box: answered after the parchment closed");
                    return;
                }
                SKSE::log::info("[Writing] The parchment's box: way {}", ways_[button]);
                BeginParchment(ways_[button]);
            }

        private:
            std::vector<std::int32_t> ways_;
            RE::FormID parchment_;
        };

        // Parchment read from the player's inventory.  How it's written is the player's choice when there's one: Special
        // Ink with the Quill of Gemination, and ink or (without ink) blood; with ink alone it begins at once.
        bool OnParchmentOpen(void*, std::uint32_t parchment)
        {
            if (!Session::IsReady()) {
                Notify(Strings::kWriteNotReady);
                return false;
            }
            // The edit key on the parchment while its box is open: no second box.  Asked of the UI, not a flag, which a
            // box closed unanswered (a load) would leave set.
            if (auto* ui = RE::UI::GetSingleton(); ui && ui->IsMenuOpen(RE::MessageBoxMenu::MENU_NAME)) return false;
            const bool special = g_api->HasQuillOfGemination() && g_api->HasSpecialInk();
            // No ink where Ink & Quill needs it (materials required, a quill): blood instead, if allowed (Writing.Blood).
            const bool noInk = g_api->WouldBeInBlood();
            const std::int32_t plain = noInk && BloodAllowed() ? IQ_WRITE_BLOOD : IQ_WRITE_INK;
            const bool plainUsable = !noInk || (plain == IQ_WRITE_BLOOD && g_api->CanBleed());
            // Nothing to choose between: Ink & Quill checks the materials and shows its notice if one is missing.
            if (!special && (plain == IQ_WRITE_INK || !plainUsable)) return BeginParchment(plain);
            std::vector<std::int32_t> ways;
            std::vector<std::string_view> labels;
            if (special) {
                ways.push_back(IQ_WRITE_SPECIAL_INK);
                labels.push_back(Strings::kWriteSpecialInk);
            }
            if (plainUsable) {
                ways.push_back(plain);
                labels.push_back(plain == IQ_WRITE_BLOOD ? Strings::kWriteBlood : Strings::kWriteInk);
            }
            labels.push_back(Strings::kWriteCancel);
            auto* data = RE::UIMessageDataFactory::Create<RE::MessageBoxData>();
            if (!data) return BeginParchment(plain);
            data->bodyText = std::string{ Strings::kWriteHow }.c_str();
            for (const auto label : labels) data->buttonText.push_back(std::string{ label }.c_str());
            data->cancelButtonIndex = static_cast<std::int32_t>(labels.size() - 1);  // Escape
            data->callback = RE::BSTSmartPointer<RE::IMessageBoxCallback>(new WayChoice(std::move(ways), parchment));
            RE::MessageBoxMenu::QueueMessage(data);
            SKSE::log::info("[Writing] The parchment's box: {} way(s) offered", labels.size() - 1);
            return true;
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

    bool Available()
    {
        return g_api && g_api->IsWritingOn();
    }

    const IQ_API* Api()
    {
        return g_api;
    }

    void Suggest(const std::string& line, std::int32_t caretOffset, bool force)
    {
        std::vector<std::string> completions;
        if (force || caretOffset == Utf16Length(WithoutBlood(line))) completions = Recipients::Completions(line);
        std::vector<const char*> texts;
        for (const auto& text : completions) texts.push_back(text.c_str());
        g_api->Suggest(texts.data(), static_cast<std::int32_t>(texts.size()));
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
        auto* parchment = Parchment::Form();
        if (!parchment || !g_api->RegisterBlank(parchment->GetFormID(), OnParchmentOpen, nullptr)) {
            SKSE::log::error("[Writing] Parchment couldn't be registered with Ink & Quill: no new letters can be written");
            return;
        }
        SKSE::log::info("[Writing] Registered with Ink & Quill: parchment and the player's letters can be written in");
    }

} // namespace PhysicalLetters::Writing
