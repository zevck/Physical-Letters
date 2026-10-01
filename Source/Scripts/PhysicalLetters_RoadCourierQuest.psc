Scriptname PhysicalLetters_RoadCourierQuest extends Quest
{Physical Letters: the vanilla courier met on the road, carrying the letters that pass there
(docs/ROAD_COURIER.md).  Started by the Story Manager from a vanilla road trigger
(WERoadStart) when the DLL says a letter passes the player.  Stages: 10 carrying letters,
15 refused a threat, 17 going to the recipient in town, 20 handed them to the player (rests),
25 walks on.}

; How many letters passing here go into the courier's inventory (0: no encounter).
int Function TakeLetters() global native
; His destination: the map marker of a recipient's town, or None.
ObjectReference Function Destination() global native
; Of the two, the one further along his letters' route.
ObjectReference Function Downstream(ObjectReference akA, ObjectReference akB) global native
; At his destination: the recipient to walk to, or None (his letters go on, due now).
Actor Function ArriveInTown() global native
; He hands his letter for akRecipient over: delivered, then read.
bool Function DeliverToRecipient(Actor akRecipient) global native
; He couldn't reach the recipient: his letters go on, due now.
Function GiveUpDelivery() global native
; The crime faction of the hold a road trigger names, or None.
Faction Function CrimeFactionFor(Location akHold) global native
; The bounty he reports for being robbed of his letters (the INI).
int Function RobberyBounty() global native
; The letters he gives the player: theirs now; the script moves them.
Form[] Function HandOverLetters() global native
; False once the encounter is over for the DLL: after a load, or the quest was stopped.
bool Function IsEncounterCurrent() global native
; The letters he still has go on their way; those he hasn't, the player took.  asWhy is logged.
Function EncounterEnded(string asWhy) global native

ReferenceAlias Property RoadTrigger Auto
ReferenceAlias Property TravelMarker1 Auto
ReferenceAlias Property TravelMarker2 Auto
ReferenceAlias Property Courier Auto
ReferenceAlias Property CourierMarker Auto
ReferenceAlias Property Goal Auto
ReferenceAlias Property RecipientAlias Auto  ; the recipient he delivers to in town
Topic Property GiveUpTopic Auto    ; "Whatever you say!", said as he hands them over
Scene Property DeliveryScene Auto  ; the town courier's scene, with the recipient
GlobalVariable Property GameHour Auto

; He's gone once farther from the player than where he started plus this (game units), and
; never sooner than MinLeaveDistance: vanilla's travel markers can be 10,000 units out.
float Property LeaveMargin = 4096.0 AutoReadOnly
float Property MinLeaveDistance = 12288.0 AutoReadOnly
; Checks (5 s apart) in a row an unloaded courier may stay before he counts as gone: his 3D
; can lag the move, and the player needs time to follow him through a door.
int Property GraceChecks = 6 AutoReadOnly
; Checks he rests after handing his letters to the player, before walking on: 30 s.
int Property RestChecks = 6 AutoReadOnly
; How near his destination's marker counts as arrived, and how near the recipient (both
; loaded) the delivery scene starts.
float Property ArrivedDistance = 1024.0 AutoReadOnly
float Property SceneDistance = 1500.0 AutoReadOnly
; Checks he tries to reach the recipient (through doors, into interiors) before giving up: 3 min.
int Property DeliveryChecks = 36 AutoReadOnly
; He delivers in town only in business hours, so never into someone's bedroom at night.
float Property OpenHour = 8.0 AutoReadOnly
float Property CloseHour = 20.0 AutoReadOnly

