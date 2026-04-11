// stopwatch.cpp — App-launching stopwatch
// Single self-contained Win32 file, statically linked.
//
// Screen 1 — Settings: optional app path (file picker), Start button.
// Screen 2 — Stopwatch: always-on-top, timer, Reset/Pause/Stop-Start/TopToggle.
//
// INI: same folder as exe if writable, else %APPDATA%\Stopwatch\stopwatch.ini
//
// Pause: freezes display only — clock keeps running. Un-pause jumps to live time.
// Reset: zeroes elapsed, keeps clock running.
// Stop/Start: stops / resumes the actual clock.
// Icon: shell32.dll index 249 (cached after first extraction).

#define WIN32_LEAN_AND_MEAN
#define UNICODE
#define _UNICODE
#include <windows.h>
#include <commctrl.h>
#include <commdlg.h>
#include <shlobj.h>
#include <shellapi.h>
#include <string>
#include <algorithm>

#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "comdlg32.lib")
#pragma comment(lib, "shell32.lib")

// ── Control IDs ──────────────────────────────────────────────────────────────
enum {
    IDC_LABEL         = 100,
    IDC_FILEPATH_EDIT = 101,
    IDC_BROWSE_BTN    = 102,
    IDC_START_BTN     = 103,

    IDC_SW_RESET      = 201,
    IDC_SW_PAUSE      = 202,
    IDC_SW_STARTSTOP  = 203,
    IDC_SW_TOPTOGGLE  = 204,

    IDT_TICK          = 1
};

// ── Globals ────────────────────────────────────────────────────────────────
static HINSTANCE g_hInst    = NULL;
static HFONT     g_fontBig  = NULL;
static HFONT     g_fontBtn  = NULL;
static HICON     g_hIcon    = NULL;

static LARGE_INTEGER g_freq        = {};
static LARGE_INTEGER g_startQPC    = {};
static LONGLONG      g_accumulated = 0;
static bool          g_running     = false;
static bool          g_dispPaused  = false;
static LONGLONG      g_frozenCS    = 0;
static bool          g_alwaysOnTop = true;

static wchar_t g_iniPath[MAX_PATH] = {};
static HWND    g_swHwnd            = NULL;
static HWND    g_hEditPath         = NULL;
static int     g_dpi               = 96;

// Scale a 96-DPI pixel value to the actual screen DPI.
static int D(int px) { return MulDiv(px, g_dpi, 96); }

// ── Font helper ───────────────────────────────────────────────────────────────
static HFONT MakeFont(int pt, bool bold, const wchar_t* face)
{
    HDC hdc = GetDC(NULL);
    int h   = -MulDiv(pt, GetDeviceCaps(hdc, LOGPIXELSY), 72);
    ReleaseDC(NULL, hdc);
    return CreateFontW(h, 0, 0, 0,
        bold ? FW_BOLD : FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH, face);
}

// ── INI helpers ───────────────────────────────────────────────────────────────
static void BuildIniPath()
{
    wchar_t exeDir[MAX_PATH];
    GetModuleFileNameW(NULL, exeDir, MAX_PATH);
    wchar_t* sl = wcsrchr(exeDir, L'\\');
    if (sl) sl[1] = L'\0'; else exeDir[0] = L'\0';

    wchar_t cand[MAX_PATH];
    swprintf_s(cand, L"%sstopwatch.ini", exeDir);

    HANDLE h = CreateFileW(cand, GENERIC_WRITE, 0, NULL,
                           OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h != INVALID_HANDLE_VALUE) { CloseHandle(h); wcscpy_s(g_iniPath, cand); return; }

    wchar_t ad[MAX_PATH];
    SHGetFolderPathW(NULL, CSIDL_APPDATA, NULL, 0, ad);
    wchar_t dir[MAX_PATH];
    swprintf_s(dir, L"%s\\Stopwatch", ad);
    CreateDirectoryW(dir, NULL);
    swprintf_s(g_iniPath, L"%s\\stopwatch.ini", dir);
}

static std::wstring IniGet(const wchar_t* k, const wchar_t* def = L"")
{
    wchar_t buf[MAX_PATH * 2] = {};
    GetPrivateProfileStringW(L"Settings", k, def, buf, MAX_PATH * 2 - 1, g_iniPath);
    return buf;
}
static void IniSet(const wchar_t* k, const wchar_t* v)
{
    WritePrivateProfileStringW(L"Settings", k, v, g_iniPath);
}

