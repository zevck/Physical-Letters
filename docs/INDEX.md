# Physical Letters developer docs

The docs are the source of truth for how the code works. A change that makes a doc wrong fixes the doc in the same change.

| Doc | Covers |
|---|---|
| [ARCHITECTURE.md](ARCHITECTURE.md) | Components, the session, threading, code shared with SkyrimNet Physical Diaries |
| [PERSISTENCE.md](PERSISTENCE.md) | Letter forms, the co-save, LetterDB, and what happens on save, load, Keep and Clear |
| [READING.md](READING.md) | How a recipient reads a letter: the prompt, the memory, the event that they received it, retries |
| [DELIVERY.md](DELIVERY.md) | Travel time (fast travel's navmesh path), the hand-over, undeliverable letters, replies and the courier |
| [COURIER.md](COURIER.md) | The vanilla courier carrying a letter to an NPC in the player's town: the errand, other courier mods, when it falls back to unseen delivery |
| [ROAD_COURIER.md](ROAD_COURIER.md) | The courier met on the road with the letters passing there: vanilla's road triggers, detecting a passing letter, the encounter, the dialogue (intimidate, brawl) |
| [HAND_IN.md](HAND_IN.md) | Handing a letter to its recipient in person: who the topic shows to (the recipients' faction), the dialogue, the gift menu filtered to their letters, delivery |
| [WRITING.md](WRITING.md) | Writing letters as an Ink & Quill client: parchment, the "From:" and "To:" lines, matching the recipient, saving and editing |
| [FORGERY.md](FORGERY.md) | Letters in someone else's name: the "From:" line, the roll at save, Special Ink |
| [DM_LETTER.md](DM_LETTER.md) | The DM Letter: a letter from one NPC to another, sent by the player as the story's dungeon master; the page, the check box, sending as written or rewritten |
| [PACKAGES.md](PACKAGES.md) | Items sent with letters: the package chests in the courier's cell, packing, manifests, where a package goes |
| [API.md](API.md) | The API for other mods: sending letters (Papyrus and C), the delivered/read/failed events, letter actions a reader may choose |
| [NPC_LETTERS.md](NPC_LETTERS.md) | NPCs writing to the player first: who, when, the prompt, cooldowns |
| [NPC_TO_NPC.md](NPC_TO_NPC.md) | Letters between NPCs: the limits, an attempt (a cheap call proposes recipients, code checks them, the letter), threads |
| [SETTINGS.md](SETTINGS.md) | The INI, the MCM, adding a setting |
| [RELEASES.md](RELEASES.md) | The release archive: `Build_Release.ps1`, what it packs and checks |
| [LOCALIZATION.md](LOCALIZATION.md) | Which language, the locale files for letter text (names, cards, the "To:" label), renaming saved letters |
| [PLUGIN.md](PLUGIN.md) | The ESP: Spriggit source in git, building it, editing it in the CK; its Papyrus scripts and SEQ file |

Much of the engine-facing code is shared with SkyrimNet Physical Diaries (SNPD), whose docs explain the engine behaviour it rests on: `docs/BOOK_FORMS.md` (runtime book forms the engine saves itself) and `docs/BOOK_TEXT.md` (the `GetDescription` and `OpenBookMenu` hooks, VR's ninth `OpenBookMenu` argument, Cyrillic in UTF-8).
