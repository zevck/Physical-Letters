# Physical Letters

SKSE plugin for Skyrim (SE, AE and VR, one DLL) and a sibling of SkyrimNet Physical Diaries. The player writes letters to NPCs and has them delivered; the NPC reads the letter through **SkyrimNet**, remembers it, and may write back.

**Status: in development.** Tested on AE: a letter reaches its recipient after the fast-travel time along the roads, the recipient reads it (with your earlier letters to them) and SkyrimNet keeps a memory of it, and their reply comes back through the vanilla courier; so does a letter whose recipient is dead or can't be found. Sending a letter by giving it to an innkeeper or the courier, for 20 gold of postage, and the MCM are tested too. NPCs writing to you first, now and then, is tested too, and so are NPCs writing to each other (their letters can be stolen and read). When a letter's recipient is outdoors in the town you're in, the vanilla courier may bring it to them in person, and you can watch him hand it over (tested on AE). On the roads you may meet him carrying a letter that passes there: post a letter with him, threaten or brawl him for his letters, or pick his pocket (built, not yet tested). You can also hand a letter you carry to someone in person ("I have a letter for you."): they read it there and react aloud, the text known to them alone, and may write back (tested on AE; needs SkyrimNet's `PublicRegisterEvent`); hand it to anyone else and they read a letter that isn't theirs (built, not yet tested). Writing your own letters isn't built yet; until it is, the plugin has dev keys (below).

## Requirements

- SKSE and the Address Library for your runtime.
- SkyUI for the MCM (optional: the INI works without it).
- **SkyrimNet with public API v11** (0.25.1, not released yet). With an older SkyrimNet the plugin loads, logs why, and letters are never read.

## What it installs

| Path | What |
|---|---|
| `SKSE/Plugins/PhysicalLetters.dll` | The plugin |
| `Physical Letters.esp` | ESL-flagged plugin: the postage dialogue for innkeepers and the courier, and the courier's errand to an NPC in town (quest, scene, lines), the courier on the road, and handing a letter over in person |
| `Scripts/PhysicalLetters_TIF_Postage.pex` | The dialogue's script (opens the gift menu) |
| `Scripts/PhysicalLetters_CourierQuest.pex`, `Scripts/PhysicalLetters_TIF_CourierHandOver.pex` | The courier's errand |
| `Scripts/PhysicalLetters_RoadCourierQuest.pex`, `Scripts/PhysicalLetters_TIF_Road*.pex` | The courier on the road |
| `Scripts/PhysicalLetters_HandInQuest.pex`, `Scripts/PhysicalLetters_TIF_HandIn.pex` | Handing a letter over in person |
| `Sound/Voice/Physical Letters.esp/` | The courier's lines: copies of his vanilla voice files, under the plugin's own records |
| `Scripts/PhysicalLetters_MCM.pex`, `Interface/Translations/` | The MCM |
| `Source/Scripts/*.psc` | The scripts' sources |
| `Seq/Physical Letters.seq` | Lets the postage and hand-in dialogue start with the game |
| `SKSE/Plugins/SkyrimNet/external/zevick.physical-letters/` | A SkyrimNet plugin: the prompts (reading a letter, yours or someone else's; NPCs writing). It shows under SkyrimNet's Installed Plugins with an External badge. |

At run time it writes `SKSE/Plugins/PhysicalLetters/SkyrimNet-<save id>/letters.db` (under MO2's `overwrite/`) and logs to `Documents/My Games/Skyrim Special Edition/SKSE/PhysicalLetters.log` (VR: `Skyrim VR` instead of `Skyrim Special Edition`).

## Settings

In the MCM (Physical Letters) or `SKSE/Plugins/PhysicalLetters.ini`, which the plugin writes on first start: the postage, how long NPCs take to write back, the travel-time tuning, when an undeliverable letter comes back, how often NPCs write to you first, whether and how much they write to each other, whether the courier delivers in person in town, whether he can be met on the road, and whether NPCs get the "I have a letter for you." topic. See [docs/SETTINGS.md](docs/SETTINGS.md).

## Dev keys

Outside menus, once the log says `[Session] Ready`:

| Key | Does |
|---|---|
| F6 | Gives you an example letter to the NPC under the crosshair (SkyrimNet must know them) |
| F7 | Sends the newest letter you wrote and carry; it arrives after the travel time (as fast travel would take) |
| F8 | Makes every letter in transit due now, including replies, which then go to the courier, and the next letter an NPC writes first |

On every load you also get a letter to "Nobody (test)" (unless you carry one): its recipient is never found, so it comes back through the courier ([docs/DELIVERY.md](docs/DELIVERY.md#undeliverable-letters)).

## Building

`.\Build_Local.ps1` builds the plugin and deploys it to the dev mod folders named in the gitignored `Build_Config_Local.ps1`. See [docs/INDEX.md](docs/INDEX.md) for how the code works.

## License

GPL-3.0 (CommonLibSSE-NG is GPL-3). See [LICENSE.md](LICENSE.md).
