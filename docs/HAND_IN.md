# Handing a letter over in person

The player can give a letter they carry to its recipient through the dialogue topic "I have a letter for you.", one letter at a time. The topic shows only to NPCs the player carries a letter for, and its gift menu lists only the letters addressed to that NPC (user, 2026-10-07: shown to everyone, the topic was in the way). Code: `src/HandIn.cpp`, `Transit::HandIn`, the reader modes in `src/Reading.cpp`.

- **To its recipient:** no postage and no travel time; it's delivered at once.
- **Only to them:** the gift menu lists only letters whose recipient UUID is the speaker's. Handing a letter to someone it isn't addressed to (they read a letter that isn't theirs, with its own prompt and memory tag, never answered) was removed on 2026-10-07 (user).
- **They read it there and react aloud**, the text in a SkyrimNet direct narration only they perceive ([Reading it there](#reading-it-there)).
- **Then the reading** of [READING.md](READING.md) runs, as for a letter delivered: their tagged memory of it, and for the recipient perhaps a reply, which travels as any reply does.

Any letter addressed to the NPC counts: one the player wrote to them, or an NPC's letter to them taken from the courier on the road ([ROAD_COURIER.md](ROAD_COURIER.md)) or from someone's pockets. Letters addressed to the player don't.

## What counts

`HandIn::Register` (at `kDataLoaded`) watches `TESContainerChangedEvent` for a letter leaving the player's inventory for an NPC's. The same sink sees letters entering or leaving the player's inventory any other way, to keep the recipients' faction right ([The dialogue](#the-dialogue)). The menus open at that moment decide what it was (read in the event, before the task: a menu may close by then):

| Situation | Result |
|---|---|
| A barter menu open | Sold: nothing |
| The hand-in topic's gift menu (`g_handInTo`, set by its TIF, is this NPC), or the postage menu of the letter's own recipient | Handed over; the menu closes |
| Another gift menu, an innkeeper or the courier, a letter the player wrote, not to them | Posted ([DELIVERY.md](DELIVERY.md#the-hand-over), `Postage::Post`) |
| Anything else | Just an item given: nothing |

**Not supported:** other mods' item transfers (iActions' `ExchangeItems`, SeverActions' `TakeItem` and `GiveItem`, follower trades, other gift menus), letters passed from one NPC to another, and planting one while pickpocketing. An earlier version read all of them; telling a deliberate hand-over from an item stored on a follower, sold, or moved by a script made it messy, so only our topic counts.

Giving a letter to someone who already read it is read again: the prompt says `read_before`, and the LLM picks up on it (tested by mail: the same letter sent twice, the second reply asked whether the first had gone astray).

## The dialogue

"I have a letter for you." — the way to hand a letter over; it can be turned off (`[Delivery] HandInDialogue`, MCM: "Hand-over dialogue", [SETTINGS.md](SETTINGS.md)).

- **Who sees it:** an NPC the player carries a letter for. The topic's info has three conditions:
  - the player carries a letter: `GetKeywordItemCount` of `PhysicalLettersHandInLetter` (`0x8B3`, on every letter form, added by `Letters`' `Configure`) above 0;
  - the speaker is a recipient: `GetInFaction` `PhysicalLettersHasLetterFaction` (`0x8B9`). An actor is in it exactly while the player carries a letter for them: `HandIn::RefreshRecipients` collects the recipient UUIDs of the letters in the player's inventory (from LetterDB) and their FormIDs (SkyrimNet), then puts every loaded actor in or out of the faction (`ProcessLists::ForAllActors`). An actor is in only while the UUID SkyrimNet gives for their FormID is still the letter's (a runtime FormID can belong to someone else by now). It runs when the session is ready, when a letter enters or leaves the player's inventory, and after an edit (the recipient may change; the item doesn't move). An actor that isn't loaded then (a recipient far away, or one whose letter was handed over, dropped or sold meanwhile) is checked when they load: `TESObjectLoadedEvent`, before anyone can talk to them. Event-driven, no polling. A recipient SkyrimNet has no actor for is logged once;
  - the global `PhysicalLettersHandInDialogue` (`0x8B4`) at 1, which `HandIn::ApplyDialogue` sets from the setting (new game, load, the MCM).

  **Why a faction:** a dialogue condition can only read the speaker, and a reference has no keywords of its own (`HasKeyword` reads the base and the race). A keyword on the recipient's base (the first build of this, 2026-10-07) also showed the topic to look-alikes sharing that base, and changed base forms' keyword arrays at run time, which other threads read (Papyrus `HasKeyword`, AI conditions). A faction is per actor and saved with them, and the player's inventory is saved in the same file, so every save holds matching members: loading one (an older one too, Keep or Clear) needs no cleanup, and `Revert` only forgets the recipient list. The only mismatches come from changes while a recipient isn't loaded, which the load check fixes. Earlier versions: until 2026-10-02 a quest with 20 recipient aliases and an alias faction, refreshed every 2 s; then the topic showed to anyone while the player carried any letter, and any letter could be handed to anyone.
- **The topic** `PhysicalLettersHandInTopic` (`0x88B`), top-level branch `PhysicalLettersHandInBranch`, in the quest `PLHandInQuest` (start-game, listed in the SEQ file; it only holds the topic).
- **No spoken answer:** one info (`0x8B5`) whose one response is a single space with no voice file, its script on begin so the gift menu opens at once: nothing is heard or seen; the reader's reaction after the menu is the answer. An info with no response, or an empty one, hides the topic, and "..." shows as a subtitle (vanilla has no silent answers: no blank or "..." English line in Skyrim.esm); a single space with the script on begin works: no subtitle, no sound, the gift menu at once (tested on AE, 2026-10-02). A thanks line before the menu thanked them before they'd seen anything.
- **The TIF** (`PhysicalLetters_TIF_HandIn`, on the info's begin) calls the native `PhysicalLetters_HandInQuest.BeginHandIn(speaker)`, which notes the speaker as the hand-over's (`g_handInTo`, cleared when the gift menu closes), tags the letters the player carries that are addressed to the speaker (by UUID, so look-alikes are told apart) with the keyword `PhysicalLettersForReader` (`0x8BA`); the TIF then opens the gift menu (giving, no favor points) on the form list `PhysicalLettersHandInFilter`, which holds that keyword, so only their letters show. The tags come off when the gift menu closes (and on `Revert`). It's the postage menu's way of filtering (a keyword the DLL puts on letter forms at run time, in a fixed filter list); a filter list holding the letter forms themselves (`AddForm` at run time) showed an empty menu on AE. One letter per hand-over: once one is given, the menu closes (as postage's does); hand another over by choosing the topic again.

## Transit::HandIn

- **Its recipient:** a new delivery: marked delivered now, its "return to sender" mark cleared, any stale parcel of it dropped.
- **Read there** if the reader is with the player (`HandIn::NearPlayer`: alive, loaded, within 2048 units in the player's interior cell or worldspace, and SkyrimNet has `PublicRegisterEvent`): [Reading it there](#reading-it-there).
- **Then the reading:** a state-1 parcel (awaiting reading) for the reader with a new delivery id, read on the next heartbeat, unless they still owe a reading of it (the player took it from them unread and gave it back), which goes on instead.
- **Replies** (recipient only, and only with `[NpcLetters] Replies` on, [SETTINGS.md](SETTINGS.md)) travel as always: to the player through the courier, or to the NPC who wrote an NPC's letter. An NPC's letter the player intercepted ended its thread's pair schedule then (`NpcToNpc::ThreadEnded`), but a reply needs only room in the thread (`CanReply`), so handing it in lets the thread go on; if they don't reply, the thread ends as after any reading.

## Reading it there

The reader reads the letter there and then, and reacts aloud: one SkyrimNet direct narration carrying its text, perceived by the reader alone.

- **The narration**, built in the DLL (`HandIn::ReadThere`): "<player> hands <reader> a letter. It reads: …", or "… a letter from <author>. …" for an NPC's letter the player intercepted (the reader is always the recipient, so naming them again said nothing), then "It is written in blood." or the passages that are, if the player wrote any of it in blood (`Letters::BloodSentence`)".
- **Private:** `PublicRegisterEvent("direct_narration", text, reader, 0, [reader])` (SkyrimNet public API v11), from a worker thread as the API allows. With an audience, the event goes to exactly the listed actors plus originator and target: no one else nearby, no virtual NPCs, not the player (the narration names them; they handed it over without reading it). It's a persistent event: in the reader's history and prompts, and in their later memory generation, and nobody else's. Its originator, the reader, responds to it; what they say aloud is ordinary dialogue that bystanders hear, so how much they let on is theirs.
- **Without `PublicRegisterEvent`** (a SkyrimNet build from before it), `NearPlayer` is false: only the reading runs, without a reaction.
- **Why the text is in the narration, not only the memory:** with the reading's memory alone, the reaction didn't see it (tested on AE: the test letter treated as a grave matter): SkyrimNet caches decorator results (`get_relevant_memories` among them) for 60 s (`PromptEngineCallbacks.cpp`, `CALLBACK_CACHE_TTL`), and recall is a relevance search with a 0.52 threshold. The narration is a recent event, always in context. The reading's tagged memory still matters later: correspondence history and `read_before` find letters by it.
- **Two memories:** the reading's, and the one SkyrimNet's own memory generation makes later from the narration and the conversation.
- **Cost:** two LLM calls per hand-over: the reaction (dialogue) and the reading.

## Testing

Tested on AE: handing a letter to its recipient in the topic's gift menu delivers it, and they read it on the spot through the private narration (`PublicRegisterEvent`) and react to what it says; the silent answer. Also tested on AE (2026-10-03): the reading after a hand-over (memory, reply), a letter given to its recipient in the postage menu. Not yet run: the topic hidden by the setting, a letter written in blood read on the spot; since 2026-10-07, the topic only for recipients (the faction set on a refresh, on an edit, on load, and for a recipient who loads later) and the gift menu listing only their letters.
