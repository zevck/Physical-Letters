# Physical Letters

SKSE plugin for Skyrim (SE, AE and VR, one DLL) and a sibling of SkyrimNet Physical Diaries. The player writes letters to NPCs and has them delivered; the NPC reads the letter through **SkyrimNet**, remembers it, and may write back.

**Status: in development.** The send flow works (tested on AE): a letter is delivered to its recipient after a delay, the recipient reads it, and SkyrimNet keeps a memory of it. Writing your own letters, handing them to an innkeeper or courier, and delivering replies are not built yet. Until they are, the plugin has dev keys (below).

## Requirements

- SKSE and the Address Library for your runtime.
- **SkyrimNet with public API v11** (0.25.1, not released yet). With an older SkyrimNet the plugin loads, logs why, and letters are never read.

## What it installs

| Path | What |
|---|---|
| `SKSE/Plugins/PhysicalLetters.dll` | The plugin |
| `SKSE/Plugins/SkyrimNet/external/zevick.physical-letters/` | A SkyrimNet plugin: the prompt the recipient reads a letter with. It shows under SkyrimNet's Installed Plugins with an External badge. |

At run time it writes `SKSE/Plugins/PhysicalLetters/SkyrimNet-<save id>/letters.db` (under MO2's `overwrite/`) and logs to `Documents/My Games/Skyrim Special Edition/SKSE/PhysicalLetters.log`.

## Dev keys

Outside menus, once the log says `[Session] Ready`:

| Key | Does |
|---|---|
| F6 | Gives you an example letter to the NPC under the crosshair (SkyrimNet must know them) |
| F7 | Sends the newest letter you carry; it arrives 2 game hours later |
| F8 | Makes every letter in transit due now |

## Building

`.\Build_Local.ps1` builds the plugin and deploys it to the dev mod folders named in the gitignored `Build_Config_Local.ps1`. See [docs/INDEX.md](docs/INDEX.md) for how the code works.

## License

GPL-3.0 (CommonLibSSE-NG is GPL-3). See [LICENSE.md](LICENSE.md).
