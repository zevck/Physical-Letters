Scriptname PhysicalLetters_RoadCourierAlias extends ReferenceAlias
{Physical Letters: the courier on the road (docs/ROAD_COURIER.md).  Beaten in vanilla's
brawl, he gives up: he rests instead of running on, and hands over his letters.}

Quest Property DGIntimidateQuest Auto  ; vanilla's brawl

Event OnEnterBleedout()
    PhysicalLetters_RoadCourierQuest owner = GetOwningQuest() as PhysicalLetters_RoadCourierQuest
    ; Only a brawl: armed combat gets nothing from him.
    if DGIntimidateQuest.IsRunning() && owner.GetStage() < 20
        owner.Beaten()
    endif
EndEvent
