// glyphpicker.cpp -- Unicode glyph / emoji picker
// Direct2D grid rendering for full-color emoji.
// Single self-contained Win32 file, statically linked.

#define WIN32_LEAN_AND_MEAN
#define UNICODE
#define _UNICODE
#include <windows.h>
#include <commctrl.h>
#include <shlobj.h>
#include <shellapi.h>
#include <d2d1.h>
#include <d2d1helper.h>
#include <dwrite.h>
#include <string>
#include <vector>
#include <algorithm>
#include <cwctype>

using std::max;
using std::min;

// -- IDs ----------------------------------------------------------------------
enum {
    IDC_BLOCK_COMBO = 101,
    IDC_SEARCH_EDIT = 102,
    IDC_STATUS      = 104,
    IDT_SEARCH      = 1
};

// -- Unicode block table ------------------------------------------------------
struct Block { const wchar_t* name; UINT32 first; UINT32 last; };
static const Block k_blocks[] = {
    { L"Smileys & Emotion",           0x1F600, 0x1F64F },
    { L"People & Body",               0x1F466, 0x1F487 },
    { L"Animals & Nature",            0x1F400, 0x1F43F },
    { L"Food & Drink",                0x1F347, 0x1F37F },
    { L"Travel & Places",             0x1F680, 0x1F6FF },
    { L"Activities",                  0x1F3A0, 0x1F3FF },
    { L"Objects",                     0x1F4A0, 0x1F4FF },
    { L"Symbols",                     0x1F500, 0x1F5FF },
    { L"Flags",                       0x1F1E0, 0x1F1FF },
    { L"Supplemental Symbols",        0x1F900, 0x1F9FF },
    { L"Symbols Extended-A",          0x1FA00, 0x1FA6F },
    { L"Symbols Extended-B",          0x1FA70, 0x1FAFF },
    { L"Misc Symbols",                0x2600,  0x26FF  },
    { L"Dingbats",                    0x2700,  0x27BF  },
    { L"Arrows",                      0x2190,  0x21FF  },
    { L"Math Operators",              0x2200,  0x22FF  },
    { L"Box Drawing",                 0x2500,  0x257F  },
    { L"Block Elements",              0x2580,  0x259F  },
    { L"Geometric Shapes",            0x25A0,  0x25FF  },
    { L"Latin Extended-A",            0x0100,  0x017F  },
    { L"Latin Extended-B",            0x0180,  0x024F  },
    { L"Currency Symbols",            0x20A0,  0x20CF  },
    { L"Letterlike Symbols",          0x2100,  0x214F  },
    { L"Number Forms",                0x2150,  0x218F  },
    { L"Enclosed Alphanumerics",      0x2460,  0x24FF  },
    { L"CJK Symbols & Punctuation",   0x3000,  0x303F  },
    { L"Hiragana",                    0x3040,  0x309F  },
    { L"Katakana",                    0x30A0,  0x30FF  },
    { L"Greek & Coptic",              0x0370,  0x03FF  },
    { L"Cyrillic",                    0x0400,  0x04FF  },
};
static const int k_nBlocks = (int)(sizeof(k_blocks) / sizeof(k_blocks[0]));

// -- Globals ------------------------------------------------------------------
static HINSTANCE g_hInst = NULL;
static HICON     g_hIcon = NULL;
static int       g_dpi   = 96;
static HFONT     g_hfUI  = NULL;
static HWND      g_hWnd  = NULL;

// D2D / DWrite
static ID2D1Factory*          g_pD2DFac  = nullptr;
static IDWriteFactory*        g_pDWFac   = nullptr;
static ID2D1HwndRenderTarget* g_pRT      = nullptr;
static IDWriteTextFormat*     g_pTFEmoji = nullptr;
static IDWriteTextFormat*     g_pTFLabel = nullptr;
// Brushes (recreated with render target)
static ID2D1SolidColorBrush*  g_pBrBg    = nullptr;
static ID2D1SolidColorBrush*  g_pBrText  = nullptr;
static ID2D1SolidColorBrush*  g_pBrSel   = nullptr;
static ID2D1SolidColorBrush*  g_pBrHov   = nullptr;
static ID2D1SolidColorBrush*  g_pBrLbl   = nullptr;
static ID2D1SolidColorBrush*  g_pBrWhite = nullptr;
static ID2D1SolidColorBrush*  g_pBrGrid  = nullptr;
static ID2D1SolidColorBrush*  g_pBrSep   = nullptr;

