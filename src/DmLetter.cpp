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

#include "DmLetter.h"
#include "Config.h"
#include "GameTime.h"
#include "IqClient.h"
#include "Letters.h"
#include "LlmJson.h"
#include "MarkedText.h"
#include "NpcToNpc.h"
#include "Parchment.h"
#include "Reading.h"
#include "Recipients.h"
#include "Session.h"
#include "SkyrimNet.h"
#include "Strings.h"
#include "Transit.h"
#include "Writing.h"

#include <charconv>
#include <cstring>

namespace PhysicalLetters::DmLetter {

    namespace {
        using json = nlohmann::json;
        using IqClient::Notify;
        using IqClient::UserOf;
        using MarkedText::Trim;
        using MarkedText::TrimRight;
        using MarkedText::WithoutBlood;

        constexpr auto kPrompt = "physical_letters\\dm_letter";
        constexpr auto kScript = "PhysicalLetters_MCM";
        // The page's runs (docs/DM_LETTER.md#the-sheet): these three, then the boxes there are, then the text.
        constexpr std::int32_t kFrom = 0, kTo = 1, kHours = 2, kFirstBox = 3;
        constexpr int kMaxHours = 9999;
        constexpr std::size_t kMaxHourDigits = 4;  // kMaxHours's
        constexpr RE::FormID kPlayer = 0x14;
        // SkyrimNet drops cancelled LLM tasks without calling back: a writer this long without an answer has failed.
        constexpr auto kAnswerTimeout = std::chrono::minutes(5);

        // Every letter starts on a blank page, with no writer yet: nothing is kept after a send.
        const Letters::Sheet kBlank{ .hours = "0", .transformLocked = true };
        // Ink & Quill holds at most one of our sessions; its `user` is its number.  0: none.
        std::uint32_t g_session = 0;
        std::uint32_t g_sessions = 0;
        bool g_rewriteShown = false;  // the Transform box as the open page shows it
        bool g_forgeryShown = false;  // the Forgery box

        // Which boxes a page has: both while "From:" names a writer, Forgery alone once it's ticked, none without one.
        struct Layout {
            bool transform = false;
            bool forgery = false;

            std::int32_t TransformRun() const { return transform ? kFirstBox : -1; }
            std::int32_t ForgeryRun() const { return forgery ? kFirstBox + (transform ? 1 : 0) : -1; }
            std::int32_t TextRun() const { return kFirstBox + (transform ? 1 : 0) + (forgery ? 1 : 0); }
            std::int32_t Count() const { return TextRun() + 1; }
        };
        Layout LayoutOf(const Letters::Sheet& sheet) { return { .transform = !sheet.transformLocked && !sheet.forgery, .forgery = !sheet.transformLocked }; }
        Layout g_layout;  // the open page's

        bool IsCurrent(void* user) { return g_session != 0 && user == UserOf(g_session); }

        // A person on the page, resolved.
        struct Party {
            std::string uuid;
            std::string name;
            std::string address;
            RE::FormID formId = 0;
        };

        // Who the "From:" line says the letter is from: a person who writes it, a name that's nobody's, or nobody.
        struct From {
            std::optional<Party> writer;
            std::string signature;
            bool anonymous = false;
            bool forged = false;  // in the writer's name, by nobody they know (docs/DM_LETTER.md#sending)

            std::string Which() const { return Strings::DmWhichLetter(writer ? writer->name : "", signature, anonymous, forged); }
        };

        // A writer asked for their letter (Transform, or no text), until the answer or kAnswerTimeout.
        struct Asked {
            std::uint64_t id = 0;
            std::uint32_t generation = 0;
            std::chrono::steady_clock::time_point since;
            std::string which;
            std::string recipient;
            std::string notes;  // logged if it fails: the page was cleared at the save
        };
        std::vector<Asked> g_asked;
        std::uint64_t g_lastAsk = 0;

        // Its item card, set once at kDataLoaded: the text before the pointer, which the hook reads from any thread.
        std::string g_cardText;
        std::atomic<const RE::TESDescription*> g_card{ nullptr };

