# Delivery

How long letters travel, and how replies reach the player. Code: `src/Travel.cpp`, `src/Courier.cpp`, `src/Transit.cpp`, `src/Postage.cpp`.

## Travel time

A letter takes as long as the engine's fast travel would between the two places.

**The engine's fast-travel time** (read from AE 1.6.1170 with Ghidra, 2026-09-29; the function at `0x140731A60`, AE ID 40445):

```
real seconds = pathLength / (fFastTravelSpeedMult × walkSpeed)
game hours   = real seconds × TimeScale / 3600
```

- `pathLength`: the length of the navmesh path to the destination (below). On failure fast travel logs "FastTravel: Could not compute path length to Cell" and uses 0.
- `fFastTravelSpeedMult`: game setting. Skyrim.esm doesn't set it; the live value on the AE test setup was 3.60 (whether the engine's default or another plugin's, not checked). `[Travel]` logs the live value.
- `walkSpeed`: the traveller's walk speed, `Actor::GetWalkSpeed()`. On a flying mount, `fFlyingMountFastTravelDragonSpeed` (7000) instead.
- Seconds become game time in the calendar's advance function (`0x140633250`), which multiplies by the `TimeScale` global and by 1/3600. So a higher timescale makes fast travel, and letters, take more game hours.

**Ours** (`Travel::Hours`) is the same formula with the player's walk speed, and the same path length, from the engine's own function:

| Call | SE ID | AE ID | VR offset |
|---|---|---|---|
| Path length between two `BSPathingLocation`s: `float (Pathing*, from, to, 0, params, 0)`; `FLT_MAX` when there's no path | 29841 | 30657 | `0x485760` |
| A `BSPathingLocation` from a reference, built in place (the reference's worldspace and position outdoors, its cell indoors) | 29820 | 30636 | `0x4831E0` |
| The 80-byte pathing parameters from the traveller's handle (fast travel passes the player's), and their destructor | 30030 / 30031 | 30845 / 30846 | `0x48E670` / `0x48E740` |
| The `Pathing` singleton (CommonLib binds it by ID) | 514893 | 401037 | `0x2FC4658` |