// Display list
static std::vector<UINT32> g_codepoints;
static int   g_curBlock    = -1;   // -1 = All
static int   g_hoveredIdx  = -1;
static int   g_selectedIdx = -1;
static WCHAR g_searchBuf[64] = {};

// Grid layout
static int g_cellSize  = 0;
static int g_cols      = 0;
static int g_gridTop   = 0;
static int g_clientW   = 0;
static int g_clientH   = 0;
static int g_scrollPos = 0;
static int g_scrollMax = 0;

// -- Helpers ------------------------------------------------------------------
static int   D(int px)       { return MulDiv(px, g_dpi, 96); }
// Physical pixels -> DIPs (D2D uses DIPs at system DPI)
static float Dip(int physPx) { return (float)physPx * 96.0f / (float)g_dpi; }

static HFONT MakeFont(int pt, bool bold, const wchar_t* face)
{
    HDC hdc = GetDC(NULL);
    int h = -MulDiv(pt, GetDeviceCaps(hdc, LOGPIXELSY), 72);
    ReleaseDC(NULL, hdc);
    return CreateFontW(h, 0,0,0, bold?FW_BOLD:FW_NORMAL,
        FALSE,FALSE,FALSE, DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH, face);
}

static std::wstring ToWStr(UINT32 cp)
{
    if (cp <= 0xFFFF) return std::wstring(1, (wchar_t)cp);
    cp -= 0x10000;
    wchar_t hi = (wchar_t)(0xD800 + (cp >> 10));
    wchar_t lo = (wchar_t)(0xDC00 + (cp & 0x3FF));
    return std::wstring{hi, lo};
}

static void CopyToClipboard(HWND hwnd, const std::wstring& s)
{
    size_t bytes = (s.size() + 1) * sizeof(wchar_t);
    HGLOBAL hg = GlobalAlloc(GMEM_MOVEABLE, bytes);
    if (!hg) return;
    memcpy(GlobalLock(hg), s.c_str(), bytes);
    GlobalUnlock(hg);
    OpenClipboard(hwnd); EmptyClipboard();
    SetClipboardData(CF_UNICODETEXT, hg);
    CloseClipboard();
}

static HICON LoadFromDll(const wchar_t* dll, int idx, int sz)
{
    wchar_t path[MAX_PATH];
    GetSystemDirectoryW(path, MAX_PATH);
    wcscat_s(path, L"\\"); wcscat_s(path, dll);
    HICON ico = NULL;
    PrivateExtractIconsW(path, idx, sz, sz, &ico, NULL, 1, 0);
    return ico;
}

// -- D2D resource management --------------------------------------------------
template<class T> static void SafeRelease(T** pp)
{ if (*pp) { (*pp)->Release(); *pp = nullptr; } }

static void DiscardDeviceResources()
{
    SafeRelease(&g_pBrBg);  SafeRelease(&g_pBrText); SafeRelease(&g_pBrSel);
    SafeRelease(&g_pBrHov); SafeRelease(&g_pBrLbl);  SafeRelease(&g_pBrWhite);
    SafeRelease(&g_pBrGrid);SafeRelease(&g_pBrSep);  SafeRelease(&g_pRT);
}