float leaveDistance
int checks
int restedChecks
int deliveringChecks
int unloadedChecks
ObjectReference townMarker  ; his destination's map marker, None for a letter to the player
Actor addressee             ; the recipient he walks to in town, once arrived
bool sceneStarted
bool delivered
bool beaten                 ; lost a brawl: hands over his letters once on his feet
bool robbed                 ; gave the player his letters under threat or after a brawl
Location hold               ; the hold the road trigger is in (the story event's location)

Event OnStoryScript(Keyword akKeyword, Location akLocation, ObjectReference akRef1, ObjectReference akRef2, int aiValue1, int aiValue2)
    checks = 0
    restedChecks = 0
    unloadedChecks = 0
    addressee = None
    sceneStarted = false
    delivered = false
    beaten = false
    robbed = false
    hold = akLocation
    if TakeLetters() <= 0
        EndEncounter("no letters")
        return
    endif
    ; As vanilla's road travellers: from the marker farther from the player, so he meets them.
    Actor player = Game.GetPlayer()
    ObjectReference start = TravelMarker1.GetReference()
    ObjectReference other = TravelMarker2.GetReference()
    if player.GetDistance(other) > player.GetDistance(start)
        start = other
        other = TravelMarker1.GetReference()
    endif
    townMarker = Destination()
    if townMarker
        Goal.ForceRefTo(townMarker)
    else
        ; A letter to the player: no town to head for; the way the letter goes.
        Goal.ForceRefTo(Downstream(start, other))
    endif
    leaveDistance = player.GetDistance(start) + LeaveMargin
    if leaveDistance < MinLeaveDistance
        leaveDistance = MinLeaveDistance
    endif
    Actor courierActor = Courier.GetActorReference()
    courierActor.MoveTo(start)
    courierActor.Enable()
    courierActor.EvaluatePackage()
    SetStage(10)
    RegisterForSingleUpdate(5.0)
EndEvent

Event OnUpdate()
    checks += 1
    Actor courierActor = Courier.GetActorReference()
    if courierActor.Is3DLoaded()
        unloadedChecks = 0
    else
        unloadedChecks += 1
    endif
    int stage = GetStage()
    if !IsEncounterCurrent()
        EndEncounter("over for the DLL")
        return
    elseif courierActor.IsInCombat() || courierActor.IsBleedingOut()
        ; Mid-brawl.
    elseif beaten
        ; He lost the brawl and is on his feet: he gives up his letters.
        beaten = false
        courierActor.Say(GiveUpTopic)
        GiveLetters(courierActor)
    elseif unloadedChecks > GraceChecks
        EndEncounter("he's unloaded")
        return
    elseif stage != 17 && courierActor.Is3DLoaded() && courierActor.GetDistance(Game.GetPlayer()) > leaveDistance
        ; Not while he goes to the recipient: through a door, distances across cells mean nothing.
        EndEncounter("he's " + (courierActor.GetDistance(Game.GetPlayer()) as int) + " units from the player")
        return
    elseif stage == 20
        restedChecks += 1
        if restedChecks > RestChecks
            SetStage(25)
            courierActor.EvaluatePackage()
        endif
    elseif stage < 17 && townMarker && courierActor.GetDistance(townMarker) < ArrivedDistance
        Arrive(courierActor)
    elseif stage == 17
        Deliver(courierActor)
    endif
    RegisterForSingleUpdate(5.0)
EndEvent

; At his destination's marker: to the recipient, in business hours; else his letters go on.
Function Arrive(Actor akCourier)
    deliveringChecks = 0
    float hour = GameHour.GetValue()
    if hour >= OpenHour && hour < CloseHour
        addressee = ArriveInTown()
    else
        GiveUpDelivery()
    endif
    if addressee
        ; A package of its own (stage 17): the finished jog to the marker wouldn't restart.
        RecipientAlias.ForceRefTo(addressee)
        Goal.ForceRefTo(addressee)
        SetStage(17)
        akCourier.EvaluatePackage()
    else
        WalkOn(akCourier)
    endif
EndFunction

; Stage 17: he goes to the recipient; near them, the town courier's scene plays and its line
; hands the letter over (DeliverNow).  A scene cut short hands it over silently.
Function Deliver(Actor akCourier)
    deliveringChecks += 1
    if sceneStarted
        if !DeliveryScene.IsPlaying()
            DeliverNow()
            WalkOn(akCourier)
        endif
    elseif akCourier.Is3DLoaded() && addressee.Is3DLoaded() && akCourier.GetDistance(addressee) < SceneDistance
        sceneStarted = true
        DeliveryScene.Start()
    elseif deliveringChecks > DeliveryChecks
        ; He couldn't reach them (a locked house): his letters go on.
        GiveUpDelivery()
        WalkOn(akCourier)
    endif
EndFunction

; The scene's "I have a letter here for you." (or a scene cut short): the letter changes hands.
Function DeliverNow()
    if !delivered && addressee
        delivered = DeliverToRecipient(addressee)
    endif
EndFunction

; Nothing more to deliver here: he walks to his destination's marker and on.
Function WalkOn(Actor akCourier)
    if townMarker
        Goal.ForceRefTo(townMarker)
    endif
    SetStage(25)
    akCourier.EvaluatePackage()
EndFunction

; From the alias: he went down in vanilla's brawl.  He rests from now (stage 20); the letters
; follow once he's out of combat (OnUpdate).
Function Beaten()
    beaten = true
    SetStage(20)
    Courier.GetActorReference().EvaluatePackage()
EndFunction

; He hands the player the letters he carries (intimidated, or beaten in a brawl), then rests.
Function GiveLetters(Actor akCourier)
    Form[] letters = HandOverLetters()
    Actor player = Game.GetPlayer()
    int i = 0
    while i < letters.Length
        akCourier.RemoveItem(letters[i], 1, false, player)
        i += 1
    endwhile
    robbed = robbed || letters.Length > 0
    if GetStage() < 20
        SetStage(20)
        akCourier.EvaluatePackage()
    endif
EndFunction

Function EndEncounter(string asWhy)
    EncounterEnded(asWhy)
    ; Robbed, he reports it to the hold once he's away: a bounty, not a violent crime.
    Faction crime = CrimeFactionFor(hold)
    if robbed && crime && RobberyBounty() > 0
        crime.ModCrimeGold(RobberyBounty(), false)
    endif
    Courier.GetReference().MoveTo(CourierMarker.GetReference())
    ; As vanilla's road encounters do when they stop.
    WETriggerScript encounterTrigger = RoadTrigger.GetReference() as WETriggerScript
    if encounterTrigger
        encounterTrigger.ReArmTrigger()
    endif
    Stop()
EndFunction
