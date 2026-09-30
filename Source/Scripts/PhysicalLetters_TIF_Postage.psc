;BEGIN FRAGMENT CODE - Do not edit anything between this and the end comment
;NEXT FRAGMENT INDEX 1
Scriptname PhysicalLetters_TIF_Postage Extends TopicInfo Hidden

;BEGIN FRAGMENT Fragment_0
Function Fragment_0(ObjectReference akSpeakerRef)
Actor akSpeaker = akSpeakerRef as Actor
;BEGIN CODE
; Only the letters the player wrote (keyword PhysicalLettersOutgoingLetter). Physical Letters'
; DLL takes the postage and sends the letter when one is given, then closes the menu.
akSpeaker.ShowGiftMenu(true, PhysicalLettersOutgoingFilter, false, false)
;END CODE
EndFunction
;END FRAGMENT

;END FRAGMENT CODE - Do not edit anything between this and the begin comment

FormList Property PhysicalLettersOutgoingFilter Auto