        // The asked letter `id`, taken off the list; nothing if it was given up (timed out, a load).
        std::optional<Asked> TakeAsked(std::uint64_t id)
        {
            const auto it = std::ranges::find(g_asked, id, &Asked::id);
            if (it == g_asked.end()) return std::nullopt;
            auto asked = *it;
            g_asked.erase(it);
            return asked;
        }

        std::string FirstLine(std::string_view run) { return Trim(WithoutBlood(std::string{ run.substr(0, run.find('\n')) })); }

        Party PartyOf(const Recipients::Recipient& person)
        {
            const RE::FormID formId = person.actor ? person.actor->GetFormID() : SkyrimNet::FormIdForUuid(person.uuid);
            return { .uuid = person.uuid, .name = person.name, .address = person.address, .formId = formId };
        }

        bool IsPlayersName(const std::string& line)
        {
            return _stricmp(line.c_str(), Trim(Letters::AsWritten(RE::PlayerCharacter::GetSingleton()->GetName())).c_str()) == 0;
        }

        // The "From:" line (docs/DM_LETTER.md#the-sheet): empty is an unsigned letter, a name that's nobody's a signature,
        // a person (as a letter's "From:" line resolves them) the writer.  Or why none: the player, a dead person, ...
        std::variant<From, std::string> ResolveFrom(const std::string& line, const std::string& recipientUuid)
        {
            if (line.empty()) return From{ .anonymous = true };
            if (IsPlayersName(line)) return std::string{ Strings::kDmFromPlayer };
            auto sender = Recipients::ResolveSender(line, recipientUuid);
            if (const auto* message = std::get_if<std::string>(&sender)) return *message;
            if (const auto* person = std::get_if<Recipients::Recipient>(&sender)) return From{ .writer = PartyOf(*person) };
            return From{ .signature = std::get<Recipients::Signature>(sender).name };
        }

        // The "To:" line: the player by their name (a letter to them), else a person as a letter's "To:" line resolves them.
        std::variant<Party, std::string> ResolveTo(const std::string& line)
        {
            if (IsPlayersName(line)) {
                return Party{ .uuid = SkyrimNet::UuidForFormId(kPlayer), .name = RE::PlayerCharacter::GetSingleton()->GetName(),
                              .formId = kPlayer };
            }
            auto to = Recipients::Resolve(line);
            if (const auto* message = std::get_if<std::string>(&to)) return *message;
            return PartyOf(std::get<Recipients::Recipient>(to));
        }

        bool NamesWriter(const std::string& line)
        {
            const auto from = ResolveFrom(Trim(line), {});
            return std::holds_alternative<From>(from) && std::get<From>(from).writer.has_value();
        }

        // The open page again as `sheet` (its boxes as `LayoutOf` says), the caret in `caretRun` at `caretOffset` (-1: its end).
        void Reload(const Letters::Sheet& sheet, std::int32_t caretRun, std::int32_t caretOffset)
        {
            const std::string marked = Letters::SheetText(sheet, true);
            const std::string reading = Letters::SheetText(kBlank, false);
            // Each new run is the run it was (Ink & Quill's from); a box that wasn't there is new (-1).
            const Layout next = LayoutOf(sheet);
            std::vector<std::int32_t> from{ kFrom, kTo, kHours };
            if (next.transform) from.push_back(g_layout.TransformRun());
            if (next.forgery) from.push_back(g_layout.ForgeryRun());
            from.push_back(g_layout.TextRun());
            if (!Writing::Api()->Reload(marked.c_str(), reading.c_str(), from.data(), static_cast<std::int32_t>(from.size()), caretRun,
                               caretOffset)) {
                SKSE::log::error("[DmLetter] Ink & Quill couldn't reload the page");
                return;
            }
            g_layout = next;
        }

        // ---- Sending ----

