;BEGIN FRAGMENT CODE - Do not edit anything between this and the end comment
;NEXT FRAGMENT INDEX 1
Scriptname PhysicalLetters_TIF_RoadIntimidate Extends TopicInfo Hidden

;BEGIN FRAGMENT Fragment_0
Function Fragment_0(ObjectReference akSpeakerRef)
Actor akSpeaker = akSpeakerRef as Actor
;BEGIN CODE
; Intimidated: vanilla's Speech experience and stats, then he hands over his letters.
DialogueFavorGeneric.Intimidate(akSpeaker)
(GetOwningQuest() as PhysicalLetters_RoadCourierQuest).GiveLetters(akSpeaker)
;END CODE
EndFunction
;END FRAGMENT

;END FRAGMENT CODE - Do not edit anything between this and the begin comment

FavorDialogueScript Property DialogueFavorGeneric Auto
