Scriptname PhysicalLetters_CourierQuest extends Quest
{Physical Letters: the vanilla courier carries a letter to an NPC near the player
(docs/COURIER.md).  The Story Manager starts this when the player steps into a town and a
letter waits for the courier; the DLL picks the letter and its recipient.}

; The recipient of the letter given to the courier, or None (nothing for here, or the
; courier is busy elsewhere).
Actor Function TakeTarget() global native
; The courier hands the letter over; the recipient reads it.
Function HandOver() global native
; False once the errand is over for the DLL: after a load, or another quest took the courier.
Bool Function IsErrandCurrent() global native
; The errand ends; a letter not handed over goes in off-screen.  True if the courier is
; still ours to send back.
Bool Function ErrandEnded() global native

ReferenceAlias Property Courier Auto
ReferenceAlias Property CourierMarker Auto
ReferenceAlias Property LocationCenterMarker Auto
ReferenceAlias Property Target Auto
Scene Property DeliveryScene Auto
Keyword Property LocTypeHabitation Auto

; Checks (5 s apart) before a scene still playing counts as stalled: 3 minutes.
int Property MaxChecks = 36 AutoReadOnly

bool released  ; the errand is over for the DLL; he lingers until the player leaves
int checks

Event OnStoryChangeLocation(ObjectReference akActor, Location akOldLocation, Location akNewLocation)
    released = false
    checks = 0
    Actor recipient = TakeTarget()
    if !recipient
        Stop()
        return
    endif
    Target.ForceRefTo(recipient)
    Actor courierActor = Courier.GetActorReference()
    ; As vanilla: he arrives at the town's centre during the player's load transition.
    courierActor.MoveTo(LocationCenterMarker.GetReference())
    courierActor.Enable()
    courierActor.EvaluatePackage()
    DeliveryScene.Start()
    RegisterForSingleUpdate(5.0)
EndEvent

Event OnUpdate()
    Location courierAt = Courier.GetReference().GetCurrentLocation()
    Location playerAt = Game.GetPlayer().GetCurrentLocation()
    checks += 1
    if !courierAt || !playerAt || !courierAt.IsSameLocation(playerAt, LocTypeHabitation)
        EndErrand()
        return
    endif
    if !released
        if !IsErrandCurrent()
            EndErrand()
            return
        endif
        ; Handed over, cut short (combat, no start) or stalled: a letter not handed over goes in unseen.
        if !DeliveryScene.IsPlaying() || checks > MaxChecks
            DeliveryScene.Stop()
            ErrandEnded()
            released = true
        endif
    endif
    RegisterForSingleUpdate(5.0)
EndEvent

Function EndErrand()
    DeliveryScene.Stop()
    if ErrandEnded()
        Courier.GetReference().MoveTo(CourierMarker.GetReference())
    endif
    Stop()
EndFunction
