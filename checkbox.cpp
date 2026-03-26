#include "checkbox.h"
#include "dpi.h"
#include <commctrl.h>
#include <windowsx.h>
#include <winreg.h>

#pragma comment(lib, "comctl32.lib")

// Window-property names (string literals used as atoms via SetPropW).
// CBP_CHECKED  — present (value 1) means checked; absent means unchecked.
// CBP_HOVERED  — present (value 1) means the cursor is inside the control.
// CBP_MARKER   — present on every custom checkbox so DrawCustomCheckbox can
//                identify them without knowing their control IDs.
static const wchar_t* CBP_CHECKED = L"CbChecked";
static const wchar_t* CBP_HOVERED = L"CbHovered";
static const wchar_t* CBP_MARKER  = L"CustomCheckbox";

// ── Theme detection ───────────────────────────────────────────────────────────

enum class CbTheme { Light, Dark, HCBlack, HCWhite };

static CbTheme DetectCbTheme()
{
    // High-contrast takes priority over light/dark.
    HIGHCONTRASTW hc = {};
    hc.cbSize = sizeof(hc);
    if (SystemParametersInfoW(SPI_GETHIGHCONTRAST, sizeof(hc), &hc, 0) &&
        (hc.dwFlags & HCF_HIGHCONTRASTON))
    {
        // Distinguish black-on-white from white-on-black by the window background
        // luminance (system colour, not the app preference).
        COLORREF bg   = GetSysColor(COLOR_WINDOW);
        int      luma = (GetRValue(bg) * 299 +
                         GetGValue(bg) * 587 +
                         GetBValue(bg) * 114) / 1000;
        return (luma < 128) ? CbTheme::HCBlack : CbTheme::HCWhite;
    }

    // Light vs dark: read the per-user preference written by Windows Settings.
    DWORD light = 1; // default: light
    HKEY  hKey  = NULL;
    if (RegOpenKeyExW(HKEY_CURRENT_USER,
            L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
            0, KEY_READ, &hKey) == ERROR_SUCCESS)
    {
        DWORD cb = sizeof(DWORD);
        RegQueryValueExW(hKey, L"AppsUseLightTheme",
                         NULL, NULL, (LPBYTE)&light, &cb);
        RegCloseKey(hKey);
    }
    return (light == 0) ? CbTheme::Dark : CbTheme::Light;
}

// ── Per-theme colour palette ──────────────────────────────────────────────────

struct CbColors {
    COLORREF paneBg;    // area outside the box — matches the parent background
    COLORREF boxBg;     // box fill
    COLORREF border;    // box border (normal)
    COLORREF hover;     // box border (hovered)
    COLORREF tick;      // ✓ glyph
    COLORREF label;     // label text
};

static CbColors GetCbColors(CbTheme t)
{
    switch (t) {
    case CbTheme::Dark:
        // Dark pane, dark box, lighter border, bright green tick, light label.
        return {
            RGB( 32,  32,  32),   // paneBg
            RGB( 45,  45,  45),   // boxBg     — slightly lighter than pane
            RGB(150, 150, 150),   // border
            RGB(  0, 150, 255),   // hover     — Windows accent blue works on dark too
            RGB(102, 204, 102),   // tick      — fresh lime-green, readable on dark
            RGB(240, 240, 240),   // label
        };

    case CbTheme::HCBlack: {
        // System colours only — High Contrast Black.
        COLORREF hl = GetSysColor(COLOR_HOTLIGHT);
        COLORREF wt = GetSysColor(COLOR_WINDOWTEXT);
        COLORREF wn = GetSysColor(COLOR_WINDOW);
        return { wn, wn, wt, hl, hl, wt };
    }

    case CbTheme::HCWhite: {
        // System colours — High Contrast White; tick uses a dark green so it
        // remains distinct from pure black text.
        COLORREF wt = GetSysColor(COLOR_WINDOWTEXT);
        COLORREF wn = GetSysColor(COLOR_WINDOW);
        COLORREF hl = GetSysColor(COLOR_HOTLIGHT);
        return { wn, wn, wt, hl, RGB(0, 100, 0), wt };
    }

    default: // Light
        // White box, classic gray border, forest-green tick, near-black label.
        return {
            GetSysColor(COLOR_WINDOW),  // paneBg — matches main window background
            RGB(255, 255, 255),         // boxBg
            RGB(102, 102, 102),         // border
            RGB(  0, 120, 215),         // hover  — Windows 10/11 accent blue
            RGB( 34, 139,  34),         // tick   — Forest Green
            GetSysColor(COLOR_WINDOWTEXT),
        };
    }
}

