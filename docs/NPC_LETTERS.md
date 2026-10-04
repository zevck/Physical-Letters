# NPC letters

Now and then an NPC the player has dealt with writes to them unprompted. There are no news triggers: the LLM gets the NPC's context and decides whether they have a reason to write, and what. Code: `src/NpcLetters.cpp`; settings in [SETTINGS.md](SETTINGS.md) (`[NpcLetters]`, on by default).

## When

A global schedule in the co-save: the next letter is due `IntervalDays` game days after the last attempt, times a random factor between 0.75 and 1.25 (5¼ to 8¾ days at 7). The first one is scheduled when the session is first ready on a save. When it's due, one attempt runs; whatever comes of it, the next is scheduled.

## Who

1. **The pool**, on a worker thread: `PublicGetActorEngagement` (player events only), every NPC with events involving the player. It walks SkyrimNet's whole memory and event history, which is why it runs off the game thread and once per attempt.
2. **Filtered on the data**: at least `MinEvents` events with the player, and never fewer than 1 (someone with none is a stranger); a FormID SkyrimNet knows a UUID for; not on cooldown. **At `MinEvents` 0** up to 10 strangers (`kStrangersPerAttempt`) are let into the pool each attempt, drawn at random from the others SkyrimNet has registered (`PublicSearchActors("", 500)`), people the player has never dealt with (user, 2026-10-04: limited so they don't outnumber the people the player knows). SkyrimNet sorts that list by name and stops at 500: in a save with more, those past it can't be drawn (logged at info when it's full). On the worker thread only SkyrimNet's data is read (who they are, whether it has memories of them); the game thread checks the rest (step 3). They have no recency and no conversation with the player.
3. **Filtered in the world**, on the game thread, every one of them: the UUID maps back to the same FormID; the actor exists, isn't dead or disabled, and is a person (an NPC race with a voice: SkyrimNet registers creatures too, `Actors::IsPerson`); a stranger is unique, or a generic NPC SkyrimNet has memories of; no letter from the player to them is on its way or unread (they have one to answer: `Transit::IsLetterPendingFor`); no letter of theirs to the player is on its way (a reply, or one written first: `Transit::IsLetterPendingFrom`, so a reply still travelling isn't followed by a second letter, now that replies don't start the cooldown); they aren't in **the same area** as the player (`Travel::Area`, like a postcode: the nearest location up the parents with `LocTypeHabitation`, so the Bannered Mare and the street outside are both Whiterun; outside settlements, the named place below the hold, such as a dungeon or camp); and they didn't speak to the player within `MinDaysApart` (1 day by default). Someone just outside a town (a farm whose location isn't the town's) isn't caught here: the pick and the letter prompt are told where the NPC is and where the player is, and that someone close enough to walk over doesn't write. (A `NearDistance` check, 8,192 units, did this until 2026-10-03: too small to mean anything, rough in interiors, and the model judges "Pelagia Farm" next to "Whiterun" better.)
4. **A shortlist of `CandidatesPerAttempt` (3)** from those, drawn at random without replacement. Each one's weight is the square root of their events with the player (someone the player sees a lot writes more often, not every time) plus the importance of their memories that involve the player: of their 100 newest memories (`kWeighedMemories`, fetched on the worker thread in step 2), each one whose `related_actors` holds the player's UUID adds its `importance_score` (0 to 1). So 16 events and 6 memories of the player at 0.5 weigh 4 + 3 = 7: someone with a few strong memories of the player can outweigh someone met often in passing (user, 2026-10-04). SkyrimNet's engagement totals can't be used for this: they count all of an actor's memories, not just the player's. Someone never dealt with weighs 0.25, a quarter of one event. **Those close to the player** grow more likely the longer they've been apart: ex-followers not with the player now (`DismissedFollowerFaction`, `0x05C84C`, which vanilla's `DialogueFollowerScript` adds on dismissal and removes on rehiring), the spouse (`PlayerMarriedFaction`, `0x0C6472`, on the marriage quest's spouse alias) and adopted children (HearthFires' `BYOHRelationshipAdoptionFaction`, `0x0042B0`, on its Spouse, Child1 and Child2 aliases), checked on the game thread (`IsClose`). These are vanilla's: a follower framework that dismisses followers its own way may never set `DismissedFollowerFaction`, and then ex-followers get no boost (not yet checked with one). Their weight rises in a straight line from normal when they last saw the player to twice normal after `DaysUntilMissed` days apart (7 by default), and stays there. Everyone else's weight doesn't change with time apart: a stranger met once in a tavern a month ago isn't missing anyone (user, 2026-10-03). The two earlier recency settings (`MissedAfterDays`, `RecentWeight`) lowered everyone's chance after a recent meeting and are gone: `MinDaysApart` already rules that out.
5. **The pick** ([below](#the-pick)): with more than one on the shortlist, one cheap call chooses who has a reason to write, or nobody. With one, there's no pick.
6. **The letter**, one full call for that NPC, checked in the world again first (the player may have moved meanwhile).

So an attempt costs at most two LLM calls, a cheap one and a full one, and nothing when nobody can write. It used to ask the shortlist in turn with full calls, up to three, and most declined.

The log names each NPC drawn with their events, days since last seen and weight; with debug logging, each engagement entry, each known NPC's memory importance and the latest exchanges found, why each skipped NPC can't write, and the LLM's raw answers.

**The engagement list's times are unusable**: `lastEventTime` is always 0 and every event counts as recent. SkyrimNet's database returns `game_time` as a `time_point`, which its event reader handles (`DatabaseManager_EventRead.cpp`) but the stats query doesn't (it only reads `double` and `long long`, `DatabaseManager_EventQueries.cpp`). Read from the source, 2026-09-30.

**Recency and the prompt's conversation come from the NPC's dialogue events** (`PublicGetRecentEvents`, types `dialogue` and `dialogue_player_text`, the newest 80), one call per NPC in the pool, on the worker thread. Kept: lines between the NPC and the player, matched by UUID (`originatingActor`, `targetActor`); the text is `data.dialogue`; the time `gameTime`, game seconds. Lines later than now are skipped: they belong to a timeline the player left (an older save loaded with Keep). The newest 20 lines go to the prompt. `PublicGetRecentDialogue` isn't used: it fetches the newest events oldest first and stops after the first 20 lines, so it keeps the oldest of them (`DataAPI/EventQueryService.cpp`, read 2026-09-30); its header also documents `text` and names where it returns `data` and `"player"`/`"npc"`.

**SkyrimNet aggregates the engagement list by actor name** (`DataAPI/ActorEngagementService.cpp`): same-named actors are merged and given one name-looked-up FormID. The UUID round trip in step 3 drops entries that don't resolve to one real actor, so generic duplicates (guards) may be merged or skipped; that's fine for a letter writer, and nothing here identifies anyone by name.

## The pick

`physical_letters/who_writes.prompt`, on SkyrimNet's `meta` OpenRouter variant (a fast, cheap model in its default config; a config without it uses the default model), like the NPC-to-NPC proposals ([NPC_TO_NPC.md](NPC_TO_NPC.md)). Per candidate (`candidates`, numbered from 1): name, where they are (`place`, `Travel::PlaceName`), whether they've never met the player (`never_met`), short profile (`short_inline`), days since seen, the newest 8 lines of their latest exchanges with the player and the same memories the letter gets. The player's name is `player`, and where they are now `player_place`. It answers `{"pick":2,"why":"..."}`, `0` for nobody, short because `meta`'s output is capped at 100 tokens in SkyrimNet's default config. The answer is read with a regex (`ReadPick`), so one cut short after the number still counts. Nobody, a number out of range or an unreadable answer ends the attempt; the log gives the reason.

The memories are fetched once per shortlisted NPC, on a worker thread, before the pick.

## The letter prompt

`physical_letters/write_letter.prompt`, next to the reading prompt ([READING.md](READING.md#the-prompt)), in the same style. Context:

| Variable | Value |
|---|---|
| `place`, `player_place` | Where the NPC is and where the player is now (`Travel::PlaceName`) |
| `never_met` | True when the NPC has never dealt with the player (`MinEvents` 0) |
| `npc` | `{ UUID, name }` of the NPC who might write |
| `recipient` | The player's name |
| `recipient_UUID` | The player's UUID, for their public profile (gender, race, summary: [READING.md](READING.md#the-prompt)) |
| `days_since_seen` | Game days since their latest exchange with the player, -1 unknown |
| `dialogue` | Their latest spoken exchanges with the player, oldest first, up to 20 lines: `{ speaker, text }`, with the speaker's name (above). Many NPCs have events with the player but no memories yet; this is what they have to write about |
| `correspondence` | The earlier letters between them that the NPC knows of (`Reading::Correspondence`) |
| `memories` | Up to 8 of the NPC's memories most relevant to the player, letters excluded |

It returns JSON: `write` (bool), `letter`, `memory`, `emotion`, `importance`, read with the same tolerant parsing as a reading ([READING.md](READING.md#reading-the-answer)). If the NPC doesn't write after all (or the answer can't be read), nothing happens until the next attempt: no one else is asked. An LLM failure ends the attempt, and so does no answer within 5 minutes for any one step, the pool scan or an LLM call (SkyrimNet drops cancelled LLM tasks without calling back).

**Each attempt has an id** (with the session generation): every step checks it before acting, and giving an attempt up changes it, so an answer arriving after the timeout, or after a load, is dropped and logged; it can't write a letter or end a newer attempt.

## The letter

A letter from the NPC to the player ("Letter from X"), queued to the courier after the travel time from the NPC to the player (`Transit::QueueToPlayer`, `LTRN` state 2, like a reply). The NPC goes on cooldown. Then their memory of writing it is stored: the `memory` text, "My letter to <player>:" and the exact letter, tagged `physical_letters`, `letter_sent` and `physical_letters_letter:<letter id>`. That tag is how later readings count the letter as part of the correspondence ([READING.md](READING.md#the-prompt)), so the memory is stored even when the LLM gives no `memory` text: then it's "I wrote a letter to <player>." (logged). The letter is made before the memory: a load in between loses only the memory.

## Cooldown

Per NPC, by UUID, in the co-save: `CooldownDays` from when they last wrote to the player first. It only limits writing first: a reply to the player's letter is never blocked by it (the reading doesn't look at it), and doesn't start it (user, 2026-10-03). An NPC on cooldown isn't drawn. Over cooldowns aren't saved.

## Saves

Co-save record `LNPC` (version 1): the next letter's time (`double`, game days; 0 = not scheduled), then a count and per NPC its UUID (string) and cooldown end (`double`). It reverts with the save like the rest of delivery. A load while an attempt runs drops its result (the session generation).

## Testing

For testing, a low `IntervalDays` brings the next attempt sooner. The log says who was shortlisted, who couldn't write and why, whom the pick chose and why, whether they wrote, and when the next attempt is. After each attempt it logs the session's tally: attempts, pick calls and how many found nobody, letter calls, letters written and declined, failures and timeouts.

Tested on AE (2026-09-30): from Whiterun, the NPCs around the player were skipped and the two asked declined; from Solitude, Aela wrote (about 37 s for the LLM) and the letter came by courier.
