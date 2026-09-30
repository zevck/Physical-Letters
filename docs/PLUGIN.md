# The plugin (ESP)

`Physical Letters.esp` holds the records the DLL can't make: the hand-over dialogue ([DELIVERY.md](DELIVERY.md#the-hand-over)). Its source is text in git, written with [Spriggit](https://github.com/Mutagen-Modding/Spriggit); the `.esp` itself is never committed. This follows SkyrimNet's setup (its `docs/skyrim_plugins.md`), in Spriggit's YAML format.

## Layout

`spriggit/PhysicalLetters/`:

| File | What |
|---|---|
| `spriggit-meta.json` | Spriggit package and version (`Spriggit.Yaml.Skyrim` 0.41.0), game release, file name |
| `RecordData.yaml` | The header: ESL-flagged (`Small`), author, master `Skyrim.esm` |
| `<RecordType>/…` | One folder per record type, one file per record |

## Records

All written by hand in YAML, modelled on the vanilla dumps, then normalised by a round trip through Spriggit (so later round trips show only real changes).

| FormID | Type | EditorID | What |
|---|---|---|---|
| `0x800` | Keyword | `PhysicalLettersOutgoingLetter` | On every letter the player wrote (added by the DLL) |
| `0x801` | FormList | `PhysicalLettersOutgoingFilter` | Holds the keyword; the gift menu's filter |
| `0x802` | Global (short) | `PhysicalLettersPostage` | 20; shown in the topic text |
| `0x803` | Quest | `PhysicalLettersPostQuest` | Holds the dialogue; starts with the game, listed in `Seq/Physical Letters.seq` ([DELIVERY.md](DELIVERY.md#the-hand-over)) |
| `0x804` | DialogBranch | `PhysicalLettersSendBranch` | Top-level, player |
| `0x805` | DialogTopic | `PhysicalLettersSendTopic` | "I need to send a letter. (… gold)" |
| `0x806` | DialogResponses | | Accept: "Of course." (shared line), TIF `PhysicalLetters_TIF_Postage`. Innkeeper or courier, gold ≥ postage, carrying a letter the player wrote |
| `0x807` | DialogResponses | | Refuse: "Nah. I don't think so." (shared line). Same, with gold < postage |

The DLL looks records up by these FormIDs; changing one means changing its constant too (`Letters.cpp`, `Postage.cpp`).

The vanilla masters in the same format (`C:\dev\Mutagen Tools\skyrim-esm-yaml` and the others) are a handy reference when writing or reviewing records.

## Tools

`utilities/Spriggit.ps1` pins the CLI: Spriggit 0.41.0, from its GitHub release, SHA-256 checked, as in SkyrimNet's `external_versions.json`. The CLI is `$spriggitPath` from `Build_Config_Local.ps1` if set (for example SkyrimNet's copy in `SkyrimNet-Dev\external`), otherwise `external\SpriggitCLI-0.41.0`, downloaded on first use (gitignored). The CLI fetches the `Spriggit.Yaml.Skyrim` package from NuGet the first time.

## Build

`Build_Local.ps1` runs `convert-to-plugin` into `build\esp\Physical Letters.esp` when any source file is newer than it, and deploys it with the DLL. `-skipEsp` skips both.

**The deploy never overwrites an edited plugin.** If the `.esp` in a deploy folder is newer than the build, someone edited it there, and the deploy reports that instance as failed instead of copying over it.

## Editing

**Edit the YAML.** That's the point of Spriggit: records are written and reviewed as text, and `Build_Local.ps1` builds the `.esp` from them. The vanilla dumps show how any record type looks.

If a record is easier to make in the Creation Kit or xEdit, edit the deployed `.esp` there, then:

1. `.\utilities\esp_to_spriggit.ps1` converts the newest copy in the deploy folders back into `spriggit/PhysicalLetters` (`-EspPath` for another file), with the pinned package and version.
2. Review `git diff spriggit`, then build: the build is newer than the deployed copy again, so the deploy goes ahead.

**FormIDs:** the plugin is ESL-flagged, so its records use `0x800` to `0xFFF`. Records written by hand take the next free one after the table above; the CK or xEdit assign their own.

## Papyrus

The TIF's source is `Source/Scripts`; `Build_Local.ps1` compiles it with Pyro (`skyrimse.ppj`, gitignored because it holds the path to the vanilla script sources, the same as SNPD's) into `Scripts/`, and deploys both. A Papyrus change is done only when its `.pex` is compiled and shipped.

## The SEQ file

`Seq/Physical Letters.seq` lists the plugin's start-game quests with dialogue: raw little-endian `uint32` FormIDs as stored in the plugin (master index in the top byte: `0x01` = the plugin itself after its one master, Skyrim.esm), no header. Today one entry, `0x01000803`. A new start-game quest, or a new master, means regenerating it.
