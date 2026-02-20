@echo off
REM Copy GnuLogo.bmp into Skeleton folder from known locations in the repo.
setlocal

set "DEST=%~dp0GnuLogo.bmp"

if exist "%~dp0..\WinProgramManager\GnuLogo.bmp" (
    copy /Y "%~dp0..\WinProgramManager\GnuLogo.bmp" "%DEST%" && echo Copied from WinProgramManager
    goto :EOF
)
if exist "%~dp0..\WinUpdate\GnuLogo.bmp" (
    copy /Y "%~dp0..\WinUpdate\GnuLogo.bmp" "%DEST%" && echo Copied from WinUpdate
    goto :EOF
)

echo GnuLogo.bmp not found in usual locations.
echo If you want a GNU logo placed here, copy a BMP named GnuLogo.bmp into this folder.
