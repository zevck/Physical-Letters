Scriptname PhysicalLetters Hidden
{Physical Letters' API for other mods (docs/API.md). Every function is global; call them from your own scripts.
Events (RegisterForModEvent): "PhysicalLetters_Delivered" (strArg: letter id, sender: the recipient),
"PhysicalLetters_Read" (strArg: letter id, numArg: 1 if they replied, sender: the reader),
"PhysicalLetters_Failed" (strArg: letter id), and "PhysicalLetters_Action_<name>" (strArg: letter id, sender: the reader).
The examples below are from a quest script with these properties:
    Actor Property Lydia Auto
    MiscObject Property Gold001 Auto}

; The API's version: 1.
;
;   If PhysicalLetters.GetApiVersion() < 1
;       Debug.Trace("Physical Letters is too old for this mod")
;   EndIf
int Function GetApiVersion() global native

; Sends a letter as the DM Letter would, and returns its id at once ("" if refused; PhysicalLetters.log says why).
;   from: the writer; None for a letter nobody wrote: signed with fromName, or unsigned when that's "".
;   to: the recipient; the player is allowed.
;   text: the letter; with transform, notes the writer puts in their own words; "" with a writer: they choose the subject.
;   forgery: in from's name, by nobody they know; it always passes, and nobody remembers writing it. Never transformed.
;   hours: counted from when the letter exists (a transformed one: once written). -1 the travel time between writer
;     and recipient; 0 at once (the player finds it in their inventory; an NPC gets it about 30 real seconds later,
;     so you can add to its package); more, that many game hours. With -1 or more than 0, a letter to the player
;     goes to the courier when due, who brings it when he finds them. A letter to an NPC goes into their inventory
;     when due, with no courier.
;   tag: your label for it, at most 64 bytes ("" none): FindLetters finds it, and replies to it carry it too.
;
;   ; Lydia writes to the player, in her own words, from these notes; it comes by courier.
;   string notes = "She heard bandits were seen near Riverwood and wants the Thane to be careful."
;   string letterId = PhysicalLetters.SendLetter(Lydia, "", Game.GetPlayer(), notes, true, false, -1, "mymod_warning")
;   If letterId == ""
;       Debug.Trace("The letter was refused: see PhysicalLetters.log")
;   EndIf
string Function SendLetter(Actor from, string fromName, Actor to, string text, bool transform, bool forgery, int hours, string tag = "") global native

; Who wrote it: None when nobody did (unsigned, signed with a name, a forgery).
;
;   Actor writer = PhysicalLetters.GetAuthor(letterId)
;   If writer
;       Debug.Trace(writer.GetDisplayName() + " wrote it")
;   EndIf
Actor Function GetAuthor(string letterId) global native

; Who it's for; the player is Game.GetPlayer().
;
;   If PhysicalLetters.GetRecipient(letterId) == Game.GetPlayer()
;       Debug.Trace("It's for the player")
;   EndIf
Actor Function GetRecipient(string letterId) global native

; Whom it says it's from: the writer, the name it's signed with, or the forged name; "" unsigned.
;
;   string sender = PhysicalLetters.GetSenderName(letterId)
;   If sender == ""
;       sender = "someone who didn't sign it"
;   EndIf
;   Debug.Notification("A letter from " + sender)
string Function GetSenderName(string letterId) global native

; The letter's text as written (not the "Enclosed:" line a package adds to its page).
;
;   string text = PhysicalLetters.GetText(letterId)
;   If StringUtil.Find(text, "Riverwood") >= 0
;       Debug.Trace("The letter mentions Riverwood")
;   EndIf
string Function GetText(string letterId) global native

; The recipient's reply once they've read it; "" until then, or if they didn't reply.
;
;   string reply = PhysicalLetters.GetReplyText(letterId)
;   If reply != ""
;       Debug.Trace("They answered: " + reply)
;   EndIf
string Function GetReplyText(string letterId) global native

; Its tag: the one it was sent with, or its first letter's for a reply; "" none.
;
;   If PhysicalLetters.GetTag(letterId) == "mymod_warning"
;       Debug.Trace("One of this mod's letters, or a reply to one")
;   EndIf
string Function GetTag(string letterId) global native

; When it was written, in game days, as Utility.GetCurrentGameTime().
;
;   float daysAgo = Utility.GetCurrentGameTime() - PhysicalLetters.GetWrittenAt(letterId)
;   If daysAgo > 7.0
;       Debug.Trace("Written over a week ago")
;   EndIf
float Function GetWrittenAt(string letterId) global native

; The letter it answers; "" when it isn't a reply.
;
;   string answered = PhysicalLetters.GetInReplyTo(letterId)
;   If answered != ""
;       Debug.Trace("A reply to: " + PhysicalLetters.GetText(answered))
;   EndIf
string Function GetInReplyTo(string letterId) global native

