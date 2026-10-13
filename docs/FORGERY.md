# Forgery

The player can write a letter in someone else's name, most likely to fool its recipient. Whether the recipient sees through it depends on the player's sleight of hand (Pickpocket), rolled when the letter is saved; written with the Quill of Gemination in Special Ink, it always passes. Code: `src/Writing.cpp` (the "From:" line, the roll), `Recipients::ResolveSender`, LetterDB's `signed_…`, `special_ink` and `forgery` columns ([PERSISTENCE.md](PERSISTENCE.md)).

**Status:** the "From:" line and the roll are built (2026-10-13), not run in game. What the recipient is told, their memory and replies to the named person are to come; the setting for those replies, `[NpcLetters] ForgeryReplies` (default on, MCM "NPCs reply to forged letters", [SETTINGS.md](SETTINGS.md)), is in but does nothing until then.

## The "From:" line

Every new letter the player writes opens with a "From:" line above "To:" ([WRITING.md](WRITING.md#the-text)), filled in with the player's name (`PlayerCharacter::GetName`). It has the same suggestions as "To:". On save (`OnSave`, after the recipient):

- **Exactly the player's name** (only spaces at either end ignored; another spelling or case is a name like any other): the player's own letter, nothing signed. The default, so it takes changing the line to forge.
- **A person:** "Name, address" as the "To:" line resolves it (`Recipients::Resolve`: the whole address, the same people, the same refusals for a name without one). A forgery in their name; its roll is taken (below). The recipient's own name is refused ("A letter can't be from the person it's to.").
- **A name that's nobody's** ("The Dark Brotherhood", "A friend"): a signed letter, with no person behind it, so no roll and no reply. No suggestions and no address make it plain to the player that the name is nobody's, so a typo of a real name isn't a silent forgery.
- **A dead person's name** (a unique person of that own name, dead, and nobody alive by it): refused ("<name> is dead: a letter can't come from them.").
- **Empty:** an anonymous letter, with no name at all (`anonymous`); no roll and no reply (user, 2026-10-13). Its "From:" line stays empty when read.

The letter keeps its author, the player (LetterDB's `author_uuid`: whose letter it is, who can edit it, who gets it back if it can't be delivered), and stores the "From:" line's sender beside it (`signed_uuid`, `signed_name`, `signed_address`; a signature has only the name, an anonymous letter none and `anonymous` set). An edit's "From:" line as it was loaded is its stored sender, by UUID, as the "To:" line (`Writing::ResolveFrom`). The item card names the sender the letter is signed with (an anonymous letter's, its author). Messages are English ([LOCALIZATION.md](LOCALIZATION.md#not-localized-yet)).

## The roll

Taken at save, once per saved letter (an edit is a new letter record, so a new roll), from the player's Pickpocket skill at that moment, and stored (`forgery`: 1 passes, 2 seen through). The chance it passes for the named person's hand: **25% at Pickpocket 15, 90% at 100**, in a line between (about 50% at 48), held at both ends (user, 2026-10-13: odds against a beginner; only the skill counts, not how well the recipient knows the hand). Fixed in code, no setting. In Special Ink (Ink & Quill's `InSpecialInk` at save) it always passes. The log names the skill, the chance and the outcome.
