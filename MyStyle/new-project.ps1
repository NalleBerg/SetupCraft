#Requires -Version 5.1
<#
.SYNOPSIS
    Bootstrap a new Win32/C++ project using the proven SetupCraft/NSBEdit skeleton.

.DESCRIPTION
    Interactive script that:
      1. Asks for project metadata (name, app title, GUID, root folder)
      2. Creates the folder structure
      3. Copies skeleton files from the SetupCraft reference project
      4. Patches placeholders (APPNAME, APPGUID, APPTITLE, APPYEAR)
      5. Creates API_list.txt pointing to MyStyleForAllProjects shared docs
      6. Creates API_INTERNALS\INTERNALS\ ready for project-specific notes
      7. Creates a .code-workspace file (gitignored, machine-local)
      8. Initialises a git repository and makes the first commit

.NOTES
    Run from anywhere; it asks for the target parent folder interactively.
    Requires: Git on PATH, MinGW/GCC (for the build skeleton to be useful).
    Optional: ImageMagick on PATH (for make_ico.ps1 to work).

    Skeleton source: SetupCraft project at
        C:\Users\NalleBerg\Documents\C++\Workspace\SetupCraft
    Shared docs:
        C:\Users\NalleBerg\Documents\C++\Workspace\MyStyleForAllProjects
#>

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

# ── Helpers ────────────────────────────────────────────────────────────────────

function Prompt-String {
    param([string]$Msg, [string]$Default = '')
    if ($Default) { $hint = " [$Default]" } else { $hint = '' }
    $raw = Read-Host "$Msg$hint"
    if ($raw.Trim() -eq '' -and $Default -ne '') { return $Default }
    return $raw.Trim()
}

function Prompt-YesNo {
    param([string]$Msg, [bool]$DefaultYes = $true)
    $hint = if ($DefaultYes) { '[Y/n]' } else { '[y/N]' }
    $raw = Read-Host "$Msg $hint"
    if ($raw.Trim() -eq '') { return $DefaultYes }
    return ($raw.Trim() -match '^[Yy]')
}

function New-Guid-Short {
    # Generates a Windows-registry-style GUID without braces
    return [System.Guid]::NewGuid().ToString().ToUpper()
}

function Patch-File {
    param([string]$Path, [hashtable]$Replacements)
    if (-not (Test-Path $Path)) { return }
    $content = Get-Content $Path -Raw -Encoding UTF8
    foreach ($kv in $Replacements.GetEnumerator()) {
        $content = $content -replace [regex]::Escape($kv.Key), $kv.Value
    }
    Set-Content $Path -Value $content -Encoding UTF8 -NoNewline
}

# ── Banner ─────────────────────────────────────────────────────────────────────

Write-Host ""
Write-Host "================================================================" -ForegroundColor Cyan
Write-Host "  new-project.ps1 — Win32/C++ Project Bootstrap" -ForegroundColor Cyan
Write-Host "================================================================" -ForegroundColor Cyan
Write-Host ""

# ── Resolve paths ──────────────────────────────────────────────────────────────

$scriptDir   = Split-Path -Parent $MyInvocation.MyCommand.Path
$sharedDir   = $scriptDir   # MyStyleForAllProjects
$skeletonSrc = "C:\Users\NalleBerg\Documents\C++\Workspace\SetupCraft"

if (-not (Test-Path $skeletonSrc)) {
    Write-Warning "Skeleton source not found: $skeletonSrc"
    Write-Warning "Some file copies will be skipped. Update the path in this script."
}

# ── Gather project metadata ────────────────────────────────────────────────────

Write-Host "── Project Metadata ───────────────────────────────────────────" -ForegroundColor Yellow
Write-Host ""

$projName  = Prompt-String "Project name (identifier, no spaces, e.g. MyTool)"
while ($projName -notmatch '^[A-Za-z][A-Za-z0-9_-]*$') {
    Write-Host "  Name must start with a letter and contain only A-Z, 0-9, _ or -" -ForegroundColor Red
    $projName = Prompt-String "Project name"
}

$appTitle  = Prompt-String "Application display title (e.g. My Tool)"
if ($appTitle -eq '') { $appTitle = $projName }