// ── Subclass proc ─────────────────────────────────────────────────────────────

static LRESULT CALLBACK CheckboxSubclassProc(
    HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam,
    UINT_PTR uIdSubclass, DWORD_PTR /*dwRefData*/)
{
    switch (msg) {

    // --- Checked-state access (compatible with BS_AUTOCHECKBOX) --------------

    case BM_GETCHECK:
        return GetPropW(hwnd, CBP_CHECKED) ? BST_CHECKED : BST_UNCHECKED;

    case BM_SETCHECK:
        if (wParam == BST_CHECKED)
            SetPropW(hwnd, CBP_CHECKED, (HANDLE)(LONG_PTR)1);
        else
            RemovePropW(hwnd, CBP_CHECKED);
        InvalidateRect(hwnd, NULL, TRUE);
        return 0;

    // --- Mouse toggle ---------------------------------------------------------

    case WM_LBUTTONUP: {
        // Toggle state BEFORE DefSubclassProc sends BN_CLICKED to the parent,
        // so BM_GETCHECK inside the parent's BN_CLICKED handler returns the
        // newly toggled value — identical to BS_AUTOCHECKBOX behaviour.
        POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
        RECT  rc;
        GetClientRect(hwnd, &rc);
        if (PtInRect(&rc, pt)) {
            if (GetPropW(hwnd, CBP_CHECKED))
                RemovePropW(hwnd, CBP_CHECKED);
            else
                SetPropW(hwnd, CBP_CHECKED, (HANDLE)(LONG_PTR)1);
            InvalidateRect(hwnd, NULL, TRUE);
        }
        break; // let DefSubclassProc send BN_CLICKED
    }

    // --- Keyboard toggle (Space bar) -----------------------------------------

    case WM_KEYUP:
        if (wParam == VK_SPACE) {
            if (GetPropW(hwnd, CBP_CHECKED))
                RemovePropW(hwnd, CBP_CHECKED);
            else
                SetPropW(hwnd, CBP_CHECKED, (HANDLE)(LONG_PTR)1);
            InvalidateRect(hwnd, NULL, TRUE);
            // DefSubclassProc will send BN_CLICKED.
        }
        break;

    // --- Hover highlight ------------------------------------------------------

    case WM_MOUSEMOVE:
        if (!GetPropW(hwnd, CBP_HOVERED)) {
            SetPropW(hwnd, CBP_HOVERED, (HANDLE)(LONG_PTR)1);
            InvalidateRect(hwnd, NULL, TRUE);
            TRACKMOUSEEVENT tme = { sizeof(tme), TME_LEAVE, hwnd, 0 };
            TrackMouseEvent(&tme);
        }
        break;

    case WM_MOUSELEAVE:
        RemovePropW(hwnd, CBP_HOVERED);
        InvalidateRect(hwnd, NULL, TRUE);
        break;

    // --- Focus indicator ------------------------------------------------------

    case WM_SETFOCUS:
    case WM_KILLFOCUS:
        InvalidateRect(hwnd, NULL, TRUE);
        break;

    // --- Cleanup --------------------------------------------------------------

    case WM_NCDESTROY:
        RemoveWindowSubclass(hwnd, CheckboxSubclassProc, uIdSubclass);
        RemovePropW(hwnd, CBP_CHECKED);
        RemovePropW(hwnd, CBP_HOVERED);
        RemovePropW(hwnd, CBP_MARKER);
        RemovePropW(hwnd, L"CbRequired");
        break;
    }

    return DefSubclassProc(hwnd, msg, wParam, lParam);
}

// ── Public API ────────────────────────────────────────────────────────────────