static HRESULT CreateDeviceResources(HWND hwnd)
{
    if (g_pRT) return S_OK;
    RECT rc; GetClientRect(hwnd, &rc);
    HRESULT hr = g_pD2DFac->CreateHwndRenderTarget(
        D2D1::RenderTargetProperties(),
        D2D1::HwndRenderTargetProperties(hwnd, D2D1::SizeU(rc.right, rc.bottom)),
        &g_pRT);
    if (FAILED(hr)) return hr;
    g_pRT->CreateSolidColorBrush(D2D1::ColorF(0.961f,0.961f,0.961f), &g_pBrBg);
    g_pRT->CreateSolidColorBrush(D2D1::ColorF(0.12f, 0.12f, 0.12f ), &g_pBrText);
    g_pRT->CreateSolidColorBrush(D2D1::ColorF(0.0f,  0.471f,0.843f), &g_pBrSel);
    g_pRT->CreateSolidColorBrush(D2D1::ColorF(0.82f, 0.898f,1.0f  ), &g_pBrHov);
    g_pRT->CreateSolidColorBrush(D2D1::ColorF(0.47f, 0.47f, 0.47f ), &g_pBrLbl);
    g_pRT->CreateSolidColorBrush(D2D1::ColorF(1.0f,  1.0f,  1.0f  ), &g_pBrWhite);
    g_pRT->CreateSolidColorBrush(D2D1::ColorF(0.863f,0.863f,0.863f), &g_pBrGrid);
    g_pRT->CreateSolidColorBrush(D2D1::ColorF(0.706f,0.706f,0.706f), &g_pBrSep);
    return S_OK;
}

// -- Build display list -------------------------------------------------------
static void BuildList()
{
    g_codepoints.clear(); g_scrollPos = 0;
    if (g_searchBuf[0] != L'\0') {
        std::wstring needle(g_searchBuf);
        std::transform(needle.begin(), needle.end(), needle.begin(), ::towlower);
        for (auto& blk : k_blocks) {
            for (UINT32 cp = blk.first; cp <= blk.last; cp++) {
                wchar_t hex[12]; swprintf_s(hex, L"%04x", cp);
                if (std::wstring(hex).find(needle) != std::wstring::npos)
                    g_codepoints.push_back(cp);
            }
        }
    } else if (g_curBlock < 0) {
        for (auto& blk : k_blocks)
            for (UINT32 cp = blk.first; cp <= blk.last; cp++)
                g_codepoints.push_back(cp);
    } else {
        const Block& blk = k_blocks[g_curBlock];
        for (UINT32 cp = blk.first; cp <= blk.last; cp++)
            g_codepoints.push_back(cp);
    }
    g_hoveredIdx = -1; g_selectedIdx = -1;
}

// -- Layout -------------------------------------------------------------------
static void UpdateScrollRange(HWND hwnd)
{
    if (g_cols <= 0 || g_cellSize <= 0) return;
    int rows   = ((int)g_codepoints.size() + g_cols - 1) / g_cols;
    int totalH = rows * g_cellSize;
    int viewH  = g_clientH - g_gridTop;
    g_scrollMax = max(0, totalH - viewH);
    if (g_scrollPos > g_scrollMax) g_scrollPos = g_scrollMax;
    SCROLLINFO si = { sizeof(si), SIF_RANGE|SIF_PAGE|SIF_POS };
    si.nMin = 0; si.nMax = totalH; si.nPage = viewH; si.nPos = g_scrollPos;
    SetScrollInfo(hwnd, SB_VERT, &si, TRUE);
}

static void RecalcLayout(HWND hwnd)
{
    RECT rc; GetClientRect(hwnd, &rc);
    HWND hSt = GetDlgItem(hwnd, IDC_STATUS);
    RECT stRc = {}; if (hSt) GetWindowRect(hSt, &stRc);
    int stH = hSt ? (stRc.bottom - stRc.top) : 0;
    g_clientW  = rc.right;
    g_clientH  = rc.bottom - stH;
    g_cellSize = D(72);
    g_cols     = max(1, g_clientW / g_cellSize);
    g_gridTop  = D(38);
    UpdateScrollRange(hwnd);
}

static void UpdateStatus(HWND hwnd)
{
    HWND hSt = GetDlgItem(hwnd, IDC_STATUS);
    if (!hSt) return;
    if (g_selectedIdx >= 0 && g_selectedIdx < (int)g_codepoints.size()) {
        UINT32 cp = g_codepoints[g_selectedIdx];
        wchar_t buf[128];
        swprintf_s(buf, L"   %s  U+%04X  --  click again or press Enter to copy",
            ToWStr(cp).c_str(), cp);
        SetWindowTextW(hSt, buf);
    } else {
        wchar_t buf[64];
        swprintf_s(buf, L"   %d glyphs  --  click to select, click again to copy",
            (int)g_codepoints.size());
        SetWindowTextW(hSt, buf);
    }
}