$appYear   = Prompt-String "Copyright year" -Default (Get-Date -Format yyyy)

$newGuid   = New-Guid-Short
$appGuid   = Prompt-String "Application GUID (leave blank to auto-generate)" -Default $newGuid

$parentDir = Prompt-String "Parent folder for new project" -Default "C:\Users\NalleBerg\Documents\C++\Workspace"
$projDir   = Join-Path $parentDir $projName

Write-Host ""
Write-Host "── Summary ─────────────────────────────────────────────────────" -ForegroundColor Yellow
Write-Host "  Project name  : $projName"
Write-Host "  App title     : $appTitle"
Write-Host "  Copyright year: $appYear"
Write-Host "  GUID          : $appGuid"
Write-Host "  Target folder : $projDir"
Write-Host ""

if (-not (Prompt-YesNo "Proceed?" $true)) {
    Write-Host "Aborted." -ForegroundColor Red
    exit 0
}

# ── Create folder structure ────────────────────────────────────────────────────

Write-Host ""
Write-Host "── Creating folders ────────────────────────────────────────────" -ForegroundColor Yellow

$foldersToCreate = @(
    $projDir,
    "$projDir\API_INTERNALS\INTERNALS",
    "$projDir\icons",
    "$projDir\locale",
    "$projDir\licenses",
    "$projDir\sqlite3",
    "$projDir\scintilla"
)

foreach ($f in $foldersToCreate) {
    New-Item -ItemType Directory -Path $f -Force | Out-Null
    Write-Host "  Created: $f" -ForegroundColor DarkGray
}

# ── Create MyStyle junction ────────────────────────────────────────────────────

Write-Host ""
Write-Host "── Creating MyStyle junction ───────────────────────────────────" -ForegroundColor Yellow

$myStyleTarget = Join-Path $scriptDir ''   # the MyStyle folder IS the script's folder
$myStyleLink   = Join-Path $projDir 'MyStyle'
$junctionResult = cmd /c mklink /J "`"$myStyleLink`"" "`"$myStyleTarget`"" 2>&1
if ($LASTEXITCODE -eq 0) {
    Write-Host "  Junction created: MyStyle -> $myStyleTarget" -ForegroundColor DarkGray
} else {
    Write-Warning "  Could not create MyStyle junction: $junctionResult"
}

# ── Define placeholder map ─────────────────────────────────────────────────────

$placeholders = @{
    'APPNAME'    = $projName
    'APPTITLE'   = $appTitle
    'APPGUID'    = $appGuid
    'APPYEAR'    = $appYear
    'SetupCraft' = $projName     # file-content occurrences only; rename handled separately
}

# ── Copy and patch build system files ─────────────────────────────────────────

Write-Host ""
Write-Host "── Copying build system ────────────────────────────────────────" -ForegroundColor Yellow

$buildFiles = @('follow.bat', 'make_ico.ps1', 'app.manifest', '.clangd', '.gitignore')

foreach ($f in $buildFiles) {
    $src = Join-Path $skeletonSrc $f
    $dst = Join-Path $projDir     $f
    if (Test-Path $src) {
        Copy-Item $src $dst -Force
        Patch-File $dst $placeholders
        Write-Host "  Copied: $f" -ForegroundColor DarkGray
    } else {
        Write-Host "  SKIP (not found): $f" -ForegroundColor DarkYellow
    }
}

# makeit.bat gets extra treatment: strip -specific flags that won't apply
$makeitSrc = Join-Path $skeletonSrc 'makeit.bat'
$makeitDst = Join-Path $projDir     'makeit.bat'
if (Test-Path $makeitSrc) {
    Copy-Item $makeitSrc $makeitDst -Force
    Patch-File $makeitDst $placeholders
    Write-Host "  Copied: makeit.bat" -ForegroundColor DarkGray
} else {
    Write-Host "  SKIP (not found): makeit.bat — create manually" -ForegroundColor DarkYellow
}

