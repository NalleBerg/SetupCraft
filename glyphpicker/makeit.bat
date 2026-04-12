@echo off
setlocal

:: Kill any running instance so the exe can be replaced
taskkill /F /IM glyphpicker.exe >nul 2>&1

:: Create build dir if needed
if not exist build mkdir build
cd build

:: Configure (no-op if already configured)
cmake .. -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Release >nul 2>&1

:: Build (CMakeLists copies exe to source folder on success)
mingw32-make
if %ERRORLEVEL% neq 0 (
    echo.
    echo BUILD FAILED
    pause
    exit /b 1
)

echo.
echo Done - glyphpicker.exe updated
