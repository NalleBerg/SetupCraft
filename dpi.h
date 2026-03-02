#pragma once
#include <windows.h>

// Global DPI scale factor: initialised to GetDpiForSystem()/96.0f in wWinMain.
// 1.0 = 96 DPI (100 %), 1.75 = 168 DPI (175 %), etc.
extern float g_dpiScale;

// Scale a design-time pixel value (designed for 96 DPI) to the current system DPI.
// Use this for every hardcoded pixel value in window/control creation.
inline int S(int px) {
    return (int)(px * g_dpiScale + 0.5f);
}
