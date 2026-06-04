@echo off
setlocal

REM ── Self-logging wrapper ──────────────────────────────────────────────────────
REM First invocation truncates makeit.log to signal a new run to _follow.ps1,
REM then re-calls this script with ALL stdout+stderr redirected into that file.
REM The _SC_LOGGED flag tells the recursive call to skip this block.
if not "%_SC_LOGGED%"=="1" (
    set "_SC_LOGGED=1"
    echo. > "%~dp0makeit.log"
    call "%~f0" %* >> "%~dp0makeit.log" 2>&1
    exit /b %errorlevel%
)

REM ─────────────────────────────────────────────────────────────────────────────
REM  SetupCraft build script
REM  Usage: makeit.bat [generator] [config]
REM ─────────────────────────────────────────────────────────────────────────────

echo ================================================================
echo   SetupCraft Build  --  %DATE%  %TIME%
echo ================================================================

REM ── [PRE] Kill any running instance so file handles are free ─────────────────
echo.
echo [PRE] Stopping any running SetupCraft.exe instances...
taskkill /IM SetupCraft.exe /F
ping -n 2 127.0.0.1 >nul 2>&1
echo [PRE] Ready.

REM ── Build parameters ──────────────────────────────────────────────────────────
set "GENERATOR=%~1"
set "CONFIG=%~2"
if "%GENERATOR%"=="" set "GENERATOR=MinGW Makefiles"
if "%CONFIG%"=="" set "CONFIG=Release"

echo.
echo   Generator : %GENERATOR%
echo   Config    : %CONFIG%
echo ================================================================

REM ── [CONFIGURE] CMake configure ───────────────────────────────────────────────
echo.
echo [CONFIGURE] Running cmake -S . -B build ...
if "%GENERATOR%"=="" (
    cmake -S . -B build -DCMAKE_BUILD_TYPE=%CONFIG%
) else (
    cmake -S . -B build -G "%GENERATOR%" -DCMAKE_BUILD_TYPE=%CONFIG%
)

if errorlevel 1 (
    echo.
    echo [ERROR] CMake configure failed — aborting.
    exit /b 1
)

REM ── [BUILD] Compile and link ───────────────────────────────────────────────────
echo.
echo [BUILD] Compiling — cmake --build build --clean-first --verbose ...
echo.
cmake --build build --config %CONFIG% --clean-first --verbose

if errorlevel 1 (
    echo.
    echo [ERROR] Build failed.
    exit /b 1
)

echo.
echo [BUILD] Build complete.

REM ── [COPY-BUILD] Copy runtime assets to build\ for in-place testing ───────────
echo.
echo [COPY-BUILD] Copying runtime assets to build\ ...
copy /y "%~dp0SetupCraft.png"  "%~dp0build\"
copy /y "%~dp0GPLv2.md"        "%~dp0build\"
copy /y "%~dp0GnuLogo.bmp"     "%~dp0build\"
copy /y "%~dp0curver.txt"      "%~dp0build\"
if exist "%~dp0LicenseImg\" (
    xcopy /e /i /y "%~dp0LicenseImg" "%~dp0build\LicenseImg\"
)
copy /y "%~dp0scintilla\Scintilla.dll" "%~dp0build\"
copy /y "%~dp0scintilla\Lexilla.dll"   "%~dp0build\"

REM ── [PACKAGE] Assemble the SetupCraft\ output folder ─────────────────────────
echo.
echo [PACKAGE] Assembling output package ...
set "PKG_DIR=%~dp0SetupCraft"
if exist "%PKG_DIR%" (
    echo [PACKAGE] Removing old package folder...
    rmdir /s /q "%PKG_DIR%"
)
mkdir "%PKG_DIR%"

REM Find SetupCraft.exe under build\
set "EXE_PATH="
set "EXE_DIR="
for /f "delims=" %%I in ('dir /b /s "build\SetupCraft.exe" 2^>nul') do (
    set "EXE_PATH=%%~fI"
    set "EXE_DIR=%%~dpI"
    goto :_found_exe
)
:_found_exe
if "%EXE_PATH%"=="" (
    echo [ERROR] SetupCraft.exe not found under build\
    exit /b 1
)

echo Copying executable: %EXE_PATH%
copy /y "%EXE_PATH%" "%PKG_DIR%\"

