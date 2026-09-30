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
int _oidDebugLog = -1

event OnConfigInit()
    ModName = "Physical Letters"
endevent

; Called by OnPageReset, which builds every option: nothing may rely on OnConfigOpen
; having run first (on the first open it hadn't, and the page came up empty).  Rebuilt
; every time: OnConfigInit runs once per save, so a script update would keep old arrays.
function Setup()
    _names = new string[5]
    _labels = new string[5]
    _tips = new string[5]
    _formats = new string[5]
    _steps = new int[5]
    _oids = new int[5]
    _names[0] = "Delivery.Postage"
    _labels[0] = "$PL_Postage"
    _tips[0] = "$PL_TipPostage"
    _formats[0] = "{0}"
    _steps[0] = 5
    _names[1] = "Delivery.WritingHours"
    _labels[1] = "$PL_WritingHours"
    _tips[1] = "$PL_TipWritingHours"
    _formats[1] = "{0}"
    _steps[1] = 1
    _names[2] = "Delivery.MinHours"
    _labels[2] = "$PL_MinHours"
    _tips[2] = "$PL_TipMinHours"
    _formats[2] = "{0}"
    _steps[2] = 1
    _names[3] = "Delivery.FallbackHours"
    _labels[3] = "$PL_FallbackHours"
    _tips[3] = "$PL_TipFallbackHours"
    _formats[3] = "{0}"
    _steps[3] = 1
    _names[4] = "Delivery.ReturnAfterDays"
    _labels[4] = "$PL_ReturnAfterDays"
    _tips[4] = "$PL_TipReturnAfterDays"
    _formats[4] = "{0}"
    _steps[4] = 1
endfunction

event OnPageReset(string page)
    Setup()
    SetCursorFillMode(TOP_TO_BOTTOM)
    AddHeaderOption("$PL_HeaderDelivery")
    int i = 0
    while i < _names.Length
        _oids[i] = AddSliderOption(_labels[i], GetSetting(_names[i]), _formats[i])
        i += 1
    endwhile
    AddEmptyOption()
    AddHeaderOption("$PL_HeaderLogging")
    _oidDebugLog = AddToggleOption("$PL_DebugLog", GetSetting("General.DebugLog") != 0)
endevent

int function SliderIndex(int oid)
    return _oids.Find(oid)
endfunction

event OnOptionSliderOpen(int oid)
    int i = SliderIndex(oid)
    if i >= 0
        SetSliderDialogStartValue(GetSetting(_names[i]))
        SetSliderDialogDefaultValue(GetSettingDefault(_names[i]))
        SetSliderDialogRange(GetSettingMin(_names[i]), GetSettingMax(_names[i]))
        SetSliderDialogInterval(_steps[i])
    endif
endevent

event OnOptionSliderAccept(int oid, float value)
    int i = SliderIndex(oid)
    if i >= 0
        SetSetting(_names[i], value as int)
        SetSliderOptionValue(oid, GetSetting(_names[i]), _formats[i])
    endif
endevent

event OnOptionSelect(int oid)
    if oid == _oidDebugLog
        bool enabled = GetSetting("General.DebugLog") == 0
        SetSetting("General.DebugLog", enabled as int)
        SetToggleOptionValue(oid, enabled)
    endif
endevent

event OnOptionDefault(int oid)
    int i = SliderIndex(oid)
    if i >= 0
        SetSetting(_names[i], GetSettingDefault(_names[i]))
        SetSliderOptionValue(oid, GetSetting(_names[i]), _formats[i])
    elseif oid == _oidDebugLog
        SetSetting("General.DebugLog", 0)
        SetToggleOptionValue(oid, false)
    endif
endevent

event OnOptionHighlight(int oid)
    int i = SliderIndex(oid)
    if i >= 0
        SetInfoText(_tips[i])
    elseif oid == _oidDebugLog
        SetInfoText("$PL_TipDebugLog")
    endif
endevent
