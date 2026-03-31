/*
 * test_scrollbar.cpp — Standalone test for my_scrollbar.h / my_scrollbar.cpp
 *
 * NO SetupCraft code here.  Only <windows.h>, <richedit.h> and my_scrollbar.h
 *
 * Window layout:
 *   Left half  — read-only  RICHEDIT50W with a vertical custom scrollbar
 *   Right half — writable   RICHEDIT50W with a vertical custom scrollbar
 *   Bottom     — writable   RICHEDIT50W with vertical + horizontal bars
 *
 * Checklist (test manually):
 *   [ ] Arrow buttons scroll content + thumb tracks
 *   [ ] Click above/below thumb (page) — thumb jumps correctly
 *   [ ] Thumb drag — content follows, thumb turns drag color
 *   [ ] Mousewheel over read-only — thumb tracks every tick, no snapping
 *   [ ] Mousewheel over writable  — same
 *   [ ] Resize window — bars reposition and repaint correctly
 *   [ ] Thumb hover color changes; arrow hover color changes
 *   [ ] Fade expand on hover (hidden-mode bar); contract on leave
 *   [ ] MSB_NOHIDE bar stays full width at all times
 *   [ ] Read-only and writable behave identically
 */

#include <windows.h>
#include <richedit.h>
#include "../my_scrollbar.h"

/* ── IDs ────────────────────────────────────────────────────────────────────*/
#define IDC_RE_LEFT   1001
#define IDC_RE_RIGHT  1002
#define IDC_RE_BOTTOM 1003

/* ── Globals ─────────────────────────────────────────────────────────────────*/
static HWND  g_reLeft   = NULL;
static HWND  g_reRight  = NULL;
static HWND  g_reBottom = NULL;
static HMSB  g_sbLeft   = NULL;
static HMSB  g_sbRight  = NULL;
static HMSB  g_sbBotV   = NULL;
static HMSB  g_sbBotH   = NULL;
static HMODULE g_hRichEdit = NULL;

/* ── Long sample text ───────────────────────────────────────────────────────*/
static const wchar_t* SampleText(void)
{
    static wchar_t buf[4096];
    buf[0] = L'\0';
    for (int i = 1; i <= 60; i++) {
        wchar_t line[64];
        wsprintfW(line, L"Line %d: The quick brown fox jumps over the lazy dog.\r\n", i);
        lstrcatW(buf, line);
    }
    return buf;
}

/* Wide text for the bottom pane — each line is long enough to require
 * horizontal scrolling at any normal window width. */
static const wchar_t* SampleTextWide(void)
{
    static wchar_t buf[16384];
    buf[0] = L'\0';
    for (int i = 1; i <= 60; i++) {
        wchar_t line[256];
        wsprintfW(line,
            L"Row %02d: AAAAAAAAA BBBBBBBBB CCCCCCCCC DDDDDDDDD EEEEEEEEE "
            L"FFFFFFFFF GGGGGGGGG HHHHHHHHH IIIIIIIII JJJJJJJJJ "
            L"KKKKKKKKK LLLLLLLLL MMMMMMMMM NNNNNNNNN OOOOOOOOO PPPPPPPPP "
            L"QQQQQQQQQ RRRRRRRRR SSSSSSSSS TTTTTTTTT  [end]\r\n", i);
        lstrcatW(buf, line);
    }
    return buf;
}

/* ── Layout helper ───────────────────────────────────────────────────────────*/
static void LayoutControls(HWND hwndParent)
{
    RECT rc;
    GetClientRect(hwndParent, &rc);
    int w = rc.right;
    int h = rc.bottom;

    int topH   = (h * 2) / 3;
    int botH   = h - topH;
    int halfW  = w / 2;

    if (g_reLeft)
        SetWindowPos(g_reLeft,   NULL, 0,     0,    halfW, topH, SWP_NOZORDER | SWP_NOACTIVATE);
    if (g_reRight)
        SetWindowPos(g_reRight,  NULL, halfW, 0,    w - halfW, topH, SWP_NOZORDER | SWP_NOACTIVATE);
    if (g_reBottom)
        SetWindowPos(g_reBottom, NULL, 0,     topH, w,     botH, SWP_NOZORDER | SWP_NOACTIVATE);
}

