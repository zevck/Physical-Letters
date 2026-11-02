# The API for other mods

Other mods can send letters, hear when letters are delivered and read, and offer a letter's reader **letter actions** to choose (a meeting to go to, a ransom to pay). Two routes to the same code: Papyrus (`Source/Scripts/PhysicalLetters.psc`, global functions and mod events) and C (`api/PhysicalLettersAPI.h`, MIT licensed like Ink & Quill's `api/InkAndQuillAPI.h`, `PL_GetAPI` as its `IQ_GetAPI`). Code: `src/Api.cpp`; sending is `DmLetter::Send` ([DM_LETTER.md](DM_LETTER.md)).

**Status:** built 2026-10-31 for a request (a SkyrimNet gameplay mod wanting real letters instead of dialogue lines), not yet run in game. Version 1 ([Versions](#versions)).

## Sending

`SendLetter(from, fromName, to, text, transform, forgery, hours, tag)` (Papyrus) / `PL_API::SendLetter(PL_Send*, idOut)` (C) sends exactly what the DM Letter's page sends, with the same meaning for every field ([DM_LETTER.md](DM_LETTER.md#the-sheet)):

- **From:** an actor writes it; none, and it's unsigned, or signed with `fromName`, a name nobody remembers writing. Refused as a writer: the player (their letters are written on parchment), the recipient, someone dead, someone SkyrimNet doesn't know, and in C a FormID that isn't an actor (a base NPC's, say: until the 2026-11-01 review it became an unsigned letter).
- **To:** any actor SkyrimNet knows, the player included. Every send is refused until letters are ready after a load (a few seconds: [ARCHITECTURE.md](ARCHITECTURE.md)), so one sent from `OnPlayerLoadGame` should wait or retry.
- **Text:** the letter, or notes the writer puts in their own words with `transform` (one LLM call, `dm_letter.prompt`, which always writes). Empty with a writer: they choose the subject. Empty with no writer, or with `forgery`, is refused.
- **Forgery:** in the writer's name, by nobody they know. It always passes; nobody remembers writing it; replies go to the person named ([FORGERY.md](FORGERY.md#the-dm-letters-forgeries)). Never transformed.
- **Hours:** -1 (the default a mod should want) is the travel time between the writer and the recipient, measured as the player's own letters are ([DELIVERY.md](DELIVERY.md#travel-time)), with the courier bringing it when the recipient is the player (to an NPC, even in the player's town, it's put in their inventory when due, as the DM Letter's are: `dispatched`); with no writer the fallback time applies. 0 is at once into the player's inventory; to an NPC about 30 seconds later, since an API letter is held so its package can be added to ([Packages](#packages)). More is that many game hours.
- **Tag:** optional, the mod's own label for the letter ("mymod_debt"), at most 64 bytes. Stored with it in LetterDB, never shown to the model or the player; replies to it carry it too ([Finding letters](#finding-letters)).

It returns the **letter id** at once (32 hex digits; "" when refused, with the reason in `PhysicalLetters.log` as `[DmLetter] A letter from the API wasn't sent: …`). A transformed letter exists once the writer's answer comes; until then only the id does, and if no letter ever comes (the call fails, or 5 minutes pass) the `Failed` event names it. No HUD notice is shown for an API letter: the DM Letter's are the DM's.

The letter is an ordinary letter between NPCs, or to the player, from there: its recipient reads it and may reply within `MaxLettersPerThread`, the writer remembers writing it, it can be stolen and read; the schedule's limits don't apply and its thread isn't counted toward `MaxOpenThreads` ([DM_LETTER.md](DM_LETTER.md#after-its-sent)).

## Events

Every letter's, whoever sent it: the schedule's, the player's, the DM Letter's and the API's, so a mod can react to letters it didn't send. Papyrus mod events (`RegisterForModEvent`; `strArg` the letter id, `sender` the actor concerned) and the C listener (`AddListener`; every call on the game thread):