# follow.bat / _follow.ps1 — sourced from MyStyleForAllProjects (shared copy)
foreach ($f in @('follow.bat', '_follow.ps1')) {
    $src = Join-Path $sharedDir $f
    if (-not (Test-Path $src)) { $src = Join-Path $skeletonSrc $f }  # fallback to SetupCraft
    if (Test-Path $src) {
        $dst = Join-Path $projDir $f
        Copy-Item $src $dst -Force
        Patch-File $dst $placeholders   # replaces 'SetupCraft' with project name in banner strings and pattern matchers
        Write-Host "  Copied: $f" -ForegroundColor DarkGray
    } else {
        Write-Host "  SKIP (not found): $f" -ForegroundColor DarkYellow
    }
}

# makeit_count.txt / makeit_today.txt — always start fresh for new project
Set-Content (Join-Path $projDir 'makeit_count.txt') '0' -Encoding ASCII
# makeit_today.txt starts empty; _follow.ps1 will append to it on first build
Set-Content (Join-Path $projDir 'makeit_today.txt') '' -Encoding ASCII

# CMakeLists.txt
$cmakeSrc = Join-Path $skeletonSrc 'CMakeLists.txt'
if (Test-Path $cmakeSrc) {
    $cmakeDst = Join-Path $projDir 'CMakeLists.txt'
    Copy-Item $cmakeSrc $cmakeDst -Force
    Patch-File $cmakeDst $placeholders
    Write-Host "  Copied: CMakeLists.txt" -ForegroundColor DarkGray
}

# ── Copy reusable toolbox module source files ─────────────────────────────────

Write-Host ""
Write-Host "── Copying shared toolbox modules ─────────────────────────────" -ForegroundColor Yellow

$toolboxModules = @(
    'tooltip.cpp',      'tooltip.h',
    'checkbox.cpp',     'checkbox.h',
    'dragdrop.cpp',     'dragdrop.h',
    'spinner_dialog.cpp','spinner_dialog.h',
    'edit_rtf.cpp',     'edit_rtf.h',
    'notes_editor.cpp', 'notes_editor.h',
    'ctrlw.cpp',        'ctrlw.h',
    'page_manual.cpp',  'page_manual.h',
    'about.cpp',        'about.h',
    'about_icon.cpp',   'about_icon.h',
    'scrollbar.cpp',    'scrollbar.h',
    'my_scrollbar_vscroll.cpp', 'my_scrollbar_vscroll.h',
    'my_scrollbar_hscroll.cpp', 'my_scrollbar_hscroll.h',
    'dpi.cpp',          'dpi.h',
    'languages.cpp',    'languages.h'
)

foreach ($f in $toolboxModules) {
    $src = Join-Path $skeletonSrc $f
    $dst = Join-Path $projDir     $f
    if (Test-Path $src) {
        Copy-Item $src $dst -Force
        # Do NOT patch source code — module names remain generic
        Write-Host "  Copied: $f" -ForegroundColor DarkGray
    } else {
        Write-Host "  SKIP (not found): $f" -ForegroundColor DarkYellow
    }
}

# ── Copy database layer ────────────────────────────────────────────────────────

Write-Host ""
Write-Host "── Copying database layer ──────────────────────────────────────" -ForegroundColor Yellow

$dbFiles = @('db.cpp', 'db.h')
foreach ($f in $dbFiles) {
    $src = Join-Path $skeletonSrc $f
    $dst = Join-Path $projDir     $f
    if (Test-Path $src) {
        Copy-Item $src $dst -Force
        Write-Host "  Copied: $f" -ForegroundColor DarkGray
    }
}

$sqlite3Src = Join-Path $skeletonSrc 'sqlite3'
if (Test-Path $sqlite3Src) {
    Copy-Item "$sqlite3Src\*" "$projDir\sqlite3\" -Recurse -Force
    Write-Host "  Copied: sqlite3\" -ForegroundColor DarkGray
}

# ── Create minimal main.cpp / mainwindow skeleton ─────────────────────────────

Write-Host ""
Write-Host "── Creating project entry-point stubs ─────────────────────────" -ForegroundColor Yellow

$mainCpp = @"
// $projName — main.cpp
// Entry point — Win32 application.

#include "mainwindow.h"
#include "languages.h"
#include "dpi.h"
#include <windows.h>

int WINAPI WinMain(HINSTANCE hInst, HINSTANCE, LPSTR, int nCmdShow)
{
    DpiInit();
    Languages_Init(hInst);

    return MainWindow_Run(hInst, nCmdShow);
}
"@
Set-Content (Join-Path $projDir 'main.cpp') $mainCpp -Encoding UTF8

