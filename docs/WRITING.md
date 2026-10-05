# Writing letters

The player writes letters in the book menu through **Ink & Quill - Writing Framework** (its own repo; its `docs/API.md` and `docs/API_DESIGN.md`), which owns the editor, `book.swf`, the writing materials (quill, ink, blood), their costs, and the close prompt (Save / Discard / Keep writing). Physical Letters is its client: it owns the parchment, the letter's text and recipient, and storing letters in LetterDB. Code: `src/Writing.cpp` (the client), `Letters::Reading` and `Letters::Marked` (the text), `include/InkAndQuillAPI.h` (Ink & Quill's header, copied).

**Status:** working on AE ([Testing](#testing)).

## Parchment

The blank letter. A book item, `PhysicalLettersParchment` (`0x8B6`), with the vanilla note model (`Clutter\Books\Note01.nif`), empty, value 2, weight 0.1. Every letter copies its look from it ([PERSISTENCE.md](PERSISTENCE.md)), so a written letter looks like the parchment. Code: `src/Parchment.cpp`.

- **Crafted** at a tanning rack: 1 Roll of Paper (Skyrim.esm `0x033761`) makes 3 parchment (`PhysicalLettersRecipeParchment`, `0x8B7`).
- **Sold** by general-goods merchants: the leveled list `PhysicalLettersLItemParchment` (`0x8B8`: 3 or 5 parchment) is added at `kDataLoaded` to Skyrim.esm's `LItemMiscVendorMiscItems75` (`0x09AF0A`) **in memory**, as Physical Diaries does with its blank journals: the list that already sells the Roll of Paper, rolled by 18 merchant chests. No vanilla record is overridden, so no patch is needed. A chest restocks every 48 game hours.
- **Why not the roll of paper itself:** it's a misc item, which the game can't read or use, and converting it would break other mods' recipes and scripts that count it.

## Starting

`Writing::Connect` finds Ink & Quill's API at `kPostLoad`; `Writing::Register` (at `kDataLoaded`, only if its writing is on) registers parchment as a blank (`RegisterBlank`) and adds an owner (`AddOwner`). Without Ink & Quill, or with its writing off (`Writing::Available`), letters can't be written or edited; the log says which. Parchment is then kept out of the game (user, 2026-10-04: an empty note nobody can use is junk): `Parchment::OnDataLoaded` doesn't add it to the merchants' list and clears its recipe's bench keyword (`0x8B7`), so no tanning rack lists it. Both are in memory only, every startup, so installing Ink & Quill later brings it back. Parchment the player already carries stays an empty note. Everything else (NPCs writing to the player and to each other, delivery, reading, handing letters over) works without it.

- **A new letter:** reading parchment from the player's own inventory (Ink & Quill checks where) begins a session at once, the caret after "To: ". Nothing is made until the first save, which turns one parchment into the letter (`ReplySaveAsBook`).
- **Editing:** the edit key on a letter the player wrote and carries (so not sent) begins a session on its text. Anyone else's letter, or one read from a container or the world, isn't ours: the key does nothing.
- **Not ready:** before the session is ready (SkyrimNet and LetterDB, [ARCHITECTURE.md](ARCHITECTURE.md)) parchment shows a HUD notice and doesn't open for writing.

Ink & Quill checks the quill, offers blood when there's no ink, and charges ink (or blood) and the parchment only when a save is accepted; discarding costs nothing.

## The text

A player's letter opens with a "To:" line (`Strings::ToLabel`), in its reading text too, so the editor and the page match. Letters to the player and between NPCs have none.

The label is in the chosen language ("An:" in German, "宛先：" in Japanese), from the locale files ([LOCALIZATION.md](LOCALIZATION.md)). It isn't stored: a letter shows the current language's.

Marked text (Ink & Quill's: what the player can't change is between U+E002 and U+E003), in the same handwriting font tags as the reading text:

```
<font face='$HandwrittenFont' size='14'>[To: ]<name, address>[\n\n</font>]<body paragraphs>[]
<font face='$HandwrittenFont' size='14'>[To: ]<name, address>[\n\n</font><body paragraphs>]      (the body locked)
```

The size is `[General] FontSize` (14 by default, [SETTINGS.md](SETTINGS.md)), on every font tag, and Ink & Quill gets the same face and size for typed text (`runFont`, `runSize`), so a new paragraph has the size the rendered page gives it.

`<body paragraphs>` is the same for every letter (the player's, NPCs', between NPCs). **Reading text:** the whole body is one `<font …>` tag, and every blank line in it holds a non-breaking space (`&nbsp;`; a line with only blood's tags counts as blank). The book menu finds each line by its first character, so an empty line never starts a new page: a letter whose overflow was blank lines read as one page, and page 2's leading blank lines vanished. With no empty line left, nothing resets the font, so one tag is enough. This is the game's own layout, used with or without Ink & Quill (2026-10-05). **Marked text:** each paragraph is `<font …>text</font>` and each `\n\n` between paragraphs is `<font …>\n\n</font>` (the editor's blank lines stay empty, and Skyrim resets the font after an empty line); an empty paragraph has no tag of its own. Every break is in the handwriting at the letter's size, so a blank line is as tall as a written one. Ink & Quill gives typed text, breaks included, the session's `runFont` and `runSize`, which are the same face and size, so the page doesn't move when a letter is saved or edited again (2026-10-04/05, AE; SNPD renders journals the same way). This needs Ink & Quill's break sizing from those values (uncommitted in Ink & Quill on 2026-10-05): with its older rule, typed breaks get the page's size and the text jumps on saving. Earlier here, breaks outside the tags were drawn in the page's size too, with the same jump.

`[…]` is locked. Run 0 is the recipient's name, run 1 the body. Enter keeps the caret in its run, so a player who types the name, presses Enter and writes on has written everything in run 0: on save, run 0's first line is the name and any further lines begin the body (tested on AE: the body run stayed empty and the save was refused, before this). The empty lock at the end keeps an empty body a run (Ink & Quill makes text after the last lock a run only if there is any). The reading text is the same without the markers, converted for the book menu (`TextHook::ForBookMenu`: Win-1251 for Cyrillic); marked text never is.

**The body is locked while the "To:" line names nobody:** a new letter begins with everything after the name locked (`Letters::Marked(letter, true)`: one run, the "To:" line), so the line can't be left. At every change to it (`onChange`), `Recipients::Resolve` decides: naming one person (the full name and address) reloads the editor with the body as a run again (`from` 0 and, for an edit, `-2 - 1`: Ink & Quill counts it as saved with its text when the session began, so a body reopened unchanged is no change, no close prompt and no ink; a new letter's body had no run then, so -1), and naming nobody locks it again, its text kept (`g_lockedBody`) and shown, given back when it opens. The caret stays where it was. Applies to edits too (their name is valid from the start, so they begin open). The line is one line: a line break typed or pasted into it is taken out in the same `onChange`, before it's drawn, and Enter on a line that names someone moves the caret to the start of the body (on one that doesn't, it does nothing). Before this, Enter added lines to the "To:" run, so a letter could be written there with the body locked. (First built so the body stayed open once opened; changed after testing on AE, 2026-10-03: clearing the name left it open.)