| Event | When | `numArg` / `replied` |
|---|---|---|
| `PhysicalLetters_Delivered` | The letter went into the recipient's inventory: off-screen, by the courier in town or on the road, or handed over by the player (`Transit`), or a letter to the player reached their inventory from the courier (`HandIn`'s container sink: from his container, the vanilla courier's hand-over, which he does out of our sight, or from the courier on the road). Taken again from a chest, a follower or a body, it isn't delivered again (until the 2026-11-01 review it was). An API letter to the player sent with 0 hours fires it at once. | 0 |
| `PhysicalLetters_Read` | An NPC read it: their memory is stored and their reply, if any, is on its way (`Transit::OnReadingDone`). The player's own reading isn't an event. A letter sent again is read again; after loading a save made between delivery and reading, the reading is replayed from LetterDB, so `Read` and its action come again for the same letter, as they should: the save undid what they led to ([READING.md](READING.md#reading-it-again)). | 1 if they replied |
| `PhysicalLetters_Failed` | A letter that was to be written (`transform`, or no text) never was: the call failed, answered with no letter, or 5 minutes passed. `sender` None. | 0 |
| `PhysicalLetters_Action_<name>` | The reader chose the letter action `<name>` ([Actions](#actions)); `sender` the reader. | 0 |

Lookups by id (Papyrus `GetAuthor`, `GetRecipient`, `GetSenderName`, `GetText`, `GetReplyText`; C `GetLetter`): the author (None when nobody wrote it: unsigned, signed, forged), the recipient, whom it says it's from (`Letters::SenderOf`: the author, the signature or the forged name; "" unsigned), its text, and the reply once it's read ("" until then, from the reading LetterDB keeps). Actors are found by their SkyrimNet UUID, checked back to the same FormID.

## Finding letters

Shaped like SkyrimNet's diary and memory queries (`PublicQueryDiaryEntries`: an actor, a JSON filter, a JSON array back; user, 2026-11-01), not like Physical Diaries' API, which only answers for one book at a time. Physical Letters owns its letters as SkyrimNet owns diaries, so it's the one to query.

- **C:** `QueryLetters(actor, filterJson)` returns a JSON array, valid until the next call. `actor` 0 is anyone's. The filter's keys, all optional: `maxCount` (default 50), `role` (`"either"`, `"writer"`: the author, who actually wrote it; `"recipient"`), `other` (a FormID: the person on the other side; needs an actor), `includeTags` (any of them), `excludeTags`, `writtenAfter` and `writtenBefore` (game days, inclusive), `orderBy` (`"WrittenDesc"`, the default, or `"WrittenAsc"`). Each letter: `id`, `author` and `recipient` (FormIDs, 0 none), `sender_name`, `text`, `written_at`, `in_reply_to`, `tag`, `package` (`[{form, count}]`). A bad filter is `[]` and a log line.
- **Papyrus:** returning JSON isn't practical there, so `FindLetters(who, role, other, tag, maxCount)` returns the ids, newest first (role 0 either, 1 wrote, 2 received; None and "" are no constraint), and the lookups give the rest: `GetTag`, `GetWrittenAt`, `GetInReplyTo` with those above.
- **Which letters:** every kind, the player's, the schedule's, the DM Letter's and the API's, but only those **this save has**. LetterDB serves every save of the character ([PERSISTENCE.md](PERSISTENCE.md)), so after loading an older save it still holds letters written later in another; a letter whose form isn't in this save (`Letters::FormFor`) is left out. The filter runs in SQL (`LetterDB::Query`, every value bound), then that check, then `maxCount`.
- **Tags and replies:** a reply carries the tag of the letter it answers (`Transit::MakeReply`), so a mod's letter, the answer and the answers after it are all found by the tag. `GetInReplyTo` walks back; `GetReplyId` forward.

## Actions

A **letter action** is something a letter's reader may do about it, that another mod carries out: go to a meeting, pay a ransom. Physical Letters never does these itself ([ARCHITECTURE.md](ARCHITECTURE.md): it moves no NPC and arranges nothing); it only asks the reader and signals the mod. They are not SkyrimNet actions, and don't go through SkyrimNet's action system: no eligibility, categories or priorities, and no extra LLM call. SkyrimNet's own action system was considered as the home for these (user, 2026-11-01): its actions carry a `triggeringEventTypes` field and an index by event type that nothing reads, so a `letter_read` type could be added. Dropped: the mechanics differ by source (a gift handed over in dialogue, a package with a reply in a letter), so every action would have to opt into letters and handle them anyway, which is the same work as registering here, with a SkyrimNet dependency on top.

- **Registering:** `RegisterAction(name, displayName, displayDescription, description, paramNames, paramDescriptions, letters)` (Papyrus) / `RegisterAction(const PL_Action*)` (C, the same fields plus the callback). The name is letters, digits and `_` (at most 40): what the model names and the event carries. The display name (at most 60 bytes) and display description are for the player, the MCM's label and tooltip (user, 2026-11-01); the description is for the model, when a reader would choose it ("The letter asks to meet, and the reader agrees to come"); each parameter has a name and what the model should put in it ("the place, as named in the letter", "the hour of the day, 0 to 23"). `letters` says whose letters' readers are offered it (user, 2026-11-01): `PL_LETTERS_PLAYER` (1) the player's, `PL_LETTERS_NPC` (2) every other (the schedule's, the DM Letter's, the API's, replies between NPCs), `PL_LETTERS_ALL` (0, the default) both; a letter is the player's when they wrote it (`NpcToNpc::IsNpcLetter` false), forged or unsigned too. All of these are required but the parameters and `letters`. At most 20 actions, 8 parameters each; the same name again replaces it. Parameter names follow the same rule as action names; descriptions are cut at 300 bytes. Registrations live in memory: a mod registers in `OnInit` (a new game has no load) and again on every game load (`OnPlayerLoadGame`), or at kDataLoaded in C++ (the registry outlives loads). `UnregisterAction` takes one away.
- **The reading:** while any action is registered, `read_letter.prompt` gets `actions` and lists them after the reply instruction: the reader decides whether the letter leads to one, and the answer carries `action` (a name, or "none") and `action_params` (an object of the parameters' values, strings). **One action per reading, at most** (user, 2026-11-01): actions from different mods can conflict, two meeting actions say, and only one may happen. A letter asking for two things (to meet, and to be repaid) gets the one the reader picks; in-game test 2026-11-01: Mikael, asked both, chose the meeting. With none registered, nothing is added to the prompt and nothing is asked. The values are free text as the model gives them: the parameter's description is what asks for a usable form, and the mod makes sense of them.
- **Switching them off:** the MCM's Letter Actions page starts with a switch for all of them (`[General] LetterActions`: off, no reading is offered any, whatever their own switches), then lists every action registered this session, by display name, with its display description as the tooltip; switched off, it's left out of the reading prompt and can't be chosen. The switches are saved in `PhysicalLetters.ini`'s `[Actions]` section by name (`meeting = 0`), kept for actions not registered this session, so a mod's switch survives a session without it ([SETTINGS.md](SETTINGS.md)).
- **The signal:** once the reading is stored (`Transit::OnReadingDone`), `Api::ActionChosen` fires, in a task of its own queued after the `Read` event's, so a mod gets `Read` first and its callback may call the API: `PhysicalLetters_Action_<name>` with the letter id and the reader, and the C callback with the values in the order the parameters were registered ("" for one the answer left out). Papyrus gets them with `GetActionValue(letterId, param)`, kept for the session. A name no mod registers any more, or one that wasn't offered for this letter (switched off, or for the other kind of letter), is dropped with a log line. The mod has the reader and, through `GetAuthor`, whom they're answering: for a meeting, the two people to bring together.

**No eligibility** (user, 2026-11-01): unlike SkyrimNet's actions (an `IsEligible` Papyrus function or YAML decorator rules, checked before the call), every switched-on action is offered in every reading of the letters it's for (the player's, NPCs', or both). The event is the signal to check and run: the mod decides from its own records whether the reader can do it (owes the player money, is free to travel) and ignores the event if not. An eligibility function was considered and dropped as not worth its complexity. The cost is a few prompt lines per action in every reading. A reply can also promise what the mod then refuses, so a mod should keep its description narrow enough that the reader rarely chooses it wrongly.

**Why the reading, and why one call** (user, 2026-10-10 and 2026-10-31): a mod event with the letter's text alone would need an LLM call per mod per letter, and its answer could contradict the reply ("I'll be there" in the letter, "no" from the mod's call). Picking the action in the same call as the reply keeps them consistent and costs a few lines of prompt. Only the reading offers them: the reader agreeing to a meeting is the moment it's settled; the writer proposing one isn't.

## Packages

A mod can send items with a letter, its own or anyone's, and see what came with one ([PACKAGES.md](PACKAGES.md)). The use asked for (user, 2026-11-01): a money mod whose NPC repays a loan by letter, or a player paying one back by mail.

- **Adding:** `AddToPackage(letterId, item, count)` (Papyrus `Form`, C FormID) makes `count` of the item in the package store. From there it travels like any package: a courier can be robbed of it, and the letter's "Enclosed:" line lists it. It works only until a courier picks the letter up (`Transit::NotYetCarried`: on its way off-screen to an NPC, or waiting for the courier) or while it's still being written after `transform` (`DmLetter::IsBeingWritten`); a letter with no package gets one. That covers the two uses, right after `SendLetter` and in an action's event for the reply, and never creates items on a courier mid-trip (user, 2026-11-01). So that window is never zero, whatever the settings (`WritingHours` 0, a short trip, a sleep): those two kinds of letter, replies and letters sent through the API, are held for 30 seconds of real time (`Transit::kHold`, session-only) before they're delivered or a courier takes them, in town, on the road or at the post. An API letter sent at 0 hours to an NPC therefore arrives about 30 seconds later; one to the player at 0 hours is in their inventory when `SendLetter` returns, so nothing can be added to it afterwards (a transformed one can be added to while it's being written, and its package goes into the inventory with it). For a letter to an NPC the cutoff is its delivery into their inventory, since no courier is involved (user's question, 2026-11-01). Other letters aren't held (user, 2026-11-01): actions come only from reading a letter, so the package a mod adds in one goes with the reply, and the DM Letter's, the player's and the schedule's letters have their packages, or none, before they're sent. Plain items of the same kind add up into one line; any inventory item is allowed, with no cap on the count. Refused, with a log line: a letter a courier has or that was delivered, an unknown one, something that isn't an inventory item (a leveled list included: it isn't rolled), a count under 1. 
- **Replies:** `GetReplyId(letterId)` is the newest reply to a letter that this save has (`LetterDB::RepliesTo`, then `Letters::FormFor`: after a reload, a reply written in the abandoned timeline isn't), "" if none. In an action's event the reply, if the reader wrote one, exists already: a mod's `repay_loan` action adds the gold to `GetReplyId(letterId)`, and the gold comes with the reply.
- **Reading:** `GetPackageCount(letterId)`, then `GetPackageItem` and `GetPackageItemCount` by index (Papyrus); `GetPackage(letterId, out, max)` (C). What the package holds now, or once delivered what arrived (a robbed courier's package is what he still had). The letter's text (`GetText`) doesn't include the "Enclosed:" line: that's drawn onto the page from the package when it's read, in the player's language.
- **A letter that won't arrive:** never written (the `Failed` event; also after loading a save made while it was being written: the asks are kept in the co-save record `LDMA` and failed on the first tick, `DmLetter::Tick`), dropped because its recipient is dead or not found, or lost to a load during its Transform: the items a mod added are deleted (`Package::Return`, [PACKAGES.md](PACKAGES.md#where-it-goes)), so the player doesn't get them; items the player packed go back to the player.

## Papyrus

Script `PhysicalLetters` (`Source/Scripts/PhysicalLetters.psc`, shipped with its `.pex`), all functions global and native: `GetApiVersion`, `SendLetter`, `GetAuthor`, `GetRecipient`, `GetSenderName`, `GetText`, `GetReplyText`, `GetTag`, `GetWrittenAt`, `GetInReplyTo`, `FindLetters`, `RegisterAction`, `UnregisterAction`, `GetActionValue`, `GetReplyId`, `AddToPackage`, `GetPackageCount`, `GetPackageItem`, `GetPackageItemCount`. Natives run on the game thread. The events above are sent with `SKSE::ModCallbackEvent`.

```papyrus
Event OnInit()
    RegisterMeeting()
EndEvent

Event OnPlayerLoadGame()
    RegisterMeeting()
EndEvent

Function RegisterMeeting()
    string[] names = new string[2]
    string[] what = new string[2]
    names[0] = "place"
    what[0] = "where they will meet, as the letter names it"
    names[1] = "hour"
    what[1] = "the hour of the day they will meet, 0 to 23"
    PhysicalLetters.RegisterAction("meeting", "Meetings", "People who agree by letter to meet go there at the hour.", \
        "The letter asks to meet, and the reader agrees to come.", names, what, 0)
    RegisterForModEvent("PhysicalLetters_Action_meeting", "OnMeeting")
EndFunction

Event OnMeeting(string eventName, string letterId, float numArg, Form sender)
    Actor reader = sender as Actor
    Actor writer = PhysicalLetters.GetAuthor(letterId)
    string place = PhysicalLetters.GetActionValue(letterId, "place")
    ; move them both there at the hour
EndEvent
```

## C

`api/PhysicalLettersAPI.h`: `PL_GetAPI(PL_API_VERSION)` from `kDataLoaded` or later gives a `PL_API` (null: Physical Letters is missing or older than the header). Every function runs on the game thread, every callback is called on it, each event in a task of its own (`Api::Post`): never inside Physical Letters' own loops, so a callback may call the API, `SendLetter` included (a 2026-11-01 review found a listener sending a letter from `Delivered` or `Failed` would have changed the queue `Transit::Tick` was walking); `GetLetter`'s strings are valid until the next `GetLetter`, and the caller sets `PL_Letter.size` first (it may grow; `PL_Item`, filled as an array, never does). Listeners (`AddListener` / `RemoveListener`) get every event; `RegisterAction` takes the action's callback; `GetReplyId`, `AddToPackage` and `GetPackage` are [Packages](#packages); `QueryLetters` is [Finding letters](#finding-letters), its string valid until the next call. The header isn't in the release archive: take it from the repository.

**The contract** (the header's top comment says the same, prompted by an outside read of it on 2026-11-01): callers call from the game thread themselves, since nothing is marshalled; every string and struct passed in is copied before the call returns, and `user` pointers are stored as given; callback arguments are valid only during the callback, `GetLetter`'s strings until the next `GetLetter`, `QueryLetters`' until the next `QueryLetters`. `PL_GetAPI(version)` returns null if the DLL is older than `version`, else the DLL's own table whatever version was asked: functions are only added at the end, so a caller checks `version` or `size` before using one a later version added. `AddToPackage` creates the items it adds (nothing is taken from anyone). Its comments, and `PhysicalLetters.psc`'s, document every function and parameter in full, longer than the project's two-line limit on comments: other mods' authors read those files, not these docs (user, 2026-11-01).

## Versions

- **1** (2026-10-31): the first. Packages (`GetReplyId`, `AddToPackage`, `GetPackage`), tags and finding letters (`QueryLetters`, `FindLetters`, `GetTag`, `GetWrittenAt`, `GetInReplyTo`) were added on 2026-11-01, before any release, so they're still version 1.

## Not done

- Events for the player reading a letter (there's no reading pipeline for them).
- Actions offered to a writer (a letter proposing a meeting).
