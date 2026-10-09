;BEGIN FRAGMENT CODE - Do not edit anything between this and the end comment
;NEXT FRAGMENT INDEX 1
Scriptname PhysicalLetters_TIF_HandIn Extends TopicInfo Hidden

;BEGIN FRAGMENT Fragment_0
Function Fragment_0(ObjectReference akSpeakerRef)
Actor akSpeaker = akSpeakerRef as Actor
;BEGIN CODE
; "I have a letter for you.": BeginHandIn tags the letters addressed to the speaker with the
; filter's keyword, so only those show. Physical Letters' DLL hands over the one given.
PhysicalLetters_HandInQuest.BeginHandIn(akSpeaker)
akSpeaker.ShowGiftMenu(true, PhysicalLettersHandInFilter, false, false)
;END CODE
EndFunction
;END FRAGMENT

;END FRAGMENT CODE - Do not edit anything between this and the begin comment

FormList Property PhysicalLettersHandInFilter Auto