        // The letter goes out: written, unsigned, signed or forged, and nobody remembers writing the last three
        // (docs/DM_LETTER.md#sending).  False: not made.
        bool Dispatch(const From& from, const Party& recipient, int hours, const std::string& body, const json& answer)
        {
            const bool forged = from.forged;
            const bool written = from.writer && !forged;
            const Letter letter{ .id = Letters::NewId(),
                                 .authorUuid = written ? from.writer->uuid : std::string{},
                                 .authorName = written ? from.writer->name : std::string{},
                                 .recipientUuid = recipient.uuid,
                                 .recipientName = recipient.name,
                                 .body = body,
                                 .writtenAt = GameTime::Now(),
                                 .signedUuid = forged ? from.writer->uuid : std::string{},
                                 .signedName = forged ? from.writer->name : from.signature,
                                 .signedAddress = forged ? from.writer->address : std::string{},
                                 .forgery = forged ? kForgeryPasses : kForgeryNone,
                                 .anonymous = from.anonymous,
                                 .dispatched = true };
            auto* book = Letters::Create(letter);
            if (!book) return false;
            if (written) NpcToNpc::RememberWriting(letter, from.writer->formId, recipient.formId, answer);
            if (recipient.formId != kPlayer) {
                Transit::QueueToNpc(letter, hours);
                SKSE::log::info("[DmLetter] {} to {}: letter {}, due in {} game hours", from.Which(), recipient.name, letter.id, hours);
                Notify(Strings::DmSent(from.Which(), recipient.name, hours));
            } else if (hours == 0) {
                // To the player: the courier brings it, unless it's due now.
                RE::PlayerCharacter::GetSingleton()->AddObjectToContainer(book, nullptr, 1, nullptr);
                SKSE::log::info("[DmLetter] {} to the player: letter {}, in their inventory", from.Which(), letter.id);
                Notify(Strings::DmInInventory(from.Which()));
            } else {
                Transit::QueueToPlayer(letter, hours);
                SKSE::log::info("[DmLetter] {} to the player: letter {}, to the courier in {} game hours", from.Which(), letter.id, hours);
                Notify(Strings::DmToCourier(from.Which(), hours));
            }
            return true;
        }

        // Game thread.  The writer's letter from the notes, if it's still awaited.
        void OnWritten(std::uint64_t askId, const Party& writer, const Party& recipient, int hours, const std::string& response,
                       bool success)
        {
            const From from{ .writer = writer };
            const auto asked = TakeAsked(askId);
            if (!asked || asked->generation != Session::Generation()) {
                SKSE::log::info("[DmLetter] {} to {} came after it was given up (a load, or too late): dropped", from.Which(),
                                recipient.name);
                return;
            }
            const auto answer = success ? LlmJson::ParseResponse(response) : json{};
            const auto text = answer.is_object() ? Trim(LlmJson::GetString(answer, "letter")) : std::string{};
            if (text.empty()) {
                SKSE::log::error("[DmLetter] {} to {} wasn't written ({}): {}\nThe notes were:\n{}", from.Which(), recipient.name,
                                 success ? "no letter in the answer" : "the call failed", response.substr(0, 300), asked->notes);
                Notify(Strings::DmFailed(from.Which(), recipient.name));
                return;
            }
            if (!Dispatch(from, recipient, hours, text, answer)) Notify(Strings::DmFailed(from.Which(), recipient.name));
        }

