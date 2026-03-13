#include <windows.h>
#include <commctrl.h>
#include <string>
#include <vector>
#include <shlwapi.h>

#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "shlwapi.lib")

const wchar_t CLASS_NAME[] = L"IconViewerWindow";

// Layout constants
const int ICON_SIZE     = 46;   // rendered icon size in pixels
const int CELL_W        = 80;   // horizontal cell stride
const int CELL_H        = 72;   // vertical cell stride (icon + label)
const int HDR_H         = 34;   // section header bar height
const int SECTION_GAP   = 24;   // vertical gap between sections
const int LEFT_MARGIN   = 20;
const int TOP_MARGIN    = 10;

// One loaded icon.
struct IconEntry {
    HICON hIcon;   // NULL if slot is empty/invalid
    int   index;
};

// One DLL section (shell32 or imageres).
struct DllSection {
    std::wstring           filename;  // e.g. L"shell32.dll"
    std::wstring           fullPath;
    std::vector<IconEntry> icons;
    int                    gridY;     // scroll-space Y where the icon grid starts
    int                    maxIcons;  // how many indices to try at startup / F5
};

static std::vector<DllSection> g_sections;
static int g_scrollPos    = 0;
static int g_scrollTarget = 0;  // smooth-scroll destination
static int g_iconsPerRow  = 8;  // recalculated on every WM_SIZE

const int SMOOTH_TIMER_ID = 1;
const int SMOOTH_STEP     = 6;  // pixels per timer tick

// ── Grid layout ──────────────────────────────────────────────────────────────

static int GridHeight(const DllSection& s) {
    int rows = ((int)s.icons.size() + g_iconsPerRow - 1) / g_iconsPerRow;
    return rows * CELL_H;
}

// Recompute gridY for every section based on sequential stacking.
// Layout per section:  [HDR_H header bar]  [grid]  [SECTION_GAP]
static void RecalcLayout() {
    int y = TOP_MARGIN;
    for (auto& s : g_sections) {
        y += HDR_H;       // header bar sits immediately above the grid
        s.gridY = y;
        y += GridHeight(s) + SECTION_GAP;
    }
}

static int TotalHeight() {
    if (g_sections.empty()) return 0;
    const auto& last = g_sections.back();
    return last.gridY + GridHeight(last) + SECTION_GAP;
}

// ── DLL loading ──────────────────────────────────────────────────────────────

static void LoadSection(DllSection& s, int maxIcons) {
    for (auto& e : s.icons)
        if (e.hIcon) DestroyIcon(e.hIcon);
    s.icons.clear();

    wchar_t path[MAX_PATH];
    if (PathIsRelativeW(s.filename.c_str())) {
        // Try System32 first; fall back to %WINDIR% (e.g. explorer.exe lives there)
        GetSystemDirectoryW(path, MAX_PATH);
        wcscat_s(path, L"\\");
        wcscat_s(path, s.filename.c_str());
        if (!PathFileExistsW(path)) {
            GetWindowsDirectoryW(path, MAX_PATH);
            wcscat_s(path, L"\\");
            wcscat_s(path, s.filename.c_str());
        }
    } else {
        wcscpy_s(path, s.filename.c_str());
    }
    s.fullPath = path;

    for (int i = 0; i < maxIcons; i++) {
        HICON h = ExtractIconW(NULL, path, i);
        IconEntry e;
        e.index = i;
        e.hIcon = (h == NULL || h == (HICON)1) ? NULL : h;
        s.icons.push_back(e);
    }
}

static void ClampScrollTarget(HWND hwnd) {
    RECT rc; GetClientRect(hwnd, &rc);
    int maxPos = std::max(0, TotalHeight() - (int)rc.bottom);
    if (g_scrollTarget < 0)       g_scrollTarget = 0;
    if (g_scrollTarget > maxPos)  g_scrollTarget = maxPos;
}

