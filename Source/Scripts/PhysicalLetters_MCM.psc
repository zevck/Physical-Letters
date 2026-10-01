Scriptname PhysicalLetters_MCM extends SKI_ConfigBase
{Physical Letters' settings (docs/SETTINGS.md).  Every value lives in the DLL's INI; a
setting is named "Section.Key", and its default and range come from the DLL too.}

int  Function GetSetting(string name)                  global native
     Function SetSetting(string name, int value)       global native
int  Function GetSettingDefault(string name)           global native
int  Function GetSettingMin(string name)               global native
int  Function GetSettingMax(string name)               global native

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

event OnConfigInit()
    ModName = "Physical Letters"
endevent

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
    _names = new string[20]
    _labels = new string[20]
    _tips = new string[20]
    _formats = new string[20]
    _steps = new int[20]
    _oids = new int[20]
    Slider(0, "Delivery.Postage", "$PL_Postage", "$PL_TipPostage", 5)
    Slider(1, "Delivery.WritingHours", "$PL_WritingHours", "$PL_TipWritingHours", 1)
    Slider(2, "Delivery.MinHours", "$PL_MinHours", "$PL_TipMinHours", 1)
    Slider(3, "Delivery.FallbackHours", "$PL_FallbackHours", "$PL_TipFallbackHours", 1)
    Slider(4, "Delivery.ReturnAfterDays", "$PL_ReturnAfterDays", "$PL_TipReturnAfterDays", 1)
    Slider(5, "NpcLetters.IntervalDays", "$PL_NpcInterval", "$PL_TipNpcInterval", 1)
    Slider(6, "NpcLetters.CooldownDays", "$PL_NpcCooldown", "$PL_TipNpcCooldown", 1)
    Slider(7, "NpcLetters.MinEvents", "$PL_NpcMinEvents", "$PL_TipNpcMinEvents", 1)
    Slider(8, "NpcLetters.MinDaysApart", "$PL_NpcMinDaysApart", "$PL_TipNpcMinDaysApart", 1)
    Slider(9, "NpcLetters.MissedAfterDays", "$PL_NpcMissedAfter", "$PL_TipNpcMissedAfter", 1)
    Slider(10, "NpcLetters.RecentWeight", "$PL_NpcRecentWeight", "$PL_TipNpcRecentWeight", 5)
    _formats[10] = "{0}%"
    Slider(11, "NpcLetters.NearDistance", "$PL_NpcNearDistance", "$PL_TipNpcNearDistance", 512)
    Slider(12, "NpcToNpc.IntervalDays", "$PL_N2nInterval", "$PL_TipN2nInterval", 1)
    Slider(13, "NpcToNpc.MaxOpenThreads", "$PL_N2nMaxThreads", "$PL_TipN2nMaxThreads", 1)
    Slider(14, "NpcToNpc.MaxLettersPerThread", "$PL_N2nMaxLetters", "$PL_TipN2nMaxLetters", 1)
    Slider(15, "NpcToNpc.PairCooldownDays", "$PL_N2nPairCooldown", "$PL_TipN2nPairCooldown", 1)
    Slider(16, "NpcToNpc.WritersPerAttempt", "$PL_N2nWriters", "$PL_TipN2nWriters", 1)
    Slider(17, "NpcToNpc.NamesPerWriter", "$PL_N2nNames", "$PL_TipN2nNames", 1)
    Slider(18, "NpcToNpc.MemoriesPerWriter", "$PL_N2nMemories", "$PL_TipN2nMemories", 1)
    Slider(19, "NpcToNpc.MinDistance", "$PL_N2nMinDistance", "$PL_TipN2nMinDistance", 1024)

    _toggleNames = new string[4]
    _toggleLabels = new string[4]
    _toggleTips = new string[4]
    _toggleOids = new int[4]
    Toggle(0, "NpcLetters.Enabled", "$PL_NpcLetters", "$PL_TipNpcLetters")
    Toggle(1, "General.DebugLog", "$PL_DebugLog", "$PL_TipDebugLog")
    Toggle(2, "NpcToNpc.Enabled", "$PL_N2n", "$PL_TipN2n")
    Toggle(3, "NpcToNpc.KnownOnly", "$PL_N2nKnownOnly", "$PL_TipN2nKnownOnly")
endfunction

function AddSliders(int first, int last)
    int i = first
    while i <= last
        _oids[i] = AddSliderOption(_labels[i], GetSetting(_names[i]), _formats[i])
        i += 1
    endwhile
endfunction

function AddToggle(int i)
    _toggleOids[i] = AddToggleOption(_toggleLabels[i], GetSetting(_toggleNames[i]) != 0)
endfunction

event OnPageReset(string page)
    Setup()
    SetCursorFillMode(TOP_TO_BOTTOM)
    AddHeaderOption("$PL_HeaderDelivery")
    AddSliders(0, 4)
    AddEmptyOption()
    AddHeaderOption("$PL_HeaderNpcLetters")
    AddToggle(0)
    AddSliders(5, 11)
    AddEmptyOption()
    AddHeaderOption("$PL_HeaderN2n")
    AddToggle(2)
    AddToggle(3)
    AddSliders(12, 19)
    AddEmptyOption()
    AddHeaderOption("$PL_HeaderLogging")
    AddToggle(1)
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
    int i = _toggleOids.Find(oid)
    if i >= 0
        bool enabled = GetSetting(_toggleNames[i]) == 0
        SetSetting(_toggleNames[i], enabled as int)
        SetToggleOptionValue(oid, enabled)
    endif
endevent

event OnOptionDefault(int oid)
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
