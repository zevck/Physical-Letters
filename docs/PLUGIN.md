# The plugin (ESP)

`Physical Letters.esp` holds the records the DLL can't make: the hand-over dialogue ([DELIVERY.md](DELIVERY.md#the-hand-over)) and the MCM quest ([SETTINGS.md](SETTINGS.md)). Its source is text in git, written with [Spriggit](https://github.com/Mutagen-Modding/Spriggit); the `.esp` itself is never committed. This follows SkyrimNet's setup (its `docs/skyrim_plugins.md`), in Spriggit's YAML format.

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
| `0x802` | Global (short) | `PhysicalLettersPostage` | The postage: shown in the topic text, checked by the gold conditions. The DLL sets it from the INI on every load ([SETTINGS.md](SETTINGS.md)) |
| `0x803` | Quest | `PhysicalLettersPostQuest` | Holds the dialogue; starts with the game, listed in `Seq/Physical Letters.seq` ([DELIVERY.md](DELIVERY.md#the-hand-over)) |
| `0x804` | DialogBranch | `PhysicalLettersSendBranch` | Top-level, player |
| `0x805` | DialogTopic | `PhysicalLettersSendTopic` | "I need to send a letter. (… gold)" |
| `0x806` | DialogResponses | | Accept: "Of course." (shared line), TIF `PhysicalLetters_TIF_Postage`. Innkeeper or courier, gold ≥ postage, carrying a letter the player wrote |
| `0x807` | DialogResponses | | Refuse: "Nah. I don't think so." (shared line). Same, with gold < postage |
| `0x808` | Quest | `PhysicalLettersMCMQuest` | The MCM: script `PhysicalLetters_MCM`; player alias with `SKI_PlayerLoadGameAlias`. Starts with the game; no dialogue, so not in the SEQ file |
| `0x809` | Global (short) | `PhysicalLettersCourierPending` | Letters waiting for the courier; set by the DLL, read by the Story Manager node ([COURIER.md](COURIER.md)) |
| `0x80A` | Quest | `PLCourierQuest` | The courier's errand: script `PhysicalLetters_CourierQuest`; aliases Location (from the event), LocationCenterMarker, Courier (vanilla `0x039FB7`, package `0x80D`), CourierMarker (`0x039FBA`), Target (filled by the script). Started by the Story Manager, not with the game |
| `0x80B` | Story Manager quest node | `PhysicalLettersCourierNode` | Change of location, after vanilla's courier node (`0x039FBD`), shares the event; conditions in [COURIER.md](COURIER.md#an-errand) |
| `0x80C` | Package | `PhysicalLettersCourierApproach` | Travel (jog) to the Target alias, radius 150; the scene's first action |
| `0x80D` | Package | `PhysicalLettersCourierLinger` | Sandbox around the town's centre marker, radius 1024 |
| `0x80E` | Scene | `PhysicalLettersCourierScene` | Approach, then both stay put (vanilla `DefaultStayAtCurrentLocationScene`): the courier's lines, the recipient's reply; ends on death or combat |
| `0x80F`–`0x816` | DialogTopic + DialogResponses ×4 | (no EditorID) | Scene topics with the courier's four lines, their text in the info and the vanilla voice copied under `Sound/Voice/Physical Letters.esp/MaleYoungEager` ([COURIER.md](COURIER.md#an-errand)); only `WICourierNPC` says them; the third plays `IdleGive`, and its TIF `PhysicalLetters_TIF_CourierHandOver` hands the letter over |
| `0x817`, `0x818` | DialogTopic + DialogResponses | `PhysicalLettersCourierReply` | The recipient's thanks (scene topic); `0x818` is "Of course." (`ResponseData` `0x0DBA22`) for `DefaultNPCVoiceTypes` not in `0x819` |
| `0x819` | FormList | `PhysicalLettersCourierThanksVoices` | The voice types that thank the courier |
| `0x81A`–`0x83E` | DialogResponses ×37 | | The thanks, one per vanilla `WISharedThanks` line (39 voice types: two lines serve two each): `ResponseData` the vanilla `WISharedThanks…` line, its voice-type conditions copied ([COURIER.md](COURIER.md#an-errand)) |
| `0x83F` | Global (short) | `PhysicalLettersRoadCourierReady` | 1 while a letter passes the player; set by the DLL, read by the road node ([ROAD_COURIER.md](ROAD_COURIER.md)) |
| `0x840` | Quest | `PLRoadCourier` | The road encounter: script `PhysicalLetters_RoadCourierQuest`; aliases Trigger (`Ref1` of `WERoadStart`), TravelMarker1/2 (linked `WETravel` markers, as vanilla's road quests), Courier (vanilla `0x039FB7`, packages `0x850`, `0x851`, `0x856`, `0x842`; script `PhysicalLetters_RoadCourierAlias`), CourierMarker, Goal (filled by the script), Recipient (in town). Stages 10 (carrying letters), 15 (refused a threat), 17 (going to the recipient in town), 20 (handed them to the player: rests) and 25 (walks on) |
| `0x841` | Story Manager quest node | `PhysicalLettersRoadCourierNode` | Under `WEQuestNode`, before `WERoadQuests`; `WERoadStart`, the global, the courier in `WICourierCell`, 8:00 to 20:00; doesn't share the event |
| `0x842` | Package | `PhysicalLettersRoadCourierTravel` | Travel (jog) to the Goal alias, radius 512, vanilla road traveller interrupt flags |
| `0x843`, `0x844` | DialogBranch + DialogTopic | `PhysicalLettersRoadBranch`, `PhysicalLettersRoadIntimidate` | "Give me the letters you're carrying. (Intimidate)" |
| `0x845`–`0x848` | DialogResponses ×4 | | He folds (Speech ≥ `0x84C` or the Intimidation perk, and `GetIntimidateSuccess`; shared `0x0E0CBD`/`BE`/`BF`, random; TIF `PhysicalLetters_TIF_RoadIntimidate`), or refuses (shared `0x0E0CC4`, "Nah. I don't think so."; TIF `PhysicalLetters_TIF_RoadRefuse` sets stage 15; links to `0x84E`) |
| `0x849`–`0x84B` | DialogBranch + DialogTopic + DialogResponses | `PhysicalLettersRoadHandOverBranch`, `PhysicalLettersRoadHandOver` | "Your letters. Now." while he's intimidated by the player; shared `0x0E0CBE`; TIF `PhysicalLetters_TIF_RoadHandOver` |
| `0x84C` | Global (short) | `PhysicalLettersRoadIntimidateSpeech` | The Speech the threat needs; set by the DLL from the INI |
| `0x84D`–`0x84F` | DialogBranch + DialogTopic + DialogResponses | `PhysicalLettersRoadBrawlBranch`, `PhysicalLettersRoadBrawl` | "Then I'll take them. (Brawl)" after a refusal (stage 15); shared `0x0E0CC6`; TIF `PhysicalLetters_TIF_RoadBrawl` |
| `0x850`, `0x851` | Package | `PhysicalLettersRoadCourierRest`, `PhysicalLettersRoadCourierWalkOn` | After handing his letters to the player: stand (stage 20, vanilla stay-put data), then walk to the Goal (stage ≥ 25); the jog package `0x842` only below stage 20 |
| `0x852`, `0x853` | DialogTopic + DialogResponses | (no EditorID) | Scene topic: "I have a letter here for you." in the road delivery scene; the courier's voice copied as `plroadcourier__00000853_1.fuz`; `IdleGive`; TIF `PhysicalLetters_TIF_RoadDeliver` hands the letter over |
| `0x854`, `0x855` | DialogTopic + DialogResponses | (no EditorID) | "Whatever you say!" (shared `0x0E0CBE`), said by the script as he hands his letters over after losing a brawl |
| `0x856` | Package | `PhysicalLettersRoadCourierDeliver` | Jog to the Goal alias (the recipient in town), radius 128, at stage 17 |
| `0x857` | Scene | `PhysicalLettersRoadDeliveryScene` | The town delivery scene, copied into the road quest: Courier (alias 3) and Recipient (alias 6) |
| `0x858`–`0x85D` | DialogTopic + DialogResponses ×3 | (no EditorID) | The courier's other three scene lines, voices copied as `plroadcourier__00000859/5B/5D_1.fuz` |
| `0x85E`–`0x884` | DialogTopic + DialogResponses ×38 | `PhysicalLettersRoadCourierReply` | The recipient's thanks, a copy of `0x817`–`0x83E` (`0x85F` is "Of course.") |
| `0x885` | Package | `PhysicalLettersRoadSceneApproach` | The scene's approach: jog to the Recipient alias, radius 150 |
| `0x886` | DialogResponses in vanilla `DGIntimidateVictoryTopic` (`0x047AC6`, overridden to hold it) | | The courier's yield after losing vanilla's brawl, "Don't hurt me! You win." (shared `0x0E0CBF`); otherwise a copy of vanilla's generic `0x047ADB` (script `TIF__00047ADB`, links, walk-away topic). Previous info `0x0F07B9`, so it comes just before vanilla's generic yields (`0x047ADB`, `0x078F76`, `0x047ADC`), the last in the topic; without the link a new info lands after them |
| `0x887` | Quest | `PLHandInQuest` | Holds the hand-in topic ([HAND_IN.md](HAND_IN.md#the-dialogue)); script `PhysicalLetters_HandInQuest` (the TIF's native `BeginHandIn`). Starts with the game, listed in the SEQ file. Its 20 recipient aliases were removed 2026-10-02 |
| `0x889` | FormList | `PhysicalLettersHandInFilter` | Holds the keyword `0x8BA` (until 2026-10-07 `0x8B3`, every letter); the hand-in gift menu's filter |
| `0x88A` | DialogBranch | `PhysicalLettersHandInBranch` | Top-level, player |
| `0x88B` | DialogTopic | `PhysicalLettersHandInTopic` | "I have a letter for you." |
| `0x8B3` | Keyword | `PhysicalLettersHandInLetter` | On every letter (the DLL, when a letter is made or loaded); the hand-in topic's "the player carries a letter" condition (until 2026-10-07 also its gift menu's filter) |
| `0x8B4` | Global (short) | `PhysicalLettersHandInDialogue` | 1 with `[Delivery] HandInDialogue` on; set by the DLL on new game, load and MCM change; in the topic's condition |
| `0x8B5` | DialogResponses | | The topic's one answer: the player carries a letter (`GetKeywordItemCount` `0x8B3` > 0), the speaker is a recipient (`GetInFaction` `0x8B9`) and the global `0x8B4` is 1; a single space (silent, no visible subtitle), TIF `PhysicalLetters_TIF_HandIn` on begin (the gift menu covers the response). `0x888` (an alias faction), `0x88C`–`0x8B2` (thanks lines) were removed 2026-10-02 |
| `0x8B6` | Book | `PhysicalLettersParchment` | Parchment, the blank letter ([WRITING.md](WRITING.md#parchment)): the vanilla note's look, empty, value 2, weight 0.1 |
| `0x8B7` | ConstructibleObject | `PhysicalLettersRecipeParchment` | Tanning rack: 1 Roll of Paper → 3 parchment |
| `0x8B8` | LeveledItem | `PhysicalLettersLItemParchment` | 3 or 5 parchment; added in memory to every general-goods merchant's chest (those stocking Skyrim.esm `LItemMiscVendorMiscItems75`) |
| `0x8B9` | Faction | `PhysicalLettersHasLetterFaction` | Hidden from the player; each actor the player carries a letter for is in it (the DLL, `HandIn::RefreshRecipients` and on actor load); the hand-in topic's condition ([HAND_IN.md](HAND_IN.md#the-dialogue)) |
| `0x8BA` | Keyword | `PhysicalLettersForReader` | On the letters addressed to the NPC the hand-in topic was chosen with, while its gift menu is open (the DLL, `BeginHandIn`); held by the filter list `0x889` |

The DLL looks records up by these FormIDs; changing one means changing its constant too (`Letters.cpp`, `Postage.cpp`, `CourierErrand.cpp`, `RoadCourier.cpp`, `HandIn.cpp`, `Parchment.cpp`).

The vanilla masters dumped with Spriggit in the same format (`skyrim-esm-yaml` and the others) are a handy reference when writing or reviewing records.

## Tools

`utilities/Spriggit.ps1` pins the CLI: Spriggit 0.41.0, from its GitHub release, SHA-256 checked, as in SkyrimNet's `external_versions.json`. The CLI is `$spriggitPath` from `Build_Config_Local.ps1` if set (for example SkyrimNet's copy in `SkyrimNet-Dev\external`), otherwise `external\SpriggitCLI-0.41.0`, downloaded on first use (gitignored). The CLI fetches the `Spriggit.Yaml.Skyrim` package from NuGet the first time.

## Build

`Build_Local.ps1` runs `convert-to-plugin` into `build\esp\Physical Letters.esp` when any source file or folder is newer than it (a folder counts so a deleted record also rebuilds it), and deploys it with the DLL. `-skipEsp` skips both.

**The deploy never overwrites an edited plugin.** If the `.esp` in a deploy folder is newer than the build, someone edited it there, and the deploy reports that instance as failed instead of copying over it.

## Editing

**Edit the YAML.** That's the point of Spriggit: records are written and reviewed as text, and `Build_Local.ps1` builds the `.esp` from them. The vanilla dumps show how any record type looks.

If a record is easier to make in the Creation Kit or xEdit, edit the deployed `.esp` there, then:

1. `.\utilities\esp_to_spriggit.ps1` converts the newest copy in the deploy folders back into `spriggit/PhysicalLetters` (`-EspPath` for another file), with the pinned package and version.
2. Review `git diff spriggit`, then build: the build is newer than the deployed copy again, so the deploy goes ahead.

**FormIDs:** the plugin is ESL-flagged, so its records use `0x800` to `0xFFF`. Records written by hand take the next free one after the table above; the CK or xEdit assign their own.

## Papyrus

The scripts' sources (the TIFs, the MCM, the two courier quests and the hand-in quest) are in `Source/Scripts`; `Build_Local.ps1` compiles it with Pyro (`skyrimse.ppj`, gitignored because it holds the path to the vanilla script sources, the same as SNPD's) into `Scripts/`, and deploys both. A Papyrus change is done only when its `.pex` is compiled and shipped.

## The SEQ file

`Seq/Physical Letters.seq` lists the plugin's start-game quests with dialogue: raw little-endian `uint32` FormIDs as stored in the plugin (master index in the top byte: `0x01` = the plugin itself after its one master, Skyrim.esm), no header. Today two entries, `0x01000803` (postage) and `0x01000887` (hand-in). A new start-game quest, or a new master, means regenerating it.
