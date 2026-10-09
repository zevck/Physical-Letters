# Releases

`.\Build_Release.ps1` makes the archive players install: `build\release\Physical Letters <version>.zip`, the version being `CMakeLists.txt`'s. A zip, so no extra tool is needed to open it; no FOMOD, since there is nothing to choose (Ink & Quill has one for its two book menus).

1. **A commit:** it refuses uncommitted changes (a release is a commit); `-allowDirty` makes a test release, marked `-dirty` in its output.
2. **The build:** `Build_Local.ps1 -noDeploy` (the plugin, Papyrus, the ESP); `-skipBuild` packs the last build as it is.
3. **The stage,** `build\release\stage`, laid out as the game's Data folder:

   | Path | What |
   |---|---|
   | `SKSE/Plugins/PhysicalLetters.dll` | The plugin |
   | `Physical Letters.esp` | Built from `spriggit/` ([PLUGIN.md](PLUGIN.md)) |
   | `Scripts/`, `Source/Scripts/` | The compiled scripts and their sources |
   | `Interface/Translations/` | The MCM's text, nine languages |
   | `Seq/Physical Letters.seq` | Lets the postage and hand-in dialogue start with the game |
   | `Sound/Voice/Physical Letters.esp/` | The courier's lines |
   | `SKSE/Plugins/PhysicalLetters/Locales/` | Letter text in each language ([LOCALIZATION.md](LOCALIZATION.md)) |
   | `SKSE/Plugins/SkyrimNet/external/zevick.physical-letters/` | The SkyrimNet plugin: its manifest and prompts |

4. **The checks** (any failure stops it, listing each):
   - every script in `Source/Scripts` has a `.pex` no older than its source (`Scripts/*.pex` is gitignored, so a stale one would otherwise ship);
   - every MCM translation has English's keys;
   - every locale file has `ENGLISH.ini`'s keys (a missing one would fall back to English);
   - the SkyrimNet manifest's `version` is `CMakeLists.txt`'s;
   - no prompt has a comment ([ARCHITECTURE.md](ARCHITECTURE.md#writing-the-prompts)).
5. **The zip.** The outcome is printed and written to `%TEMP%\pl-release-result.json`.

**Before a release:** set the version in `CMakeLists.txt`, `vcpkg.json` and the SkyrimNet manifest, and `min_skyrimnet_version` in the manifest to the oldest SkyrimNet it needs (public API v11: [ARCHITECTURE.md](ARCHITECTURE.md)).
