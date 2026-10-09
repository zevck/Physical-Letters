# Settings and the MCM

Code: `include/Config.h`, `src/Papyrus.cpp`, `Source/Scripts/PhysicalLetters_MCM.psc`. Adapted from SNPD's `Config.h` and MCM (SNPD `docs/CONFIG_AND_MCM.md`).

## The INI

`Data/SKSE/Plugins/PhysicalLetters.ini`. Loaded in `SKSEPlugin_Load` and **written straight back**, so MO2 puts it in `overwrite/` and mod updates don't replace it. No INI ships with the mod. The MCM saves each change at once.

| Key | Default | Range | Effect |
|---|---|---|---|
| `[General] Language` | *(unset)* | a locale name | Letter text in this language instead of the game's (`GERMAN`); INI only, the one string setting ([LOCALIZATION.md](LOCALIZATION.md#which-language)). Read at startup |
| `[General] DebugLog` | 0 | 0/1 | Log level `debug` instead of `info`; live from the MCM |
| `[General] FontSize` | 14 | 8–24 | Size of letter text, every letter's (written, received, between NPCs), as SNPD's `[Fonts] ContentSize` (same default and range). Also Ink & Quill's size for typed text (`runSize`). A change in the MCM renders every letter in the save again at once |
| `[General] Font` | 0 | 0–2 | Face of letter text, every letter's, and Ink & Quill's for typed text (`runFont`): 0 handwriting (`$HandwrittenFont`, the vanilla letters'), 1 the UI's font (`$EverywhereFont`), 2 the books' font (`$SkyrimBooks`): SNPD's three choices for `[Fonts] FontFace` (user, 2026-10-05), for a language whose handwriting is hard to read. A number, not a face name as SNPD stores it, so its range lives in `kSettings` like every other setting (`Letters::FontFace` maps it). A change in the MCM renders every letter again at once |
| `[Delivery] Postage` | 20 | 0–1000 | Gold an innkeeper or the courier charges. Written into the ESP's global `PhysicalLettersPostage`, which the topic text and the gold conditions read ([DELIVERY.md](DELIVERY.md#the-hand-over)) |
| `[Delivery] WritingHours` | 12 | 0–168 | Game hours an NPC takes to write back, before the reply travels |
| `[Delivery] MinHours` | 2 | 0–48 | Shortest travel time ([DELIVERY.md](DELIVERY.md#travel-time)) |
| `[Delivery] FallbackHours` | 48 | 1–336 | Travel time between worldspaces, or when a place can't be found |
| `[Delivery] ReturnAfterDays` | 3 | 1–30 | Game days a due letter waits for a recipient who can't be found, or has died (the courier finds out), before the courier brings it back ([DELIVERY.md](DELIVERY.md#undeliverable-letters)) |
| `[Delivery] HandInDialogue` | 1 | 0/1 | The "I have a letter for you." topic, shown to an NPC the player carries a letter for ([HAND_IN.md](HAND_IN.md#the-dialogue)): the way to hand a letter over in person. Written into the ESP's global `PhysicalLettersHandInDialogue`, which the topic's condition reads. Off, letters can only be posted |
| `[Courier] Enabled` | 1 | 0/1 | The courier brings letters in person to NPCs in the player's town ([COURIER.md](COURIER.md)) |
| `[Courier] WaitHours` | 2 | 0–24 | Game hours a letter for someone in the player's town waits for the courier before it goes in unseen |
| `[Courier] RoadEncounters` | 1 | 0/1 | The courier may be met on the road with letters passing there ([ROAD_COURIER.md](ROAD_COURIER.md)) |
| `[Courier] RoadCooldownDays` | 3 | 0–30 | Game days after meeting him on the road before he can be met again |
| `[Courier] IntimidateSpeech` | 40 | 0–100 | Speech needed to threaten the courier on the road into handing over his letters (or the Intimidation perk); otherwise he refuses and only a brawl gets them |
| `[Courier] RobberyBounty` | 40 | 0–1000 | Bounty (gold, non-violent) the courier reports in that hold when threatened or brawled into handing over his letters; 0 = none |
| `[NpcLetters] Enabled` | 1 | 0/1 | NPCs write to the player first ([NPC_LETTERS.md](NPC_LETTERS.md)) |
| `[NpcLetters] Replies` | 1 | 0/1 | NPCs may write back to the player's letters ([READING.md](READING.md)); off, they read and remember them but never answer (the prompt is told not to reply, and a reply is never kept). Letters between NPCs aren't affected |
| `[NpcLetters] IntervalDays` | 7 | 1–60 | Game days between attempts, times 0.75–1.25 at random (5¼–8¾ at 7) |
| `[NpcLetters] CooldownDays` | 14 | 0–120 | After an NPC writes to the player first, game days before they may write first again; replies are never blocked by it and don't start it |
| `[NpcLetters] MinEvents` | 5 | 0–200 | Events with the player SkyrimNet must have recorded for an NPC to be picked; 0 also lets people the player never dealt with write (anyone SkyrimNet has registered; generic NPCs only with memories), each weighted as a quarter of one event ([NPC_LETTERS.md](NPC_LETTERS.md#who)) |
| `[NpcLetters] MinDaysApart` | 1 | 0–30 | Game days since their last exchange with the player below which an NPC isn't picked (0 = off) |
| `[NpcLetters] CandidatesPerAttempt` | 3 | 1–10 | NPCs drawn each attempt; with more than one, a cheap call (`meta`) picks who writes, then one full call writes it ([NPC_LETTERS.md](NPC_LETTERS.md#the-pick)) |
| `[NpcLetters] DaysUntilMissed` | 7 | 1–60 | Ex-followers, the spouse and adopted children are twice as likely to write after this many game days apart, rising to it from the day they last saw the player; nobody else's chance changes with time apart |
| `[NpcToNpc] Enabled` | 1 | 0/1 | NPCs write to each other ([NPC_TO_NPC.md](NPC_TO_NPC.md)); needs `PublicSearchActors` |
| `[NpcToNpc] KnownOnly` | 0 | 0/1 | Only NPCs the player knows (`NpcLetters.MinEvents`) write; else anyone SkyrimNet has registered |
| `[NpcToNpc] IntervalDays` | 5 | 1–60 | Game days between attempts to start a correspondence, times 0.75–1.25 at random |
| `[NpcToNpc] MaxOpenThreads` | 3 | 1–10 | Correspondences between NPCs running at once |
| `[NpcToNpc] MaxLettersPerThread` | 3 | 1–10 | Letters in one correspondence, the first and every reply |
| `[NpcToNpc] PairCooldownDays` | 21 | 0–120 | Game days after a correspondence ends before the same two start another |
| `[NpcToNpc] WritersPerAttempt` | 4 | 1–10 | People drawn per attempt; one cheap call proposes recipients for all of them ([NPC_TO_NPC.md](NPC_TO_NPC.md#an-attempt)) |
| `[NpcToNpc] NamesPerWriter` | 3 | 1–5 | Recipients proposed per person, best first; the first usable one is kept |
| `[NpcToNpc] MemoriesPerWriter` | 3 | 0–10 | Newest memories each person's proposal and letter see (most involve the player; 0 = none) |
| `[Writing] GenericRecipients` | 0 | 0–2 | Whom the player's letters can be addressed to besides unique NPCs: 0 nobody else, 1 generic NPCs SkyrimNet has memories of, 2 any NPC with the name ([WRITING.md](WRITING.md#the-recipient)). MCM: a menu |

**Every setting is one row in `Config::kSettings`**: section, key, default, range. Reads clamp to the range and fall back to the default on a non-number, so a hand-edited INI can't feed the code nonsense. `Save()` writes only the keys in the table (anything else is dropped at the next start), building the file in memory first so a failure can't leave it half-written.

**Postage and saves:** a save stores a global's value, so `Postage::ApplyPrice()` sets the global again on every new game and load, and when the MCM changes it.

## The MCM

`PhysicalLetters_MCM` (`extends SKI_ConfigBase`) on the start-game quest `PhysicalLettersMCMQuest` (`0x808`), whose player alias runs `SKI_PlayerLoadGameAlias`, as SNPD's does. Needs SkyUI; without it the INI still works.

Three pages, two columns each (`SetCursorFillMode(LEFT_TO_RIGHT)`; a section's header takes a row, the right half empty):

- **General:** General (NPCs write to you, NPCs reply to you; NPCs write to each other, whom you can write to, letter text size and font), Logging (debug logging).
- **NPC Letters:** NPC-Player Letters (the schedule, cooldown, interactions needed, not after talking within, days until they miss you, people considered), NPC-NPC Letters (known only, then the schedule, limits and proposals).
- **Delivery:** Delivery (postage, writing time, the hand-over dialogue, travel times, returning undeliverable letters), Courier (in town, on the roads, the road meeting's cooldown, speech and bounty).

The page names are passed to `OnPageReset` as their `$PL_Page…` keys; the first open (no page chosen) shows General. `Pages` is set in `OnConfigInit` and again in `OnConfigOpen`, since a save from before the pages ran its `OnConfigInit` without them. Sliders and toggles are rows in two arrays, placed one by one by index. The script knows only the setting names (`"Delivery.Postage"`): the natives `GetSetting`, `SetSetting`, `GetSettingDefault`, `GetSettingMin` and `GetSettingMax` on `PhysicalLetters_MCM` take them, so defaults and ranges live only in `kSettings`. The two menus, recipients (`Writing.GenericRecipients`) and font (`General.Font`), are apart from them: each one's choices are its values (0–2), and each has its own option id and handlers (`OnOptionMenuOpen`, `OnOptionMenuAccept`, and branches in `OnOptionDefault` and `OnOptionHighlight`).

Strings are `$PL_…` keys in `Interface/Translations/Physical Letters_<LANGUAGE>.txt` (UTF-16 LE with BOM, key and text separated by a tab). The English file is the source; the other eight are translations of it (2026-10-04), regenerated when the English changes.

## Adding a setting

1. A row in `Config::kSettings`, and its use in the code.
2. If changing it must do something at once (like the postage global), a branch in `SetSetting` (`src/Papyrus.cpp`).
3. A slider (or toggle) in `PhysicalLetters_MCM.psc`: its name, label, tooltip, format and step; or a menu, as the recipients menu. Then place it on its page in `OnPageReset` (`AddSlider` or `AddToggle` with its index): pages aren't index ranges.
4. Its `$PL_…` label and tooltip in all nine translation files.
5. This table, and the README.