**Blood:** text written in blood comes back between U+E000 and U+E001. `Letter::body` (and LetterDB's `body`) is the text without them, which is what prompts, narrations and memories read; `blood` keeps the marked body when any of it is in blood, and the book shows that part red (`#2B0202`, Ink & Quill's colour). **A letter begun in blood** has a red "To:" line (label, name and address), as Physical Diaries' headings: decided when a parchment's session starts (`WouldBeInBlood`), kept with the letter (`Letter::bloodHeading`, LetterDB's `blood_heading`), so it stays red when read and edited. Its reader is told: the reading prompts get `letter.blood` ("all" or "part") and the passages, the reading's memory and the hand-over narration say so ([READING.md](READING.md#the-prompt)).

## The recipient

Code: `src/Recipients.cpp`. The "To:" line's first line is a name, optionally followed by a comma and an address: "Karita" or "Karita, 6391 Dawnstar".

**Who a name stands for.** Every actor reference in memory (`RE::TESForm::GetAllForms`: persistent references anywhere, the rest only while their cell is loaded) of an NPC race, not the player, not deleted, disabled or dead, whose display name is the name (ASCII case-insensitive). Then:

- **`[Writing] GenericRecipients`** ([SETTINGS.md](SETTINGS.md)): unique NPCs always, and only them at 0 (the default); generic ones too at 1 if SkyrimNet has memories of them (`SkyrimNet::HasMemories`, one short query each, kept for the session), or all at 2.
- **One reference per unique NPC:** several references of one base are the same person; the one kept is loaded, then persistent, then the lowest FormID.
- **Disabled actors never count** (user, 2026-10-03): they're mostly disabled for a reason (cut content, quest doubles, a replacer's original, someone not in the world yet), and allowing them caused more trouble than it solved. Lydia, disabled in Breezehome until the player is Thane, becomes addressable when the game enables her.

**Addresses.** Every person of a name gets one: `<number> <place>`.

- **The number** is the end of their SkyrimNet UUID: 4 digits, more when two of the name's people share their last 4. It's their real SkyrimNet identity (UUIDs are deterministic: the same actor has the same UUID in every save), so it can be matched against what SkyrimNet shows. Getting it registers the actor with SkyrimNet (`PublicFormIDToUUID`), so only a complete name's people are registered, never those of a prefix.
- **The place** is where they belong: their reference's persistent location (`Actor::GetEditorLocation1`, e.g. Lydia's is `WhiterunLocation`), else where they are now; named by the location just below its hold (the town: Breezehome is "Whiterun"), or the hold itself. A location that isn't under a hold (`LocTypeHold`, `0x016771`), such as the holding cells quest NPCs start in (`HoldingCell`, `0x10D4BA`, has no name, parent or keywords), or that has no name, gives "Tamriel" (`Strings::kNoPlace`): nothing reliably says which land it is in.
- **Order:** the people SkyrimNet has memories of first (the one you know is the one you mean), then by FormID.

**Resolving** (`Recipients::Resolve`, on save, and at every change to the line for the body's lock): only a whole "Name, address" names someone (ASCII case-insensitive), whether the name is shared or not: a suggestion fills the address in (Tab picks among several, Right accepts). A name alone, or part of an address, doesn't (user, 2026-10-03: a recipient is locked in only once the full name and matching address are written). Otherwise the save is refused with a message: nobody of that name; one person (their address, to write after the name); several (listing their addresses, with an example); or an address none of them has (listing them).

**An edit's recipient** is the stored letter's while its "To:" line reads as it was loaded (`Writing::ResolveLine`): the same UUID, name and address, with no search, so a letter whose recipient the game or SkyrimNet now names differently (a quest NPC renamed after SkyrimNet registered them) can still be edited and saved. A changed line is searched as any other. The UUID is what decides who gets the letter.

**More than 10 people of one name** (generic names at `GenericRecipients` 2) aren't addressed: the save says there are too many to address one of them, and the name isn't suggested.

The letter stores the address it was saved with (LetterDB's `address`) and shows "To: Karita, 6391 Dawnstar" when read; letters from before addresses show the name alone. `PublicFormIDToUUID` has registered the recipient by then, and the stored name is SkyrimNet's.

## The name as you type

Ink & Quill's suggestions (its `Suggest`, docs/API.md › Suggestions) show what could follow on the "To:" line, faded after the caret, one at a time: **Tab** and **Shift+Tab** cycle, **Right** accepts (typed as if by the player), **Escape** dismisses. Ink & Quill draws them and owns the keys; Physical Letters only gives the candidates. The player can still type everything.

- **When:** `onChange` (Ink & Quill tells a session the player changed a run, in the key's own UI task, with the caret's offset). For run 0 on one line with the caret at its end, `Recipients::Completions` gives the candidates and `Suggest` shows them with the keystroke; anything else clears them.
- **The candidates,** from 2 letters on:
  - an incomplete name: the rest of each name with people that starts with what's typed ("Lyd": "ia"; a trailing space counts: "Aela ": "the Huntress");
  - a complete name: ", " and each of its people's addresses, the people SkyrimNet knows first (", 6391 Dawnstar", then ", 2207 Throat of the World"), ahead of any longer names that start the same;
  - after the comma: the rest of each address that starts with what's typed.

  At most 10. A complete name registers its people with SkyrimNet for their numbers.
- **Accepting** types the completion, so `onChange` fires again: accepting "ia" offers the address next.
- **Long completions** aren't wrapped: one at the end of a line is clipped at the page edge (Ink & Quill's overlay).
- **Cost:** each change to run 0 scans the form map once (`Recipients::NewText`, then `Resolve` for the body's lock and `Completions` filter that scan); SkyrimNet's answers (UUIDs, names, memories) and the places are kept per actor for the session. SkyrimNet is asked about memories only for a complete name's people, so at `GenericRecipients` 1 a generic NPC's name is suggested once it has been typed in full in that session, never while it's being typed. Changes to the body cost nothing.
- **History:** before `Suggest`, the preview was locked faded text after the name, replaced with `Reload` (first from a watcher polling every 150 ms, then 16 ms, then from `onChange`); it couldn't cycle, and typed text took the faded colour from text before it.

## Saving

`OnSave` gets the runs (one while a new letter's body is still locked). Refused with a message (writing goes on, nothing is charged), in this order: the session isn't ready; no name ("The letter isn't addressed yet…"); no single recipient (the reasons in [The recipient](#the-recipient)); an empty body; a failure (logged). The recipient comes before the body, so a letter with a wrong name says so rather than that nothing is written. The body is stored as written, trimmed only at the end: blank lines or spaces before the first paragraph are kept (user, 2026-10-04; until then a full trim dropped them). A body of nothing but blank lines counts as empty. How its blank lines are rendered: [The text](#the-text).

- **Parchment:** a new letter (`Letters::Create`), put in the player's inventory; `ReplySaveAsBook` has Ink & Quill take one parchment and show the letter in the open menu.
- **An edit:** a new LetterDB record (a new letter id) that the item now holds (`Letters::Rewrite`: its DynamicForms key, name, text, item card); the old record stays, so other saves of the character keep their version. A "return to sender" mark goes with the old record. If only the name's spelling changed (the same recipient, address, body and blood), nothing new is stored.
- Either way the letter is the player's (`authorUuid` theirs), written now, and is posted or handed over as any other ([DELIVERY.md](DELIVERY.md), [HAND_IN.md](HAND_IN.md)).

A save ends the session (Ink & Quill's rule): writing again is the edit key.

## Testing

Tested on AE (2026-10-03): writing on parchment, the "To:" suggestions (Tab, Right), the full-address rule, the body's lock (opening, and locking again on backspace), Enter on the "To:" line, saving, and mailing the letter. Not yet run: no quill, blood (shown red, and the "To:" line's suggestions while writing in blood), editing a letter (new record, recipient change, the old record in an older save), a Cyrillic letter, a letter to a generic NPC at each `GenericRecipients` setting, SE and VR.
