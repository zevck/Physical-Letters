# NPC letters

Now and then an NPC the player has dealt with writes to them unprompted. There are no news triggers: the LLM gets the NPC's context and decides whether they have a reason to write, and what. Code: `src/NpcLetters.cpp`; settings in [SETTINGS.md](SETTINGS.md) (`[NpcLetters]`, on by default).

## When

A global schedule in the co-save: the next letter is due `IntervalDays` game days after the last attempt, times a random factor between 0.5 and 1.5. The first one is scheduled when the session is first ready on a save. When it's due, one attempt runs; whatever comes of it, the next is scheduled.

## Who

1. **The pool**, on a worker thread: `PublicGetActorEngagement` (player events only), every NPC with events involving the player. It walks SkyrimNet's whole memory and event history, which is why it runs off the game thread and once per attempt.
2. **Filtered on the data**: at least `MinEvents` events with the player; a FormID SkyrimNet knows a UUID for; not on cooldown.
3. **A shortlist of 3**, drawn at random without replacement, weighted by the square root of their events with the player: someone the player sees a lot writes more often, not every time.
4. **Checked in the world**, on the game thread, in shortlist order: the UUID maps back to the same FormID, the actor exists and isn't dead, and they aren't around the player (`Is3DLoaded`: a letter from someone in the next room is odd). The first who passes is asked.

**SkyrimNet aggregates the engagement list by actor name** (`DataAPI/ActorEngagementService.cpp`): same-named actors are merged and given one name-looked-up FormID. The UUID round trip in step 4 drops entries that don't resolve to one real actor, so generic duplicates (guards) may be merged or skipped; that's fine for a letter writer, and nothing here identifies anyone by name.

## The prompt

`physical_letters_write_letter.prompt`, next to the reading prompt ([READING.md](READING.md#the-prompt)), in the same style. Context:

| Variable | Value |
|---|---|
| `npc` | `{ UUID, name }` of the NPC who might write |
| `recipient` | The player's name |
| `days_since_seen` | Game days since their last event with the player (`lastEventTime`, game seconds / 86400), -1 unknown |
| `correspondence` | The earlier letters between them that the NPC knows of (`Reading::Correspondence`) |
| `memories` | Up to 8 of the NPC's memories most relevant to the player, letters excluded |

It returns JSON: `write` (bool), `letter`, `memory`, `emotion`, `importance`, read with the same tolerant parsing as a reading ([READING.md](READING.md#reading-the-answer)). If the NPC doesn't write (or the answer can't be read), the next on the shortlist is asked; if nobody writes, nothing happens until the next attempt. An LLM failure ends the attempt, and so does no answer within 5 minutes (SkyrimNet drops cancelled LLM tasks without calling back).

## The letter

A letter from the NPC to the player ("Letter from X"), queued to the courier after the travel time from the NPC to the player (`Transit::QueueToPlayer`, `LTRN` state 2, like a reply). The NPC goes on cooldown. Then their memory of writing it is stored: the `memory` text, "My letter to <player>:" and the exact letter, tagged `physical_letters`, `letter_sent` and `physical_letters_letter:<letter id>`. That tag is how later readings count the letter as part of the correspondence ([READING.md](READING.md#the-prompt)). The letter is made before the memory: a load in between loses only the memory.

## Cooldown

Per NPC, by UUID, in the co-save: `CooldownDays` from when they last wrote to the player, first or in reply (`Transit` starts it for replies). An NPC on cooldown isn't drawn. Over cooldowns aren't saved.

## Saves

Co-save record `LNPC` (version 1): the next letter's time (`double`, game days; 0 = not scheduled), then a count and per NPC its UUID (string) and cooldown end (`double`). It reverts with the save like the rest of delivery. A load while an attempt runs drops its result (the session generation).

## Testing

F8 makes the next NPC letter due now, with the letters in transit. The log says who was shortlisted, who couldn't write and why, whether the asked NPC wrote, and when the next attempt is.

Tested on AE (2026-09-30): from Whiterun, the NPCs around the player were skipped and the two asked declined; from Solitude, Aela wrote (about 37 s for the LLM) and the letter came by courier.
