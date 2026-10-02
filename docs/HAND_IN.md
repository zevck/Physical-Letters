# Handing a letter over in person

The player can give a letter they carry to an NPC through the dialogue topic "I have a letter for you.", one letter at a time. Code: `src/HandIn.cpp`, `Transit::HandIn`, the reader modes in `src/Reading.cpp`.

- **To its recipient:** no postage and no travel time; it's delivered at once.
- **To anyone else:** they read a letter that isn't theirs; the letter stays with them.
- **They read it there and react aloud**, the text in a SkyrimNet direct narration only they perceive ([Reading it there](#reading-it-there)).
- **Then the reading** of [READING.md](READING.md) runs, as for a letter delivered: their tagged memory of it, and for the recipient perhaps a reply, which travels as any reply does. Someone else's letter is read as in [Someone else's letter](#someone-elses-letter), and never answered.

Any letter counts: one the player wrote, a reply to them, or an NPC's letter taken from the courier on the road ([ROAD_COURIER.md](ROAD_COURIER.md)) or from someone's pockets.

## What counts

`HandIn::Register` (at `kDataLoaded`) watches `TESContainerChangedEvent` for a letter leaving the player's inventory for an NPC's. The menus open at that moment decide what it was (read in the event, before the task: a menu may close by then):

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

- **Who sees it:** anyone, while the player carries a letter. Every letter form carries the keyword `PhysicalLettersHandInLetter` (`0x8B3`, added by `Letters`' `Configure` when a letter is made or loaded), and the topic's info requires `GetKeywordItemCount` of it on the player above 0, as the postage topic does with its own keyword, plus the global `PhysicalLettersHandInDialogue` (`0x8B4`) at 1, which `HandIn::ApplyDialogue` sets from the setting (new game, load, the MCM). An earlier version showed the topic only to NPCs the player carried a letter for (a quest with 20 recipient aliases and an alias faction, refreshed every 2 s); once any letter could be handed to anyone, that restriction wasn't worth the machinery.
- **The topic** `PhysicalLettersHandInTopic` (`0x88B`), top-level branch `PhysicalLettersHandInBranch`, in the quest `PLHandInQuest` (start-game, listed in the SEQ file; it only holds the topic).
- **No spoken answer:** one info (`0x8B5`) whose one response is a single space with no voice file, its script on begin so the gift menu opens at once: nothing is heard or seen; the reader's reaction after the menu is the answer. An info with no response, or an empty one, hides the topic, and "..." shows as a subtitle (vanilla has no silent answers: no blank or "..." English line in Skyrim.esm); a single space with the script on begin works: no subtitle, no sound, the gift menu at once (tested on AE, 2026-10-02). A thanks line before the menu thanked them before they'd seen anything.
- **The TIF** (`PhysicalLetters_TIF_HandIn`, on the info's begin) calls the native `PhysicalLetters_HandInQuest.BeginHandIn(speaker)`, which notes the speaker as the hand-over's (`g_handInTo`, cleared when the gift menu closes), then opens the gift menu (giving, no favor points) on the form list `PhysicalLettersHandInFilter`, which holds the keyword, so every letter the player carries shows. A filter list holding the letter forms themselves (`AddForm` at run time) showed an empty menu on AE. One letter per hand-over: once one is given, the menu closes (as postage's does); hand another over by choosing the topic again.

## Transit::HandIn

- **Its recipient:** a new delivery: marked delivered now, its "return to sender" mark cleared, any stale parcel of it dropped.
- **Anyone else:** the addressee's records are untouched (not delivered; a reading they owe goes on, though they no longer have it).
- **Read there** if the reader is with the player (`HandIn::NearPlayer`: alive, loaded, within 2048 units in the player's interior cell or worldspace, and SkyrimNet has `PublicRegisterEvent`): [Reading it there](#reading-it-there).
- **Then the reading:** a state-1 parcel (awaiting reading) for the reader with a new delivery id, read on the next heartbeat, unless they still owe a reading of it (the player took it from them unread and gave it back), which goes on instead.
- **Replies** (recipient only) travel as always: to the player through the courier, or to the NPC who wrote an NPC's letter. An NPC's letter the player intercepted ended its thread's pair schedule then (`NpcToNpc::ThreadEnded`), but a reply needs only room in the thread (`CanReply`), so handing it in lets the thread go on; if they don't reply, the thread ends as after any reading.

## Someone else's letter

`Reading::Read` with `Reader::kHandedOther` (the parcel's reader isn't the letter's recipient):

- **The prompt** `physical_letters_read_other_letter` (in the SkyrimNet plugin folder, beside the recipient's): the reader's profile, that the player handed it to them, whether it's their own letter come back (`reader_is_author`), whether they've seen it before, and their memories of the author and of the recipient (half the usual count each). No correspondence history: the letters between author and recipient aren't theirs. Same JSON as the recipient's reading; `reply` is always false.
- **The memory**: tagged `letter_seen` (not `letter_received`) besides the letter and delivery tags, its text "The letter from A to B: …". LetterDB's stored reading is the recipient's, so none is kept for someone else.
- **No reply and no thread change**: `OnReadingDone` makes no reply and doesn't end an NPC thread when the reader isn't the recipient.

## Reading it there

The reader reads the letter there and then, and reacts aloud: one SkyrimNet direct narration carrying its text, perceived by the reader alone.

- **The narration**, built in the DLL (`HandIn::ReadThere`): "<player> hands <reader> a letter from A to B. It reads: …".
- **Private:** `PublicRegisterEvent("direct_narration", text, reader, 0, [reader])` (SkyrimNet public API v11), from a worker thread as the API allows. With an audience, the event goes to exactly the listed actors plus originator and target: no one else nearby, no virtual NPCs, not the player (the narration names them; they handed it over without reading it). It's a persistent event: in the reader's history and prompts, and in their later memory generation, and nobody else's. Its originator, the reader, responds to it; what they say aloud is ordinary dialogue that bystanders hear, so how much they let on is theirs.
- **Without `PublicRegisterEvent`** (a SkyrimNet build from before it), `NearPlayer` is false: only the reading runs, without a reaction.
- **Why the text is in the narration, not only the memory:** with the reading's memory alone, the reaction didn't see it (tested on AE: the test letter treated as a grave matter): SkyrimNet caches decorator results (`get_relevant_memories` among them) for 60 s (`PromptEngineCallbacks.cpp`, `CALLBACK_CACHE_TTL`), and recall is a relevance search with a 0.52 threshold. The narration is a recent event, always in context. The reading's tagged memory still matters later: correspondence history and `read_before` find letters by it.
- **Two memories:** the reading's, and the one SkyrimNet's own memory generation makes later from the narration and the conversation.
- **Cost:** two LLM calls per hand-over: the reaction (dialogue) and the reading.

## Testing

Tested on AE: handing a letter to its recipient in the topic's gift menu delivers it, and they read it on the spot through the private narration (`PublicRegisterEvent`) and react to what it says; the silent answer. Not yet run: the topic shown by the keyword (and hidden by the setting), the reading after a hand-over (memory, reply), someone else's letter, a letter given to its recipient in the postage menu.