; The ids of the letters in this save, newest first, at most maxCount (0: 50). who: None for anyone's, else the
; letters they wrote (role 1), received (role 2) or either (role 0); other: with that person on the other side
; (needs who); tag: only letters with that tag ("" any).
;
;   ; Every letter between Lydia and the player, either way, newest first.
;   string[] ids = PhysicalLetters.FindLetters(Lydia, 0, Game.GetPlayer())
;   int i = 0
;   While i < ids.Length
;       Debug.Trace(PhysicalLetters.GetSenderName(ids[i]) + ": " + PhysicalLetters.GetText(ids[i]))
;       i += 1
;   EndWhile
string[] Function FindLetters(Actor who, int role = 0, Actor other = None, string tag = "", int maxCount = 0) global native

; An action a letter's reader may choose, offered in readings while registered. Register it in OnInit and again on
; every game load (OnPlayerLoadGame): registrations aren't saved. name: letters, digits and _, at most 40, what the
; model names and the event carries; displayName / displayDescription: its label and tooltip on the MCM's Letter
; Actions page, for the player; description: for the model, when the reader would choose it; paramNames /
; paramDescriptions: its parameters and what the model should put in each; letters: offered to readers of the
; player's letters (1), NPCs' letters (2: every other), or both (0). The same name again replaces it.
;
;   Function RegisterMeeting()
;       string[] names = new string[2]
;       string[] what = new string[2]
;       names[0] = "place"
;       what[0] = "where they will meet, as the letter names it"
;       names[1] = "hour"
;       what[1] = "the hour of the day they will meet, 0 to 23"
;       PhysicalLetters.RegisterAction("meeting", "Meetings", "People who agree by letter to meet go there at the hour.", \
;           "The letter asks to meet, and the reader agrees to come.", names, what, 0)
;       RegisterForModEvent("PhysicalLetters_Action_meeting", "OnMeeting")
;   EndFunction
bool Function RegisterAction(string name, string displayName, string displayDescription, string description, string[] paramNames, string[] paramDescriptions, int letters = 0) global native

; Takes an action out of the readings.
;
;   PhysicalLetters.UnregisterAction("meeting")
Function UnregisterAction(string name) global native

; In a "PhysicalLetters_Action_<name>" event: the chosen action's value for that parameter ("" if none). Always a
; string as the model wrote it, even for a parameter described as a number: an hour may come as "20" or as
; "8 in the evening", and parsing it is yours.
;
;   Event OnMeeting(string eventName, string letterId, float numArg, Form sender)
;       Actor reader = sender as Actor
;       Actor writer = PhysicalLetters.GetAuthor(letterId)
;       string place = PhysicalLetters.GetActionValue(letterId, "place")
;       int hour = PhysicalLetters.GetActionValue(letterId, "hour") as int
;       ; bring reader and writer to the place at the hour
;   EndEvent
string Function GetActionValue(string letterId, string param) global native

; The reply to the letter ("" if none yet). In an action's event the reply, if the reader wrote one, exists already.
;
;   string replyId = PhysicalLetters.GetReplyId(letterId)
;   If replyId != ""
;       Debug.Trace("They wrote back: " + PhysicalLetters.GetText(replyId))
;   EndIf
string Function GetReplyId(string letterId) global native

; Adds count of item to the letter's package while it's still on its way and in no one's hands: while a transformed
; letter is being written, then until it's delivered into an NPC's inventory (when due, and never within 30 real
; seconds of SendLetter or of a reply being written) or a courier takes it (the vanilla courier for a letter to the
; player; for some replies, the courier in town or on the road). So: right after SendLetter, or in an action's
; event for the reply (GetReplyId). Not a letter sent to the player with hours 0: it's in their inventory when
; SendLetter returns (a transformed one, once written: until then it can be added to). False otherwise;
; PhysicalLetters.log says why. The items are created, not taken from anyone: for a payment out of an
; NPC's purse, remove it from them yourself first. Any inventory item but a leveled list. If the letter never
; arrives (never written, its recipient dead), what you added is deleted, not given to the player.
;
;   ; In a "repay_loan" action's event: 500 gold travels with the reader's reply.
;   Event OnRepayLoan(string eventName, string letterId, float numArg, Form sender)
;       string replyId = PhysicalLetters.GetReplyId(letterId)
;       If replyId != ""
;           PhysicalLetters.AddToPackage(replyId, Gold001, 500)
;       EndIf
;   EndEvent
bool Function AddToPackage(string letterId, Form item, int count) global native

; How many kinds of item the letter's package holds, or held when it arrived (0: no package).
;
;   If PhysicalLetters.GetPackageCount(letterId) > 0
;       Debug.Trace("It came with a package")
;   EndIf
int Function GetPackageCount(string letterId) global native

; The package's item at index i, from 0 (None past the end).
;
;   int i = 0
;   While i < PhysicalLetters.GetPackageCount(letterId)
;       Debug.Trace("Enclosed: " + PhysicalLetters.GetPackageItem(letterId, i).GetName())
;       i += 1
;   EndWhile
Form Function GetPackageItem(string letterId, int i) global native

; How many of the package's item at index i (0 past the end).
;
;   int gold = 0
;   int i = 0
;   While i < PhysicalLetters.GetPackageCount(letterId)
;       If PhysicalLetters.GetPackageItem(letterId, i) == Gold001
;           gold += PhysicalLetters.GetPackageItemCount(letterId, i)
;       EndIf
;       i += 1
;   EndWhile
int Function GetPackageItemCount(string letterId, int i) global native
