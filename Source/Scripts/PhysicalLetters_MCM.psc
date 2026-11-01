Scriptname PhysicalLetters_MCM extends SKI_ConfigBase
{Physical Letters' settings (docs/SETTINGS.md).  Every value lives in the DLL's INI; a
setting is named "Section.Key", and its default and range come from the DLL too.}

int  Function GetSetting(string name)                  global native
     Function SetSetting(string name, int value)       global native
int  Function GetSettingDefault(string name)           global native
int  Function GetSettingMin(string name)               global native
int  Function GetSettingMax(string name)               global native
; Puts the DM Letter in the player's inventory: 0 added, 1 already carried, 2 can't be used (docs/DM_LETTER.md).
int  Function AddDmLetter()                            global native
; The letter actions mods registered this session, and their switches (docs/API.md#actions).
int    Function GetActionCount()                       global native
string Function GetActionName(int i)                   global native
string Function GetActionDisplayName(int i)            global native
string Function GetActionDisplayDescription(int i)     global native
bool   Function IsActionEnabled(string name)           global native
       Function SetActionEnabled(string name, bool on) global native

; The sliders, in page order: setting, label, tooltip, format, step.
string[] _names
string[] _labels
string[] _tips
string[] _formats
int[] _steps
int[] _oids

; The toggles (on/off settings): setting, label, tooltip.
string[] _toggleNames
string[] _toggleLabels
string[] _toggleTips
int[] _toggleOids

; Writing.GenericRecipients (0-2), a menu: its choices are its values.
string[] _recipientChoices
int _recipientsOid = -1
; General.Font (0-2), a menu the same way: handwriting, the UI's font, the books' font.
string[] _fontChoices
int _fontOid = -1
; The DM Letter's button.
int _dmOid = -1
; The letter actions shown, and their toggles.
string[] _actionNames
int[] _actionOids
int _actionsNoneOid = -1

event OnConfigInit()
    ModName = "Physical Letters"
    SetPages()
endevent

; Also on every open: a save made before the pages existed ran OnConfigInit without them.
event OnConfigOpen()
    SetPages()
endevent

function SetPages()
    Pages = new string[4]
    Pages[0] = "$PL_PageGeneral"
    Pages[1] = "$PL_PageNpcLetters"
    Pages[2] = "$PL_PageDelivery"
    Pages[3] = "$PL_PageActions"
endfunction

function Slider(int i, string name, string label, string tip, int step)
    _names[i] = name
    _labels[i] = label
    _tips[i] = tip
    _formats[i] = "{0}"
    _steps[i] = step
endfunction

function Toggle(int i, string name, string label, string tip)
    _toggleNames[i] = name
    _toggleLabels[i] = label
    _toggleTips[i] = tip
endfunction

; Called by OnPageReset, which builds every option: nothing may rely on OnConfigOpen
; having run first (on the first open it hadn't, and the page came up empty).  Rebuilt
; every time: OnConfigInit runs once per save, so a script update would keep old arrays.
function Setup()
    _names = new string[25]
    _labels = new string[25]
    _tips = new string[25]
    _formats = new string[25]
    _steps = new int[25]
    _oids = new int[25]
    Slider(0, "Delivery.Postage", "$PL_Postage", "$PL_TipPostage", 5)
    Slider(1, "Delivery.WritingHours", "$PL_WritingHours", "$PL_TipWritingHours", 1)
    Slider(2, "Delivery.MinHours", "$PL_MinHours", "$PL_TipMinHours", 1)
    Slider(3, "Delivery.FallbackHours", "$PL_FallbackHours", "$PL_TipFallbackHours", 1)
    Slider(4, "Delivery.ReturnAfterDays", "$PL_ReturnAfterDays", "$PL_TipReturnAfterDays", 1)
    Slider(5, "NpcLetters.IntervalDays", "$PL_NpcInterval", "$PL_TipNpcInterval", 1)
    Slider(6, "NpcLetters.CooldownDays", "$PL_NpcCooldown", "$PL_TipNpcCooldown", 1)
    Slider(7, "NpcLetters.MinEvents", "$PL_NpcMinEvents", "$PL_TipNpcMinEvents", 1)
    Slider(8, "NpcLetters.MinDaysApart", "$PL_NpcMinDaysApart", "$PL_TipNpcMinDaysApart", 1)
    Slider(9, "NpcLetters.DaysUntilMissed", "$PL_NpcMissDays", "$PL_TipNpcMissDays", 1)
    Slider(10, "General.FontSize", "$PL_FontSize", "$PL_TipFontSize", 1)
    ; 11 and 19 are free (settings removed 2026-10-03).
    Slider(12, "NpcToNpc.IntervalDays", "$PL_N2nInterval", "$PL_TipN2nInterval", 1)
    Slider(13, "NpcToNpc.MaxOpenThreads", "$PL_N2nMaxThreads", "$PL_TipN2nMaxThreads", 1)
    Slider(14, "NpcToNpc.MaxLettersPerThread", "$PL_N2nMaxLetters", "$PL_TipN2nMaxLetters", 1)
    Slider(15, "NpcToNpc.PairCooldownDays", "$PL_N2nPairCooldown", "$PL_TipN2nPairCooldown", 1)
    Slider(16, "NpcToNpc.WritersPerAttempt", "$PL_N2nWriters", "$PL_TipN2nWriters", 1)
    Slider(17, "NpcToNpc.NamesPerWriter", "$PL_N2nNames", "$PL_TipN2nNames", 1)
    Slider(18, "NpcToNpc.MemoriesPerWriter", "$PL_N2nMemories", "$PL_TipN2nMemories", 1)
    Slider(20, "NpcLetters.CandidatesPerAttempt", "$PL_NpcCandidates", "$PL_TipNpcCandidates", 1)
    Slider(21, "Courier.WaitHours", "$PL_CourierWait", "$PL_TipCourierWait", 1)
    Slider(22, "Courier.RoadCooldownDays", "$PL_RoadCooldown", "$PL_TipRoadCooldown", 1)
    Slider(23, "Courier.IntimidateSpeech", "$PL_RoadSpeech", "$PL_TipRoadSpeech", 5)
    Slider(24, "Courier.RobberyBounty", "$PL_RoadBounty", "$PL_TipRoadBounty", 5)

    _toggleNames = new string[11]
    _toggleLabels = new string[11]
    _toggleTips = new string[11]
    _toggleOids = new int[11]
    Toggle(0, "NpcLetters.Enabled", "$PL_NpcLetters", "$PL_TipNpcLetters")
    Toggle(1, "General.DebugLog", "$PL_DebugLog", "$PL_TipDebugLog")
    Toggle(2, "NpcToNpc.Enabled", "$PL_N2n", "$PL_TipN2n")
    Toggle(3, "NpcToNpc.KnownOnly", "$PL_N2nKnownOnly", "$PL_TipN2nKnownOnly")
    Toggle(4, "Courier.Enabled", "$PL_Courier", "$PL_TipCourier")
    Toggle(5, "Courier.RoadEncounters", "$PL_Road", "$PL_TipRoad")
    Toggle(6, "Delivery.HandInDialogue", "$PL_HandIn", "$PL_TipHandIn")
    Toggle(7, "NpcLetters.Replies", "$PL_NpcReplies", "$PL_TipNpcReplies")
    Toggle(8, "Writing.Blood", "$PL_Blood", "$PL_TipBlood")
    Toggle(9, "NpcLetters.ForgeryReplies", "$PL_ForgeryReplies", "$PL_TipForgeryReplies")
    Toggle(10, "General.LetterActions", "$PL_LetterActions", "$PL_TipLetterActions")
    _recipientsOid = -1

    _recipientChoices = new string[3]
    _recipientChoices[0] = "$PL_RecipientsUnique"
    _recipientChoices[1] = "$PL_RecipientsKnown"
    _recipientChoices[2] = "$PL_RecipientsAnyone"

    _fontOid = -1
    _dmOid = -1
    _actionsNoneOid = -1
    ; Another page's action toggles mustn't match this page's options.
    _actionNames = new string[1]
    _actionOids = new int[1]
    _actionOids[0] = -1
    _fontChoices = new string[3]
    _fontChoices[0] = "$PL_FontHandwritten"
    _fontChoices[1] = "$PL_FontEverywhere"
    _fontChoices[2] = "$PL_FontBook"
endfunction

function AddSlider(int i)
    _oids[i] = AddSliderOption(_labels[i], GetSetting(_names[i]), _formats[i])
endfunction

; One toggle per registered letter action, by its display name; none registered: a line saying so.
function AddActions()
    ; 20: Api.cpp's kMaxActions (Papyrus array sizes must be literals).
    _actionNames = new string[20]
    _actionsNoneOid = -1
    _actionOids = new int[20]
    int count = GetActionCount()
    if count == 0
        _actionsNoneOid = AddTextOption("$PL_ActionsNone", "")
        AddEmptyOption()
        return
    endif
    int i = 0
    while i < count && i < 20
        _actionNames[i] = GetActionName(i)
        _actionOids[i] = AddToggleOption(GetActionDisplayName(i), IsActionEnabled(_actionNames[i]))
        i += 1
    endwhile
    if count % 2 == 1
        AddEmptyOption()
    endif
endfunction

function AddToggle(int i)
    _toggleOids[i] = AddToggleOption(_toggleLabels[i], GetSetting(_toggleNames[i]) != 0)
endfunction

; A section: its header across both columns.
function AddHeader(string text)
    AddHeaderOption(text)
    AddEmptyOption()
endfunction

function AddGap()
    AddEmptyOption()
    AddEmptyOption()
endfunction

; Four pages, two columns filled left to right (docs/SETTINGS.md#the-mcm).
event OnPageReset(string page)
    Setup()
    SetCursorFillMode(LEFT_TO_RIGHT)
    if page == "$PL_PageNpcLetters"
        AddHeader("$PL_HeaderNpcLetters")
        AddSlider(5)
        AddSlider(6)
        AddSlider(7)
        AddSlider(8)
        AddSlider(9)
        AddSlider(20)
        AddGap()
        AddHeader("$PL_HeaderN2n")
        AddToggle(3)
        AddSlider(12)
        AddSlider(13)
        AddSlider(14)
        AddSlider(15)
        AddSlider(16)
        AddSlider(17)
        AddSlider(18)
    elseif page == "$PL_PageDelivery"
        AddHeader("$PL_HeaderDelivery")
        AddSlider(0)
        AddSlider(1)
        AddToggle(6)
        AddSlider(2)
        AddSlider(4)
        AddSlider(3)
        AddGap()
        AddHeader("$PL_HeaderCourier")
        AddToggle(4)
        AddSlider(21)
        AddToggle(5)
        AddSlider(22)
        AddSlider(23)
        AddSlider(24)
    elseif page == "$PL_PageActions"
        AddHeader("$PL_HeaderActions")
        AddToggle(10)
        AddEmptyOption()
        AddGap()
        AddActions()
    else
        ; General, and the first open (no page chosen yet).
        AddHeader("$PL_HeaderGeneral")
        AddToggle(0)
        AddToggle(7)
        AddToggle(9)
        AddToggle(2)
        _recipientsOid = AddMenuOption("$PL_Recipients", _recipientChoices[GetSetting("Writing.GenericRecipients")])
        AddToggle(8)
        AddSlider(10)
        _fontOid = AddMenuOption("$PL_Font", _fontChoices[GetSetting("General.Font")])
        AddGap()
        AddHeader("$PL_HeaderDm")
        _dmOid = AddTextOption("$PL_AddDmLetter", "")
        AddEmptyOption()
        AddGap()
        AddHeader("$PL_HeaderLogging")
        AddToggle(1)
    endif
endevent

event OnOptionSliderOpen(int oid)
    int i = _oids.Find(oid)
    if i >= 0
        SetSliderDialogStartValue(GetSetting(_names[i]))
        SetSliderDialogDefaultValue(GetSettingDefault(_names[i]))
        SetSliderDialogRange(GetSettingMin(_names[i]), GetSettingMax(_names[i]))
        SetSliderDialogInterval(_steps[i])
    endif
endevent

event OnOptionSliderAccept(int oid, float value)
    int i = _oids.Find(oid)
    if i >= 0
        SetSetting(_names[i], value as int)
        SetSliderOptionValue(oid, GetSetting(_names[i]), _formats[i])
    endif
endevent

event OnOptionSelect(int oid)
    if _actionOids
        int a = _actionOids.Find(oid)
        if a >= 0
            bool on = !IsActionEnabled(_actionNames[a])
            SetActionEnabled(_actionNames[a], on)
            SetToggleOptionValue(oid, on)
            return
        endif
    endif
    if oid == _dmOid
        int result = AddDmLetter()
        if result == 0
            SetTextOptionValue(oid, "$PL_DmAdded")
        elseif result == 1
            SetTextOptionValue(oid, "$PL_DmCarried")
        else
            SetTextOptionValue(oid, "$PL_DmUnavailable")
        endif
        return
    endif
    int i = _toggleOids.Find(oid)
    if i >= 0
        bool enabled = GetSetting(_toggleNames[i]) == 0
        SetSetting(_toggleNames[i], enabled as int)
        SetToggleOptionValue(oid, enabled)
    endif
endevent

event OnOptionMenuOpen(int oid)
    if oid == _recipientsOid
        SetMenuDialogOptions(_recipientChoices)
        SetMenuDialogStartIndex(GetSetting("Writing.GenericRecipients"))
        SetMenuDialogDefaultIndex(GetSettingDefault("Writing.GenericRecipients"))
    elseif oid == _fontOid
        SetMenuDialogOptions(_fontChoices)
        SetMenuDialogStartIndex(GetSetting("General.Font"))
        SetMenuDialogDefaultIndex(GetSettingDefault("General.Font"))
    endif
endevent

event OnOptionMenuAccept(int oid, int index)
    if oid == _recipientsOid && index >= 0
        SetSetting("Writing.GenericRecipients", index)
        SetMenuOptionValue(oid, _recipientChoices[GetSetting("Writing.GenericRecipients")])
    elseif oid == _fontOid && index >= 0
        SetSetting("General.Font", index)
        SetMenuOptionValue(oid, _fontChoices[GetSetting("General.Font")])
    endif
endevent

event OnOptionDefault(int oid)
    if oid == _recipientsOid
        SetSetting("Writing.GenericRecipients", GetSettingDefault("Writing.GenericRecipients"))
        SetMenuOptionValue(oid, _recipientChoices[GetSetting("Writing.GenericRecipients")])
        return
    elseif oid == _fontOid
        SetSetting("General.Font", GetSettingDefault("General.Font"))
        SetMenuOptionValue(oid, _fontChoices[GetSetting("General.Font")])
        return
    endif
    int i = _oids.Find(oid)
    if i >= 0
        SetSetting(_names[i], GetSettingDefault(_names[i]))
        SetSliderOptionValue(oid, GetSetting(_names[i]), _formats[i])
        return
    endif
    i = _toggleOids.Find(oid)
    if i >= 0
        SetSetting(_toggleNames[i], GetSettingDefault(_toggleNames[i]))
        SetToggleOptionValue(oid, GetSetting(_toggleNames[i]) != 0)
    endif
endevent

event OnOptionHighlight(int oid)
    if oid == _actionsNoneOid
        SetInfoText("$PL_TipActionsNone")
        return
    endif
    if _actionOids
        int a = _actionOids.Find(oid)
        if a >= 0
            SetInfoText(GetActionDisplayDescription(a))
            return
        endif
    endif
    if oid == _dmOid
        SetInfoText("$PL_TipAddDmLetter")
        return
    elseif oid == _recipientsOid
        SetInfoText("$PL_TipRecipients")
        return
    elseif oid == _fontOid
        SetInfoText("$PL_TipFont")
        return
    endif
    int i = _oids.Find(oid)
    if i >= 0
        SetInfoText(_tips[i])
        return
    endif
    i = _toggleOids.Find(oid)
    if i >= 0
        SetInfoText(_toggleTips[i])
    endif
endevent
