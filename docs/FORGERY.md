# Forgery

The player can write a letter in someone else's name, most likely to fool its recipient. Whether the recipient sees through it depends on the player's sleight of hand (Pickpocket), rolled when the letter is saved; written with the Quill of Gemination in Special Ink, it always passes. Code: `src/Writing.cpp` (the "From:" line, the roll), `Recipients::ResolveSender`, LetterDB's `signed_…`, `forgery` and `anonymous` columns ([PERSISTENCE.md](PERSISTENCE.md)).

**Status:** runs in game (AE, 2026-10-13): the "From:" line and the roll; forgeries that passed (Matlara, Ingrid and Gunnar in Torolf's name, the replies reaching Torolf), one seen through (Skalei: "clearly a forgery", no reply), a signature (Mikael, "Pee Pee Poo Poo Man": no reply) and an anonymous letter (its card).

## The "From:" line

Every new letter the player writes opens with a "From:" line above "To:" ([WRITING.md](WRITING.md#the-text)), filled in with the player's name (`PlayerCharacter::GetName`). It has the same suggestions as "To:". On save (`OnSave`, after the recipient):

- **Exactly the player's name** (only spaces at either end ignored; another spelling or case is a name like any other): the player's own letter, nothing signed. The default, so it takes changing the line to forge.
- **A person:** "Name, address" as the "To:" line resolves it (`Recipients::Resolve`: the whole address, the same people, the same refusals for a name without one). A forgery in their name; its roll is taken (below). The recipient's own name is refused ("A letter can't be from the person it's to."), and so is a name more than 10 people share ("Too many people are named … to sign as one of them.").
- **A name that's nobody's** ("The Dark Brotherhood", "A friend"): a signed letter, with no person behind it, so no roll and no reply. The whole line is kept as written, anything after a comma included. No suggestions and no address make it plain to the player that the name is nobody's. A name the lookup can't match (a variant such as "Torolf.", or someone not in memory or disabled) is a signature too, read with no roll: accepted as it is (user, 2026-10-13).
- **A dead person's name** (a unique person of that own name, dead, and nobody alive by it): refused ("<name> is dead: a letter can't come from them.").
- **Empty:** an anonymous letter, with no name at all (`anonymous`); no roll and no reply (user, 2026-10-13). Its "From:" line stays empty when read.

The letter keeps its author, the player (LetterDB's `author_uuid`: whose letter it is, who can edit it, who gets it back if it can't be delivered), and stores the "From:" line's sender beside it (`signed_uuid`, `signed_name`, `signed_address`; a signature has only the name, an anonymous letter none and `anonymous` set). An edit's "From:" line as it was loaded is its stored sender, by UUID, as the "To:" line (`Writing::ResolveFrom`). The item card names the sender the letter is signed with; an anonymous letter's names only its recipient ("A letter to Lydia.", locale key `CardTo`; the author until 2026-10-13, which gave the player away in their own inventory). Messages are English ([LOCALIZATION.md](LOCALIZATION.md#not-localized-yet)).

## The roll

Taken at save, once per saved letter (an edit is a new letter record, so a new roll), from the player's Pickpocket skill at that moment, and stored (`forgery`: 1 passes, 2 seen through). The chance it passes for the named person's hand: **25% at Pickpocket 15, 90% at 100**, in a line between (about 50% at 48), held at both ends (user, 2026-10-13: odds against a beginner; only the skill counts, not how well the recipient knows the hand). Fixed in code, no setting. In Special Ink (Ink & Quill's `InSpecialInk` at save) it always passes; that's all Special Ink changes, so it isn't stored. The log names the skill, the chance and the outcome.

## Reading it

Who a letter is from, to its reader, is `Letters::SenderOf`: the person a forgery names (seen through or not), a signature's name with nobody behind it, nobody for an anonymous letter, else the author. Everything the reader is told or keeps uses it, never the author of a signed letter (the player):

- **The reading prompt** ([READING.md](READING.md#the-prompt)): `letter.author` is that name ("" when anonymous) and `author_UUID` that person's (0 when nobody); `letter.named` says there's a person behind the name (their profile is shown: "About X, Who Wrote It", or "Whose Name It Bears" for a forgery seen through), `letter.anonymous` that it's unsigned (the template says so in place of a "From:" line), `letter.forged` that a forgery is seen through: one line after the letter, that the handwriting gives it away, someone else wrote it with that person's name, and the reader can't tell who (user, 2026-10-13: the outcome is a fact for the model to play, not a judgement it makes). A forgery that passes reads exactly as a letter from that person.
- **The earlier letters** (`Reading::Correspondence`, `LetterDB::Between`): a forgery counts as a letter between the reader and the person it names, never between the reader and the player; a signature or an anonymous letter is in nobody's correspondence, and neither is a forgery the reader saw through (their memory holds it, as forged; listed as a letter from that person it would read as genuine). So the player's letters to an NPC never show their forgeries, and an NPC fooled twice remembers both as that person's.
- **The memory:** "The letter from <name>:" (or "The unsigned letter:"), the forgery sentence when seen through (`Letters::ForgerySentence`), and the related actor is the person named, nobody for a signature or an anonymous letter (never the player as a fallback: that would give the forger away).
- **Handed over in person** ([HAND_IN.md](HAND_IN.md#reading-it-there)): the narration says "… a letter from <name>" (or "…, unsigned") and the forgery sentence when seen through. The player is the one handing it over, so a reader who sees through it can draw their own conclusions; nothing more is told.

**Two layers, on purpose** (user, 2026-10-13). The roll decides only whether the forgery *shows*: the handwriting, from the player's skill, certain in Special Ink. Whether what it says is believable is the reader's own judgement: the prompt says nothing about it, and the model weighs the letter against what the reader knows of the person. Tested on AE: two letters to Matlara in her husband Torolf's name both passed the roll (Pickpocket 10, then Special Ink), and she rejected both on their content ("I know my husband, and this is not his voice"), which is the reaction wanted: one forged letter shouldn't end a marriage. Letting the model decide whether a forgery shows was considered and not done: the skill and Special Ink would stop meaning anything.

## Replies

- **A forgery that fools the reader** may be answered to the person it names, with `[NpcLetters] ForgeryReplies` on (default; MCM "NPCs reply to forged letters", [SETTINGS.md](SETTINGS.md)). `Transit::MakeReply` sends it to them as a letter between NPCs: from there it's an ordinary thread ([NPC_TO_NPC.md](NPC_TO_NPC.md#keeping-it-bounded)) in which the forgery is letter 1, so the reader's reply itself needs room in the thread (`NpcToNpc::CanReply`: none at `MaxLettersPerThread` 1), and with 3 the named person may answer once. They read it with no instruction about the forgery: they have no memory of writing the letter it answers, and the model works it out (user, 2026-10-13: try it without first). A reader who doubts the letter may still write to the person it names: Matlara wrote to Torolf asking whether he wrote it or had been hexed, and Torolf, given nothing about the forgery, answered that he had never heard of the man it named and was coming home (AE, 2026-10-13). Kept as it is (user). The thread is the player's doing, apart from the schedule's open-thread limit (`NpcToNpc::OpenThreads` doesn't count a thread whose first letter isn't between NPCs), but its two NPCs are busy in it like any other: the schedule doesn't start another thread with either while it's open.
- **A forgery seen through, a signature, an anonymous letter:** no reply (`can_reply` false): there's nobody to write to.
- **The player's own letter:** `[NpcLetters] Replies`, as before.

`Transit::CanReplyTo` decides, at the reading and again when its reply comes back.

## Not done

- A reader guessing who wrote an unsigned or seen-through letter and writing to them (user's idea, 2026-10-13): parked; replies to forgeries have now been seen in game, so it's for the user to take up.
