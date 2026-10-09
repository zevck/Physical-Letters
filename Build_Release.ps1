# Release build for Physical Letters: the archive players install.
#
#   1. Refuses a working tree with uncommitted changes (a release is a commit), unless -allowDirty.
#   2. Builds everything with .\Build_Local.ps1 -noDeploy (plugin, Papyrus, ESP), unless -skipBuild.
#   3. Stages build\release\stage as the game's Data folder: the DLL, the ESP, Scripts, Source\Scripts,
#      Interface\Translations, Seq, the courier's voice files, the locale files and the SkyrimNet plugin.
#   4. Checks what it staged: a .pex for every script, every translation and locale file with English's
#      keys, the SkyrimNet manifest at CMakeLists.txt's version, and no comments in the prompts.
#   5. Zips it: build\release\Physical Letters <version>.zip (no FOMOD: there is nothing to choose).
#
# Usage:
#   .\Build_Release.ps1                # build, check, zip
#   .\Build_Release.ps1 -skipBuild     # pack the last build as it is
#   .\Build_Release.ps1 -allowDirty    # a test release from uncommitted work
#
# The outcome is printed and written to %TEMP%\pl-release-result.json. See docs/RELEASES.md.

#Requires -Version 7

param(
    [string]$config = "Release",
    [switch]$skipBuild,
    [switch]$allowDirty
)
$ErrorActionPreference = "Stop"
Set-Location -LiteralPath $PSScriptRoot
Add-Type -AssemblyName System.IO.Compression.FileSystem

$resultFile = Join-Path $env:TEMP "pl-release-result.json"
function Complete-Release {
    param([string]$Status, [string]$Message, [string]$Archive = "")
    $color = if ($Status -eq 'SUCCESS') { 'Green' } else { 'Red' }
    Write-Host ""
    Write-Host "==================== RELEASE $Status ====================" -ForegroundColor $color
    if ($Archive) { Write-Host "  Archive: $Archive" -ForegroundColor $color }
    if ($Message) { Write-Host "  $Message" -ForegroundColor $color }
    Write-Host "=========================================================" -ForegroundColor $color
    @{ status = $Status; message = $Message; archive = $Archive; finished = (Get-Date -Format o) } |
        ConvertTo-Json | Set-Content -LiteralPath $resultFile -Encoding UTF8
    exit $(if ($Status -eq 'SUCCESS') { 0 } else { 1 })
}
trap { Complete-Release -Status 'FAILURE' -Message $_.Exception.Message }

# --- 1. A commit ---------------------------------------------------------------
$commit = (git rev-parse --short HEAD).Trim()
$dirty = git status --porcelain
if ($dirty) {
    if (-not $allowDirty) { Complete-Release -Status 'FAILURE' -Message "Uncommitted changes: commit first, or -allowDirty for a test release" }
    Write-Host "Uncommitted changes: a test release, not commit $commit's." -ForegroundColor Yellow
    $commit += "-dirty"
}
$version = (Select-String -Path "CMakeLists.txt" -Pattern '^\s*VERSION\s+([\d.]+)').Matches[0].Groups[1].Value
Write-Host "Physical Letters $version ($commit)" -ForegroundColor Cyan

# --- 2. Build ------------------------------------------------------------------
if (-not $skipBuild) {
    & (Join-Path $PSScriptRoot "Build_Local.ps1") -noDeploy -config $config
    if ($LASTEXITCODE -ne 0) { Complete-Release -Status 'FAILURE' -Message "Build_Local.ps1 failed (see above)" }
}

# --- 3. Stage --------------------------------------------------------------------
$release = Join-Path $PSScriptRoot "build\release"
$stage = Join-Path $release "stage"
if (Test-Path $stage) { Remove-Item $stage -Recurse -Force }
$skyrimNetPlugin = "SKSE\Plugins\SkyrimNet\external\zevick.physical-letters"
$files = [ordered]@{
    "SKSE\Plugins\PhysicalLetters.dll" = "build\$config\PhysicalLetters.dll"
    "Physical Letters.esp"             = "build\esp\Physical Letters.esp"
    "Seq\Physical Letters.seq"         = "Seq\Physical Letters.seq"
}
$folders = @(
    "Scripts",
    "Source\Scripts",
    "Interface\Translations",
    "Sound\Voice\Physical Letters.esp",
    "SKSE\Plugins\PhysicalLetters\Locales",
    $skyrimNetPlugin
)
foreach ($to in $files.Keys) {
    if (-not (Test-Path -LiteralPath $files[$to])) { throw "Missing $($files[$to]): build first" }
    New-Item -ItemType Directory -Force -Path (Split-Path (Join-Path $stage $to)) | Out-Null
    Copy-Item -LiteralPath $files[$to] -Destination (Join-Path $stage $to)
}
foreach ($folder in $folders) {
    if (-not (Test-Path -LiteralPath $folder)) { throw "Missing $folder" }
    New-Item -ItemType Directory -Force -Path (Join-Path $stage $folder) | Out-Null
    Copy-Item -Path (Join-Path $folder "*") -Destination (Join-Path $stage $folder) -Recurse
}