HWND CreateCustomCheckbox(HWND hwndParent, int id, const std::wstring& label,
                           bool initiallyChecked,
                           int x, int y, int width, int height,
                           HINSTANCE hInst)
{
    HWND hwnd = CreateWindowExW(0, L"BUTTON", label.c_str(),
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
        x, y, width, height,
        hwndParent, (HMENU)(LONG_PTR)id, hInst, NULL);
    if (!hwnd) return NULL;

    // Mark as a custom checkbox (used by DrawCustomCheckbox to identify us).
    SetPropW(hwnd, CBP_MARKER, (HANDLE)(LONG_PTR)1);

    // Set initial checked state.
    if (initiallyChecked)
        SetPropW(hwnd, CBP_CHECKED, (HANDLE)(LONG_PTR)1);

    // Install the subclass that handles BM_GETCHECK/BM_SETCHECK and hover.
    SetWindowSubclass(hwnd, CheckboxSubclassProc, 1, 0);

    return hwnd;
}

BOOL DrawCustomCheckbox(LPDRAWITEMSTRUCT dis)
{
    // Bail out immediately for anything that isn't one of our checkboxes.
    if (!GetPropW(dis->hwndItem, CBP_MARKER))
        return FALSE;

    CbTheme  theme    = DetectCbTheme();
    CbColors c        = GetCbColors(theme);
    bool     checked  = (GetPropW(dis->hwndItem, CBP_CHECKED) != NULL);
    bool     hovered  = (GetPropW(dis->hwndItem, CBP_HOVERED) != NULL);
    // When the control is disabled (e.g. Pre-selected locked by Required), mute all
    // colours so it reads as unavailable — both border and text go to system grey.
    // Exception: if the checkbox is tagged CbRequired (a "required" component)
    // keep the label in normal window-text colour so only the tick is muted.
    if (dis->itemState & ODS_DISABLED) {
        bool isRequired = (GetPropW(dis->hwndItem, L"CbRequired") != NULL);
        c.border = RGB(190, 190, 190);
        c.hover  = RGB(190, 190, 190);
        c.tick   = RGB(160, 160, 160);
        c.label  = isRequired ? GetSysColor(COLOR_WINDOWTEXT)
                              : GetSysColor(COLOR_GRAYTEXT);
        hovered  = false; // no hover highlight on disabled control
    }

    HDC  hdc  = dis->hDC;
    RECT rcAll = dis->rcItem;

    // 1. Erase the whole control area with the pane background so no
    //    artefacts remain from previous paints.
    HBRUSH hPaneBr = CreateSolidBrush(c.paneBg);
    FillRect(hdc, &rcAll, hPaneBr);
    DeleteObject(hPaneBr);

    // 2. Box — S(15)×S(15), aligned with the top of the first text line.
    // Top-aligning (S(2) inset) works for both single-line and word-wrapped
    // multi-line checkboxes without needing to query font metrics.
    const int boxSz = S(15);
    const int boxX  = rcAll.left;
    const int boxY  = rcAll.top + S(2);

    HBRUSH hBoxBr  = CreateSolidBrush(c.boxBg);
    HPEN   hBoxPen = CreatePen(PS_SOLID, 1, hovered ? c.hover : c.border);
    HBRUSH hOldBr  = (HBRUSH)SelectObject(hdc, hBoxBr);
    HPEN   hOldPen = (HPEN)SelectObject(hdc, hBoxPen);
    RoundRect(hdc, boxX, boxY, boxX + boxSz, boxY + boxSz, S(3), S(3));
    SelectObject(hdc, hOldBr);
    SelectObject(hdc, hOldPen);
    DeleteObject(hBoxBr);
    DeleteObject(hBoxPen);

    // 3. Tick glyph (U+2714 ✔ heavy, Segoe UI Symbol) when checked.
    // The font is sized to the full box so the right ascending stroke of the
    // heavy check naturally overflows the right border, giving a hand-written feel.
    if (checked) {
        // Font is 40% larger than the box — glyph is bold and the ascending
        // right stroke visibly crosses the box border.
        HFONT hTickFont = CreateFontW(
            -(boxSz * 14 / 10), 0, 0, 0, FW_NORMAL,
            FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE,
            L"Segoe UI Symbol");
        if (hTickFont) {
            HFONT hOldF = (HFONT)SelectObject(hdc, hTickFont);
            SetBkMode(hdc, TRANSPARENT);
            SetTextColor(hdc, c.tick);
            // Centre on the box but extend rect right so overflow is not clipped.
            // Shift 4px up so the tick clears the bottom of the box.
            RECT rcBox = { boxX - S(3), boxY - S(3) - S(4), boxX + boxSz + S(8), boxY + boxSz + S(3) - S(4) };
            DrawTextW(hdc, L"\u2714", 1, &rcBox,
                      DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
            SelectObject(hdc, hOldF);
            DeleteObject(hTickFont);
        }
    }

    // 4. Label — S(6) gap between the right edge of the box and the text.
    {
        HFONT hCtrlFont = (HFONT)SendMessageW(dis->hwndItem, WM_GETFONT, 0, 0);
        HFONT hOldF     = NULL;
        if (hCtrlFont) hOldF = (HFONT)SelectObject(hdc, hCtrlFont);

        wchar_t text[512] = {};
        GetWindowTextW(dis->hwndItem, text, 512);

        SetBkMode(hdc, TRANSPARENT);
        SetTextColor(hdc, c.label);
        RECT rcLabel = { boxX + boxSz + S(6), rcAll.top, rcAll.right, rcAll.bottom };
        DrawTextW(hdc, text, -1, &rcLabel,
                  DT_LEFT | DT_WORDBREAK | DT_NOPREFIX);

        if (hOldF) SelectObject(hdc, hOldF);
    }

    // 5. Focus rectangle when the control has keyboard focus.
    if (dis->itemState & ODS_FOCUS) {
        RECT rcFocus = rcAll;
        InflateRect(&rcFocus, -1, -1);
        DrawFocusRect(hdc, &rcFocus);
    }

    return TRUE;
}

void OnCheckboxSettingChange(HWND hwndParent)
{
    // Repaint every custom checkbox that is a direct child of hwndParent so
    // it picks up the new theme (light ↔ dark ↔ high-contrast) immediately.
    EnumChildWindows(hwndParent, [](HWND hChild, LPARAM /*lp*/) -> BOOL {
        if (GetPropW(hChild, L"CustomCheckbox"))
            InvalidateRect(hChild, NULL, TRUE);
        return TRUE;
    }, 0);
}

// ── TreeView state image list ─────────────────────────────────────────────────

// Draw one checkbox bitmap (unchecked or checked) into a sizePx × sizePx DIB.
static HBITMAP DrawCheckboxBitmap(int sizePx, bool checked, const CbColors& c)
{
    BITMAPINFO bmi = {};
    bmi.bmiHeader.biSize        = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth       = sizePx;
    bmi.bmiHeader.biHeight      = -sizePx; // top-down
    bmi.bmiHeader.biPlanes      = 1;
    bmi.bmiHeader.biBitCount    = 32;
    bmi.bmiHeader.biCompression = BI_RGB;
    void* pBits = nullptr;
    HBITMAP hBmp = CreateDIBSection(NULL, &bmi, DIB_RGB_COLORS, &pBits, NULL, 0);
    if (!hBmp) return NULL;

    HDC     hDC  = CreateCompatibleDC(NULL);
    HBITMAP hOld = (HBITMAP)SelectObject(hDC, hBmp);

    // Background fill.
    RECT rcAll = { 0, 0, sizePx, sizePx };
    HBRUSH hBgBr = CreateSolidBrush(c.paneBg);
    FillRect(hDC, &rcAll, hBgBr);
    DeleteObject(hBgBr);

    // Box: 1-pixel margin each side, rounded corners.
    const int mg    = std::max(1, sizePx / 14);
    const int boxSz = sizePx - mg * 2;
    const int boxX  = mg;
    const int boxY  = mg;
    const int cr    = std::max(2, sizePx / 6);

    HBRUSH hBoxBr  = CreateSolidBrush(c.boxBg);
    HPEN   hBoxPen = CreatePen(PS_SOLID, 1, c.border);
    HBRUSH hOldBr  = (HBRUSH)SelectObject(hDC, hBoxBr);
    HPEN   hOldPen = (HPEN  )SelectObject(hDC, hBoxPen);
    RoundRect(hDC, boxX, boxY, boxX + boxSz, boxY + boxSz, cr, cr);
    SelectObject(hDC, hOldBr);
    SelectObject(hDC, hOldPen);
    DeleteObject(hBoxBr);
    DeleteObject(hBoxPen);

    // Tick glyph (U+2714 heavy, Segoe UI Symbol) — 1.4× box size so the right
    // ascending stroke crosses the border, giving a hand-written feel.
    if (checked) {
        HFONT hTickFont = CreateFontW(
            -(boxSz * 14 / 10), 0, 0, 0, FW_NORMAL,
            FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE,
            L"Segoe UI Symbol");
        if (hTickFont) {
            HFONT hOldF = (HFONT)SelectObject(hDC, hTickFont);
            SetBkMode(hDC, TRANSPARENT);
            SetTextColor(hDC, c.tick);
            // Extend right & vertical so the oversized glyph is not clipped.
            // Shift up by sizePx/4 so the tick clears the bottom of the box.
            const int up = sizePx / 4;
            RECT rcBox = { boxX - mg,            boxY - mg - up,
                           boxX + boxSz + sizePx / 3,
                           boxY + boxSz + mg - up };
            DrawTextW(hDC, L"\u2714", 1, &rcBox,
                      DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
            SelectObject(hDC, hOldF);
            DeleteObject(hTickFont);
        }
    }

    SelectObject(hDC, hOld);
    DeleteDC(hDC);
    return hBmp;
}

HIMAGELIST CreateCheckboxStateImageList(int sizePx)
{
    CbTheme  theme = DetectCbTheme();
    CbColors c     = GetCbColors(theme);

    // [0] blank, [1] unchecked, [2] checked  — matches TVS_CHECKBOXES indices.
    HIMAGELIST hIL = ImageList_Create(sizePx, sizePx, ILC_COLOR32, 3, 0);
    if (!hIL) return NULL;

    // [0] Blank — solid background, no box (used when state index == 0).
    {
        BITMAPINFO bmi = {};
        bmi.bmiHeader.biSize        = sizeof(BITMAPINFOHEADER);
        bmi.bmiHeader.biWidth       = sizePx;
        bmi.bmiHeader.biHeight      = -sizePx;
        bmi.bmiHeader.biPlanes      = 1;
        bmi.bmiHeader.biBitCount    = 32;
        bmi.bmiHeader.biCompression = BI_RGB;
        void* pBits = nullptr;
        HBITMAP hBlank = CreateDIBSection(NULL, &bmi, DIB_RGB_COLORS, &pBits, NULL, 0);
        if (hBlank && pBits) {
            BYTE r = GetRValue(c.paneBg), g = GetGValue(c.paneBg), b = GetBValue(c.paneBg);
            BYTE* p = (BYTE*)pBits;
            for (int i = 0; i < sizePx * sizePx; i++, p += 4)
                { p[0] = b; p[1] = g; p[2] = r; p[3] = 0; }
            ImageList_Add(hIL, hBlank, NULL);
            DeleteObject(hBlank);
        }
    }

    HBITMAP hUnchecked = DrawCheckboxBitmap(sizePx, false, c);
    if (hUnchecked) { ImageList_Add(hIL, hUnchecked, NULL); DeleteObject(hUnchecked); }

    HBITMAP hChecked = DrawCheckboxBitmap(sizePx, true, c);
    if (hChecked)   { ImageList_Add(hIL, hChecked,   NULL); DeleteObject(hChecked); }

    return hIL;
}

void UpdateTreeViewCheckboxImages(HWND hTreeView, int sizePx)
{
    HIMAGELIST hNew = CreateCheckboxStateImageList(sizePx);
    if (!hNew) return;
    HIMAGELIST hOld = TreeView_SetImageList(hTreeView, hNew, TVSIL_STATE);
    if (hOld) ImageList_Destroy(hOld);
}
