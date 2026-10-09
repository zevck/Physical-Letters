# Reading

When a letter is delivered, its recipient reads it: one LLM call decides how they react, what they remember and whether they write back, and the memory goes into SkyrimNet. Code: `src/Reading.cpp`, the queue side in `src/Transit.cpp`, the JSON reading in `include/LlmJson.h` (shared with [NPC letters](NPC_LETTERS.md)).

## The prompt

`SKSE/Plugins/SkyrimNet/external/zevick.physical-letters/prompts/physical_letters/read_letter.prompt`, in the plugin's own `prompts/physical_letters/` folder (sent as `physical_letters\read_letter`: SkyrimNet resolves a prompt name as a path under `prompts`, as its own `render_template("components\\event_history")` does; all six of our prompts are there, out of the root that every plugin's prompts share), shipped as a SkyrimNet plugin (Beta 25 content layout: SkyrimNet no longer reads `SkyrimNet/prompts/`). SkyrimNet names a template by its path under `prompts/`, so `PublicSendCustomPromptToLLM("physical_letters\read_letter", …) (SkyrimNet splits a prompt name on either slash, so "physical_letters/read_letter" works too)` finds it in the external layer.

Context variables the plugin sets:

| Variable | Value |
|---|---|
| `npc` | `{ UUID, name }` of the recipient; the template uses `decnpc(npc.UUID)` and `render_character_profile("full", npc.UUID)` |
| `letter` | `{ author, recipient, body, read_before, can_reply, blood, blood_text, author_UUID, recipient_UUID }`; the UUIDs are for the writer's public profile (below); `blood` is `"all"` or `"part"` when the player wrote it in blood (`Letters::BloodOf`, from LetterDB's `blood`), with the passages in `blood_text`, one per line, and the template says so after the letter; `read_before` is true when the recipient already has a memory of this letter (it was sent again); `can_reply` is false when a thread between NPCs is at its limit ([NPC_TO_NPC.md](NPC_TO_NPC.md#keeping-it-bounded)) or, for any letter from the player (mailed or handed over), when `[NpcLetters] Replies` is off ([SETTINGS.md](SETTINGS.md)): the prompt then says not to reply, and a reply is ignored |
| `correspondence` | Earlier letters between the two that the recipient knows of, oldest first, up to 20: `{ from, to, days_ago, body }`. The letter being read isn't in it; the recipient's replies to it are, when it was sent again. |
| `memories` | Up to 8 of the recipient's other memories most relevant to the writer (letters excluded: they're in `correspondence`) |

**What the reader knows of the writer**: an "About" section, the public part of the writer's SkyrimNet profile: gender, race (`decnpc`) and the character summary (`render_character_profile("bio_summary", …)`). It's what SkyrimNet shows an NPC of someone they speak to (its `dialogue_target` profile, also what telepathy uses), less what only sight gives (physical activity, appearance, worn equipment, health): a letter's reader can't see its writer. Background, personality, relationships, occupation and memories are private in SkyrimNet's profile too, and stay out. The summary is whatever the writer's bio says; some mention secrets, but SkyrimNet shows it to anyone the writer talks to as well. The same section describes the player in `physical_letters/write_letter` and the recipient in `physical_letters/npc_letter` (2026-10-03; the latter had the summary alone before).

**How many earlier letters the prompt shows** is set at the top of the template: `{% set max_earlier_letters = 5 %}`. Edit it there (or in a SkyrimNet overlay of the prompt); up to 20 are passed.

**Which earlier letters count** follows the same rule as the rest of the mod: SkyrimNet's memory decides ([PERSISTENCE.md](PERSISTENCE.md#was-a-delivery-read)). A letter to the recipient counts if they have its tagged memory; their own reply counts if they remember the letter it answers (that memory holds the reply); a letter they wrote first counts if they have its tagged memory of writing it ([NPC_LETTERS.md](NPC_LETTERS.md#the-letter)). Letters from timelines the player left, and letters still on their way, drop out without any bookkeeping of ours.

The template follows SkyrimNet's prompt guide (`docs/modding/WORKFLOW_PROMPTS.md` in SkyrimNet): the actor is never addressed as "you", names and pronouns come from `decnpc()`.

It asks for JSON: `memory` (first person, 2–4 sentences), `emotion`, `importance` (0–1), `reply` (bool), `reply_text`. The importance guidelines are copied word for word from SkyrimNet's own memory prompt (`memory/generate_memory.prompt`, its Importance Score Guidelines) into one component, `physical_letters/components/memory_importance.prompt`, which all four of our prompts that make memories render (`render_template`), so letter memories are scored on SkyrimNet's scale; asked for a bare 0–1, models answered 0.5 even for an alarming letter (2026-10-03). Keep the copy in step if SkyrimNet changes it.

## Reading the answer

- The JSON object is cut out of any text or code fence around it. If it doesn't parse, it is repaired (trailing commas before `}` or `]`, raw line breaks and tabs inside strings) and parsed again. SkyrimNet's own diary parsing does neither.
- Fields are read by type, accepting what LLMs write instead: `"0.8"` for a number, `"yes"` for true, `null` for a string. Of the fields, only a missing or empty `memory` fails the reading.

## The memory

`PublicAddMemory` for the recipient: type `RELATIONSHIP`, the letter's author as related actor (the player, or the NPC who wrote it), the emotion, the importance, and the tags `physical_letters`, `letter_received`, `physical_letters_letter:<letter id>` and `physical_letters_delivery:<delivery id>` ([PERSISTENCE.md](PERSISTENCE.md#was-a-delivery-read)). The content:

```
<the first-person memory>

The letter from <author>:
<the exact letter>

It is written in blood.    (or: Part of it is written in blood: <the passages>; only if it was)

My reply:
<the exact reply>          (only if they reply)
```

The summary comes first because SkyrimNet embeds only the start of a long memory; the exact texts follow so the NPC can recall what was written. SkyrimNet stores diary entries the same way. Writing a memory makes no LLM request; SkyrimNet embeds it locally.

## Failures and retries

A parcel leaves the queue only when the reading succeeds (or can never succeed: the letter isn't in LetterDB). Everything else is a retry:

- the LLM call fails, SkyrimNet refuses the prompt, the answer has no usable memory, `AddMemory` fails;
- SkyrimNet doesn't answer within 5 minutes (it drops cancelled LLM tasks without calling back);
- a load happens meanwhile (the loaded save's queue takes over).

Retries wait 30 s, then double. After 5 failures the letter waits for the next load, which retries it again. Before every LLM call, and again before storing, the reading checks for the delivery's tagged memory, so a retry never makes a second memory of one delivery.

## A letter handed over

A letter handed over in person is read by this call too, after the reader's reaction on the spot ([HAND_IN.md](HAND_IN.md#reading-it-there)). Only its recipient is handed it, so every reading is the recipient's (until 2026-10-07 someone else could be, with its own prompt, `read_other_letter`).

## Not done yet

- The recipient's location and the time are not in the prompt.

A reply becomes a letter to the player and travels to the courier: [DELIVERY.md](DELIVERY.md#replies-and-the-courier).