        // One call on the default model: the writer writes the letter from the notes, in their own voice.
        void Rewrite(const Party& writer, const Party& recipient, int hours, const std::string& notes)
        {
            const std::uint64_t askId = ++g_lastAsk;
            g_asked.push_back({ .id = askId,
                                .generation = Session::Generation(),
                                .since = std::chrono::steady_clock::now(),
                                .which = From{ .writer = writer }.Which(),
                                .recipient = recipient.name,
                                .notes = notes });
            std::thread([askId, writer, recipient, hours, notes, now = GameTime::Now(),
                         memoryCount = Config::GetSingleton()->Get(Config::kN2nMemories)]() {
                try {
                    const json context = {
                        { "npc", { { "UUID", SkyrimNet::UuidNumber(writer.uuid) }, { "name", writer.name } } },
                        { "recipient_UUID", SkyrimNet::UuidNumber(recipient.uuid) },
                        { "notes", notes },
                        { "memories", NpcToNpc::Memories(writer.formId, memoryCount) },
                        { "correspondence", Reading::Correspondence(writer.uuid, recipient.uuid, writer.formId, now) },
                    };
                    const bool queued = SkyrimNet::SendPrompt(kPrompt, context.dump(), [=](std::string response, bool success) {
                        SKSE::GetTaskInterface()->AddTask([=, response = std::move(response)]() {
                            // An exception must not cross into the engine.
                            try {
                                OnWritten(askId, writer, recipient, hours, response, success);
                            } catch (const std::exception& e) {
                                SKSE::log::error("[DmLetter] Sending {}'s letter failed: {}", writer.name, e.what());
                            }
                        });
                    });
                    if (!queued) throw std::runtime_error("SkyrimNet refused the prompt");
                    SKSE::log::info("[DmLetter] Asking {} to write to {}", writer.name, recipient.name);
                } catch (const std::exception& e) {
                    SKSE::log::error("[DmLetter] Asking {} for the letter failed: {}", writer.name, e.what());
                    SKSE::GetTaskInterface()->AddTask([askId]() {
                        try {
                            if (const auto asked = TakeAsked(askId)) {
                                SKSE::log::error("[DmLetter] The notes were:\n{}", asked->notes);
                                Notify(Strings::DmFailed(asked->which, asked->recipient));
                            }
                        } catch (const std::exception& e) {
                            SKSE::log::error("[DmLetter] Reporting a failed letter failed: {}", e.what());
                        }
                    });
                }
            }).detach();
        }

        // ---- Ink & Quill's callbacks ----

        void Refuse(IQ_SaveReply* reply, std::string_view message) { IqClient::Refuse(Writing::Api(), reply, message); }

        // The page sends its letter (docs/DM_LETTER.md#sending); the DM Letter stays, ready for the next one.
        void OnSave(void* user, const char* const* runs, std::int32_t count, IQ_SaveReply* reply)
        {
            if (!IsCurrent(user)) return;  // no answer: refused
            const Layout layout{ .transform = count == Layout{ true, true }.Count(), .forgery = count > Layout{}.Count() };
            if (count != layout.Count() || count > Layout{ true, true }.Count() ||
                std::any_of(runs, runs + count, [](const char* run) { return !run; })) {
                SKSE::log::error("[DmLetter] {} runs to save, not 4 to 6", count);
                return Refuse(reply, Strings::kWriteFailed);
            }
            if (!Session::IsReady()) return Refuse(reply, Strings::kWriteNotReady);
            const std::int32_t textRun = layout.TextRun();
            Recipients::NewText();
            auto to = ResolveTo(FirstLine(runs[kTo]));
            if (const auto* message = std::get_if<std::string>(&to)) return Refuse(reply, *message);
            const Party recipient = std::get<Party>(to);
            auto resolved = ResolveFrom(FirstLine(runs[kFrom]), recipient.uuid);
            if (const auto* message = std::get_if<std::string>(&resolved)) return Refuse(reply, *message);
            From from = std::get<From>(resolved);
            // The boxes' runs are empty (their marks are locked): ticked is what the page shows.
            from.forged = from.writer && layout.forgery && g_forgeryShown;
            // No text: the writer writes about whatever they would; with no writer, or a forgery, nobody is asked.
            const std::string text = Trim(WithoutBlood(runs[textRun])).empty() ? std::string{} : TrimRight(WithoutBlood(runs[textRun]));
            if ((!from.writer || from.forged) && text.empty()) return Refuse(reply, Strings::kWriteEmpty);
            if (recipient.uuid.empty() || recipient.formId == 0 || (from.writer && from.writer->formId == 0)) {
                SKSE::log::error("[DmLetter] Can't send: {} has UUID '{}', FormID 0x{:X}; writer {}, FormID 0x{:X}", recipient.name,
                                 recipient.uuid, recipient.formId, from.writer ? from.writer->name : "none",
                                 from.writer ? from.writer->formId : 0);
                return Refuse(reply, Strings::kWriteFailed);
            }
            const std::string hoursText = FirstLine(runs[kHours]);
            int hours = 0;
            std::from_chars(hoursText.data(), hoursText.data() + hoursText.size(), hours);
            hours = std::clamp(hours, 0, kMaxHours);
            const bool rewrite = from.writer && !from.forged && (text.empty() || (layout.transform && g_rewriteShown));
            SKSE::log::info("[DmLetter] {} to {}, {} game hours, {} ({} bytes)", from.Which(), recipient.name, hours,
                            text.empty() ? "the writer's choice" : rewrite ? "transformed" : "as written", text.size());

            if (rewrite) {
                Notify(Strings::DmWriting(from.writer->name, recipient.name));
                Rewrite(*from.writer, recipient, hours, text);
            } else if (!Dispatch(from, recipient, hours, text, json::object())) {
                return Refuse(reply, Strings::kWriteFailed);  // the text stays on the page
            }
            Writing::Api()->ReplySave(reply, true, "", Letters::SheetText(kBlank, false).c_str());
        }

