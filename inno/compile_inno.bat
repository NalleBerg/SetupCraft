@echo off
REM compile_inno.bat - simple helper to compile an Inno Setup script
REM Usage: compile_inno.bat template.iss output.exe

set "SCRIPT=%~1"
set "OUT=%~2"

if "%SCRIPT%"=="" (
  echo Usage: compile_inno.bat script.iss [outputfilename]
  exit /b 1
)

rem Try locate ISCC.exe in common locations
set "ISCC="
for %%p in (
  "C:\Program Files (x86)\Inno Setup 6\ISCC.exe"
  "C:\Program Files\Inno Setup 6\ISCC.exe"
) do (
  if exist %%~p set "ISCC=%%~p"
)

if "%ISCC%"=="" (
  echo ISCC.exe not found. Please install Inno Setup or set PATH.
  echo You can download: https://jrsoftware.org/isdl.php
  exit /b 2
)

if "%OUT%"=="" (
  "%ISCC%" "%SCRIPT%"
) else (
  "%ISCC%" /O"%CD%" /F"%OUT%" "%SCRIPT%"
)