static int HitTest(int mx, int my)
{
    int gy = my + g_scrollPos - g_gridTop;
    if (gy < 0) return -1;
    int col = mx / g_cellSize, row = gy / g_cellSize;
    if (col < 0 || col >= g_cols) return -1;
    int idx = row * g_cols + col;
    return (idx >= 0 && idx < (int)g_codepoints.size()) ? idx : -1;
}

// -- D2D rendering ------------------------------------------------------------
static void RenderFrame(HWND hwnd)
{
    if (FAILED(CreateDeviceResources(hwnd))) return;

    g_pRT->BeginDraw();
    g_pRT->Clear(D2D1::ColorF(0.941f, 0.941f, 0.941f)); // toolbar bg

    float gridTopF  = Dip(g_gridTop);
    float clientWF  = Dip(g_clientW);
    float clientHF  = Dip(g_clientH);
    float cellF     = Dip(g_cellSize);
    float lblH      = Dip(D(16));

    // Grid background
    g_pRT->FillRectangle({ 0, gridTopF, clientWF, clientHF }, g_pBrBg);

    // Separator line
    g_pRT->DrawLine(D2D1::Point2F(0, gridTopF),
                    D2D1::Point2F(clientWF, gridTopF), g_pBrSep, 1.0f);

    int viewH    = g_clientH - g_gridTop;
    int firstRow = g_scrollPos / g_cellSize;
    int lastRow  = (g_scrollPos + viewH + g_cellSize - 1) / g_cellSize;

    for (int row = firstRow; row <= lastRow; row++) {
        for (int col = 0; col < g_cols; col++) {
            int idx = row * g_cols + col;
            if (idx >= (int)g_codepoints.size()) break;

            float cx = Dip(col * g_cellSize);
            float cy = Dip(g_gridTop + row * g_cellSize - g_scrollPos);
            D2D1_RECT_F cell = { cx, cy, cx + cellF, cy + cellF };

            if (idx == g_selectedIdx)
                g_pRT->FillRectangle(cell, g_pBrSel);
            else if (idx == g_hoveredIdx)
                g_pRT->FillRectangle(cell, g_pBrHov);

            // Cell borders
            g_pRT->DrawLine({cx + cellF, cy}, {cx + cellF, cy + cellF}, g_pBrGrid, 0.5f);
            g_pRT->DrawLine({cx, cy + cellF}, {cx + cellF, cy + cellF}, g_pBrGrid, 0.5f);

            // Glyph -- color emoji via ENABLE_COLOR_FONT
            UINT32 cp = g_codepoints[idx];
            std::wstring glyph = ToWStr(cp);
            auto* br = (idx == g_selectedIdx) ? g_pBrWhite : g_pBrText;
            D2D1_RECT_F glyphR = { cx, cy, cx + cellF, cy + cellF - lblH };
            g_pRT->DrawText(glyph.c_str(), (UINT32)glyph.size(),
                g_pTFEmoji, glyphR, br,
                D2D1_DRAW_TEXT_OPTIONS_ENABLE_COLOR_FONT);

            // Label
            wchar_t lbl[12]; swprintf_s(lbl, L"U+%04X", cp);
            auto* lbr = (idx == g_selectedIdx) ? g_pBrWhite : g_pBrLbl;
            D2D1_RECT_F lblR = { cx, cy + cellF - lblH, cx + cellF, cy + cellF };
            g_pRT->DrawText(lbl, 6, g_pTFLabel, lblR, lbr,
                D2D1_DRAW_TEXT_OPTIONS_NONE);
        }
    }

    if (g_pRT->EndDraw() == D2DERR_RECREATE_TARGET)
        DiscardDeviceResources();
}

