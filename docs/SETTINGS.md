# Settings and the MCM

Code: `include/Config.h`, `src/Papyrus.cpp`, `Source/Scripts/PhysicalLetters_MCM.psc`. Adapted from SNPD's `Config.h` and MCM (SNPD `docs/CONFIG_AND_MCM.md`).

## The INI

`Data/SKSE/Plugins/PhysicalLetters.ini`. Loaded in `SKSEPlugin_Load` and **written straight back**, so MO2 puts it in `overwrite/` and mod updates don't replace it. No INI ships with the mod. The MCM saves each change at once.

| Key | Default | Range | Effect |
|---|---|---|---|
| `[General] DebugLog` | 0 | 0/1 | Log level `debug` instead of `info`; live from the MCM |
| `[Delivery] Postage` | 20 | 0–1000 | Gold an innkeeper or the courier charges. Written into the ESP's global `PhysicalLettersPostage`, which the topic text and the gold conditions read ([DELIVERY.md](DELIVERY.md#the-hand-over)) |
| `[Delivery] WritingHours` | 12 | 0–168 | Game hours an NPC takes to write back, before the reply travels |
| `[Delivery] MinHours` | 2 | 0–48 | Shortest travel time ([DELIVERY.md](DELIVERY.md#travel-time)) |
| `[Delivery] FallbackHours` | 48 | 1–336 | Travel time between worldspaces, or when a place can't be found |
| `[Delivery] ReturnAfterDays` | 3 | 1–30 | Game days a due letter waits for a recipient who can't be found before the courier brings it back ([DELIVERY.md](DELIVERY.md#undeliverable-letters)) |
| `[NpcLetters] Enabled` | 1 | 0/1 | NPCs write to the player first ([NPC_LETTERS.md](NPC_LETTERS.md)) |
| `[NpcLetters] IntervalDays` | 7 | 1–60 | Game days between attempts, times 0.5–1.5 at random |
| `[NpcLetters] CooldownDays` | 14 | 0–120 | After an NPC writes to the player (first or in reply), game days before they may write first again |
| `[NpcLetters] MinEvents` | 5 | 1–200 | Events with the player SkyrimNet must have recorded for an NPC to be picked |
| `[NpcLetters] MinDaysApart` | 1 | 0–30 | Game days since their last exchange with the player below which an NPC isn't picked (0 = off) |
| `[NpcLetters] MissedAfterDays` | 3 | 0–30 | Game days apart for an NPC's full weight; 0 ignores recency |
| `[NpcLetters] RecentWeight` | 10 | 0–100 | Weight (percent of full) of someone the player spoke to today; it grows to full over `MissedAfterDays` |
| `[NpcLetters] NearDistance` | 8192 | 0–65536 | Game units within which an NPC is around the player, besides the same area (it also catches a farm just outside a town) |
| `[NpcToNpc] Enabled` | 1 | 0/1 | NPCs write to each other ([NPC_TO_NPC.md](NPC_TO_NPC.md)); needs `PublicSearchActors` |
| `[NpcToNpc] KnownOnly` | 0 | 0/1 | Only NPCs the player knows (`NpcLetters.MinEvents`) write; else anyone SkyrimNet has registered |
| `[NpcToNpc] IntervalDays` | 5 | 1–60 | Game days between attempts to start a correspondence, times 0.5–1.5 at random |
| `[NpcToNpc] MaxOpenThreads` | 3 | 1–10 | Correspondences between NPCs running at once |
| `[NpcToNpc] MaxLettersPerThread` | 3 | 1–10 | Letters in one correspondence, the first and every reply |
| `[NpcToNpc] PairCooldownDays` | 21 | 0–120 | Game days after a correspondence ends before the same two start another |
| `[NpcToNpc] WritersPerAttempt` | 4 | 1–10 | People drawn per attempt; one cheap call proposes recipients for all of them ([NPC_TO_NPC.md](NPC_TO_NPC.md#an-attempt)) |
| `[NpcToNpc] NamesPerWriter` | 3 | 1–5 | Recipients proposed per person, best first; the first usable one is kept |
| `[NpcToNpc] MemoriesPerWriter` | 3 | 0–10 | Newest memories each person's proposal and letter see (most involve the player; 0 = none) |
| `[NpcToNpc] MinDistance` | 16384 | 0–131072 | Game units writer and recipient must be apart, besides living in different places |

**Every setting is one row in `Config::kSettings`**: section, key, default, range. Reads clamp to the range and fall back to the default on a non-number, so a hand-edited INI can't feed the code nonsense. `Save()` writes only the keys in the table (anything else is dropped at the next start), building the file in memory first so a failure can't leave it half-written.

**Postage and saves:** a save stores a global's value, so `Postage::ApplyPrice()` sets the global again on every new game and load, and when the MCM changes it.

## The MCM

`PhysicalLetters_MCM` (`extends SKI_ConfigBase`) on the start-game quest `PhysicalLettersMCMQuest` (`0x808`), whose player alias runs `SKI_PlayerLoadGameAlias`, as SNPD's does. Needs SkyUI; without it the INI still works.

One page, four sections: Delivery (five sliders), NPC letters (the toggle and seven sliders), Letters between NPCs (two toggles and eight sliders), Logging (the debug toggle). Sliders and toggles are rows in two arrays, so a section is a range of them. The script knows only the setting names (`"Delivery.Postage"`): the natives `GetSetting`, `SetSetting`, `GetSettingDefault`, `GetSettingMin` and `GetSettingMax` on `PhysicalLetters_MCM` take them, so defaults and ranges live only in `kSettings`.

Strings are `$PL_…` keys in `Interface/Translations/Physical Letters_<LANGUAGE>.txt` (UTF-16 LE with BOM, key and text separated by a tab). All nine files hold the English text for now.

## Adding a setting

1. A row in `Config::kSettings`, and its use in the code.
2. If changing it must do something at once (like the postage global), a branch in `SetSetting` (`src/Papyrus.cpp`).
3. A slider (or toggle) in `PhysicalLetters_MCM.psc`: its name, label, tooltip, format and step.
4. Its `$PL_…` label and tooltip in all nine translation files.
5. This table, and the README.