// ── Scrollbar ────────────────────────────────────────────────────────────────

static void UpdateScrollbar(HWND hwnd) {
    RECT rc; GetClientRect(hwnd, &rc);
    SCROLLINFO si = {};
    si.cbSize = sizeof(si);
    si.fMask  = SIF_RANGE | SIF_PAGE | SIF_POS;
    si.nMin   = 0;
    si.nMax   = TotalHeight();
    si.nPage  = rc.bottom;
    si.nPos   = g_scrollPos;
    SetScrollInfo(hwnd, SB_VERT, &si, TRUE);
}

// ── Clipboard ────────────────────────────────────────────────────────────────

// Copy text and confirm by updating the title bar.
static void CopyText(HWND hwnd, const std::wstring& text) {
    if (!OpenClipboard(hwnd)) return;
    EmptyClipboard();
    size_t bytes = (text.size() + 1) * sizeof(wchar_t);
    HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE, bytes);
    if (hMem) {
        memcpy(GlobalLock(hMem), text.c_str(), bytes);
        GlobalUnlock(hMem);
        SetClipboardData(CF_UNICODETEXT, hMem);
    }
    CloseClipboard();
    SetWindowTextW(hwnd, (L"Icon Viewer  —  copied: " + text).c_str());
    MessageBoxW(hwnd, text.c_str(), L"Copied to clipboard", MB_OK | MB_ICONINFORMATION);
}

// ── Window procedure ─────────────────────────────────────────────────────────

