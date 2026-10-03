# Physical Letters developer docs

The docs are the source of truth for how the code works. A change that makes a doc wrong fixes the doc in the same change.

| Doc | Covers |
|---|---|
| [ARCHITECTURE.md](ARCHITECTURE.md) | Components, the session, threading, code shared with SkyrimNet Physical Diaries |
| [PERSISTENCE.md](PERSISTENCE.md) | Letter forms, the co-save, LetterDB, and what happens on save, load, Keep and Clear |
| [READING.md](READING.md) | How a recipient reads a letter: the prompt, the memory, retries |
| [DELIVERY.md](DELIVERY.md) | Travel time (fast travel's navmesh path), the hand-over, undeliverable letters, replies and the courier |
| [COURIER.md](COURIER.md) | The vanilla courier carrying a letter to an NPC in the player's town: the errand, other courier mods, when it falls back to unseen delivery |
| [ROAD_COURIER.md](ROAD_COURIER.md) | The courier met on the road with the letters passing there: vanilla's road triggers, detecting a passing letter, the encounter, the dialogue (intimidate, brawl) |
| [HAND_IN.md](HAND_IN.md) | Handing a letter to its recipient in person: the recipient aliases and their faction, the dialogue, the gift menu, delivery |
| [WRITING.md](WRITING.md) | Writing letters (in progress): parchment, and the plan as an Ink & Quill client |
| [NPC_LETTERS.md](NPC_LETTERS.md) | NPCs writing to the player first: who, when, the prompt, cooldowns |
| [NPC_TO_NPC.md](NPC_TO_NPC.md) | Letters between NPCs: the limits, an attempt (a cheap call proposes recipients, code checks them, the letter), threads |
| [SETTINGS.md](SETTINGS.md) | The INI, the MCM, adding a setting |
| [PLUGIN.md](PLUGIN.md) | The ESP: Spriggit source in git, building it, editing it in the CK; its Papyrus scripts and SEQ file |

Much of the engine-facing code is shared with SkyrimNet Physical Diaries (SNPD), whose docs explain the engine behaviour it rests on: `docs/BOOK_FORMS.md` (runtime book forms the engine saves itself) and `docs/BOOK_TEXT.md` (the `GetDescription` hook, Win-1251 for Cyrillic).