// ── Icon loader ───────────────────────────────────────────────────────────────
static HICON LoadShell32(int idx, int sz)
{
    wchar_t p[MAX_PATH];
    GetSystemDirectoryW(p, MAX_PATH);
    wcscat_s(p, L"\\shell32.dll");
    HICON ico = NULL;
    PrivateExtractIconsW(p, idx, sz, sz, &ico, NULL, 1, 0);
    return ico;
}

// ── Timer helpers ─────────────────────────────────────────────────────────────
static LONGLONG ElapsedCS()
{
    LONGLONG t = g_accumulated;
    if (g_running) {
        LARGE_INTEGER now; QueryPerformanceCounter(&now);
        t += now.QuadPart - g_startQPC.QuadPart;
    }
    if (!g_freq.QuadPart) return 0;
    return (t * 100) / g_freq.QuadPart;
}

static void FormatCS(LONGLONG cs, wchar_t* buf, int n)
{
    LONGLONG h = cs / 360000, m = (cs / 6000) % 60,
             s = (cs / 100) % 60, f = cs % 100;
    swprintf_s(buf, n, L"%02lld:%02lld:%02lld:%02lld", h, m, s, f);
}

// ── Button draw ───────────────────────────────────────────────────────────────
static void DrawBtn(LPDRAWITEMSTRUCT dis, const wchar_t* txt,
                    COLORREF bg, COLORREF fg, HFONT hf)
{
    HDC hdc = dis->hDC;
    RECT rc = dis->rcItem;
    if (dis->itemState & ODS_SELECTED)
        bg = RGB((int)GetRValue(bg)*7/10, (int)GetGValue(bg)*7/10, (int)GetBValue(bg)*7/10);

    HBRUSH hbr = CreateSolidBrush(bg);
    FillRect(hdc, &rc, hbr); DeleteObject(hbr);

    HPEN pen = CreatePen(PS_SOLID, 1, RGB(90,90,90));
    HPEN op  = (HPEN)SelectObject(hdc, pen);
    MoveToEx(hdc, rc.left, rc.bottom-1, NULL);
    LineTo(hdc, rc.left, rc.top); LineTo(hdc, rc.right-1, rc.top);
    LineTo(hdc, rc.right-1, rc.bottom-1); LineTo(hdc, rc.left, rc.bottom-1);
    SelectObject(hdc, op); DeleteObject(pen);

    if (hf) SelectObject(hdc, hf);
    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, fg);
    DrawTextW(hdc, txt, -1, &rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    if (dis->itemState & ODS_FOCUS) DrawFocusRect(hdc, &rc);
}

// ═════════════════════════════════════════════════════════════════════════════
// STOPWATCH WINDOW
// ═════════════════════════════════════════════════════════════════════════════

