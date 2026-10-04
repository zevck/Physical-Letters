# NPC letters

Now and then an NPC the player has dealt with writes to them unprompted. There are no news triggers: the LLM gets the NPC's context and decides whether they have a reason to write, and what. Code: `src/NpcLetters.cpp`; settings in [SETTINGS.md](SETTINGS.md) (`[NpcLetters]`, on by default).

## When

A global schedule in the co-save: the next letter is due `IntervalDays` game days after the last attempt, times a random factor between 0.5 and 1.5. The first one is scheduled when the session is first ready on a save. When it's due, one attempt runs; whatever comes of it, the next is scheduled.

## Who

1. **The pool**, on a worker thread: `PublicGetActorEngagement` (player events only), every NPC with events involving the player. It walks SkyrimNet's whole memory and event history, which is why it runs off the game thread and once per attempt.
2. **Filtered on the data**: at least `MinEvents` events with the player; a FormID SkyrimNet knows a UUID for; not on cooldown.
3. **Filtered in the world**, on the game thread, every one of them: the UUID maps back to the same FormID, the actor exists and isn't dead, no letter from the player to them is on its way or unread (they have one to answer: `Transit::IsLetterPendingFor`), and they aren't around the player. Around means **the same area** (`Travel::Area`, like a postcode: the nearest location up the parents with `LocTypeHabitation`, so the Bannered Mare and the street outside are both Whiterun; outside settlements, the named place below the hold, such as a dungeon or camp), or within `NearDistance` (8,192 units by default, about two exterior cells) measured like travel time. The distance applies everywhere, not only in the wilderness: it also keeps out a farm or hamlet just outside a town whose location isn't the town's. Also out: anyone who spoke to the player within `MinDaysApart` (1 day by default). Positions alone fail here: an interior resolves to its location's marker, which can be far from the door the NPC outside stands at. A letter from someone in the same town is odd.
4. **A shortlist of `CandidatesPerAttempt` (3)** from those, drawn at random without replacement, weighted by the square root of their events with the player (someone the player sees a lot writes more often, not every time) times **recency**: from `RecentWeight` (10% by default) if they spoke to the player today up to full after `MissedAfterDays` (3 by default), from the time of their latest exchange with the player, so someone met two days ago is two thirds as likely. Unknown recency (no recorded dialogue) counts as 1.
5. **The pick** ([below](#the-pick)): with more than one on the shortlist, one cheap call chooses who has a reason to write, or nobody. With one, there's no pick.
6. **The letter**, one full call for that NPC, checked in the world again first (the player may have moved meanwhile).

So an attempt costs at most two LLM calls, a cheap one and a full one, and nothing when nobody can write. It used to ask the shortlist in turn with full calls, up to three, and most declined.

The log names each NPC drawn with their events, days since last seen and weight; with debug logging, each engagement entry and the latest exchanges found, why each skipped NPC can't write, and the LLM's raw answers.

**The engagement list's times are unusable**: `lastEventTime` is always 0 and every event counts as recent. SkyrimNet's database returns `game_time` as a `time_point`, which its event reader handles (`DatabaseManager_EventRead.cpp`) but the stats query doesn't (it only reads `double` and `long long`, `DatabaseManager_EventQueries.cpp`). Read from the source, 2026-09-30.

**Recency and the prompt's conversation come from the NPC's dialogue events** (`PublicGetRecentEvents`, types `dialogue` and `dialogue_player_text`, the newest 80), one call per NPC in the pool, on the worker thread. Kept: lines between the NPC and the player, matched by UUID (`originatingActor`, `targetActor`); the text is `data.dialogue`; the time `gameTime`, game seconds. Lines later than now are skipped: they belong to a timeline the player left (an older save loaded with Keep). The newest 20 lines go to the prompt. `PublicGetRecentDialogue` isn't used: it fetches the newest events oldest first and stops after the first 20 lines, so it keeps the oldest of them (`DataAPI/EventQueryService.cpp`, read 2026-09-30); its header also documents `text` and names where it returns `data` and `"player"`/`"npc"`.

**SkyrimNet aggregates the engagement list by actor name** (`DataAPI/ActorEngagementService.cpp`): same-named actors are merged and given one name-looked-up FormID. The UUID round trip in step 3 drops entries that don't resolve to one real actor, so generic duplicates (guards) may be merged or skipped; that's fine for a letter writer, and nothing here identifies anyone by name.

## The pick

`physical_letters_who_writes.prompt`, on SkyrimNet's `meta` OpenRouter variant (a fast, cheap model in its default config; a config without it uses the default model), like the NPC-to-NPC proposals ([NPC_TO_NPC.md](NPC_TO_NPC.md)). Per candidate (`candidates`, numbered from 1): name, short profile (`short_inline`), days since seen, the newest 8 lines of their latest exchanges with the player and the same memories the letter gets. The player's name is `player`. It answers `{"pick":2,"why":"..."}`, `0` for nobody, short because `meta`'s output is capped at 100 tokens in SkyrimNet's default config. The answer is read with a regex (`ReadPick`), so one cut short after the number still counts. Nobody, a number out of range or an unreadable answer ends the attempt; the log gives the reason.

The memories are fetched once per shortlisted NPC, on a worker thread, before the pick.

## The letter prompt

`physical_letters_write_letter.prompt`, next to the reading prompt ([READING.md](READING.md#the-prompt)), in the same style. Context:

| Variable | Value |
|---|---|
| `npc` | `{ UUID, name }` of the NPC who might write |
| `recipient` | The player's name |
| `days_since_seen` | Game days since their latest exchange with the player, -1 unknown |
| `dialogue` | Their latest spoken exchanges with the player, oldest first, up to 20 lines: `{ speaker, text }`, with the speaker's name (above). Many NPCs have events with the player but no memories yet; this is what they have to write about |
| `correspondence` | The earlier letters between them that the NPC knows of (`Reading::Correspondence`) |
| `memories` | Up to 8 of the NPC's memories most relevant to the player, letters excluded |

It returns JSON: `write` (bool), `letter`, `memory`, `emotion`, `importance`, read with the same tolerant parsing as a reading ([READING.md](READING.md#reading-the-answer)). If the NPC doesn't write after all (or the answer can't be read), nothing happens until the next attempt: no one else is asked. An LLM failure ends the attempt, and so does no answer within 5 minutes for any one step, the pool scan or an LLM call (SkyrimNet drops cancelled LLM tasks without calling back).

**Each attempt has an id** (with the session generation): every step checks it before acting, and giving an attempt up changes it, so an answer arriving after the timeout, or after a load, is dropped and logged; it can't write a letter or end a newer attempt.

## The letter

A letter from the NPC to the player ("Letter from X"), queued to the courier after the travel time from the NPC to the player (`Transit::QueueToPlayer`, `LTRN` state 2, like a reply). The NPC goes on cooldown. Then their memory of writing it is stored: the `memory` text, "My letter to <player>:" and the exact letter, tagged `physical_letters`, `letter_sent` and `physical_letters_letter:<letter id>`. That tag is how later readings count the letter as part of the correspondence ([READING.md](READING.md#the-prompt)), so the memory is stored even when the LLM gives no `memory` text: then it's "I wrote a letter to <player>." (logged). The letter is made before the memory: a load in between loses only the memory.

## Cooldown

Per NPC, by UUID, in the co-save: `CooldownDays` from when they last wrote to the player, first or in reply (`Transit` starts it for replies). An NPC on cooldown isn't drawn. Over cooldowns aren't saved.

## Saves

Co-save record `LNPC` (version 1): the next letter's time (`double`, game days; 0 = not scheduled), then a count and per NPC its UUID (string) and cooldown end (`double`). It reverts with the save like the rest of delivery. A load while an attempt runs drops its result (the session generation).

## Testing

For testing, a low `IntervalDays` brings the next attempt sooner. The log says who was shortlisted, who couldn't write and why, whom the pick chose and why, whether they wrote, and when the next attempt is. After each attempt it logs the session's tally: attempts, pick calls and how many found nobody, letter calls, letters written and declined, failures and timeouts.

Tested on AE (2026-09-30): from Whiterun, the NPCs around the player were skipped and the two asked declined; from Solitude, Aela wrote (about 37 s for the LLM) and the letter came by courier.