// -- Window proc --------------------------------------------------------------
static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg)
    {
    case WM_CREATE:
    {
        g_hWnd = hwnd;
        HWND hCb = CreateWindowExW(0, L"COMBOBOX", NULL,
            WS_CHILD|WS_VISIBLE|CBS_DROPDOWNLIST|WS_VSCROLL|WS_TABSTOP,
            D(4), D(5), D(260), D(300),
            hwnd, (HMENU)IDC_BLOCK_COMBO, g_hInst, NULL);
        SendMessageW(hCb, WM_SETFONT, (WPARAM)g_hfUI, TRUE);
        SendMessageW(hCb, CB_ADDSTRING, 0, (LPARAM)L"— All —");
        for (int i = 0; i < k_nBlocks; i++)
            SendMessageW(hCb, CB_ADDSTRING, 0, (LPARAM)k_blocks[i].name);
        SendMessageW(hCb, CB_SETCURSEL, 0, 0);

        HWND hSe = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", NULL,
            WS_CHILD|WS_VISIBLE|ES_AUTOHSCROLL|WS_TABSTOP,
            D(272), D(7), D(200), D(24),
            hwnd, (HMENU)IDC_SEARCH_EDIT, g_hInst, NULL);
        SendMessageW(hSe, WM_SETFONT, (WPARAM)g_hfUI, TRUE);
        SendMessageW(hSe, EM_SETCUEBANNER, TRUE, (LPARAM)L"Search U+hex...");

        HWND hSt = CreateWindowExW(0, STATUSCLASSNAMEW, NULL,
            WS_CHILD|WS_VISIBLE|SBARS_SIZEGRIP,
            0,0,0,0, hwnd, (HMENU)IDC_STATUS, g_hInst, NULL);
        SendMessageW(hSt, WM_SETFONT, (WPARAM)g_hfUI, TRUE);

        BuildList(); UpdateStatus(hwnd);
        return 0;
    }
    case WM_SIZE:
    {
        HWND hSt = GetDlgItem(hwnd, IDC_STATUS);
        SendMessageW(hSt, WM_SIZE, 0, 0);
        RECT cr; GetClientRect(hwnd, &cr);
        SetWindowPos(GetDlgItem(hwnd, IDC_BLOCK_COMBO), NULL,
            D(4), D(5), D(260), D(260), SWP_NOZORDER);
        SetWindowPos(GetDlgItem(hwnd, IDC_SEARCH_EDIT), NULL,
            D(272), D(7), cr.right - D(272) - D(4), D(24), SWP_NOZORDER);
        if (g_pRT) g_pRT->Resize(D2D1::SizeU(cr.right, cr.bottom));
        RecalcLayout(hwnd);
        InvalidateRect(hwnd, NULL, FALSE);
        return 0;
    }
    case WM_PAINT:
    {
        PAINTSTRUCT ps; BeginPaint(hwnd, &ps);
        RenderFrame(hwnd);
        EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_ERASEBKGND: return 1;
    case WM_VSCROLL:
    {
        SCROLLINFO si = {sizeof(si), SIF_ALL}; GetScrollInfo(hwnd, SB_VERT, &si);
        int p = si.nPos;
        switch (LOWORD(wp)) {
        case SB_LINEUP:     p -= g_cellSize; break;
        case SB_LINEDOWN:   p += g_cellSize; break;
        case SB_PAGEUP:     p -= si.nPage;   break;
        case SB_PAGEDOWN:   p += si.nPage;   break;
        case SB_THUMBTRACK: p  = si.nTrackPos; break;
        case SB_TOP:        p  = 0;          break;
        case SB_BOTTOM:     p  = si.nMax;    break;
        }
        p = max(0, min(p, g_scrollMax));
        if (p != g_scrollPos) { g_scrollPos = p; SetScrollPos(hwnd,SB_VERT,p,TRUE); InvalidateRect(hwnd,NULL,FALSE); }
        return 0;
    }
    case WM_MOUSEWHEEL:
    {
        int p = g_scrollPos - (GET_WHEEL_DELTA_WPARAM(wp)/WHEEL_DELTA)*g_cellSize;
        p = max(0, min(p, g_scrollMax));
        if (p != g_scrollPos) { g_scrollPos = p; SetScrollPos(hwnd,SB_VERT,p,TRUE); InvalidateRect(hwnd,NULL,FALSE); }
        return 0;
    }
    case WM_MOUSEMOVE:
    {
        int idx = HitTest(LOWORD(lp), HIWORD(lp));
        if (idx != g_hoveredIdx) {
            g_hoveredIdx = idx; InvalidateRect(hwnd, NULL, FALSE);
            TRACKMOUSEEVENT tme = {sizeof(tme), TME_LEAVE, hwnd, 0}; TrackMouseEvent(&tme);
        }
        return 0;
    }
    case WM_MOUSELEAVE:
        g_hoveredIdx = -1; InvalidateRect(hwnd, NULL, FALSE);
        return 0;
    case WM_LBUTTONDOWN:
    {
        SetFocus(hwnd);
        int idx = HitTest(LOWORD(lp), HIWORD(lp));
        if (idx >= 0) {
            if (idx == g_selectedIdx) {
                UINT32 cp = g_codepoints[idx];
                CopyToClipboard(hwnd, ToWStr(cp));
                wchar_t buf[80]; swprintf_s(buf, L"   Copied  %s  U+%04X  to clipboard", ToWStr(cp).c_str(), cp);
                SetWindowTextW(GetDlgItem(hwnd, IDC_STATUS), buf);
            } else {
                g_selectedIdx = idx; InvalidateRect(hwnd, NULL, FALSE); UpdateStatus(hwnd);
            }
        } else {
            g_selectedIdx = -1; InvalidateRect(hwnd, NULL, FALSE); UpdateStatus(hwnd);
        }
        return 0;
    }
    case WM_KEYDOWN:
    {
        if (g_selectedIdx < 0 && !g_codepoints.empty()) g_selectedIdx = 0;
        int idx = g_selectedIdx;
        switch (wp) {
        case VK_RIGHT: idx++; break; case VK_LEFT:  idx--; break;
        case VK_DOWN:  idx += g_cols; break; case VK_UP: idx -= g_cols; break;
        case VK_RETURN: case VK_SPACE:
            if (idx >= 0 && idx < (int)g_codepoints.size()) {
                UINT32 cp = g_codepoints[idx]; CopyToClipboard(hwnd, ToWStr(cp));
                wchar_t buf[80]; swprintf_s(buf, L"   Copied  %s  U+%04X  to clipboard", ToWStr(cp).c_str(), cp);
                SetWindowTextW(GetDlgItem(hwnd, IDC_STATUS), buf);
            }
            return 0;
        }
        idx = max(0, min(idx, (int)g_codepoints.size()-1));
        if (idx != g_selectedIdx) {
            g_selectedIdx = idx;
            int row = idx/g_cols, cellTop = row*g_cellSize, cellBot = cellTop+g_cellSize;
            int viewH = g_clientH - g_gridTop;
            if (cellTop < g_scrollPos) g_scrollPos = cellTop;
            else if (cellBot > g_scrollPos+viewH) g_scrollPos = cellBot-viewH;
            g_scrollPos = max(0, min(g_scrollPos, g_scrollMax));
            SetScrollPos(hwnd, SB_VERT, g_scrollPos, TRUE);
            InvalidateRect(hwnd, NULL, FALSE); UpdateStatus(hwnd);
        }
        return 0;
    }
    case WM_COMMAND:
        if (LOWORD(wp)==IDC_BLOCK_COMBO && HIWORD(wp)==CBN_SELCHANGE) {
            int sel = (int)SendMessageW(GetDlgItem(hwnd,IDC_BLOCK_COMBO), CB_GETCURSEL, 0, 0);
            g_curBlock = sel - 1;  // 0="All" -> -1, 1..n -> 0..n-1
            SetWindowTextW(GetDlgItem(hwnd,IDC_SEARCH_EDIT), L""); g_searchBuf[0]=0;
            BuildList(); UpdateScrollRange(hwnd); UpdateStatus(hwnd); InvalidateRect(hwnd,NULL,FALSE);
        }
        if (LOWORD(wp)==IDC_SEARCH_EDIT && HIWORD(wp)==EN_CHANGE) {
            KillTimer(hwnd, IDT_SEARCH); SetTimer(hwnd, IDT_SEARCH, 300, NULL);
        }
        return 0;
    case WM_TIMER:
        if (wp == IDT_SEARCH) {
            KillTimer(hwnd, IDT_SEARCH);
            GetWindowTextW(GetDlgItem(hwnd,IDC_SEARCH_EDIT), g_searchBuf, 63);
            BuildList(); UpdateScrollRange(hwnd); UpdateStatus(hwnd); InvalidateRect(hwnd,NULL,FALSE);
        }
        return 0;
    case WM_DESTROY:
        DiscardDeviceResources(); PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

// -- Entry point --------------------------------------------------------------
int WINAPI wWinMain(HINSTANCE hInst, HINSTANCE, LPWSTR, int)
{
    g_hInst = hInst;
    INITCOMMONCONTROLSEX icc = {sizeof(icc), ICC_WIN95_CLASSES|ICC_BAR_CLASSES};
    InitCommonControlsEx(&icc);

    HDC hScreen = GetDC(NULL);
    g_dpi = GetDeviceCaps(hScreen, LOGPIXELSY);
    ReleaseDC(NULL, hScreen);

    // Init D2D + DWrite
    D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, &g_pD2DFac);
    DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED,
        __uuidof(IDWriteFactory), reinterpret_cast<IUnknown**>(&g_pDWFac));

    // Text formats (sizes in DIPs; render target handles DPI scaling)
    if (g_pDWFac) {
        g_pDWFac->CreateTextFormat(L"Segoe UI Emoji", nullptr,
            DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL,
            DWRITE_FONT_STRETCH_NORMAL, 28.0f, L"", &g_pTFEmoji);
        g_pDWFac->CreateTextFormat(L"Segoe UI", nullptr,
            DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL,
            DWRITE_FONT_STRETCH_NORMAL, 9.0f, L"", &g_pTFLabel);
        if (g_pTFEmoji) {
            g_pTFEmoji->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
            g_pTFEmoji->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        }
        if (g_pTFLabel) {
            g_pTFLabel->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
            g_pTFLabel->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        }
    }

    g_hfUI  = MakeFont(9, false, L"Segoe UI");
    g_hIcon = LoadFromDll(L"imageres.dll", 81, D(32));

    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(wc); wc.lpfnWndProc = WndProc;
    wc.hInstance = hInst; wc.hIcon = g_hIcon; wc.hIconSm = g_hIcon;
    wc.hCursor = LoadCursorW(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE+1);
    wc.lpszClassName = L"GlyphPicker";
    RegisterClassExW(&wc);

    int sw = GetSystemMetrics(SM_CXSCREEN), sh = GetSystemMetrics(SM_CYSCREEN);
    DWORD style = WS_OVERLAPPEDWINDOW, exStyle = WS_EX_APPWINDOW;
    RECT adj = {0,0,D(700),D(560)}; AdjustWindowRectEx(&adj, style, FALSE, exStyle);
    int ww = adj.right-adj.left, wh = adj.bottom-adj.top;

    HWND hWnd = CreateWindowExW(exStyle, L"GlyphPicker",
        L"Glyph & Emoji Picker", style,
        (sw-ww)/2, (sh-wh)/2, ww, wh, NULL, NULL, hInst, NULL);
    if (!hWnd) return 1;

    SendMessageW(hWnd, WM_SETICON, ICON_BIG,   (LPARAM)g_hIcon);
    SendMessageW(hWnd, WM_SETICON, ICON_SMALL, (LPARAM)g_hIcon);
    ShowWindow(hWnd, SW_SHOW); UpdateWindow(hWnd);

    MSG msg;
    while (GetMessageW(&msg, NULL, 0, 0) > 0) {
        TranslateMessage(&msg); DispatchMessageW(&msg);
    }

    SafeRelease(&g_pTFEmoji); SafeRelease(&g_pTFLabel);
    SafeRelease(&g_pDWFac);   SafeRelease(&g_pD2DFac);
    return (int)msg.wParam;
}