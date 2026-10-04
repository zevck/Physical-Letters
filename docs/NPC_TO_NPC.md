# Letters between NPCs

Now and then an NPC writes to someone in their own life, in another town, and the recipient may write back. The letters are real items in their inventories: the player can steal and read them. Code: `src/NpcToNpc.cpp` (starting correspondences), `src/Transit.cpp` and `src/Reading.cpp` (delivering, reading, replying); settings in [SETTINGS.md](SETTINGS.md) (`[NpcToNpc]`, on by default). Separate from [NPC letters to the player](NPC_LETTERS.md), so each is configured on its own.

**Requires `PublicSearchActors`** (SkyrimNet public API v11, added after 0.25.1's first builds). Without it the feature is off and the rest of the mod works.

## Keeping it bounded

Replies can breed replies, so there are hard limits, all settings:

- **A thread** is a letter and the replies that follow it (`in_reply_to` in LetterDB). It ends after `MaxLettersPerThread` letters (3), whatever the NPCs would say: the reader of the last letter is told not to reply (`can_reply` in the reading prompt) and any reply is dropped.
- **At most `MaxOpenThreads` threads** (3) run at once; no new one starts until one ends. An NPC in an open thread doesn't start another or receive one.
- **New threads only come from the schedule**: every `IntervalDays` (5), give or take half. A reply continues its thread; it never starts one.
- **A pair cooldown**: when a thread ends, its two NPCs don't start another for `PairCooldownDays` (21).

Open threads aren't stored: they're the letters between NPCs with a parcel in Transit's queue (on their way or unread), followed back to their first letter. Nothing can drift or leak; a thread ends when its last parcel leaves the queue without a reply.

Each letter costs a reading call, so these limits also bound the spend; starting one costs at most two calls (below).

## An attempt

The LLM chooses whom to write to, from what it knows of the writer; the code does every check it can for free, and the calls are few (docs principle: every LLM call is paid).

1. **Writers** (code): everyone SkyrimNet has registered in this save (`PublicSearchActors("", 500)`, the player excluded), or with `KnownOnly` the NPCs with at least `NpcLetters.MinEvents` events involving the player. `WritersPerAttempt` (4) drawn **uniformly at random** among those who can write: the UUID maps back to the same FormID, alive, **a unique person** (a unique actor base, not a generic guard or bandit; an NPC race, `ActorTypeNPC`, with a voice type, not a child: SkyrimNet registers rabbits too), not in an open thread. Nobody, and the attempt ends with no LLM call.
2. **Proposals**, one call on SkyrimNet's `meta` OpenRouter variant (a fast, cheap model in its default config; a config without it uses its default model): `physical_letters_npc_propose.prompt`. Per writer: name and place, their short profile (`short_inline`), their `MemoriesPerWriter` (3) newest memories, and the letters they've exchanged with other NPCs and remember ("wrote to X, 5 days ago"). It returns **names only**, up to `NamesPerWriter` (3) per writer, best first, as compact JSON keyed by the writer's number (`{"1":["Vignar Gray-Mane","Olfrid Battle-Born"],"3":["Maul"]}`). Names only because SkyrimNet's `meta` caps output at 100 tokens in its default config (`max_tokens` counts output only, so the input can be rich); a first version that asked for a tie and a purpose per name was cut off after one and a half. The answer is read tolerantly (`ReadProposals`): every writer's list that closed before a cut counts.
3. **The recipient** (code): each writer's names are tried in order and the first that passes [the rules](#the-recipient) is kept; one of the writers' pairs is drawn at random. None passes, and the attempt ends.
4. **The letter**, one call on the default model: `physical_letters_npc_letter.prompt`, with the writer's full profile, the recipient's short one, the writer's newest memories and their earlier letters to each other (`Reading::Correspondence`). It first judges the **tie** ("fellow smith in Whiterun Hold; Eorlund is the master") and the **purpose** ("to ask about Skyforge steel") from both profiles, then writes in a tone that matches, or declines (`write: false`: no letter this time). It returns the tie and the purpose too, for the log. The cheap model, describing ties without the recipient's profile, got them wrong (it made Katarina "Jarl Balgruuf's daughter").

So an attempt costs **0 LLM calls** with no writer, **1** when no proposed name passes, **2** when a letter is written or declined. No retries: a refusal is no letter, and the next attempt tries others.

**Relationship records aren't used.** Skyrim's `TESNPC::relationships` levels drive mechanics (a village smith and a master smith are "Friend"), not stories; the SkyrimNet profile is the source of who someone knows. An earlier version built pairs from them and let a cheap call pick one: the pairs were mostly neighbours, and the pick, given only names, declined everything.

**Memories involve the player**, since things mostly happen around the player; that's how news of the player travels ("Grandpa, I met the Dragonborn!"). The skew stays bounded: writers are drawn uniformly, so most never met the player; `MemoriesPerWriter` 0 keeps letters to their own lives. The prompts give guidance only where it's needed: another place, a tie they really have (a shared trade isn't friendship, but can be a reason for a letter of that kind), a tone matching the tie, not to the player.

Before the letter is created, the writer and the recipient are checked again (the world may have moved during the calls). Each attempt carries a token (session generation and attempt id) and each LLM call a 5-minute timeout, as for letters to the player ([NPC_LETTERS.md](NPC_LETTERS.md#the-letter-prompt)).

## The recipient

A name the proposals gave is used only if all of these hold:

1. **Exactly one** actor SkyrimNet has registered has exactly that name (`PublicSearchActors`, case-insensitive): two of the same name are skipped, not guessed.
   **Jarls** match by any of their names: SkyrimNet calls them "Jarl Balgruuf the Greater", the game "Balgruuf the Greater" (short name "Balgruuf"), and letters use any of these. `kJarls` lists the members of Skyrim.esm's `JobJarlFaction` (`0x050920`) who rule: every hold's jarl in both outcomes of the civil war (Erikur, Hrongar and Bryling are in the faction but never rule: cut content) with their short names from the game data; a name, with or without "Jarl ", that is a jarl's full or short name is searched by the full name and matched with SkyrimNet's "Jarl " dropped. Skald's "Skald the Elder" comes from his EditorID. SkyrimNet's Identity Links don't help here: they link actor forms, not names, and aren't in the public API.
2. It isn't the player or the writer.
3. The UUID maps back to the same FormID, and the actor exists, is alive and is a unique person.
4. **They live elsewhere**: another area (`Travel::Area`) **and** at least `MinDistance` apart (16384 units, about four exterior cells; `Travel::Distance`, an interior as its location's marker). The area alone lets neighbours through: the College and Winterhold, a farm and the city next to it.
5. The pair isn't on cooldown, and the recipient isn't in an open thread.

This is the one place the mod resolves a person by name, and only a name the LLM proposed, under these rules.

**Only actors SkyrimNet has resolved can be found**, so an NPC writes only to someone SkyrimNet already knows. On a new save most of an NPC's people aren't resolved yet (a first test: Muiri's letter to Nilsine Shatter-Shield, the case the feature is for, failed on that). Accepted (2026-09-30): it fills in over a playthrough, and resolving people from the game's own data would cost more.

## The letter

A letter from the writer to the recipient ("Letter to X"), queued to the recipient after the travel time between them (`Transit::QueueToNpc`, `LTRN` state 0, like the player's own letters). The writer keeps a memory of writing it, tagged `physical_letters`, `letter_sent` and its letter tag, with the recipient as related actor (stored even without `memory` text, as for [letters to the player](NPC_LETTERS.md#the-letter)).

Then it's Transit's and Reading's, as for any letter to an NPC:

- **Delivered** into the recipient's inventory when due, off-screen. It can be pickpocketed and read.
- **Read** by the recipient ([READING.md](READING.md)): their memory of it (related actor: the writer), and a reply if the thread has room.
- **A reply** goes to the other NPC after `WritingHours` plus the travel time, as a new letter in the same thread.
- **Undeliverable** (the recipient dead, or not found for `ReturnAfterDays`): dropped, and the thread ends. It doesn't come to the player.

## Saves

Co-save record `LN2N` (version 1): the next attempt's time (`double`, game days; 0 = not scheduled), then a count and per pair its key (the two UUIDs, sorted, joined by `|`) and cooldown end (`double`). Over cooldowns aren't saved. The letters themselves are Transit's parcels (`LTRN`), so a thread reverts with the save like everything else in the queue.

## Not built yet

- **Handing letters in person** to their recipient.

## Testing

For testing, a low `IntervalDays` brings the next attempt sooner. The log (`[NpcToNpc]`) names the writers drawn and where they live, each proposed name and why it was refused or that it's usable, the letter written with its tie and purpose (or the tie of one declined); debug logging adds the raw proposals and letters. After every attempt a **tally for the play session** (not saved, not reset by loads): attempts, those with no writer (no LLM call), proposal calls, names proposed and refused by reason, attempts with no usable name, letter calls, written, declined, LLM failures and timeouts. `[NpcLetters]` logs one for letters to the player.

First tests on AE (2026-09-30), with two earlier versions: writers naming a recipient with a retry (3 attempts, 17 LLM calls, nothing written; right refusals: same town, unresolved names, rabbits as writers), then pairs from relationship records with a cheap pick (the pairs were neighbours, the pick declined). The current version, tested 2026-09-30: letters written in two calls, and two threads ran to three letters and closed with the pair cooldown.
