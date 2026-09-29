# Reading

When a letter is delivered, its recipient reads it: one LLM call decides how they react, what they remember and whether they write back, and the memory goes into SkyrimNet. Code: `src/Reading.cpp`, the queue side in `src/Transit.cpp`.

## The prompt

`SKSE/Plugins/SkyrimNet/external/zevick.physical-letters/prompts/physical_letters_read_letter.prompt`, shipped as a SkyrimNet plugin (Beta 25 content layout: SkyrimNet no longer reads `SkyrimNet/prompts/`). SkyrimNet names a template by its path under `prompts/`, so `PublicSendCustomPromptToLLM("physical_letters_read_letter", …)` finds it in the external layer.

Context variables the plugin sets:

| Variable | Value |
|---|---|
| `npc` | `{ UUID, name }` of the recipient; the template uses `decnpc(npc.UUID)` and `render_character_profile("full", npc.UUID)` |
| `letter` | `{ author, recipient, body }` |
| `memories` | Up to 8 of the recipient's memory texts, most relevant to "<author> letter" (so earlier letters between them come up) |

The template follows SkyrimNet's prompt guide (`docs/modding/WORKFLOW_PROMPTS.md` in SkyrimNet): the actor is never addressed as "you", names and pronouns come from `decnpc()`.

It asks for JSON: `memory` (first person, 2–4 sentences), `emotion`, `importance` (0–1), `reply` (bool), `reply_text`.

## Reading the answer

- The JSON object is cut out of any text or code fence around it. If it doesn't parse, it is repaired (trailing commas before `}` or `]`, raw line breaks and tabs inside strings) and parsed again. SkyrimNet's own diary parsing does neither.
- Fields are read by type, accepting what LLMs write instead: `"0.8"` for a number, `"yes"` for true, `null` for a string. Of the fields, only a missing or empty `memory` fails the reading.

## The memory

`PublicAddMemory` for the recipient: type `RELATIONSHIP`, the player as related actor, the emotion, the importance, and the tags `physical_letters`, `letter_received` and `physical_letters_letter:<letter id>` ([PERSISTENCE.md](PERSISTENCE.md#was-a-letter-read)). The content:

```
<the first-person memory>

The letter from <author>:
<the exact letter>

My reply:
<the exact reply>          (only if they reply)
```

The summary comes first because SkyrimNet embeds only the start of a long memory; the exact texts follow so the NPC can recall what was written. SkyrimNet stores diary entries the same way. Writing a memory makes no LLM request; SkyrimNet embeds it locally.

## Failures and retries

A parcel leaves the queue only when the reading succeeds (or can never succeed: the letter isn't in LetterDB). Everything else is a retry:

- the LLM call fails, SkyrimNet refuses the prompt, the answer has no usable memory, `AddMemory` fails;
- SkyrimNet doesn't answer within 5 minutes (it drops cancelled LLM tasks without calling back);
- a load happens meanwhile (the loaded save's queue takes over).

Retries wait 30 s, then double. After 5 failures the letter waits for the next load, which retries it again. Before every LLM call, and again before storing, the reading checks for the tagged memory, so a retry never makes a second memory.

## Not done yet

- Replies are logged, not delivered.
- The recipient's location and the time are not in the prompt.
