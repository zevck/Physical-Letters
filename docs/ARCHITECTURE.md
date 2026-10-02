# Architecture

## The flow

1. **A letter is created** (for now by the F6 dev key): `Letters::Create` makes a runtime book form, records the letter in LetterDB, and the player gets it.
2. **It is sent**: given to an innkeeper or the courier through the postage dialogue (or the F7 dev key); `Transit::Send` takes it from whoever holds it and queues it, due after the travel time to the recipient ([DELIVERY.md](DELIVERY.md)).
3. **It is delivered** when due: `Transit::Tick` puts it in the recipient's inventory, or, if they're in the player's town, the courier may bring it in person ([COURIER.md](COURIER.md)). The parcel stays in the queue as *awaiting reading*.
4. **The recipient reads it**: `Reading::Read` sends one prompt to the LLM through SkyrimNet and stores the NPC's memory of the letter. Only then does the parcel leave the queue. See [READING.md](READING.md).
5. **If they reply**, the same step creates the reply letter and queues it to the player. When due (writing time plus travel), it goes to the vanilla courier, who brings it to the player in a town.

Separately, every few days an NPC the player has dealt with may **write first**: `NpcLetters` picks one, the LLM decides whether they write, and the letter takes the same road to the courier ([NPC_LETTERS.md](NPC_LETTERS.md)). And NPCs write **to each other**: `NpcToNpc` starts a correspondence, and its letters go through steps 3 to 5 like the player's, with replies going to the other NPC ([NPC_TO_NPC.md](NPC_TO_NPC.md)).

If the recipient is dead when the letter is due, or can't be found for `ReturnAfterDays`, the letter turns back at step 3 and reaches the player through the courier ([DELIVERY.md](DELIVERY.md#undeliverable-letters)).

## Components

| Component | Files | Does |
|---|---|---|
| Entry point | `src/main.cpp` | Log, the INI, SKSE messages (the postage price on a new game or load), the heartbeat |
| Session | `src/Session.cpp` | When letters may be touched after a load or new game |
| SkyrimNet client | `src/SkyrimNet.cpp`, `include/SkyrimNet/PublicAPI.h` (vendored; `PublicRegisterEvent` added from SkyrimNet-Dev's v11 header) | SkyrimNet's public API, resolved at run time; requires v11 |
| Letters | `src/Letters.cpp` | Letter forms, their look, their rendered text (a thread-safe snapshot) |
| LetterDB | `src/LetterDB.cpp` | SQLite store of each letter's text, per SkyrimNet save folder |
| Transit | `src/Transit.cpp` | The queue: delivery, the reading owed (with retries), replies, NPC letters and undeliverable letters to the courier |
| Reading | `src/Reading.cpp`, `include/LlmJson.h` | The LLM call and the SkyrimNet memory; the correspondence history; reading the LLM's JSON |
| NpcToNpc | `src/NpcToNpc.cpp` | Letters between NPCs: the schedule, the proposals, the recipient checks, the letter, thread limits, pair cooldowns |
| NpcLetters | `src/NpcLetters.cpp` | NPCs writing to the player first: the schedule, the pick, the prompt, cooldowns |
| Travel | `src/Travel.cpp` | How long a letter travels (the engine's fast-travel formula); areas, towns and distances (`Area`, `IsTown`, `Distance`) |
| Courier | `src/Courier.cpp` | Hands a letter to the vanilla courier (`WICourierScript`) |
| RoadCourier | `src/RoadCourier.cpp` | The courier met on the road: which letters pass the player, the Story Manager global, the cooldown, the quest's natives ([ROAD_COURIER.md](ROAD_COURIER.md)) |
| CourierErrand | `src/CourierErrand.cpp` | The courier carrying a letter to an NPC in the player's town: the quest's natives, who holds him, the Story Manager global ([COURIER.md](COURIER.md)) |
| Postage | `src/Postage.cpp` | Posting: a letter the player wrote, given to an innkeeper or the courier in a gift menu (HandIn's watcher calls it) |
| HandIn | `src/HandIn.cpp` | Letters leaving the player's inventory: posted, or handed over in the hand-in topic's gift menu; the private narration of a letter read there; the topic's native and setting global ([HAND_IN.md](HAND_IN.md)) |
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

- **Game thread:** everything that touches game state: the SKSE messages, the load callbacks, the heartbeat task (`Session::Poll`, `Transit::Tick`, `NpcLetters::Tick`, `NpcToNpc::Tick`), the dev keys, and the result of every reading (`Reading` reports back with `AddTask`). Each task catches exceptions so none crosses into the engine.
- **Any thread:** the `GetDescription` hook (the book menu and other readers, including the engine's "Poll controls" job). It reads only the `Letters` snapshot, under its mutex.
- **Worker threads:** a reading's memory queries, the LLM call (SkyrimNet's pool calls back) and storing the memory, which blocks while SkyrimNet embeds it; an NPC letter's pool (the engagement list and each NPC's dialogue events), the shortlist's memories, the pick and letter calls and the writer's memory; the same for letters between NPCs (the actor list, related actors, recent memories). Work that finishes after a load is dropped: it carries the session generation and checks it before writing, and again after the blocking `AddMemory`. NPC letters also carry an attempt id, so an attempt given up after its timeout can't act later ([NPC_LETTERS.md](NPC_LETTERS.md#the-letter-prompt)).
- **LetterDB** has its own mutex; readings write to it from worker threads.

## Code shared with SkyrimNet Physical Diaries

| Here | SNPD | How |
|---|---|---|
| `include/DynamicForms.h`, `src/DynamicForms.cpp` | the same files | Identical apart from the license header and a note; copied at SNPD commit `dd20705`. Keep the two in step: a fix in one goes into the other. |
| `src/TextHook.cpp` (the Win-1251 helpers and `StripFontTags`) | `src/BookTextHook.cpp` | Copied unchanged at `dd20705`; the hook itself answers letters instead of diaries |
| `Letters::Render`'s punctuation table | `BookText.cpp` `SanitizeBookText` | The typographic step only |

Both plugins hook `GetDescription` (`RELOCATION_ID(14399, 14552)`) with MinHook; each answers only for its own forms and passes everything else on.

The in-book editor (SNPD's `swf/book` and `BookEditor`) will be shared the same way; see SNPD's `docs/EDITING.md`.
