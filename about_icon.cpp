#include "about_icon.h"
#include "tooltip.h"
#include "about.h"
#include <shlwapi.h>
#include <shellapi.h>
#include <windowsx.h>
#include <string>

static HWND s_aboutIcon = NULL;
static HWND s_currentTooltipIcon = NULL;
static bool s_mouseTracking = false;
static HWND s_parentWnd = NULL;
static const std::map<std::wstring, std::wstring>* s_localePtr = nullptr;
static WNDPROC s_prevAboutProc = NULL;

// Subclass proc for the about icon control
static LRESULT CALLBACK AboutIcon_SubclassProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_MOUSEMOVE: {
        // Show tooltip when mouse enters; track leave on the control itself
        if (!IsTooltipVisible()) {
            if (s_localePtr) {
                auto it = s_localePtr->find(L"about_setupcraft");
                std::wstring tooltipText = (it != s_localePtr->end()) ? it->second : L"About SetupCraft";
                std::vector<std::pair<std::wstring,std::wstring>> simpleEntry;
                simpleEntry.push_back({L"", tooltipText});

                RECT rcIcon;
                GetWindowRect(hwnd, &rcIcon);
                // rcIcon is in screen coordinates already; place tooltip just below the icon
                POINT ptIcon = { rcIcon.left, rcIcon.bottom + 5 };
                ShowMultilingualTooltip(simpleEntry, ptIcon.x, ptIcon.y, s_parentWnd);
                s_currentTooltipIcon = hwnd;
            }
        }

        if (!s_mouseTracking) {
            TRACKMOUSEEVENT tme = {0};
            tme.cbSize = sizeof(tme);
            tme.dwFlags = TME_LEAVE;
            tme.hwndTrack = hwnd;
            TrackMouseEvent(&tme);
            s_mouseTracking = true;
        }
        break;
    }
    case WM_MOUSELEAVE: {
        if (IsTooltipVisible() && s_currentTooltipIcon == hwnd) {
            HideTooltip();
            s_currentTooltipIcon = NULL;
        }
        s_mouseTracking = false;
        break;
    }
    case WM_LBUTTONDOWN: {
        if (s_parentWnd) ShowAboutDialog(s_parentWnd);
        break;
    }
    case WM_NCDESTROY: {
        SetWindowLongPtrW(hwnd, GWLP_WNDPROC, (LONG_PTR)s_prevAboutProc);
        break;
    }
    }
    return CallWindowProcW(s_prevAboutProc, hwnd, msg, wParam, lParam);
}

HWND CreateAboutIconControl(HWND parent, HINSTANCE hInst, int x, int y, int size, int id,
    const std::map<std::wstring, std::wstring>& locale) {
    // Create static control (receive mouse messages so it can track leave)
    s_aboutIcon = CreateWindowExW(
        0,
        L"STATIC", NULL,
        WS_CHILD | WS_VISIBLE | SS_ICON | SS_CENTERIMAGE | SS_NOTIFY,
        x, y, size, size,
        parent, (HMENU)(INT_PTR)id, hInst, NULL);

    // Load info icon from shell32.dll (icon #221 is information/about icon)
    wchar_t dllPath[MAX_PATH];
    GetSystemDirectoryW(dllPath, MAX_PATH);
    PathAppendW(dllPath, L"\\shell32.dll");

    HICON hAboutIconImage = NULL;
    UINT extracted = PrivateExtractIconsW(dllPath, 221, size, size, &hAboutIconImage, NULL, 1, 0);
    if (extracted > 0 && hAboutIconImage) {
        SendMessageW(s_aboutIcon, STM_SETICON, (WPARAM)hAboutIconImage, 0);
    }

    // store parent and locale pointer for subclass
    s_parentWnd = parent;
    s_localePtr = &locale;

    // Subclass the static so we can receive mouse leave for the control
    s_prevAboutProc = (WNDPROC)SetWindowLongPtrW(s_aboutIcon, GWLP_WNDPROC, (LONG_PTR)AboutIcon_SubclassProc);

    return s_aboutIcon;
}

static bool PointInWindow(HWND hwnd, POINT pt) {
    if (!hwnd) return false;
    RECT rc;
    GetWindowRect(hwnd, &rc);
    return PtInRect(&rc, pt);
}

// The parent-side OnMouseMove is no longer used when the control is subclassed,
// but keep the function for compatibility if callers still invoke it.
void AboutIcon_OnMouseMove(HWND parent, WPARAM wParam, LPARAM lParam, const std::map<std::wstring, std::wstring>& locale) {
    // Forward to subclass behavior if needed by calling ShowAboutDialog on click
    UNREFERENCED_PARAMETER(parent);
    UNREFERENCED_PARAMETER(wParam);
    UNREFERENCED_PARAMETER(lParam);
    (void)locale;
}

void AboutIcon_OnLButtonDown(HWND parent, WPARAM wParam, LPARAM lParam) {
    UNREFERENCED_PARAMETER(wParam);
    UNREFERENCED_PARAMETER(lParam);
    if (parent) ShowAboutDialog(parent);
}

void AboutIcon_Cleanup() {
    // Nothing special: tooltip system handles tooltip window
    s_aboutIcon = NULL;
    s_currentTooltipIcon = NULL;
    s_mouseTracking = false;
}