$mainwindowH = @"
// $projName — mainwindow.h
#pragma once
#include <windows.h>

int  MainWindow_Run(HINSTANCE hInst, int nCmdShow);
HWND MainWindow_GetHwnd();
"@
Set-Content (Join-Path $projDir 'mainwindow.h') $mainwindowH -Encoding UTF8

$mainwindowCpp = @"
// $projName — mainwindow.cpp
#include "mainwindow.h"

static HWND s_hwnd = nullptr;

HWND MainWindow_GetHwnd() { return s_hwnd; }

static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg)
    {
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

int MainWindow_Run(HINSTANCE hInst, int nCmdShow)
{
    WNDCLASSEXW wc = { sizeof(wc) };
    wc.lpfnWndProc   = WndProc;
    wc.hInstance     = hInst;
    wc.lpszClassName = L"${projName}MainWnd";
    wc.hCursor       = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    RegisterClassExW(&wc);

    s_hwnd = CreateWindowExW(0, wc.lpszClassName, L"$appTitle",
        WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT,
        800, 600, nullptr, nullptr, hInst, nullptr);

    ShowWindow(s_hwnd, nCmdShow);
    UpdateWindow(s_hwnd);

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0))
    {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    return (int)msg.wParam;
}
"@
Set-Content (Join-Path $projDir 'mainwindow.cpp') $mainwindowCpp -Encoding UTF8

Write-Host "  Created: main.cpp, mainwindow.h, mainwindow.cpp" -ForegroundColor DarkGray

# ── Create resource file and icon stubs ───────────────────────────────────────

Write-Host ""
Write-Host "── Creating resource stubs ─────────────────────────────────────" -ForegroundColor Yellow

$rcContent = @"
// $projName — ${projName}.rc
#include <windows.h>
#include "app.manifest"

// App icon — replace ${projName}.ico with your actual icon file
// IDI_APPICON  ICON  "${projName}.ico"
"@
Set-Content (Join-Path $projDir "${projName}.rc") $rcContent -Encoding UTF8

$iconsReadme = Join-Path $skeletonSrc 'icons\README.txt'
if (Test-Path $iconsReadme) {
    Copy-Item $iconsReadme (Join-Path $projDir 'icons\README.txt') -Force
    Write-Host "  Copied: icons\README.txt" -ForegroundColor DarkGray
}

Write-Host "  Created: ${projName}.rc" -ForegroundColor DarkGray

# ── Create locale seed file ────────────────────────────────────────────────────

Write-Host ""
Write-Host "── Creating locale seed ────────────────────────────────────────" -ForegroundColor Yellow

$localeContent = @"
# $projName — en_GB.txt
# Locale seed file.  Copy and translate for each supported language.
# Format:  KEY=Value
# Use \n for newlines inside a value, \" for literal quotes.

APP_TITLE=$appTitle
BTN_OK=OK
BTN_CANCEL=Cancel
BTN_CLOSE=Close
BTN_YES=Yes
BTN_NO=No
"@
Set-Content (Join-Path $projDir 'locale\en_GB.txt') $localeContent -Encoding UTF8
Write-Host "  Created: locale\en_GB.txt" -ForegroundColor DarkGray

# ── Create project API_list.txt ────────────────────────────────────────────────

Write-Host ""
Write-Host "── Creating project API_list.txt ───────────────────────────────" -ForegroundColor Yellow

$apiListContent = @"
================================================================================
  $projName — Documentation Index
================================================================================

  IMPORTANT — AI / AGENT NOTE:
    The files under API_INTERNALS\ document the AUTHOR'S PERSONAL CODING STYLE
    and patterns that travel with them across all Win32/C++ projects.
    They are NOT a separate project.  Any code examples, layout constants, or
    helper names in those files that look "project-specific" are illustrative
    placeholders.  The PATTERNS they describe — measure-then-create dialogs,
    owner-draw buttons, DPI-aware S() sizing, i18n via Ls(), AdjustWindowRectEx
    sizing, modal pump with EnableWindow — are the canonical approach to use in
    THIS project too, adapted to $projName's own helper names.
    For shared reusable toolbox modules and patterns see:
      ..\MyStyleForAllProjects\API_list.txt

  Location:
    API_INTERNALS\API\         Reusable toolbox modules — public interfaces.
    API_INTERNALS\INTERNALS\   $projName-specific implementation notes.

  Living document — update this index whenever a module is added or changed.

