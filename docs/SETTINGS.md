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

**Every setting is one row in `Config::kSettings`**: section, key, default, range. Reads clamp to the range and fall back to the default on a non-number, so a hand-edited INI can't feed the code nonsense. `Save()` writes only the keys in the table (anything else is dropped at the next start), building the file in memory first so a failure can't leave it half-written.

**Postage and saves:** a save stores a global's value, so `Postage::ApplyPrice()` sets the global again on every new game and load, and when the MCM changes it.

## The MCM

`PhysicalLetters_MCM` (`extends SKI_ConfigBase`) on the start-game quest `PhysicalLettersMCMQuest` (`0x808`), whose player alias runs `SKI_PlayerLoadGameAlias`, as SNPD's does. Needs SkyUI; without it the INI still works.

One page: the five delivery sliders and the debug-log toggle. The script knows only the setting names (`"Delivery.Postage"`): the natives `GetSetting`, `SetSetting`, `GetSettingDefault`, `GetSettingMin` and `GetSettingMax` on `PhysicalLetters_MCM` take them, so defaults and ranges live only in `kSettings`.

Strings are `$PL_…` keys in `Interface/Translations/Physical Letters_<LANGUAGE>.txt` (UTF-16 LE with BOM, key and text separated by a tab). All nine files hold the English text for now.

## Adding a setting

1. A row in `Config::kSettings`, and its use in the code.
2. If changing it must do something at once (like the postage global), a branch in `SetSetting` (`src/Papyrus.cpp`).
3. A slider (or toggle) in `PhysicalLetters_MCM.psc`: its name, label, tooltip, format and step.
4. Its `$PL_…` label and tooltip in all nine translation files.
5. This table, and the README.
