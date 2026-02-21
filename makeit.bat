@echo off
setlocal

REM Kill any running SetupCraft.exe processes before building
taskkill /IM SetupCraft.exe /F >nul 2>&1

REM Simple build helper for the Skeleton project
REM Usage: makeit.bat [generator] [config]

set "GENERATOR=%~1"
set "CONFIG=%~2"

if "%GENERATOR%"=="" set "GENERATOR=MinGW Makefiles"
if "%CONFIG%"=="" set "CONFIG=Release"

echo Generator: %GENERATOR%
echo Configuration: %CONFIG%

if "%GENERATOR%"=="" (
  cmake -S . -B build -DCMAKE_BUILD_TYPE=%CONFIG%
) else (
  cmake -S . -B build -G "%GENERATOR%" -DCMAKE_BUILD_TYPE=%CONFIG%
)

if errorlevel 1 (
  echo [ERROR] CMake configure failed
  exit /b 1
)

cmake --build build --config %CONFIG%

echo Build complete.

REM --- Package the build into .\SetupCraft\ ---
set "PKG_DIR=%~dp0SetupCraft"
if exist "%PKG_DIR%" (
  rmdir /s /q "%PKG_DIR%"
)
mkdir "%PKG_DIR%"

REM Find the built executable (first *.exe under build folder)
set "EXE_PATH="
set "EXE_DIR="
for /f "delims=" %%I in ('dir /b /s "build\*.exe" 2^>nul') do (
  set "EXE_PATH=%%~fI"
  set "EXE_DIR=%%~dpI"
  goto :_found_exe
)
:_found_exe
if "%EXE_PATH%"=="" (
  echo [ERROR] No executable found under build\
  exit /b 1
)

echo Copying executable: %EXE_PATH%
copy /y "%EXE_PATH%" "%PKG_DIR%\" >nul

REM Copy any DLLs that were placed next to the executable (runtime dependencies)
if exist "%EXE_DIR%*.dll" (
  echo Copying DLLs from build folder
  xcopy /y /q "%EXE_DIR%*.dll" "%PKG_DIR%\"
)

REM Copy runtime DLLs shipped with the repo (mingw_dlls) as a subfolder
REM Prefer `mingw_dll` (singular) if present, otherwise fall back to `mingw_dlls`
if exist "%~dp0mingw_dll\" (
  mkdir "%PKG_DIR%\mingw_dlls" >nul 2>&1
  echo Copying DLLs from mingw_dll
  xcopy /y /q "%~dp0mingw_dll\*.dll" "%PKG_DIR%\mingw_dlls\" >nul 2>&1
  xcopy /y /q "%~dp0mingw_dll\*.dll" "%PKG_DIR%\" >nul 2>&1
  if exist "%PKG_DIR%\mingw_dlls\README.md" del /q "%PKG_DIR%\mingw_dlls\README.md"
) else if exist "%~dp0mingw_dlls\" (
  xcopy /e /i /y "%~dp0mingw_dlls" "%PKG_DIR%\mingw_dlls\" >nul 2>&1
)

REM Copy locale files so the language dropdown works in the package
if exist "%~dp0locale\" (
  xcopy /e /i /y "%~dp0locale" "%PKG_DIR%\locale\" >nul 2>&1
)

REM Copy sqlite folder if present
if exist "%~dp0sqlite3\" (
  xcopy /e /i /y "%~dp0sqlite3" "%PKG_DIR%\sqlite3\" >nul 2>&1
)

REM Copy inno installer template if present
if exist "%~dp0inno\" (
  xcopy /e /i /y "%~dp0inno" "%PKG_DIR%\inno\" >nul 2>&1
)

REM Copy helper scripts and assets
copy /y "%~dp0install_gnulogo.bat" "%PKG_DIR%\" >nul 2>&1
copy /y "%~dp0SetupCraft.ico" "%PKG_DIR%\" >nul 2>&1
copy /y "%~dp0SetupCraft.png" "%PKG_DIR%\" >nul 2>&1
copy /y "%~dp0SetupCraft.svg" "%PKG_DIR%\" >nul 2>&1
copy /y "%~dp0GPLv2.md" "%PKG_DIR%\" >nul 2>&1

echo Package created at %PKG_DIR%