================================================================================
  $projName — Project-Specific Modules
================================================================================

  (Add entries here as the project grows.  For shared toolbox modules such as
   tooltip, spinner_dialog, dragdrop, scrollbar, checkbox, edit_rtf,
   notes_editor, ctrlw, page_manual, about, dpi, languages, db — see
   ..\MyStyleForAllProjects\API_list.txt)

"@
Set-Content (Join-Path $projDir 'API_list.txt') $apiListContent -Encoding UTF8
Write-Host "  Created: API_list.txt" -ForegroundColor DarkGray

# ── Create .gitignore adjustments ─────────────────────────────────────────────

Write-Host ""
Write-Host "── Updating .gitignore ─────────────────────────────────────────" -ForegroundColor Yellow

$gitignorePath = Join-Path $projDir '.gitignore'
if (Test-Path $gitignorePath) {
    # makeit_count.txt and code-workspace already handled below
} else {
    # Create a minimal one
    Set-Content $gitignorePath "" -Encoding UTF8
}

$extraIgnore = @"

# Build output
build/

# Local counter — never commit
makeit_count.txt
makeit.log
*.log

# Machine-specific workspace file (absolute paths)
*.code-workspace

# MyStyle junction — machine-local, not part of the repo
MyStyle
"@
Add-Content $gitignorePath $extraIgnore -Encoding UTF8
Write-Host "  Updated: .gitignore" -ForegroundColor DarkGray

# ── Create .code-workspace ────────────────────────────────────────────────────

Write-Host ""
Write-Host "── Creating .code-workspace ────────────────────────────────────" -ForegroundColor Yellow

$wsPath        = Join-Path $projDir "${projName}.code-workspace"
$projDirSlash  = $projDir   -replace '\\', '/'
$sharedDirSlash= $sharedDir -replace '\\', '/'

$wsContent = @"
{
  "folders": [
    { "name": "$projName",            "path": "$projDirSlash" },
    { "name": "MyStyleForAllProjects","path": "$sharedDirSlash" }
  ]
}
"@
Set-Content $wsPath $wsContent -Encoding UTF8
Write-Host "  Created: ${projName}.code-workspace" -ForegroundColor DarkGray

# ── Initialise git repo ────────────────────────────────────────────────────────

Write-Host ""
Write-Host "── Initialising Git repository ─────────────────────────────────" -ForegroundColor Yellow

Push-Location $projDir
try {
    & git init -b main 2>&1 | Out-Null
    & git add --all 2>&1 | Out-Null

    $commitMsg = "Initial commit — $projName skeleton"
    Set-Content 'commit_msg.tmp' $commitMsg -Encoding UTF8
    & git commit -F 'commit_msg.tmp' 2>&1 | Out-Null
    Remove-Item 'commit_msg.tmp' -ErrorAction SilentlyContinue

    Write-Host "  Git repo initialised, initial commit made." -ForegroundColor DarkGray
}
catch {
    Write-Warning "Git init failed: $_"
}
finally {
    Pop-Location
}

# ── Done ──────────────────────────────────────────────────────────────────────

Write-Host ""
Write-Host "================================================================" -ForegroundColor Green
Write-Host "  Done!  Project created at:" -ForegroundColor Green
Write-Host "    $projDir" -ForegroundColor Green
Write-Host ""
Write-Host "  Next steps:" -ForegroundColor Green
Write-Host "    1. Open ${projName}.code-workspace in VS Code" -ForegroundColor Green
Write-Host "    2. Add your app icon as ${projName}.ico and uncomment in .rc" -ForegroundColor Green
Write-Host "    3. Update makeit.bat with your actual source file list" -ForegroundColor Green
Write-Host "    4. Add locale keys to locale\en_GB.txt as you build" -ForegroundColor Green
Write-Host "    5. Document modules in API_list.txt as they are added" -ForegroundColor Green
Write-Host "================================================================" -ForegroundColor Green
Write-Host ""
