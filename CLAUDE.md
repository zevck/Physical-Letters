# Physical Letters

SKSE plugin (CommonLibSSE-NG, C++23; one DLL for SE, AE and VR). The player writes letters to NPCs; the NPC reads them through **SkyrimNet**, remembers them and may write back. Sibling of SkyrimNet Physical Diaries (SNPD), which much of the engine-facing code comes from.

**Developer docs: [docs/INDEX.md](docs/INDEX.md).** The docs are the source of truth for how the code works. Keep them current: a change that makes a doc wrong fixes the doc in the same change.

## Ground rules

- Identify NPCs by **SkyrimNet UUID**. A FormID from SkyrimNet is used only after it maps back to the same UUID; never identify anyone by name.
- Letter forms are runtime forms the engine saves itself (`DynamicForms`). Never pick a FormID yourself, and never remove a form from the save. See [docs/PERSISTENCE.md](docs/PERSISTENCE.md).
- `DynamicForms` is shared with SNPD: keep the two copies identical ([docs/ARCHITECTURE.md](docs/ARCHITECTURE.md#code-shared-with-skyrimnet-physical-diaries)).
- Every persistence change must survive: save → reload, reload without saving, loading an older save with SkyrimNet **Keep and Clear**, and a second character.
- Whether a delivery was read is SkyrimNet's tagged memory (the delivery tag), not our records.
- Game state only on the game thread (`SKSE::GetTaskInterface()->AddTask`), and every task catches exceptions. SkyrimNet callbacks run on its thread pool: no `RE::` there.
- SkyrimNet content (the prompt, the manifest) follows SkyrimNet's modding docs (`docs/modding` in the SkyrimNet-GamePlugin repo): Beta 25 plugin layout under `SKSE/Plugins/SkyrimNet/external/zevick.physical-letters/`, and its prompt style guide.
- Text the player sees goes in `include/Strings.h`.
- Requires SkyrimNet public API v11.

## Build

`.\Build_Local.ps1`: incremental plugin build and deploy to the `Physical Letters - Dev` mod folder in each test instance (paths in the gitignored `Build_Config_Local.ps1`). PASS/FAIL also goes to `%TEMP%\snpl-build-result.json`. Never `/t:Rebuild`: it rebuilds all of CommonLib. After a fresh clone: `git submodule update --init --recursive` (CommonLib has a nested `openvr` submodule).

The ESP's source is Spriggit YAML in `spriggit/PhysicalLetters`, edited as text (no CK needed); the `.esp` is built from it and never committed. If the `.esp` was edited in the CK or xEdit, run `.\utilities\esp_to_spriggit.ps1` before building. A Papyrus change is done only when its `.pex` is compiled (Pyro, via the build) and shipped. See [docs/PLUGIN.md](docs/PLUGIN.md).

Ask before committing.
