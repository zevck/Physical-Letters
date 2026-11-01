# Packages

Items sent with a letter: gold, gear, anything the player can put in a chest. Code: `src/Package.cpp`.

**Status:** built 2026-10-31; a DM Letter's gold went by courier and arrived (AE, 2026-11-01). Posting's question, a robbed courier and other mods' packages are untested. Who can send one: the player, with their own letters (asked when posting) and with the DM Letter (its Package box), and other mods, with any letter still on its way ([API.md](API.md#packages)). NPCs send nothing by themselves: items from NPCs (ordering from a smith) and fetching from the player's home are for later, and money between NPCs and the player is left to money mods (below).

## The chests

Two persistent containers placed in the vanilla courier's holding cell, `WICourierCell` (Skyrim.esm `039F67`), next to his own delivery container (user, 2026-10-31: no new cell; a new cell in an ESL-flagged plugin was the worry). The ESP overrides the cell only to add them ([PLUGIN.md](PLUGIN.md)):

- **The packing chest** (`PhysicalLettersPackingChestRef`, `0x8BE`; base `0x8BC`, named from the locale key `PackingChest` at `kDataLoaded`, as the container menu's title): where the player packs. Opened from wherever the player is by the engine's own opener (`ContainerMenu::OpenMenu`, loot mode), after closing the menus in the way; activating it doesn't work while a menu is open. It's always empty except while open.
- **The store** (`PhysicalLettersPackageStoreRef`, `0x8BF`; base "Packages in Transit", `0x8BD`): where every package waits until its letter moves. Never opened.

Persistent references are always in memory and their contents are saved with the game like any chest's, so nothing has to load to move items in or out.

## Packing

Posting asks: `Package::Ask(question, done)`, a message box at once, over whatever is open, as the letter is handed over (`Postage::Post`): "Add a package to your letter to Lydia?". One box at a time; a load drops a question still waiting. The DM Letter doesn't ask: its page has a Package box, and a save with it ticked goes straight to the chest (`Package::Pack`, [DM_LETTER.md](DM_LETTER.md#the-sheet)). **No** sends the letter as it is, and nothing closes. **Yes** (or the ticked box) closes what's in the way (the book, the inventory, the gift menu, the conversation: `CloseMenus`), then opens the packing chest with the engine's own opener, `ContainerMenu::OpenMenu` (SE 50211, AE 51140), so the chest's cell needn't be loaded and nothing is activated. Two earlier tries failed (AE, 2026-10-31): activating the chest (`ActivateRef`) while the inventory was open opened nothing, and waiting until the player was back in the world queued one question per DM Letter save, out of context. Only a container menu opened on that chest counts (`ContainerMenu::GetTargetRefHandle`); if none opens within 5 seconds the letter goes without a package (logged, `Package::Tick`). When its menu closes, everything in it moves to the store and becomes the letter's package; nothing put in is no package.

## The manifest

A package is a **manifest**, not a container of its own: what it holds, item by item, saved in the co-save record `LPKG` ([PERSISTENCE.md](PERSISTENCE.md)), keyed by the letter id. Several packages in the store, or on one courier, mix freely: two packages of two iron ingots each are four ingots, and which two go where doesn't matter (user, 2026-10-31). Each entry is the base item, the count, and what made that stack unique when packed: its enchantment (`ExtraEnchantment`, a player-enchanted item's), its tempering (`ExtraHealth`) and a custom name (`ExtraTextDisplayData`). Moving a package moves each entry's own stack first, then any of the same base item, so a tempered sword stays with its package unless it's gone. What can't be found is logged as missing, and the rest goes on; an entry that came short is renamed for what's left ("200 Gold", not the 300 packed). A package also records whether the player packed it (posting, the DM Letter) or a mod added it (`AddToPackage`), for when its letter won't arrive (below).

## Where it goes

The package follows its letter (`Package::Deliver(letterId, to)`, wherever the letter changes hands in `Transit` and `DmLetter`):

- delivered off-screen to an NPC: into their inventory with the letter;
- a letter for the player given to the vanilla courier: into his container (`WICourierContainerRef`, `Courier::Container`), which he hands over all at once;
- handed over by the courier in town or on the road: to the recipient; the road courier's letters given to the player: to the player;
- a DM Letter to the player at 0 hours: into their inventory;
- a letter that won't arrive (`Package::Return`): a DM Letter that can't be made, a transformed one whose writer never answers, a letter between NPCs dropped because its recipient is dead or not found (`Transit::Tick`), and one being written when the game was saved (failed after loading that save: `DmLetter`'s co-save record `LDMA`). A once-per-load scan for such packages (`ReturnStranded`) was dropped on 2026-11-01: `LDMA` fails those letters first. The player's items go back to them; a mod's are deleted (`Package::Discard`). Found by the 2026-11-01 review: until then such items stayed in the store for good.
- a letter that parts from its package (`Package::Forget`, the items left where they are): sent again (`Transit::Send`), so the new reader isn't told about last time's; posting may attach a new one. Handed over in person (`Transit::HandIn`): a package still on its way goes back (`Return`) and last time's is forgotten. Taken from the courier in town or on the road, the package left on him (`Transit::Lost`, `TakenByPlayer`; for a letter to the player only what hadn't arrived); or a letter to an NPC the road courier hands the player, its package handed over with it: the player has the items, and the recipient, given the letter in person, is told nothing came (a 2026-11-01 review found they were).

**Couriers carry them.** When the courier in town or the road courier takes a letter (`Transit::TakeForCourier`, `TakeForRoad`), its package goes into his inventory (`Package::Carry`), and from him when he hands the letter over. Robbed, pickpocketed or killed, he loses what's taken: the package is what's left, and the shortfall is logged when it's delivered. When he gives a letter up (his errand ends, or the road encounter does and the letter goes on), what he still has goes back to the store. A letter the player takes from him leaves its package on him.

Once delivered, the manifest stays (holder 0) with what actually arrived. Only then does a reading hear of it (`Package::Arrived`): a letter handed over in person, its package still in the store or on a courier, says nothing came. The reading is told "With the letter came: 300 Gold, Steel Sword." (`letter.package` in `read_letter.prompt`), and the reader's memory and the received-letter event add "With it came: 300 Gold, Steel Sword." (`Reading::PackageSentence`, [READING.md](READING.md)). 

## On the letter

A letter with a package lists it under its text when read: "Enclosed: 50 Gold, Steel Sword" (`Letters::Render`, locale key `Enclosed`), and its item card adds "With a package." (`CardPackage`). It's drawn from the manifest each time the letter's text is made, not stored in the letter, so it shows what the package holds now: after a courier was robbed, what's left; once delivered, what arrived. The letter is refreshed whenever its package moves (`Letters::Refresh`). Since the letter rides with its package, whoever robs a courier sees whose shipment it was and what it held (user, 2026-10-31).

Until 2026-11-01 a separate "Package Manifest" note travelled with the items; the user found two pieces of paper excessive. The co-save records of those dev builds (versions 1 and 2) are skipped, not converted: no release wrote them.

## Gold from NPCs

Not Physical Letters' to decide (user, 2026-11-01). A built-in `send_gold` action let a reader send gold with their reply, its amount the model's judgement; lied to, Lydia sent 50,000 "from your personal stores in Whiterun" (AE, 2026-11-01). A budget per NPC (their station's reserve plus what they carry, spent and slowly refilled) was designed, then set aside: SkyrimNet money mods such as SeverActions already have lending systems, so they should decide who pays what, and Physical Letters only carry it: such a mod sends gold with a letter or a reply through `AddToPackage` ([API.md](API.md#packages)). `send_gold` was removed.
