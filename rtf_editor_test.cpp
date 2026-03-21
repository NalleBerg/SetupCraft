// ─── RTF Editor — standalone test harness ────────────────────────────────────
// Compile as the separate target "RtfEditorTest" (see CMakeLists.txt).
// Run from the command line or Explorer; opens the editor immediately.
// If Save is clicked: writes output RTF to rtf_editor_test_out.rtf in the
// current directory then shows a message box.
//
// Build:    cmake --build build --target RtfEditorTest
// Run:      build\rtf_editor_test.exe   (or double-click it)

#include <windows.h>
#include <commctrl.h>
#include <stdio.h>
#include <string>
#include "dpi.h"
#include "edit_rtf.h"
#include "tooltip.h"

// g_dpiScale is defined in dpi.cpp (linked into every target that uses dpi.cpp).

int WINAPI wWinMain(HINSTANCE hInst, HINSTANCE, LPWSTR, int)
{
    // ── DPI awareness ─────────────────────────────────────────────────────────
    SetProcessDPIAware();
    {
        HDC hdc = GetDC(NULL);
        g_dpiScale = GetDeviceCaps(hdc, LOGPIXELSX) / 96.0f;
        ReleaseDC(NULL, hdc);
    }

    // ── Common controls ───────────────────────────────────────────────────────
    INITCOMMONCONTROLSEX icc = { sizeof(icc), ICC_WIN95_CLASSES };
    InitCommonControlsEx(&icc);

    // ── Tooltip system ────────────────────────────────────────────────────────
    InitTooltipSystem(hInst);

    // ── Open the editor ───────────────────────────────────────────────────────
    RtfEditorData data;
    data.titleText    = L"RTF Editor — Test Harness";
    data.okText       = L"Save";
    data.cancelText   = L"Cancel";
    // Uncomment to test the optional char-limit status bar:
    // data.maxChars      = 500;
    // data.charsLeftFmt  = L"%d characters left";

    bool saved = OpenRtfEditor(NULL, data);

    if (saved) {
        // Write to file so the RTF can be inspected in Notepad / Word.
        FILE* f = NULL;
        if (_wfopen_s(&f, L"rtf_editor_test_out.rtf", L"wb") == 0 && f) {
            int n = WideCharToMultiByte(CP_UTF8, 0,
                        data.outRtf.c_str(), -1, NULL, 0, NULL, NULL);
            if (n > 1) {
                std::string utf8(n - 1, '\0');
                WideCharToMultiByte(CP_UTF8, 0,
                    data.outRtf.c_str(), -1, &utf8[0], n, NULL, NULL);
                fwrite(utf8.c_str(), 1, utf8.size(), f);
            }
            fclose(f);
        }
        MessageBoxW(NULL,
            L"Saved!\n\nOutput written to:\nrtf_editor_test_out.rtf",
            L"RTF Editor Test", MB_OK | MB_ICONINFORMATION);
    } else {
        MessageBoxW(NULL,
            L"Editor closed without saving.",
            L"RTF Editor Test", MB_OK | MB_ICONINFORMATION);
    }

    CleanupTooltipSystem();
    return 0;
}