static LRESULT CALLBACK SwProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg)
    {
    case WM_CREATE:
    {
        struct BtnDef { int id, x, y, w, h; };
        BtnDef btns[] = {
            {IDC_SW_RESET,     D(8),   D(8),  D(234), D(44)},
            {IDC_SW_STARTSTOP, D(8),  D(148), D(114), D(36)},
            {IDC_SW_PAUSE,    D(130), D(148), D(112), D(36)},
            {IDC_SW_TOPTOGGLE, D(8),  D(192), D(234), D(32)},
        };
        for (auto& b : btns) {
            HWND hb = CreateWindowExW(0, L"BUTTON", L"",
                WS_CHILD | WS_VISIBLE | BS_OWNERDRAW | WS_TABSTOP,
                b.x, b.y, b.w, b.h, hwnd, (HMENU)(INT_PTR)b.id, g_hInst, NULL);
            SendMessageW(hb, WM_SETFONT, (WPARAM)g_fontBtn, TRUE);
        }
        SetTimer(hwnd, IDT_TICK, 10, NULL);
        return 0;
    }
    case WM_TIMER:
        if (wp == IDT_TICK && g_running && !g_dispPaused) {
            RECT rc; GetClientRect(hwnd, &rc);
            rc.top = D(56); rc.bottom = D(148);
            InvalidateRect(hwnd, &rc, FALSE);
        }
        return 0;

    case WM_PAINT:
    {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);
        RECT rc; GetClientRect(hwnd, &rc);

        HBRUSH bg = CreateSolidBrush(RGB(22,22,22));
        FillRect(hdc, &rc, bg); DeleteObject(bg);

        wchar_t buf[32];
        FormatCS(g_dispPaused ? g_frozenCS : ElapsedCS(), buf, 32);

        RECT tr = {D(4), D(56), D(246), D(140)};
        HFONT old = (HFONT)SelectObject(hdc, g_fontBig);
        SetBkMode(hdc, TRANSPARENT);
        SetTextColor(hdc, RGB(0, 230, 80));
        DrawTextW(hdc, buf, -1, &tr, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        SelectObject(hdc, old);

        if (g_dispPaused) {
            RECT pr = {D(4), D(128), D(246), D(146)};
            SelectObject(hdc, g_fontBtn);
            SetTextColor(hdc, RGB(255, 180, 0));
            DrawTextW(hdc, L"DISPLAY PAUSED \u2014 clock running",
                      -1, &pr, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        }
        EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_ERASEBKGND: return 1;

    case WM_DRAWITEM:
    {
        LPDRAWITEMSTRUCT dis = (LPDRAWITEMSTRUCT)lp;
        switch (dis->CtlID) {
        case IDC_SW_RESET:
            DrawBtn(dis, L"Reset", RGB(55,55,55), RGB(220,220,220), g_fontBtn); break;
        case IDC_SW_PAUSE:
            DrawBtn(dis,
                g_dispPaused ? L"Resume" : L"Pause",
                g_dispPaused ? RGB(160,100,0) : RGB(40,80,150),
                RGB(220,220,220), g_fontBtn); break;
        case IDC_SW_STARTSTOP:
            DrawBtn(dis,
                g_running ? L"Stop" : L"Start",
                g_running ? RGB(150,35,35) : RGB(30,120,50),
                RGB(220,220,220), g_fontBtn); break;
        case IDC_SW_TOPTOGGLE:
            DrawBtn(dis,
                g_alwaysOnTop ? L"Unpin from Top" : L"Pin to Top",
                g_alwaysOnTop ? RGB(75,45,110) : RGB(45,45,45),
                RGB(200,200,200), g_fontBtn); break;
        }
        return TRUE;
    }
    case WM_COMMAND:
        switch (LOWORD(wp)) {
        case IDC_SW_RESET:
            g_accumulated = 0;
            if (g_running) QueryPerformanceCounter(&g_startQPC);
            g_dispPaused = false; g_frozenCS = 0;
            InvalidateRect(hwnd, NULL, FALSE);
            InvalidateRect(GetDlgItem(hwnd, IDC_SW_PAUSE), NULL, TRUE);
            break;
        case IDC_SW_PAUSE:
            if (!g_dispPaused) { g_frozenCS = ElapsedCS(); g_dispPaused = true; }
            else                  g_dispPaused = false;
            InvalidateRect(hwnd, NULL, FALSE);
            InvalidateRect(GetDlgItem(hwnd, IDC_SW_PAUSE), NULL, TRUE);
            break;
        case IDC_SW_STARTSTOP:
            if (g_running) {
                LARGE_INTEGER now; QueryPerformanceCounter(&now);
                g_accumulated += now.QuadPart - g_startQPC.QuadPart;
                g_running = false;
            } else {
                QueryPerformanceCounter(&g_startQPC);
                g_running = true;
            }
            InvalidateRect(GetDlgItem(hwnd, IDC_SW_STARTSTOP), NULL, TRUE);
            break;
        case IDC_SW_TOPTOGGLE:
            g_alwaysOnTop = !g_alwaysOnTop;
            SetWindowPos(hwnd,
                g_alwaysOnTop ? HWND_TOPMOST : HWND_NOTOPMOST,
                0,0,0,0, SWP_NOMOVE | SWP_NOSIZE);
            InvalidateRect(GetDlgItem(hwnd, IDC_SW_TOPTOGGLE), NULL, TRUE);
            break;
        }
        return 0;
    case WM_DESTROY:
        KillTimer(hwnd, IDT_TICK);
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

// ═════════════════════════════════════════════════════════════════════════════
// SETTINGS WINDOW
// ═════════════════════════════════════════════════════════════════════════════

static void LaunchSW(HWND hSet, const std::wstring& path)
{
    IniSet(L"AppPath", path.c_str());

    if (!path.empty()) {
        SHELLEXECUTEINFOW sei = {};
        sei.cbSize = sizeof(sei); sei.lpVerb = L"open";
        sei.lpFile = path.c_str(); sei.nShow = SW_SHOWNORMAL;
        ShellExecuteExW(&sei);
    }

    QueryPerformanceFrequency(&g_freq);
    QueryPerformanceCounter(&g_startQPC);
    g_accumulated = 0; g_running = true; g_dispPaused = false;

    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(wc); wc.lpfnWndProc = SwProc;
    wc.hInstance = g_hInst; wc.hIcon = g_hIcon; wc.hIconSm = g_hIcon;
    wc.hCursor = LoadCursorW(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
    wc.lpszClassName = L"StopwatchWnd";
    RegisterClassExW(&wc);

    const int cx = D(250), cy = D(238);
    int sx = GetSystemMetrics(SM_CXSCREEN), sy = GetSystemMetrics(SM_CYSCREEN);
    DWORD ex = WS_EX_APPWINDOW | (g_alwaysOnTop ? WS_EX_TOPMOST : 0);
    DWORD st = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_THICKFRAME;
    RECT adj = {0,0,cx,cy};
    AdjustWindowRectEx(&adj, st, FALSE, ex);
    int ww = adj.right-adj.left, wh = adj.bottom-adj.top;

    g_swHwnd = CreateWindowExW(ex, L"StopwatchWnd", L"Stopwatch",
        st, sx-ww-20, sy-wh-60, ww, wh, NULL, NULL, g_hInst, NULL);
    if (g_swHwnd) {
        SendMessageW(g_swHwnd, WM_SETICON, ICON_BIG,   (LPARAM)g_hIcon);
        SendMessageW(g_swHwnd, WM_SETICON, ICON_SMALL, (LPARAM)g_hIcon);
        ShowWindow(g_swHwnd, SW_SHOW);
        UpdateWindow(g_swHwnd);
    }
    DestroyWindow(hSet);
}

// Layout the settings controls to fill the current client area.
static void LayoutSettings(HWND hwnd)
{
    RECT rc; GetClientRect(hwnd, &rc);
    int W = rc.right;           // full client width
    const int M  = D(12);       // margin
    const int BW = D(90);       // Browse button width
    const int BH = D(26);       // row height
    const int LH = D(32);       // label height (2 lines worth)
    const int SH = D(42);       // Start button height
    const int G  = D(6);        // gap
    // label
    SetWindowPos(GetDlgItem(hwnd, IDC_LABEL), NULL,
        M, D(10), W - 2*M, LH, SWP_NOZORDER);
    // edit + browse on same row
    int rowY = D(10) + LH + D(4);
    SetWindowPos(g_hEditPath, NULL,
        M, rowY, W - 2*M - BW - G, BH, SWP_NOZORDER);
    SetWindowPos(GetDlgItem(hwnd, IDC_BROWSE_BTN), NULL,
        W - M - BW, rowY, BW, BH, SWP_NOZORDER);
    // Start button
    SetWindowPos(GetDlgItem(hwnd, IDC_START_BTN), NULL,
        M, rowY + BH + D(8), W - 2*M, SH, SWP_NOZORDER);
}

static LRESULT CALLBACK SettingsProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg)
    {
    case WM_CREATE:
    {
        HFONT hf = MakeFont(10, false, L"Segoe UI");

        HWND hL = CreateWindowExW(0, L"STATIC",
            L"App to launch (leave blank for stopwatch only):",
            WS_CHILD|WS_VISIBLE|SS_LEFT, 0,0,0,0,
            hwnd, (HMENU)IDC_LABEL, g_hInst, NULL);
        SendMessageW(hL, WM_SETFONT, (WPARAM)hf, TRUE);

        g_hEditPath = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
            WS_CHILD|WS_VISIBLE|ES_AUTOHSCROLL,
            0,0,0,0, hwnd, (HMENU)IDC_FILEPATH_EDIT, g_hInst, NULL);
        SendMessageW(g_hEditPath, WM_SETFONT, (WPARAM)hf, TRUE);

        HWND hB = CreateWindowExW(0, L"BUTTON", L"Browse...",
            WS_CHILD|WS_VISIBLE|BS_PUSHBUTTON|WS_TABSTOP,
            0,0,0,0, hwnd, (HMENU)IDC_BROWSE_BTN, g_hInst, NULL);
        SendMessageW(hB, WM_SETFONT, (WPARAM)hf, TRUE);

        HWND hS = CreateWindowExW(0, L"BUTTON", L"Start Stopwatch",
            WS_CHILD|WS_VISIBLE|BS_DEFPUSHBUTTON|WS_TABSTOP,
            0,0,0,0, hwnd, (HMENU)IDC_START_BTN, g_hInst, NULL);
        SendMessageW(hS, WM_SETFONT, (WPARAM)MakeFont(11,true,L"Segoe UI"), TRUE);

        SetWindowTextW(g_hEditPath, IniGet(L"AppPath").c_str());
        return 0;
    }
    case WM_SIZE:
        LayoutSettings(hwnd);
        return 0;
    case WM_COMMAND:
        if (LOWORD(wp) == IDC_BROWSE_BTN) {
            wchar_t buf[MAX_PATH] = {};
            OPENFILENAMEW ofn = {};
            ofn.lStructSize = sizeof(ofn); ofn.hwndOwner = hwnd;
            ofn.lpstrFilter = L"Executables (*.exe)\0*.exe\0All Files (*.*)\0*.*\0";
            ofn.lpstrFile = buf; ofn.nMaxFile = MAX_PATH;
            ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
            if (GetOpenFileNameW(&ofn)) SetWindowTextW(g_hEditPath, buf);
        }
        if (LOWORD(wp) == IDC_START_BTN) {
            wchar_t buf[MAX_PATH] = {};
            GetWindowTextW(g_hEditPath, buf, MAX_PATH);
            std::wstring p(buf);
            auto ns = [](wchar_t c){ return !iswspace(c); };
            p.erase(p.begin(), std::find_if(p.begin(), p.end(), ns));
            p.erase(std::find_if(p.rbegin(), p.rend(), ns).base(), p.end());
            LaunchSW(hwnd, p);
        }
        return 0;
    case WM_DESTROY:
        // Only post quit if the stopwatch window is NOT open.
        // If LaunchSW already created the stopwatch, it will post quit on its own destroy.
        if (!g_swHwnd || !IsWindow(g_swHwnd))
            PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

// ═════════════════════════════════════════════════════════════════════════════
// ENTRY POINT
// ═════════════════════════════════════════════════════════════════════════════

int WINAPI wWinMain(HINSTANCE hInst, HINSTANCE, LPWSTR, int)
{
    g_hInst = hInst;
    INITCOMMONCONTROLSEX icc = {sizeof(icc), ICC_WIN95_CLASSES};
    InitCommonControlsEx(&icc);

    QueryPerformanceFrequency(&g_freq);
    BuildIniPath();

    // Capture DPI before creating any windows or fonts.
    HDC hScreen = GetDC(NULL);
    g_dpi = GetDeviceCaps(hScreen, LOGPIXELSY);
    ReleaseDC(NULL, hScreen);

    g_fontBig = MakeFont(28, true,  L"Consolas");
    g_fontBtn = MakeFont(10, true,  L"Segoe UI");
    g_hIcon   = LoadShell32(249, D(32));

    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(wc); wc.lpfnWndProc = SettingsProc;
    wc.hInstance = hInst; wc.hIcon = g_hIcon; wc.hIconSm = g_hIcon;
    wc.hCursor = LoadCursorW(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE+1);
    wc.lpszClassName = L"StopwatchSettings";
    RegisterClassExW(&wc);

    // Client area in 96-DPI logical units, scaled to actual DPI.
    DWORD settStyle = WS_OVERLAPPED|WS_CAPTION|WS_SYSMENU;
    RECT rc = {0, 0, D(440), D(130)};
    AdjustWindowRect(&rc, settStyle, FALSE);
    int ww = rc.right - rc.left, wh = rc.bottom - rc.top;
    int sw = GetSystemMetrics(SM_CXSCREEN), sh = GetSystemMetrics(SM_CYSCREEN);
    HWND hWnd = CreateWindowExW(WS_EX_APPWINDOW,
        L"StopwatchSettings", L"Stopwatch \u2014 Settings",
        settStyle,
        (sw - ww) / 2, (sh - wh) / 2, ww, wh,
        NULL, NULL, hInst, NULL);
    if (!hWnd) return 1;

    SendMessageW(hWnd, WM_SETICON, ICON_BIG,   (LPARAM)g_hIcon);
    SendMessageW(hWnd, WM_SETICON, ICON_SMALL, (LPARAM)g_hIcon);
    ShowWindow(hWnd, SW_SHOW);
    UpdateWindow(hWnd);

    MSG msg;
    while (GetMessageW(&msg, NULL, 0, 0) > 0) {
        TranslateMessage(&msg); DispatchMessageW(&msg);
    }
    if (g_fontBig) DeleteObject(g_fontBig);
    if (g_fontBtn) DeleteObject(g_fontBtn);
    if (g_hIcon)   DestroyIcon(g_hIcon);
    return (int)msg.wParam;
}