# --- 4. Check --------------------------------------------------------------------
$problems = @()
# Every script is compiled (Scripts\*.pex is gitignored: an old or missing one would ship otherwise).
foreach ($source in Get-ChildItem (Join-Path $stage "Source\Scripts\*.psc")) {
    $pex = Join-Path $stage "Scripts\$($source.BaseName).pex"
    if (-not (Test-Path -LiteralPath $pex)) { $problems += "No compiled script for $($source.Name)" }
    elseif ((Get-Item -LiteralPath $pex).LastWriteTime -lt $source.LastWriteTime) { $problems += "$($source.BaseName).pex is older than its source" }
}
# MCM translations: UTF-16, the same keys as English.
function Get-TranslationKeys([string]$path) {
    [IO.File]::ReadAllText($path, [Text.Encoding]::Unicode) -split "`r?`n" |
        Where-Object { $_ -match '^\$' } | ForEach-Object { ($_ -split "`t", 2)[0] }
}
$english = Get-TranslationKeys (Join-Path $stage "Interface\Translations\Physical Letters_ENGLISH.txt")
foreach ($file in Get-ChildItem (Join-Path $stage "Interface\Translations\*.txt")) {
    $diff = Compare-Object $english (Get-TranslationKeys $file.FullName)
    if ($diff) { $problems += "$($file.Name): keys differ from English ($(($diff | ForEach-Object InputObject) -join ', '))" }
}
# Letter text: every locale file has English's [Letters] keys (a missing one would fall back to English).
function Get-LocaleKeys([string]$path) {
    Get-Content -LiteralPath $path -Encoding UTF8 | Where-Object { $_ -match '^\s*[^;\[\s][^=]*=' } |
        ForEach-Object { ($_ -split '=', 2)[0].Trim() }
}
$localeDir = Join-Path $stage "SKSE\Plugins\PhysicalLetters\Locales"
$englishLocale = Get-LocaleKeys (Join-Path $localeDir "ENGLISH.ini")
foreach ($file in Get-ChildItem (Join-Path $localeDir "*.ini")) {
    $diff = Compare-Object $englishLocale (Get-LocaleKeys $file.FullName)
    if ($diff) { $problems += "Locales\$($file.Name): keys differ from English ($(($diff | ForEach-Object InputObject) -join ', '))" }
}
# The SkyrimNet plugin says the same version, and its prompts carry no comments (docs/ARCHITECTURE.md).
$manifest = Get-Content -LiteralPath (Join-Path $stage "$skyrimNetPlugin\manifest.json") -Raw | ConvertFrom-Json
if ($manifest.version -ne $version) { $problems += "SkyrimNet manifest is version $($manifest.version), not $version" }
foreach ($prompt in Get-ChildItem (Join-Path $stage $skyrimNetPlugin) -Recurse -Filter "*.prompt") {
    if ((Get-Content -LiteralPath $prompt.FullName -Raw) -match '\{#') { $problems += "$($prompt.Name) has a comment" }
}
if ($problems) { Complete-Release -Status 'FAILURE' -Message ("Checks failed:`n  - " + ($problems -join "`n  - ")) }

# --- 5. Zip ----------------------------------------------------------------------
$archive = Join-Path $release "Physical Letters $version.zip"
if (Test-Path -LiteralPath $archive) { Remove-Item -LiteralPath $archive -Force }
[IO.Compression.ZipFile]::CreateFromDirectory($stage, $archive, [IO.Compression.CompressionLevel]::Optimal, $false)
$count = (Get-ChildItem $stage -Recurse -File).Count
$size = "{0:N0} KB" -f ((Get-Item -LiteralPath $archive).Length / 1KB)
Complete-Release -Status 'SUCCESS' -Archive $archive -Message "$count files, $size, from $commit"
