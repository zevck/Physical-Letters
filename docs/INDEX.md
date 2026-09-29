# Physical Letters developer docs

The docs are the source of truth for how the code works. A change that makes a doc wrong fixes the doc in the same change.

| Doc | Covers |
|---|---|
| [ARCHITECTURE.md](ARCHITECTURE.md) | Components, the session, threading, code shared with SkyrimNet Physical Diaries |
| [PERSISTENCE.md](PERSISTENCE.md) | Letter forms, the co-save, LetterDB, and what happens on save, load, Keep and Clear |
| [READING.md](READING.md) | How a recipient reads a letter: the prompt, the memory, retries |
| [DELIVERY.md](DELIVERY.md) | Travel time (the engine's fast-travel formula), replies and the courier |

Much of the engine-facing code is shared with SkyrimNet Physical Diaries (SNPD), whose docs explain the engine behaviour it rests on: `docs/BOOK_FORMS.md` (runtime book forms the engine saves itself) and `docs/BOOK_TEXT.md` (the `GetDescription` hook, Win-1251 for Cyrillic).