LRESULT CALLBACK WindowProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {

    case WM_CREATE: {
        // Define all DLL sections in display order.
        auto addSec = [](const wchar_t* name, int max) {
            DllSection s; s.filename = name; s.maxIcons = max; s.gridY = 0;
            g_sections.push_back(std::move(s));
        };
        addSec(L"shell32.dll",   336);
        addSec(L"imageres.dll",  300);
        addSec(L"netshell.dll",  130);
        addSec(L"compstui.dll",   70);
        addSec(L"setupapi.dll",   20);
        for (auto& s : g_sections)
            LoadSection(s, s.maxIcons);
        RecalcLayout();
        UpdateScrollbar(hwnd);
        return 0;
    }

    case WM_SIZE: {
        RECT rc; GetClientRect(hwnd, &rc);
        int newIPR = std::max(1, (int)(rc.right - LEFT_MARGIN) / CELL_W);
        if (newIPR != g_iconsPerRow) {
            g_iconsPerRow = newIPR;
            RecalcLayout();
        }
        UpdateScrollbar(hwnd);
        InvalidateRect(hwnd, NULL, TRUE);
        return 0;
    }

    case WM_VSCROLL: {
        SCROLLINFO si = {}; si.cbSize = sizeof(si); si.fMask = SIF_ALL;
        GetScrollInfo(hwnd, SB_VERT, &si);
        switch (LOWORD(wParam)) {
        case SB_LINEUP:        g_scrollTarget -= CELL_H;        break;
        case SB_LINEDOWN:      g_scrollTarget += CELL_H;        break;
        case SB_PAGEUP:        g_scrollTarget -= (int)si.nPage; break;
        case SB_PAGEDOWN:      g_scrollTarget += (int)si.nPage; break;
        case SB_THUMBTRACK:
        case SB_THUMBPOSITION:
            // Dragging the thumb: snap immediately, no animation.
            g_scrollTarget = si.nTrackPos;
            g_scrollPos    = si.nTrackPos;
            SetScrollPos(hwnd, SB_VERT, g_scrollPos, TRUE);
            InvalidateRect(hwnd, NULL, TRUE);
            return 0;
        }
        ClampScrollTarget(hwnd);
        SetTimer(hwnd, SMOOTH_TIMER_ID, 10, NULL);
        return 0;
    }

    case WM_MOUSEWHEEL: {
        int delta = GET_WHEEL_DELTA_WPARAM(wParam);
        // Each notch = 3 rows of scroll
        g_scrollTarget -= (delta / WHEEL_DELTA) * CELL_H * 3;
        ClampScrollTarget(hwnd);
        SetTimer(hwnd, SMOOTH_TIMER_ID, 10, NULL);
        return 0;
    }

    case WM_ERASEBKGND:
        return 1;  // skip erase — double-buffered paint handles background

    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);
        RECT rc; GetClientRect(hwnd, &rc);

        // ── Double-buffer ────────────────────────────────────────────────────
        HDC     memDC  = CreateCompatibleDC(hdc);
        HBITMAP memBmp = CreateCompatibleBitmap(hdc, rc.right, rc.bottom);
        HBITMAP oldBmp = (HBITMAP)SelectObject(memDC, memBmp);

        // White background onto back-buffer
        HBRUSH hWhite = CreateSolidBrush(RGB(255, 255, 255));
        FillRect(memDC, &rc, hWhite);
        DeleteObject(hWhite);

        SetBkMode(memDC, TRANSPARENT);

        HFONT hFontHdr = CreateFontW(-14, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
        HFONT hFontLbl = CreateFontW(-11, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");

        // Distinct header colour per DLL
        const COLORREF hdrColours[] = {
            RGB( 30,  70, 150),   // shell32   – dark blue
            RGB( 20, 110, 100),   // imageres  – teal
            RGB(160,  80,   0),   // netshell  – burnt orange
            RGB(140,  30,  30),   // compstui  – dark red
            RGB( 30, 100,  40),   // setupapi  – dark green
        };

        for (int si = 0; si < (int)g_sections.size(); si++) {
            const DllSection& s = g_sections[si];

            int hdrScreenY  = (s.gridY - HDR_H) - g_scrollPos;
            int gridScreenY = s.gridY - g_scrollPos;

            // ── Section header bar ───────────────────────────────────────────
            if (hdrScreenY + HDR_H >= ps.rcPaint.top &&
                hdrScreenY         <  ps.rcPaint.bottom) {
                RECT hdrR = { 0, hdrScreenY, rc.right, hdrScreenY + HDR_H };
                HBRUSH hHbr = CreateSolidBrush(hdrColours[si % 5]);
                FillRect(memDC, &hdrR, hHbr);
                DeleteObject(hHbr);

                wchar_t hdrText[256];
                swprintf_s(hdrText,
                    L"  %s  (%zu icons)  —  click an icon to copy \"dll - index\" to clipboard",
                    s.filename.c_str(), s.icons.size());
                HFONT hOld = (HFONT)SelectObject(memDC, hFontHdr);
                SetTextColor(memDC, RGB(255, 255, 255));
                DrawTextW(memDC, hdrText, -1, &hdrR,
                    DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
                SelectObject(memDC, hOld);
            }

            // ── Icon grid ────────────────────────────────────────────────────
            HFONT hOld = (HFONT)SelectObject(memDC, hFontLbl);
            int col = 0, row = 0;
            for (const auto& e : s.icons) {
                int x = LEFT_MARGIN + col * CELL_W;
                int y = gridScreenY + row * CELL_H;

                if (y + CELL_H >= ps.rcPaint.top && y < ps.rcPaint.bottom) {
                    if (e.hIcon) {
                        DrawIconEx(memDC, x, y, e.hIcon,
                            ICON_SIZE, ICON_SIZE, 0, NULL, DI_NORMAL);
                    } else {
                        // Light-grey placeholder for empty/missing slots
                        RECT pr = { x, y, x + ICON_SIZE, y + ICON_SIZE };
                        HBRUSH hGray = CreateSolidBrush(RGB(230, 230, 230));
                        FillRect(memDC, &pr, hGray);
                        DeleteObject(hGray);
                    }
                    // Index label centred below the icon
                    wchar_t lbl[16];
                    swprintf_s(lbl, L"#%d", e.index);
                    RECT lr = { x - 8, y + ICON_SIZE + 2,
                                x + ICON_SIZE + 8, y + ICON_SIZE + 18 };
                    SetTextColor(memDC, RGB(60, 60, 60));
                    DrawTextW(memDC, lbl, -1, &lr,
                        DT_CENTER | DT_SINGLELINE | DT_NOPREFIX);
                }

                if (++col >= g_iconsPerRow) { col = 0; ++row; }
            }
            SelectObject(memDC, hOld);
        }

        DeleteObject(hFontHdr);
        DeleteObject(hFontLbl);

        // Flush back-buffer to screen in one blit — no flicker
        BitBlt(hdc, 0, 0, rc.right, rc.bottom, memDC, 0, 0, SRCCOPY);
        SelectObject(memDC, oldBmp);
        DeleteObject(memBmp);
        DeleteDC(memDC);

        EndPaint(hwnd, &ps);
        return 0;
    }

    case WM_LBUTTONDOWN: {
        // Hit-test every section; copy "dll - index" immediately, no dialog.
        int mx = (short)LOWORD(lParam);
        int my = (short)HIWORD(lParam);
        for (const auto& s : g_sections) {
            int col = 0, row = 0;
            for (const auto& e : s.icons) {
                int x = LEFT_MARGIN + col * CELL_W;
                int y = (s.gridY - g_scrollPos) + row * CELL_H;
                if (mx >= x && mx < x + ICON_SIZE &&
                    my >= y && my < y + ICON_SIZE) {
                    wchar_t text[128];
                    swprintf_s(text, L"%s - %d", s.filename.c_str(), e.index);
                    CopyText(hwnd, text);
                    return 0;
                }
                if (++col >= g_iconsPerRow) { col = 0; ++row; }
            }
        }
        return 0;
    }

    case WM_TIMER:
        if (wParam == SMOOTH_TIMER_ID) {
            int diff = g_scrollTarget - g_scrollPos;
            if (diff == 0) {
                KillTimer(hwnd, SMOOTH_TIMER_ID);
                return 0;
            }
            // Move a fraction of the remaining distance, minimum 1px.
            int step = diff / 4;
            if (step == 0) step = (diff > 0) ? 1 : -1;
            g_scrollPos += step;
            SetScrollPos(hwnd, SB_VERT, g_scrollPos, TRUE);
            InvalidateRect(hwnd, NULL, TRUE);
        }
        return 0;

    case WM_KEYDOWN:
        if (wParam == VK_F5) {
            for (auto& s : g_sections)
                LoadSection(s, s.maxIcons);
            RecalcLayout();
            g_scrollPos = 0;
            UpdateScrollbar(hwnd);
            InvalidateRect(hwnd, NULL, TRUE);
            SetWindowTextW(hwnd, L"Icon Viewer");
        }
        return 0;

    case WM_DESTROY:
        for (auto& s : g_sections)
            for (auto& e : s.icons)
                if (e.hIcon) DestroyIcon(e.hIcon);
        PostQuitMessage(0);
        return 0;
    }

    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR, int nCmdShow) {
    WNDCLASSW wc = {};
    wc.lpfnWndProc   = WindowProc;
    wc.hInstance     = hInstance;
    wc.lpszClassName = CLASS_NAME;
    wc.hCursor       = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    RegisterClassW(&wc);

    HWND hwnd = CreateWindowExW(
        0, CLASS_NAME, L"Icon Viewer",
        WS_OVERLAPPEDWINDOW | WS_VSCROLL,
        CW_USEDEFAULT, CW_USEDEFAULT, 720, 700,
        NULL, NULL, hInstance, NULL);
    if (!hwnd) return 0;

    ShowWindow(hwnd, SW_SHOWMAXIMIZED);
    UpdateWindow(hwnd);

    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
    return 0;
}
