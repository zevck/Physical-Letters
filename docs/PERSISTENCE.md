# Persistence

Three places hold a letter's state, each for a reason:

| What | Where | Why there |
|---|---|---|
| The letter item | A runtime `TESObjectBOOK` form (`0xFF` FormID) the engine saves itself | Items in inventories and the world need a real form. See SNPD `docs/BOOK_FORMS.md`. |
| Which letter each form is | Co-save record `LFRM` | The save keeps only a form's flags |
| Letters in transit, waiting for or with the courier in town, awaiting reading, or on their way to the courier; which of the player's letters came back and why | Co-save record `LTRN` | Must revert with the save |
| When the next NPC letter is due; NPCs on cooldown | Co-save record `LNPC` ([NPC_LETTERS.md](NPC_LETTERS.md#saves)) | Must revert with the save |
| When the courier was last met on the road | Co-save record `LROD` ([ROAD_COURIER.md](ROAD_COURIER.md#saves)) | Must revert with the save |
| When NPCs next start writing to each other; pairs on cooldown | Co-save record `LN2N` ([NPC_TO_NPC.md](NPC_TO_NPC.md#saves)); the letters are `LTRN` parcels | Must revert with the save |
| A reply the courier holds | The courier's container (`WICourierContainerRef`) | The engine saves it like any other inventory |
| Each letter's text, author, recipient, reading | LetterDB | The text is written once, so one row serves every save of the character |

## Letter forms

Created by `DynamicForms::Create<TESObjectBOOK>()` (the engine picks the FormID) and given the look of the vanilla letter `Skyrim.esm 0x10596A` (WIDBAssassinLetter: Note01 model, no script) every session. **A letter form is never removed from the save**: a world copy of a dropped form would come back with an unrelated base and crash the game (SNPD `BOOK_FORMS.md`, fact 8). Letters aren't retired yet; nothing needs to get rid of one.

## The co-save

Unique ID `'SNPL'`:

- **`LFRM`** (version 2, DynamicForms' format): per form, FormID, form type, flags (bit 0 = retired), then three strings: key (the letter id), template EditorID (unused, empty), display name ("Letter to X", or "Letter from X" for a letter to the player).
- **`LTRN`** (version 6): per parcel, three strings (letter id, recipient UUID, recipient name), `dueAt` (double, game days), state (`uint8`: 0 in transit to an NPC, 1 delivered and awaiting reading, 2 on its way to the courier: a reply, a letter an NPC wrote first ([NPC_LETTERS.md](NPC_LETTERS.md)), or the player's own letter coming back undelivered; for state 2 the "recipient" is the player; 3 waiting for the courier and 4 with the courier, [COURIER.md](COURIER.md#saves); 5 and 6 with the courier on the road, from 0 and 2, [ROAD_COURIER.md](ROAD_COURIER.md#saves)), then the delivery id (string: this sending of the letter; a letter can be sent again), then the route (v6: world `uint32`, from and to as four `float`s, set-out time `double`). Then the returned letters: a count, and per letter its id (string) and why it came back (`uint8`: 1 the recipient is dead, 2 not found), for its item card. Version 5 had no routes, version 4 no courier states, version 3 no returned letters, version 2 no delivery id and version 1 no state; versions 1 and 2 load with the letter id as delivery id, and version 1 parcels as in transit.

Strings are a `uint32` length and the bytes, at most 4096 (`include/CoSave.h`). The load callback fills the letter forms in from `LFRM` at once; their text follows when the session is ready.

## LetterDB

`Data/SKSE/Plugins/PhysicalLetters/SkyrimNet-<save id>/letters.db` (MO2: under `overwrite/`). `<save id>` is SkyrimNet's (`PublicGetSaveUniqueID`, digits and dashes only, anything else is refused). One DB per character, shared by all their saves, like SkyrimNet's own.

**`letters`**, one row per letter, `letter_id` (random 128-bit hex) the primary key:

| Column | Notes |
|---|---|
| `author_uuid`, `author_name`, `recipient_uuid`, `recipient_name` | SkyrimNet identities |
| `body` | The letter's plain text |
| `written_at`, `delivered_at` | Game days; `delivered_at` is 0 until delivered |
| `reading`, `memory_id` | The LLM's answer (JSON) and the SkyrimNet memory it became, from the latest reading of the letter (a letter sent again is read again). A record only; see below |
| `in_reply_to` | For a reply, the id of the letter it answers ('' otherwise; replies from before this column have '' too, and so don't appear in the correspondence) |
| `blood` | A letter the player wrote partly in blood: the body with that text between U+E000 and U+E001, for the book to show red; '' otherwise ([WRITING.md](WRITING.md#the-text)) |
| `address` | A letter the player wrote: the recipient's address after their name on the "To:" line ("6391 Dawnstar", [WRITING.md](WRITING.md#the-recipient)); '' otherwise, and for letters from before addresses |
| `blood_heading` | 1 when the player began the letter in blood: its "To:" line is red ([WRITING.md](WRITING.md#the-text)); 0 otherwise |

New columns are added with `ALTER TABLE … ADD COLUMN … DEFAULT`, as in SNPD.

**Unlike SNPD's DiaryDB, LetterDB is the only copy of a letter's text**: nothing can rebuild it. A letter whose row is missing shows "The ink has run; the letter can't be read."

## Was a delivery read?

SkyrimNet decides, not LetterDB. Each sending of a letter is a delivery with its own id (saved with the parcel), and the recipient's memory of it carries two tags: `physical_letters_letter:<letter id>` (which letter; the correspondence history counts it) and `physical_letters_delivery:<delivery id>` (which delivery). A reading first asks SkyrimNet (`PublicQueryMemoriesForActor`, `includeTags`) whether that delivery's memory exists, and again before storing. SkyrimNet's history reverts with a Clear and not with a Keep, so the answer is right in every case below without tracking timelines ourselves, and a retry or a reload never makes a second memory of one delivery. A letter sent again is a new delivery, so it's always read.


A letter an NPC writes first carries its letter tag on the **writer's** memory of writing it; that's how their later readings count it ([NPC_LETTERS.md](NPC_LETTERS.md#the-letter)).
## Scenarios

| Scenario | Result |
|---|---|
| Save → reload | Forms and parcels come back from the co-save; the text from LetterDB |
| Reload without saving | Forms made after the loaded save aren't in it; parcels are the save's. A letter sent after it is back in the player's inventory and never arrives. |
| Load a save from before a delivery, **Keep** | Delivered again (the same delivery, from the save's parcel); the reading finds its memory and stops: no second memory |
| Load a save from before a delivery, **Clear** | Delivered again; the memory went with Clear, so the recipient reads it again |
| Load a save made between delivery and reading | The parcel is awaiting reading and is read (or found read); a reply comes from LetterDB's stored reading if the memory already exists |
| Load a save from before a reply reached the courier | The reply's parcel is in that save and goes to the courier when due |
| A load while a reading is running | The result is dropped; the loaded save's own parcels decide what is read |
| Second character | Another SkyrimNet save id, so another LetterDB |
| An edited letter | The edit is a new LetterDB row (a new letter id); the old row stays (LetterDB only grows on edits). The form's `LFRM` key is the new id from the next save on, and the old id's "return to sender" mark is dropped. A save from before the edit (or a reload without saving) still has the old key, so the letter reads as it was then, and its `LTRN` marks are that save's. A second character never sees either: another LetterDB |
| NPC letters: a load while an attempt runs | The attempt's result is dropped (session generation); the loaded save's schedule decides when the next runs |
| NPC letters: a save from before an NPC wrote, **Keep** | The schedule and cooldowns are the save's (`LNPC`), and the letter's parcel isn't in it; the writer's tagged memory of writing stays, so their next reading lists a letter the player never got. Accepted: SkyrimNet's Keep keeps what the NPC lived through |
| NPC letters: the same, **Clear** | The memory goes with the rest of that history |
| NPC letters: a save without `LNPC` (older, or new) | The next letter is scheduled when the session is first ready |
| Letters between NPCs: a load while one is written, read or answered | The result is dropped (session generation, attempt token); the loaded save's parcels and schedule decide |
| Letters between NPCs: a save from before a letter, **Keep** | The letter's parcel isn't in the save; the writer's tagged memory of writing, and any reader's memory, stay (SkyrimNet's Keep) |
| Letters between NPCs: the same, **Clear** | The memories go with the rest of that history |
| A save made while a letter waited for the courier in town | It waits again, from the save's time; `WaitHours` then unseen delivery as usual ([COURIER.md](COURIER.md#saves)) |
| A save made, or a reload without saving, during the courier's errand | The errand is over for the DLL (a new session): `Transit::Tick` takes the letter back from the courier and delivers it unseen; the quest's next check sends him home. Not run in game yet |
| A save from before the courier states (`LTRN` v4) | Loads as is: its parcels can't be waiting for or with the courier |
| A save made, or a reload without saving, during a road encounter | The encounter is over for the DLL: `Transit::Tick` takes the letters back from the courier and they travel on; the quest's next check sends him home. Not run in game yet |
| A save from before routes (`LTRN` v5 or older) | Its letters have no route: they aren't met on the road |
| SkyrimNet missing or too old | Letters keep their look, show `...`, nothing is delivered; the log says why |

Tested in game on AE (2026-09-30): creation, sending, delivery after the delay, reading and the memory; Keep, Clear and a load during a reading; replies through the courier and the correspondence; the hand-over; returned letters and their card line. Not tested yet: a letter sent again (`read_before`), the retry path, a second character, SE and VR.
