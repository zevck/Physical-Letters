# Writing letters

The player writes letters in the book menu through **Ink & Quill - Writing Framework** (its own repo; its `docs/API_DESIGN.md`), which owns the editor, `book.swf`, the writing materials (quill, ink, blood) and their costs. Physical Letters is a client: it owns the parchment, the recipient, and storing letters in LetterDB. **In progress:** only parchment exists; the rest is the plan below.

## Parchment

The blank letter. A book item, `PhysicalLettersParchment` (`0x8B6`), with the look of the vanilla note every letter uses (`Clutter\Books\Note01.nif`, as `WIDBAssassinLetter`), empty, value 2, weight 0.1. Code: `src/Parchment.cpp`.

- **Crafted** at a tanning rack: 1 Roll of Paper (Skyrim.esm `0x033761`) makes 3 parchment (`PhysicalLettersRecipeParchment`, `0x8B7`).
- **Sold** by general-goods merchants: the leveled list `PhysicalLettersLItemParchment` (`0x8B8`: 3 or 5 parchment) is added at `kDataLoaded` to Skyrim.esm's `LItemMiscVendorMiscItems75` (`0x09AF0A`) **in memory**, as Physical Diaries does with its blank journals: the list that already sells the Roll of Paper, rolled by 18 merchant chests. No vanilla record is overridden, so no patch is needed. A chest restocks every 48 game hours.
- **Why not the roll of paper itself:** it's a misc item, which the game can't read or use, and converting it would break other mods' recipes and scripts that count it.

## The plan

- **Writing** starts by reading parchment, registered with Ink & Quill as a blank (`RegisterBlank`): Ink & Quill notices it opened and starts a session with our marked text, a locked "To:" label before an ordinary run for the recipient, then the body.
- **Costs** are Ink & Quill's (`IQ_Costs`): it refuses without a quill, offers blood when there's no ink, and charges ink (or blood) and the parchment only when we accept the save; discarding costs nothing. Closing the book asks Save / Discard / Keep writing (Ink & Quill's prompt).
- **The recipient:** Ink & Quill has no suggestions or required fields (its decision, 2026-10-02). On save we match the typed name against the actors in memory (persistent NPCs anywhere, everyone in loaded cells), alive and named, generic NPCs by a `GenericRecipients` setting (0 unique only, 1 also generic NPCs SkyrimNet has memories of, 2 every named NPC); a save without exactly one match is refused with a message, so a letter always has a recipient. The match is registered with SkyrimNet at once (`PublicFormIDToUUID` registers an actor it hasn't seen).
- **Editing:** the player's own letters can be edited until sent. A change after the letter is saved makes a new LetterDB record, which the item then points to, so older saves keep their version.