        void OnEnd(void* user)
        {
            if (IsCurrent(user)) g_session = 0;
        }

        // The player typed: name lines suggest, hours keep digits, a box flips, "From:" opens or locks them; Enter goes on
        // to the next line (docs/DM_LETTER.md#the-sheet).
        void OnChange(void* user, std::int32_t run, std::int32_t caretOffset)
        {
            if (!IsCurrent(user) || run == g_layout.TextRun()) return;
            auto runs = IqClient::CurrentRuns(Writing::Api());
            if (runs.size() != static_cast<std::size_t>(g_layout.Count())) {
                SKSE::log::error("[DmLetter] {} runs on the page, not {}", runs.size(), g_layout.Count());
                return;
            }
            Letters::Sheet sheet{ .from = runs[kFrom],
                                  .to = runs[kTo],
                                  .hours = runs[kHours],
                                  .rewrite = g_rewriteShown,
                                  .text = runs[g_layout.TextRun()],
                                  .transformLocked = !g_layout.forgery,
                                  .forgery = g_forgeryShown };
            if (run == g_layout.TransformRun()) {
                sheet.rewrite = g_rewriteShown = !g_rewriteShown;
                Reload(sheet, run, 0);
                return;
            }
            if (run == g_layout.ForgeryRun()) {
                // Ticked, it takes Transform's box away: a forgery goes out as written.
                sheet.forgery = g_forgeryShown = !g_forgeryShown;
                if (sheet.forgery) sheet.rewrite = g_rewriteShown = false;
                Reload(sheet, LayoutOf(sheet).ForgeryRun(), 0);
                return;
            }
            std::string& line = run == kFrom ? sheet.from : run == kTo ? sheet.to : sheet.hours;
            const bool broke = IqClient::TakeLineBreaks(line);
            if (run == kHours) {
                const auto digits = line.size();
                std::erase_if(line, [](char c) { return c < '0' || c > '9'; });
                if (line.size() > kMaxHourDigits) line.resize(kMaxHourDigits);
                if (broke || line.size() != digits) Reload(sheet, broke ? g_layout.TextRun() : kHours, broke ? 0 : -1);
                return;
            }
            Recipients::NewText();
            bool reload = broke;
            if (run == kFrom) {
                // A writer, or none: the Transform line opens or locks (and clears).
                if (const bool writer = NamesWriter(sheet.from); writer == sheet.transformLocked) {
                    sheet.transformLocked = !writer;
                    if (!writer) {
                        sheet.rewrite = g_rewriteShown = false;
                        sheet.forgery = g_forgeryShown = false;
                    }
                    reload = true;
                }
            }
            if (reload) {
                Reload(sheet, broke ? run + 1 : run, broke ? -1 : caretOffset);
                if (broke) return;
            }
            Writing::Suggest(line, caretOffset, false);
        }