/* ── Helper: create a RichEdit ───────────────────────────────────────────────*/
static HWND CreateRE(HWND parent, int id, BOOL readOnly)
{
    DWORD style = WS_CHILD | WS_VISIBLE | WS_VSCROLL | WS_HSCROLL |
                  ES_MULTILINE | ES_AUTOVSCROLL | ES_AUTOHSCROLL;

    HWND hRE = CreateWindowExW(
        WS_EX_CLIENTEDGE,
        L"RICHEDIT50W", NULL,
        style,
        0, 0, 10, 10,
        parent, (HMENU)(LONG_PTR)id,
        (HINSTANCE)GetWindowLongPtrW(parent, GWLP_HINSTANCE),
        NULL);

    if (!hRE) return NULL;

    if (readOnly)
        SendMessageW(hRE, EM_SETREADONLY, TRUE, 0);

    SendMessageW(hRE, WM_SETTEXT, 0, (LPARAM)SampleText());
    return hRE;
}

/* ── Main window proc ────────────────────────────────────────────────────────*/
static LRESULT CALLBACK MainWndProc(HWND hwnd, UINT msg,
                                     WPARAM wParam, LPARAM lParam)
{
    switch (msg) {
        case WM_CREATE: {
            g_reLeft   = CreateRE(hwnd, IDC_RE_LEFT,   TRUE);
            g_reRight  = CreateRE(hwnd, IDC_RE_RIGHT,  FALSE);
            g_reBottom = CreateRE(hwnd, IDC_RE_BOTTOM, FALSE);
            if (!g_reLeft || !g_reRight || !g_reBottom) return -1;
            /* Override bottom pane with wide text for horizontal scroll testing */
            SendMessageW(g_reBottom, WM_SETTEXT, 0, (LPARAM)SampleTextWide());

            g_sbLeft  = msb_attach(g_reLeft,   MSB_VERTICAL | MSB_NOHIDE);
            g_sbRight = msb_attach(g_reRight,  MSB_VERTICAL | MSB_NOHIDE);
            g_sbBotV  = msb_attach(g_reBottom, MSB_VERTICAL | MSB_NOHIDE);
            g_sbBotH  = msb_attach(g_reBottom, MSB_HORIZONTAL | MSB_NOHIDE);
            return 0;
        }

        case WM_SIZE:
            LayoutControls(hwnd);
            return 0;

        case WM_DESTROY:
            msb_detach(g_sbLeft);
            msb_detach(g_sbRight);
            msb_detach(g_sbBotV);
            msb_detach(g_sbBotH);
            PostQuitMessage(0);
            return 0;
    }

    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

/* ── WinMain ─────────────────────────────────────────────────────────────────*/
int WINAPI wWinMain(HINSTANCE hInst, HINSTANCE, LPWSTR, int nCmdShow)
{
    /* Load RichEdit library */
    g_hRichEdit = LoadLibraryW(L"msftedit.dll");
    if (!g_hRichEdit)
        g_hRichEdit = LoadLibraryW(L"riched20.dll");
    if (!g_hRichEdit) return 1;

    /* Register main window class */
    WNDCLASSEXW wc = {};
    wc.cbSize        = sizeof(wc);
    wc.lpfnWndProc   = MainWndProc;
    wc.hInstance     = hInst;
    wc.hCursor       = LoadCursorW(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.lpszClassName = L"ScrollbarTest";
    if (!RegisterClassExW(&wc)) return 1;

    HWND hwnd = CreateWindowExW(
        0, L"ScrollbarTest",
        L"my_scrollbar — Phase 1 Test",
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT, 900, 700,
        NULL, NULL, hInst, NULL);
    if (!hwnd) return 1;

    ShowWindow(hwnd, nCmdShow);
    UpdateWindow(hwnd);

    MSG m;
    while (GetMessageW(&m, NULL, 0, 0)) {
        TranslateMessage(&m);
        DispatchMessageW(&m);
    }

    if (g_hRichEdit) FreeLibrary(g_hRichEdit);
    return (int)m.wParam;
}
