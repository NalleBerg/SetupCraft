@echo off
echo Building IconViewer...

REM Compile IconViewer
g++ -O2 -mwindows IconViewer.cpp -o IconViewer.exe -lcomctl32 -lshlwapi -static-libgcc -static-libstdc++

if %ERRORLEVEL% EQU 0 (
    echo.
    echo Build successful!
    echo IconViewer.exe created
    echo.
    echo Usage:
    echo   - Left click an icon to see its index and copy to clipboard
    echo   - Right click to switch between shell32.dll and imageres.dll
    echo   - Mouse wheel to scroll
    echo   - F5 to reload
) else (
    echo.
    echo Build failed!
)

pause
