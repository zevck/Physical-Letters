# Architecture

## The flow

1. **A letter is created** (for now by the F6 dev key): `Letters::Create` makes a runtime book form, records the letter in LetterDB, and the player gets it.
2. **It is sent**: given to an innkeeper or the courier through the postage dialogue (or the F7 dev key); `Transit::Send` takes it from whoever holds it and queues it, due after the travel time to the recipient ([DELIVERY.md](DELIVERY.md)).
3. **It is delivered** when due: `Transit::Tick` puts it in the recipient's inventory. The parcel stays in the queue as *awaiting reading*.
4. **The recipient reads it**: `Reading::Read` sends one prompt to the LLM through SkyrimNet and stores the NPC's memory of the letter. Only then does the parcel leave the queue. See [READING.md](READING.md).
5. **If they reply**, the same step creates the reply letter and queues it to the player. When due (writing time plus travel), it goes to the vanilla courier, who brings it to the player in a town.

Separately, every few days an NPC the player has dealt with may **write first**: `NpcLetters` picks one, the LLM decides whether they write, and the letter takes the same road to the courier ([NPC_LETTERS.md](NPC_LETTERS.md)).

If the recipient is dead when the letter is due, or can't be found for `ReturnAfterDays`, the letter turns back at step 3 and reaches the player through the courier ([DELIVERY.md](DELIVERY.md#undeliverable-letters)).

## Components

| Component | Files | Does |
|---|---|---|
| Entry point | `src/main.cpp` | Log, the INI, SKSE messages (the postage price on a new game or load), the heartbeat |
| Session | `src/Session.cpp` | When letters may be touched after a load or new game |
| SkyrimNet client | `src/SkyrimNet.cpp`, `include/SkyrimNet/PublicAPI.h` (vendored) | SkyrimNet's public API, resolved at run time; requires v11 |
| Letters | `src/Letters.cpp` | Letter forms, their look, their rendered text (a thread-safe snapshot) |
| LetterDB | `src/LetterDB.cpp` | SQLite store of each letter's text, per SkyrimNet save folder |
| Transit | `src/Transit.cpp` | The queue: delivery, the reading owed (with retries), replies, NPC letters and undeliverable letters to the courier |
| Reading | `src/Reading.cpp`, `include/LlmJson.h` | The LLM call and the SkyrimNet memory; the correspondence history; reading the LLM's JSON |
| NpcLetters | `src/NpcLetters.cpp` | NPCs writing to the player first: the schedule, the pick, the prompt, cooldowns |
| Travel | `src/Travel.cpp` | How long a letter travels (the engine's fast-travel formula); areas and distances (`Area`, `Distance`) |
| Courier | `src/Courier.cpp` | Hands a letter to the vanilla courier (`WICourierScript`) |
| Postage | `src/Postage.cpp` | The hand-over: a letter given to an innkeeper or the courier in the postage topic's gift menu |
| TextHook | `src/TextHook.cpp` | `GetDescription` hook serving each letter's text and item card |
| DynamicForms | `src/DynamicForms.cpp` | Runtime forms the engine saves itself (shared with SNPD) |
| Serialization | `src/Serialization.cpp`, `include/CoSave.h` | The co-save records |
| Config | `include/Config.h` | The INI settings ([SETTINGS.md](SETTINGS.md)) |
| Papyrus | `src/Papyrus.cpp` | The MCM's natives |
| Strings | `include/Strings.h` | Every piece of text the player sees (English only for now) |
| DebugKeys | `src/DebugKeys.cpp` | F6, F7, F8 until the editor exists (F7 sends without the hand-over); the "Nobody (test)" letter given when the session is ready |

## The session

Nothing reads or writes letters, LetterDB or SkyrimNet until the session is **ready**:

- `kPreLoadGame` (and `kNewGame`) ends the session: LetterDB closes and the session generation changes.
- `kPostLoadGame` / `kNewGame` starts it. Every 2 seconds the heartbeat calls `Session::Poll`, which makes it ready once SkyrimNet is: its database is open and its keep/clear check isn't pending (`PublicGetTimelineState`, which SkyrimNet sets to pending in its own `kPreLoadGame`). Poll then opens LetterDB for SkyrimNet's save id, attaches every letter's text, and gives the dev test letter (`DebugKeys::GiveUndeliverableLetter`).
- The save id is asked for only once SkyrimNet is ready: asked earlier, SkyrimNet makes up a new one.
- Until then letters show `...`. If the session isn't ready 30 seconds after a load, the log says why, once.

## Threading

- **Game thread:** everything that touches game state: the SKSE messages, the load callbacks, the heartbeat task (`Session::Poll`, `Transit::Tick`, `NpcLetters::Tick`), the dev keys, and the result of every reading (`Reading` reports back with `AddTask`). Each task catches exceptions so none crosses into the engine.
- **Any thread:** the `GetDescription` hook (the book menu and other readers, including the engine's "Poll controls" job). It reads only the `Letters` snapshot, under its mutex.
- **Worker threads:** a reading's memory queries, the LLM call (SkyrimNet's pool calls back) and storing the memory, which blocks while SkyrimNet embeds it; an NPC letter's pool (the engagement list and each NPC's dialogue events), its prompt context and the writer's memory. Work that finishes after a load is dropped: it carries the session generation and checks it before writing, and again after the blocking `AddMemory`. NPC letters also carry an attempt id, so an attempt given up after its timeout can't act later ([NPC_LETTERS.md](NPC_LETTERS.md#the-prompt)).
- **LetterDB** has its own mutex; readings write to it from worker threads.

## Code shared with SkyrimNet Physical Diaries

| Here | SNPD | How |
|---|---|---|
| `include/DynamicForms.h`, `src/DynamicForms.cpp` | the same files | Identical apart from the license header and a note; copied at SNPD commit `dd20705`. Keep the two in step: a fix in one goes into the other. |
| `src/TextHook.cpp` (the Win-1251 helpers and `StripFontTags`) | `src/BookTextHook.cpp` | Copied unchanged at `dd20705`; the hook itself answers letters instead of diaries |
| `Letters::Render`'s punctuation table | `BookText.cpp` `SanitizeBookText` | The typographic step only |

Both plugins hook `GetDescription` (`RELOCATION_ID(14399, 14552)`) with MinHook; each answers only for its own forms and passes everything else on.

The in-book editor (SNPD's `swf/book` and `BookEditor`) will be shared the same way; see SNPD's `docs/EDITING.md`.
