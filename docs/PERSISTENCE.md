# Persistence

Three places hold a letter's state, each for a reason:

| What | Where | Why there |
|---|---|---|
| The letter item | A runtime `TESObjectBOOK` form (`0xFF` FormID) the engine saves itself | Items in inventories and the world need a real form. See SNPD `docs/BOOK_FORMS.md`. |
| Which letter each form is | Co-save record `LFRM` | The save keeps only a form's flags |
| Letters in transit, awaiting reading, or on their way to the courier | Co-save record `LTRN` | Must revert with the save |
| A reply the courier holds | The courier's container (`WICourierContainerRef`) | The engine saves it like any other inventory |
| Each letter's text, author, recipient, reading | LetterDB | Written once, so one row serves every save of the character |

## Letter forms

Created by `DynamicForms::Create<TESObjectBOOK>()` (the engine picks the FormID) and given the look of the vanilla letter `Skyrim.esm 0x10596A` (WIDBAssassinLetter: Note01 model, no script) every session. **A letter form is never removed from the save**: a world copy of a dropped form would come back with an unrelated base and crash the game (SNPD `BOOK_FORMS.md`, fact 8). Letters aren't retired yet; nothing needs to get rid of one.

## The co-save

Unique ID `'SNPL'`:

- **`LFRM`** (version 2, DynamicForms' format): per form, FormID, form type, flags (bit 0 = retired), then three strings: key (the letter id), template EditorID (unused, empty), display name ("Letter to X").
- **`LTRN`** (version 2): per parcel, three strings (letter id, recipient UUID, recipient name), `dueAt` (double, game days), state (`uint8`: 0 in transit to an NPC, 1 delivered and awaiting reading, 2 a reply on its way to the courier; for state 2 the "recipient" is the player). Version 1, from dev builds only, had no state; it loads as in transit.

Strings are a `uint32` length and the bytes, at most 4096 (`include/CoSave.h`). The load callback fills the letter forms in from `LFRM` at once; their text follows when the session is ready.

## LetterDB

`Data/SKSE/Plugins/PhysicalLetters/SkyrimNet-<save id>/letters.db` (MO2: under `overwrite/`). `<save id>` is SkyrimNet's (`PublicGetSaveUniqueID`, digits and dashes only, anything else is refused). One DB per character, shared by all their saves, like SkyrimNet's own.

**`letters`**, one row per letter, `letter_id` (random 128-bit hex) the primary key:

| Column | Notes |
|---|---|
| `author_uuid`, `author_name`, `recipient_uuid`, `recipient_name` | SkyrimNet identities |
| `body` | The letter's plain text |
| `written_at`, `delivered_at` | Game days; `delivered_at` is 0 until delivered |
| `reading`, `memory_id` | The LLM's answer (JSON) and the SkyrimNet memory it became. A record only; see below |
| `in_reply_to` | For a reply, the id of the letter it answers ('' otherwise; replies from before this column have '' too, and so don't appear in the correspondence) |

New columns are added with `ALTER TABLE … ADD COLUMN … DEFAULT`, as in SNPD.

**Unlike SNPD's DiaryDB, LetterDB is the only copy of a letter's text**: nothing can rebuild it. A letter whose row is missing shows "The ink has run; the letter can't be read."

## Was a letter read?

SkyrimNet decides, not LetterDB: the recipient's memory of a letter is tagged `physical_letters_letter:<letter id>`, and a reading first asks SkyrimNet (`PublicQueryMemoriesForActor`, `includeTags`) whether that memory exists. SkyrimNet's history reverts with a Clear and not with a Keep, so the answer is right in every case below without tracking timelines ourselves.

## Scenarios

| Scenario | Result |
|---|---|
| Save → reload | Forms and parcels come back from the co-save; the text from LetterDB |
| Reload without saving | Forms made after the loaded save aren't in it; parcels are the save's. A letter sent after it is back in the player's inventory and never arrives. |
| Load a save from before a delivery, **Keep** | Delivered again; the reading finds the memory and stops: no second memory |
| Load a save from before a delivery, **Clear** | Delivered again; the memory went with Clear, so the recipient reads it again |
| Load a save made between delivery and reading | The parcel is awaiting reading and is read (or found read); a reply comes from LetterDB's stored reading if the memory already exists |
| Load a save from before a reply reached the courier | The reply's parcel is in that save and goes to the courier when due |
| A load while a reading is running | The result is dropped; the loaded save's own parcels decide what is read |
| Second character | Another SkyrimNet save id, so another LetterDB |
| SkyrimNet missing or too old | Letters keep their look, show `...`, nothing is delivered; the log says why |

Tested in game on AE (2026-09-29): creation, sending, delivery after the delay, reading and the memory. The retry path, Keep and Clear, and a second character are not tested yet.