REM DLLs placed next to the exe by the linker
if exist "%EXE_DIR%*.dll" (
    echo Copying DLLs from build folder
    xcopy /y "%EXE_DIR%*.dll" "%PKG_DIR%\"
)

REM MinGW runtime DLLs
if exist "%~dp0mingw_dll\" (
    mkdir "%PKG_DIR%\mingw_dlls"
    echo Copying DLLs from mingw_dll
    xcopy /y "%~dp0mingw_dll\*.dll" "%PKG_DIR%\mingw_dlls\"
    xcopy /y "%~dp0mingw_dll\*.dll" "%PKG_DIR%\"
    if exist "%PKG_DIR%\mingw_dlls\README.md" del /q "%PKG_DIR%\mingw_dlls\README.md"
) else if exist "%~dp0mingw_dlls\" (
    xcopy /e /i /y "%~dp0mingw_dlls" "%PKG_DIR%\mingw_dlls\"
)

REM Locale files
if exist "%~dp0locale\" (
    echo Copying locale files...
    xcopy /e /i /y "%~dp0locale" "%PKG_DIR%\locale\"
)

REM SQLite
if exist "%~dp0sqlite3\" (
    echo Copying sqlite3...
    xcopy /e /i /y "%~dp0sqlite3" "%PKG_DIR%\sqlite3\"
)

REM Inno Setup (bundled ISCC + compiler DLLs + language files + template)
if exist "%~dp0inno\" (
    echo Copying inno directory...
    xcopy /e /i /y "%~dp0inno" "%PKG_DIR%\inno\"

    REM Remove files that belong to the Inno Setup self-installer or are
    REM developer-only and have no place in the distributed SetupCraft package.
    for %%F in (
        "%PKG_DIR%\inno\unins000.dat"
        "%PKG_DIR%\inno\unins000.exe"
        "%PKG_DIR%\inno\unins000.msg"
        "%PKG_DIR%\inno\ISetup.chm"
        "%PKG_DIR%\inno\ISetup-dark.chm"
        "%PKG_DIR%\inno\isfaq.url"
        "%PKG_DIR%\inno\whatsnew.htm"
        "%PKG_DIR%\inno\WizClassicImage.bmp"
        "%PKG_DIR%\inno\WizClassicImage-IS.bmp"
        "%PKG_DIR%\inno\WizClassicSmallImage.bmp"
        "%PKG_DIR%\inno\WizClassicSmallImage-IS.bmp"
        "%PKG_DIR%\inno\SetupClassicIcon.ico"
        "%PKG_DIR%\inno\ISPPBuiltins.iss"
        "%PKG_DIR%\inno\Compil32.exe"
        "%PKG_DIR%\inno\ISSigTool.exe"
        "%PKG_DIR%\inno\ISetup.chm"
        "%PKG_DIR%\inno\license.txt"
    ) do if exist %%F del /q %%F

    REM Remove Examples subfolder — not needed at runtime
    if exist "%PKG_DIR%\inno\Examples\" rmdir /s /q "%PKG_DIR%\inno\Examples"
)

REM Scintilla / Lexilla
copy /y "%~dp0scintilla\Scintilla.dll" "%PKG_DIR%\"
copy /y "%~dp0scintilla\Lexilla.dll"   "%PKG_DIR%\"

REM Helper scripts and assets
copy /y "%~dp0install_gnulogo.bat" "%PKG_DIR%\"
copy /y "%~dp0SetupCraft.ico"      "%PKG_DIR%\"
copy /y "%~dp0SetupCraft.png"      "%PKG_DIR%\"
copy /y "%~dp0SetupCraft.svg"      "%PKG_DIR%\"
copy /y "%~dp0GPLv2.md"            "%PKG_DIR%\"
copy /y "%~dp0GnuLogo.bmp"         "%PKG_DIR%\"
copy /y "%~dp0curver.txt"          "%PKG_DIR%\"
if exist "%~dp0LicenseImg\" (
    xcopy /e /i /y "%~dp0LicenseImg" "%PKG_DIR%\LicenseImg\"
)

echo.
echo Package created at %PKG_DIR%

REM Show exe timestamp
for %%F in ("%PKG_DIR%\SetupCraft.exe") do echo SetupCraft.exe: %%~tF

echo.
echo [DONE] All steps completed successfully.