        bool Begin()
        {
            Recipients::Reset();
            g_session = ++g_sessions;
            g_rewriteShown = kBlank.rewrite;
            g_forgeryShown = kBlank.forgery;
            g_layout = LayoutOf(kBlank);
            const std::string marked = Letters::SheetText(kBlank, true);
            IQ_Session session{};
            session.size = sizeof(IQ_Session);
            session.markedText = marked.c_str();
            session.runFont = Letters::FontFace();
            session.runSize = Config::GetSingleton()->Get(Config::kFontSize);
            session.caretRun = kFrom;
            session.user = UserOf(g_session);
            session.onSave = OnSave;
            session.onEnd = OnEnd;
            session.onChange = OnChange;
            session.way = IQ_WRITE_FREE;
            return Writing::Api()->BeginSession(&session);
        }

        // The DM Letter read from the player's inventory: writing begins, no materials needed.
        bool OnOpen(void*, std::uint32_t)
        {
            if (!Session::IsReady()) {
                Notify(Strings::kWriteNotReady);
                return false;
            }
            return Begin();
        }

        // 0 added, 1 already carried, 2 can't be used (the log says why).
        std::int32_t AddDmLetter(RE::StaticFunctionTag*)
        {
            auto* form = Form();
            auto* player = RE::PlayerCharacter::GetSingleton();
            if (!form || !player || !Writing::Available()) {
                SKSE::log::warn("[DmLetter] Can't be given: {}", !form ? "PhysicalLettersDMLetter isn't in Physical Letters.esp"
                                                                       : "Ink & Quill isn't installed, too old or its writing is off");
                return 2;
            }
            if (player->GetInventoryCounts([form](RE::TESBoundObject& item) { return &item == form; })[form] > 0) return 1;
            player->AddObjectToContainer(form, nullptr, 1, nullptr);
            SKSE::log::info("[DmLetter] Given to the player");
            return 0;
        }
    }

    RE::TESObjectBOOK* Form()
    {
        auto* data = RE::TESDataHandler::GetSingleton();
        return data ? data->LookupForm<RE::TESObjectBOOK>(kFormId, Parchment::kPlugin) : nullptr;
    }

    std::string CardFor(const RE::TESDescription* description)
    {
        return description && description == g_card.load(std::memory_order_acquire) ? g_cardText : std::string{};
    }

    void Register()
    {
        auto* form = Form();
        if (!form) {
            SKSE::log::error("[DmLetter] PhysicalLettersDMLetter not found");
            return;
        }
        form->SetFullName(Strings::DmLetterName().c_str());  // the ESP's name and card are English
        g_cardText = Strings::DmLetterCard();
        g_card.store(&form->itemCardDescription, std::memory_order_release);
        if (!Writing::Available()) {
            SKSE::log::info("[DmLetter] Ink & Quill isn't available: the DM Letter can't be used");
            return;
        }
        if (!Writing::Api()->RegisterBlank(form->GetFormID(), OnOpen, nullptr)) {
            SKSE::log::error("[DmLetter] Ink & Quill didn't register the DM Letter");
            return;
        }
        SKSE::log::info("[DmLetter] Registered with Ink & Quill");
    }

    void Tick()
    {
        const auto now = std::chrono::steady_clock::now();
        std::erase_if(g_asked, [now](const Asked& asked) {
            if (asked.generation != Session::Generation()) return true;  // asked before a load: its answer is dropped
            if (now - asked.since < kAnswerTimeout) return false;
            SKSE::log::warn("[DmLetter] No answer for {} to {} after {} minutes: given up.  The notes were:\n{}", asked.which,
                            asked.recipient, std::chrono::duration_cast<std::chrono::minutes>(kAnswerTimeout).count(), asked.notes);
            Notify(Strings::DmFailed(asked.which, asked.recipient));
            return true;
        });
    }

    bool RegisterFunctions(RE::BSScript::IVirtualMachine* vm)
    {
        vm->RegisterFunction("AddDmLetter", kScript, AddDmLetter);
        return true;
    }

} // namespace PhysicalLetters::DmLetter