VR's Address Library doesn't list the four functions (checked in the 2026-09-27 CSV, 14,320 entries; it lists the singleton, at the offset above, and the 2025 CSV doesn't), so VR uses raw offsets for all of them (`REL::VariantID`), found in VR's fast travel (`0x1406BE070`) by its log string. Fast travel builds its start from the player (SE 29819, AE 30635); ours builds both ends from references, so any two places work.

- **Positions in the exterior world.** A reference in an exterior cell is used as it is. Anyone indoors, or an NPC not loaded, uses the world marker of their current location, or its parent's (Dragonsreach → Whiterun), then of their editor location. Letters travel the roads.
- **No path** (the engine returns `FLT_MAX`): the straight line × 1.3. The log says which it was, and how long the pathing took.
- **At least 2 game hours** (`MinHours`): someone still carries the letter across town. Two people indoors in the same town resolve to the same marker, 0 units apart.
- **Worldspaces compare by their root.** City worldspaces (`WhiterunWorld`, `SolitudeWorld`, …) are children of `Tamriel` and share its coordinates, so a letter from Solitude to someone in Whiterun's streets is within one world. The engine's pathing crosses from a city worldspace into Tamriel (tested: Solitude to Whiterun's streets, below); where no path is found, the straight line applies, which is valid because the coordinates are shared. Some city locations have no marker of their own (`SolitudeLocation`): their hold's is used.
- **No common root worldspace** (Solstheim and Skyrim, Blackreach), or no place found (or no usable walk speed or `fFastTravelSpeedMult`): 48 game hours (`FallbackHours`).

Tested on AE (2026-09-30): Solitude (Winking Skeever) to Katarina in Whiterun's streets, road 142,905 units against a straight line of 141,323 (the engine's path is coarse), 9 ms of pathing, 2.8 game hours at `fFastTravelSpeedMult` 3.60 and timescale 20.

Letters to an NPC arrive after the travel time from whoever took them (the innkeeper or courier). A reply goes to the courier after 12 game hours of writing (`WritingHours`) plus the travel time from the NPC to the player.

**In the player's town**, the courier may bring the letter in person instead ([COURIER.md](COURIER.md)).

## Undeliverable letters

When a letter is due, its recipient must be found (`FindActor`: SkyrimNet's UUID to a FormID in memory, and back to the same UUID). An NPC who isn't persistent is only in memory while their cell is loaded, so a letter to them waits until the player comes near.

- **The recipient is dead:** the letter turns around at once and comes back **through the courier**, after the travel time from their body to the player. The courier handing it back is the signal, and its item card says why; there's no message.
- **The recipient can't be found** for `ReturnAfterDays` game days after the letter was due (3 by default, [SETTINGS.md](SETTINGS.md)): it goes to the courier at once: the wait was the delay, and there's no place to measure a trip back from. A mod may have removed them, or they only exist while their cell is loaded.

A returning letter is a state-2 parcel like a reply: the same letter form, now addressed to the player. Its item card adds a line, "Return to sender (deceased)" or "(not found)" (a `\n` in the card text breaks the line; tested on AE); the co-save keeps that ([PERSISTENCE.md](PERSISTENCE.md#the-co-save)), and sending the letter again clears it. It keeps its keyword, so it can be sent again.

## Replies and the courier

When a reading returns a reply ([READING.md](READING.md)), the same game-thread step that ends the reading creates the reply letter (author the NPC, recipient the player, "Letter from X", the same template) and queues it as a parcel *to the player* (`LTRN` state 2). A save can't hold a finished reading without its reply. If a save was made between the memory and that step, the reading after a load finds the memory and takes the reply from LetterDB's stored reading.

A letter an NPC writes first takes the same path, queued after the travel time alone, with no writing time (`Transit::QueueToPlayer`, [NPC_LETTERS.md](NPC_LETTERS.md#the-letter)).

When the parcel is due, it goes to the vanilla courier: `WICourierScript.addItemToContainer(letter, 1)` on the quest `WICourier` (Skyrim.esm `0x039F82`), called through the Papyrus VM as vanilla quests call it (`WIKill03`, the Hearthfire steward letters), so mods that change the courier see our letters too. From the vanilla script (`WICourierScript.psc`):

- `addItemToContainer` puts the item in `WICourierContainerRef` and raises the global `WICourierItemCount`, which makes the courier quest run on the player's next change of location.
- The courier finds the player in a town (`LocTypeHabitation`); his dialogue calls `GiveItemsToPlayer`, which moves everything to the player and shows the vanilla "items added" message.

Once with the courier, the letter is out of our queue; the courier's container holds it, and the engine saves it there.

## The hand-over

The player sends a letter by giving it to an innkeeper or the courier. Everything the player sees is vanilla dialogue in `Physical Letters.esp` ([PLUGIN.md](PLUGIN.md)); SkyrimNet's dialogue actions pick the topic up by themselves, so "Hulda, can you mail this for me?" in a SkyrimNet conversation fires the same line.

- **The topic** `PhysicalLettersSendTopic`, in a top-level branch of the quest `PhysicalLettersPostQuest`: "I need to send a letter. (<Global=PhysicalLettersPostage> gold)". The quest lists the global in its text display globals, as vanilla's `DialogueGeneric` does for the room cost.
- **The quest starts with the game**, as vanilla's do: flags `StartGameEnabled` and `0x10` (329 of Skyrim.esm's 330 start-game quests set both), and `Seq/Physical Letters.seq` listing it: the engine initializes a start-game quest's dialogue only from the plugin's SEQ file (without it the quest runs but its topic never shows). Never `StartGameEnabled` alone: it is CommonLib's `kEnabled` ("running"), and without `0x10` (`kStartsEnabled`) the quest is marked running without ever starting, and a save keeps that state.
- **Who and when:** speakers in `JobInnkeeperFaction` (Skyrim.esm `0x05091B`) or the courier `WICourierNPC` (`0x039F83`), and only while the player carries a letter they wrote (`GetKeywordItemCount PhysicalLettersOutgoingLetter > 0` on the player). **Any letter the player wrote can be sent, as often as they like**: a letter taken back from its recipient and sent again is a new delivery, read again. Letters addressed to the player (replies) can't be sent: they don't carry the keyword.
- **Two responses with opposite gold conditions** (so their order doesn't matter): with at least `PhysicalLettersPostage` gold (20 by default, `Postage` in [SETTINGS.md](SETTINGS.md)), "Of course."; without, "Nah. I don't think so." Both reuse vanilla voiced lines through `ResponseData` from the shared-info topic `DialogueGenericSharedInfo` (`0x0DBA22`, `0x0E0CC4`), recorded for all 42 generic voice types, which covers every innkeeper's voice type and the courier's (checked against `Skyrim - Voices_en0.bsa`). No gold-specific refusal line is voiced for every innkeeper; the topic text states the price instead.
- **The TIF** (`Source/Scripts/PhysicalLetters_TIF_Postage.psc`, on the accepting response's end) opens the gift menu, giving, filtered to the form list `PhysicalLettersOutgoingFilter`, which holds the keyword `PhysicalLettersOutgoingLetter`. The DLL puts that keyword on every letter the player wrote (`Letters`), so only those show. Every letter's item card reads "A letter to X from Y." (the `CNAM` path of the `GetDescription` hook).
- **The DLL** (`src/HandIn.cpp` watches `TESContainerChangedEvent`, `src/Postage.cpp` posts): a letter the player wrote, moving from the player to an innkeeper or the courier while a gift menu is open and not addressed to them, is sent from them (`Transit::Send`, travel time from the holder), the postage is taken (the vanilla "gold removed" message), and the menu closes: one letter per postage. If the session isn't ready, the letter isn't in LetterDB, or the player can no longer pay, the letter goes back to the player. Closing the menu without giving anything costs nothing. A letter given in the hand-in topic's gift menu, or to its own recipient in the postage menu, is handed over in person instead ([HAND_IN.md](HAND_IN.md)).
