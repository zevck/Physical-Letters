# Delivery

How long letters travel, and how replies reach the player. Code: `src/Travel.cpp`, `src/Courier.cpp`, `src/Transit.cpp`.

## Travel time

A letter takes as long as the engine's fast travel would between the two places, approximated.

**The engine's fast-travel time** (read from AE 1.6.1170 with Ghidra, 2026-09-29; the function at `0x140731A60`):

```
real seconds = pathLength / (fFastTravelSpeedMult × walkSpeed)
game hours   = real seconds × TimeScale / 3600
```

- `pathLength`: the pathfinding path length to the destination (`0x1404D2360`; on failure it logs "FastTravel: Could not compute path length to Cell" and uses 0).
- `fFastTravelSpeedMult`: game setting, default 1.0 (its `Setting` object is at `0x142006EE0`).
- `walkSpeed`: the traveller's walk speed, `Actor::GetWalkSpeed()` (virtual `0xEE`). On a flying mount, `fFlyingMountFastTravelDragonSpeed` (7000) instead.
- Seconds become game time in the calendar's advance function (`0x140633250`), which multiplies by the `TimeScale` global and by 1/3600 (`0x1417C5C68`). So a higher timescale makes fast travel, and letters, take more game hours.

**Our approximation** (`Travel::Hours`):

```
game hours = straightLineDistance × 1.3 / (fFastTravelSpeedMult × player walk speed) × TimeScale / 3600
```

- The straight line instead of the path; the factor 1.3 allows for roads winding.
- `fFastTravelSpeedMult` from the game settings and `TimeScale` from the calendar at run time, so mods that change them change letters the same way.
- **Positions in the exterior world.** A reference in an exterior cell uses its position. Anyone indoors, or an NPC not loaded, uses the world marker of their current location, or its parent's (Dragonsreach → Whiterun), then of their editor location. Interior coordinates never compare with exterior ones.
- **At least 2 game hours**: someone still carries the letter across town. Two people indoors in the same town resolve to the same marker, 0 units apart.
- **No common worldspace** (Solstheim and Skyrim), or no place found: 48 game hours.

Letters to an NPC arrive after the travel time from the player to them. A reply goes to the courier after 12 game hours of writing plus the travel time from the NPC to the player.

## Replies and the courier

When a reading returns a reply ([READING.md](READING.md)), the same game-thread step that ends the reading creates the reply letter (author the NPC, recipient the player, "Letter from X", the same template) and queues it as a parcel *to the player* (`LTRN` state 2). A save can't hold a finished reading without its reply. If a save was made between the memory and that step, the reading after a load finds the memory and takes the reply from LetterDB's stored reading.

When the parcel is due, it goes to the vanilla courier: `WICourierScript.addItemToContainer(letter, 1)` on the quest `WICourier` (Skyrim.esm `0x039F82`), called through the Papyrus VM as vanilla quests call it (`WIKill03`, the Hearthfire steward letters), so mods that change the courier see our letters too. From the vanilla script (`WICourierScript.psc`):

- `addItemToContainer` puts the item in `WICourierContainerRef` and raises the global `WICourierItemCount`, which makes the courier quest run on the player's next change of location.
- The courier finds the player in a town (`LocTypeHabitation`); his dialogue calls `GiveItemsToPlayer`, which moves everything to the player and shows the vanilla "items added" message.

Once with the courier, the letter is out of our queue; the courier's container holds it, and the engine saves it there.
