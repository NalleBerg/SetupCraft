#include "edit_rtf.h"
#include "dpi.h"
#include "button.h"
#include "tooltip.h"
#include <richedit.h>
#include <commdlg.h>
#include <commctrl.h>
#include <string>
#include <vector>
#include <algorithm>
#include <stdio.h>

// ─── RTF stream helpers (identical pattern to notes_editor — bug-free) ────────

struct RtfEdStreamBuf { const std::string* src; size_t pos; };

static DWORD CALLBACK RtfEdReadCb(DWORD_PTR cookie, LPBYTE buf, LONG cb, LONG* pcb)
{
    RtfEdStreamBuf* rb = (RtfEdStreamBuf*)cookie;
    size_t rem = rb->src->size() - rb->pos;
    LONG   n   = (LONG)(rem < (size_t)cb ? rem : (size_t)cb);
    if (n > 0) { memcpy(buf, rb->src->c_str() + rb->pos, n); rb->pos += n; }
    *pcb = n;
    return 0;
}

static DWORD CALLBACK RtfEdWriteCb(DWORD_PTR cookie, LPBYTE buf, LONG cb, LONG* pcb)
{
    std::string* s = (std::string*)cookie;
    s->append((char*)buf, cb);
    *pcb = cb;
    return 0;
}

static void RtfEd_StreamIn(HWND hEdit, const std::wstring& wrtf)
{
    if (wrtf.empty()) { SetWindowTextW(hEdit, L""); return; }
    std::string rtf;
    int n = WideCharToMultiByte(CP_UTF8, 0, wrtf.c_str(), -1, NULL, 0, NULL, NULL);
    if (n > 1) { rtf.resize(n - 1); WideCharToMultiByte(CP_UTF8, 0, wrtf.c_str(), -1, &rtf[0], n, NULL, NULL); }
    RtfEdStreamBuf rb = { &rtf, 0 };
    EDITSTREAM es     = { (DWORD_PTR)&rb, 0, RtfEdReadCb };
    SendMessageW(hEdit, EM_STREAMIN, SF_RTF, (LPARAM)&es);
}

static std::wstring RtfEd_StreamOut(HWND hEdit)
{
    std::string rtf;
    EDITSTREAM es = { (DWORD_PTR)&rtf, 0, RtfEdWriteCb };
    SendMessageW(hEdit, EM_STREAMOUT, SF_RTF, (LPARAM)&es);
    if (rtf.empty()) return L"";
    int n = MultiByteToWideChar(CP_UTF8, 0, rtf.c_str(), -1, NULL, 0);
    if (n <= 1) return L"";
    std::wstring out(n - 1, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, rtf.c_str(), -1, &out[0], n);
    return out;
}

// ─── Character format helpers ─────────────────────────────────────────────────

// Read-modify-write: preserves CFE_AUTOCOLOR and explicit colour — from notes_editor.
static void RtfEd_ToggleEffect(HWND hEdit, DWORD maskBit, DWORD effectBit)
{
    CHARFORMAT2W cf = {}; cf.cbSize = sizeof(cf);
    cf.dwMask = maskBit | CFM_COLOR;
    SendMessageW(hEdit, EM_GETCHARFORMAT, SCF_SELECTION, (LPARAM)&cf);
    if (cf.dwEffects & effectBit) cf.dwEffects &= ~effectBit;
    else                          cf.dwEffects |=  effectBit;
    SendMessageW(hEdit, EM_SETCHARFORMAT, SCF_SELECTION, (LPARAM)&cf);
}

// Subscript / Superscript are mutually exclusive — from notes_editor.
static void RtfEd_ToggleScript(HWND hEdit, DWORD wantBit)
{
    CHARFORMAT2W cf = {}; cf.cbSize = sizeof(cf);
    cf.dwMask = CFM_SUBSCRIPT | CFM_COLOR;
    SendMessageW(hEdit, EM_GETCHARFORMAT, SCF_SELECTION, (LPARAM)&cf);
    bool wasSet = (cf.dwEffects & wantBit) != 0;
    cf.dwEffects &= ~(CFE_SUBSCRIPT | CFE_SUPERSCRIPT);
    if (!wasSet) cf.dwEffects |= wantBit;
    SendMessageW(hEdit, EM_SETCHARFORMAT, SCF_SELECTION, (LPARAM)&cf);
}

// ─── Paragraph format helpers ─────────────────────────────────────────────────

static void RtfEd_SetAlignment(HWND hEdit, WORD align)
{
    PARAFORMAT2 pf = {}; pf.cbSize = sizeof(pf);
    pf.dwMask    = PFM_ALIGNMENT;
    pf.wAlignment = align;
    SendMessageW(hEdit, EM_SETPARAFORMAT, 0, (LPARAM)&pf);
}

static void RtfEd_ToggleBullet(HWND hEdit)
{
    PARAFORMAT2 pf = {}; pf.cbSize = sizeof(pf);
    pf.dwMask = PFM_NUMBERING;
    SendMessageW(hEdit, EM_GETPARAFORMAT, 0, (LPARAM)&pf);
    pf.dwMask     = PFM_NUMBERING | PFM_OFFSET;
    if (pf.wNumbering == PFN_BULLET) { pf.wNumbering = 0;          pf.dxOffset = 0;   }
    else                              { pf.wNumbering = PFN_BULLET;  pf.dxOffset = 360; }
    SendMessageW(hEdit, EM_SETPARAFORMAT, 0, (LPARAM)&pf);
}

static void RtfEd_ToggleNumbered(HWND hEdit)
{
    PARAFORMAT2 pf = {}; pf.cbSize = sizeof(pf);
    pf.dwMask = PFM_NUMBERING;
    SendMessageW(hEdit, EM_GETPARAFORMAT, 0, (LPARAM)&pf);
    pf.dwMask          = PFM_NUMBERING | PFM_OFFSET | PFM_NUMBERINGSTYLE | PFM_NUMBERINGSTART;
    if (pf.wNumbering == PFN_ARABIC) {
        pf.wNumbering       = 0;  pf.dxOffset = 0;
        pf.wNumberingStyle  = 0;  pf.wNumberingStart = 1;
    } else {
        pf.wNumbering       = PFN_ARABIC; pf.dxOffset        = 360;
        pf.wNumberingStyle  = PFNS_PERIOD; pf.wNumberingStart = 1;
    }
    SendMessageW(hEdit, EM_SETPARAFORMAT, 0, (LPARAM)&pf);
}

// ─── Image insertion ────────────────────────────────────────────────────────

enum class RtfImgFmt { Unknown, PNG, JPEG };
struct RtfImgInfo { RtfImgFmt fmt; int w, h; };

static RtfImgInfo RtfEd_ClassifyImage(const std::vector<unsigned char>& d)
{
    RtfImgInfo info = { RtfImgFmt::Unknown, 0, 0 };
    if (d.size() < 24) return info;
    // PNG: 8-byte signature, then IHDR chunk — width at [16..19], height at [20..23] (big-endian)
    if (d[0]==0x89 && d[1]==0x50 && d[2]==0x4E && d[3]==0x47 &&
        d[4]==0x0D && d[5]==0x0A && d[6]==0x1A && d[7]==0x0A) {
        info.fmt = RtfImgFmt::PNG;
        info.w = (d[16]<<24)|(d[17]<<16)|(d[18]<<8)|d[19];
        info.h = (d[20]<<24)|(d[21]<<16)|(d[22]<<8)|d[23];
        return info;
    }
    // JPEG: starts FF D8, scan for SOF0/SOF1/SOF2 marker (FF C0/C1/C2)
    if (d[0]==0xFF && d[1]==0xD8) {
        info.fmt = RtfImgFmt::JPEG;
        size_t i = 2;
        while (i + 8 < d.size()) {
            if (d[i] != 0xFF) break;
            unsigned char m = d[i+1];
            if (m == 0xC0 || m == 0xC1 || m == 0xC2) {
                info.h = (d[i+5]<<8)|d[i+6];
                info.w = (d[i+7]<<8)|d[i+8];
                return info;
            }
            if (m == 0xD9 || m == 0xD8 || m == 0x01) break;
            int segLen = (d[i+2]<<8)|d[i+3];
            i += 2 + (size_t)segLen;
        }
        return info;
    }
    return info;
}

static void RtfEd_InsertImage(HWND hwnd, HWND hEdit)
{
    wchar_t path[MAX_PATH] = {};
    OPENFILENAMEW ofn = {}; ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner   = hwnd;
    ofn.lpstrFilter = L"Image files\0*.png;*.jpg;*.jpeg\0"
                      L"PNG files (*.png)\0*.png\0"
                      L"JPEG files (*.jpg;*.jpeg)\0*.jpg;*.jpeg\0\0";
    ofn.lpstrFile   = path;
    ofn.nMaxFile    = MAX_PATH;
    ofn.Flags       = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
    ofn.lpstrTitle  = L"Insert Image";
    if (!GetOpenFileNameW(&ofn)) return;

    FILE* f = nullptr;
    if (_wfopen_s(&f, path, L"rb") != 0 || !f) {
        MessageBoxW(hwnd, L"Could not open image file.", L"Insert Image", MB_OK|MB_ICONERROR);
        return;
    }
    fseek(f, 0, SEEK_END);
    long fsz = ftell(f);
    fseek(f, 0, SEEK_SET);
    std::vector<unsigned char> raw((size_t)fsz);
    fread(raw.data(), 1, (size_t)fsz, f);
    fclose(f);

    RtfImgInfo info = RtfEd_ClassifyImage(raw);
    if (info.fmt == RtfImgFmt::Unknown || info.w <= 0 || info.h <= 0) {
        MessageBoxW(hwnd, L"Unsupported format. Please use PNG or JPEG.",
                    L"Insert Image", MB_OK|MB_ICONWARNING);
        return;
    }

    // Display size: natural size at 96 dpi (1 px = 15 twips), capped at 4 inches wide.
    int goalW = info.w * 15;
    int goalH = info.h * 15;
    if (goalW > 5760) {  // 4 inches = 5760 twips
        goalH = (int)((long long)5760 * info.h / info.w);
        goalW = 5760;
    }
    if (goalH > 11520) {
        goalW = (int)((long long)11520 * info.w / info.h);
        goalH = 11520;
    }

    // Hex-encode raw bytes (one line per 64 bytes = 128 hex chars).
    static const char hx[] = "0123456789ABCDEF";
    std::string hex;
    hex.reserve(raw.size() * 2 + raw.size() / 64 + 2);
    for (size_t i = 0; i < raw.size(); i++) {
        hex += hx[(raw[i] >> 4) & 0xF];
        hex += hx[raw[i] & 0xF];
        if ((i + 1) % 64 == 0) hex += '\n';
    }

    const char* blip = (info.fmt == RtfImgFmt::PNG) ? "\\pngblip" : "\\jpegblip";
    std::string snippet = "{\\rtf1\\ansi {\\pict";
    snippet += blip;
    snippet += "\\picw"    + std::to_string(info.w);
    snippet += "\\pich"    + std::to_string(info.h);
    snippet += "\\picwgoal" + std::to_string(goalW);
    snippet += "\\pichgoal" + std::to_string(goalH);
    snippet += "\r\n";
    snippet += hex;
    snippet += "}}";

    // Stream into the current caret / selection position.
    RtfEdStreamBuf rb = { &snippet, 0 };
    EDITSTREAM es     = { (DWORD_PTR)&rb, 0, RtfEdReadCb };
    SendMessageW(hEdit, EM_STREAMIN, SF_RTF | SFF_SELECTION, (LPARAM)&es);
    SetFocus(hEdit);
}

// ─── Char count ───────────────────────────────────────────────────────────────

static int RtfEd_PlainLen(HWND hEdit)
{
    GETTEXTLENGTHEX gtl = {};
    gtl.flags    = GTL_NUMCHARS | GTL_PRECISE;
    gtl.codepage = 1200;
    return (int)SendMessageW(hEdit, EM_GETTEXTLENGTHEX, (WPARAM)&gtl, 0);
}

// ─── Font sizes offered in the combobox ──────────────────────────────────────

static const int s_rtfFontSizes[] = { 8,9,10,11,12,14,16,18,20,22,24,28,32,36,48,72 };
static const int s_rtfFontCount   = (int)(sizeof(s_rtfFontSizes)/sizeof(s_rtfFontSizes[0]));
static const int s_rtfFontDefault = 4; // index of 12 pt

// ─── Font face enumeration ────────────────────────────────────────────────────

static int CALLBACK RtfEd_FontEnumProc(const LOGFONTW* lf, const TEXTMETRICW*, DWORD, LPARAM lp)
{
    if (lf->lfFaceName[0] == L'@') return 1; // skip vertical fonts
    std::vector<std::wstring>* fonts = (std::vector<std::wstring>*)lp;
    for (auto& f : *fonts) if (_wcsicmp(f.c_str(), lf->lfFaceName) == 0) return 1; // dedup
    fonts->push_back(lf->lfFaceName);
    return 1;
}

// ─── Internal state struct ────────────────────────────────────────────────────

struct RtfEdState {
    RtfEditorData* pData;
    int  editY;        // top of the RichEdit control (for WM_SIZE)
    int  statusH;      // height of status bar (0 when maxChars <= 0)
    int  bottomBarH;   // height of OK/Cancel row incl. padding
    int  pad;          // outer padding
    bool updatingToolbar; // re-entrancy guard for toolbar sync
};

// ─── Toolbar sync (called on EN_SELCHANGE) ────────────────────────────────────

static void RtfEd_SyncToolbar(HWND hwnd, HWND hEdit)
{
    RtfEdState* st = (RtfEdState*)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
    if (!st || st->updatingToolbar) return;
    st->updatingToolbar = true;

    // ── Char format ───────────────────────────────────────────────────────────
    CHARFORMAT2W cf = {}; cf.cbSize = sizeof(cf);
    cf.dwMask = CFM_BOLD | CFM_ITALIC | CFM_UNDERLINE | CFM_STRIKEOUT |
                CFM_SUBSCRIPT | CFM_FACE | CFM_SIZE;
    SendMessageW(hEdit, EM_GETCHARFORMAT, SCF_SELECTION, (LPARAM)&cf);

    // Update font face combo — find exact match (case-insensitive).
    HWND hFace = GetDlgItem(hwnd, IDC_RTFE_FONTFACE);
    if (hFace && cf.szFaceName[0]) {
        int cnt = (int)SendMessageW(hFace, CB_GETCOUNT, 0, 0);
        for (int i = 0; i < cnt; i++) {
            wchar_t buf[LF_FACESIZE] = {};
            SendMessageW(hFace, CB_GETLBTEXT, i, (LPARAM)buf);
            if (_wcsicmp(buf, cf.szFaceName) == 0) {
                SendMessageW(hFace, CB_SETCURSEL, (WPARAM)i, 0);
                break;
            }
        }
    }

    // Update font size combo — find closest match.
    HWND hSzCombo = GetDlgItem(hwnd, IDC_RTFE_FONTSIZE);
    if (hSzCombo && cf.yHeight > 0) {
        int pt   = cf.yHeight / 20;
        int cnt  = (int)SendMessageW(hSzCombo, CB_GETCOUNT, 0, 0);
        int best = 0, bestDiff = INT_MAX;
        for (int i = 0; i < cnt; i++) {
            wchar_t buf[16] = {};
            SendMessageW(hSzCombo, CB_GETLBTEXT, i, (LPARAM)buf);
            int diff = abs(_wtoi(buf) - pt);
            if (diff < bestDiff) { bestDiff = diff; best = i; }
        }
        SendMessageW(hSzCombo, CB_SETCURSEL, (WPARAM)best, 0);
    }

    // ── Paragraph format ──────────────────────────────────────────────────────
    PARAFORMAT2 pf = {}; pf.cbSize = sizeof(pf);
    pf.dwMask = PFM_ALIGNMENT | PFM_NUMBERING;
    SendMessageW(hEdit, EM_GETPARAFORMAT, 0, (LPARAM)&pf);

    // Update button visual states (BS_AUTOCHECKBOX | BS_PUSHLIKE buttons).
    auto setCheck = [&](int id, bool on) {
        HWND h = GetDlgItem(hwnd, id);
        if (h) SendMessageW(h, BM_SETCHECK, on ? BST_CHECKED : BST_UNCHECKED, 0);
    };

    setCheck(IDC_RTFE_BOLD,      (cf.dwEffects & CFE_BOLD)      != 0);
    setCheck(IDC_RTFE_ITALIC,    (cf.dwEffects & CFE_ITALIC)    != 0);
    setCheck(IDC_RTFE_UNDERLINE, (cf.dwEffects & CFE_UNDERLINE) != 0);
    setCheck(IDC_RTFE_STRIKE,    (cf.dwEffects & CFE_STRIKEOUT) != 0);
    setCheck(IDC_RTFE_SUBSCRIPT,   (cf.dwEffects & CFE_SUBSCRIPT)   != 0);
    setCheck(IDC_RTFE_SUPERSCRIPT, (cf.dwEffects & CFE_SUPERSCRIPT) != 0);

    setCheck(IDC_RTFE_ALIGN_L, pf.wAlignment == PFA_LEFT   || pf.wAlignment == 0);
    setCheck(IDC_RTFE_ALIGN_C, pf.wAlignment == PFA_CENTER);
    setCheck(IDC_RTFE_ALIGN_R, pf.wAlignment == PFA_RIGHT);
    setCheck(IDC_RTFE_ALIGN_J, pf.wAlignment == PFA_JUSTIFY);
    setCheck(IDC_RTFE_BULLET,   pf.wNumbering == PFN_BULLET);
    setCheck(IDC_RTFE_NUMBERED, pf.wNumbering == PFN_ARABIC);

    st->updatingToolbar = false;
}

// ─── Status bar refresh ───────────────────────────────────────────────────────

static void RtfEd_UpdateStatus(HWND hwnd, HWND hEdit, const RtfEditorData* pData)
{
    if (!pData || pData->maxChars <= 0) return;
    int left = pData->maxChars - RtfEd_PlainLen(hEdit);
    if (left < 0) left = 0;
    wchar_t buf[80];
    const wchar_t* fmt = pData->charsLeftFmt.empty() ? L"%d characters left" : pData->charsLeftFmt.c_str();
    swprintf(buf, 80, fmt, left);
    HWND hSt = GetDlgItem(hwnd, IDC_RTFE_STATUSBAR);
    if (hSt) SetWindowTextW(hSt, buf);
}

// ─── Per-toolbar button tooltip subclass ─────────────────────────────────────
// Uses the project's custom multilingual tooltip system (tooltip.h/.cpp).
// Each toolbar button/combo has its tooltip text stored as prop L"rtfeTipText".
// A shared subclass proc handles WM_MOUSEMOVE / WM_MOUSELEAVE on every control.

static bool s_rtfeTipTracking = false;
static HWND  s_rtfeTipHwnd    = NULL;

static LRESULT CALLBACK RtfEd_ToolbarBtnProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    WNDPROC prev = (WNDPROC)(LONG_PTR)GetPropW(hwnd, L"rtfeTipProc");
    if (!prev) return DefWindowProcW(hwnd, msg, wParam, lParam);

    switch (msg) {
    case WM_MOUSEMOVE:
        if (!IsTooltipVisible() || s_rtfeTipHwnd != hwnd) {
            const wchar_t* txt = (const wchar_t*)(void*)GetPropW(hwnd, L"rtfeTipText");
            if (txt && *txt) {
                RECT rc; GetWindowRect(hwnd, &rc);
                std::vector<TooltipEntry> entries = { {L"", txt} };
                ShowMultilingualTooltip(entries, rc.left, rc.bottom + 4, GetParent(hwnd));
                s_rtfeTipHwnd = hwnd;
            }
        }
        if (!s_rtfeTipTracking) {
            TRACKMOUSEEVENT tme = { sizeof(tme), TME_LEAVE, hwnd, 0 };
            TrackMouseEvent(&tme);
            s_rtfeTipTracking = true;
        }
        break;
    case WM_MOUSELEAVE:
        HideTooltip();
        s_rtfeTipHwnd     = NULL;
        s_rtfeTipTracking = false;
        break;
    case WM_NCDESTROY:
        RemovePropW(hwnd, L"rtfeTipProc");
        RemovePropW(hwnd, L"rtfeTipText");
        SetWindowLongPtrW(hwnd, GWLP_WNDPROC, (LONG_PTR)prev);
        return CallWindowProcW(prev, hwnd, msg, wParam, lParam);
    }
    return CallWindowProcW(prev, hwnd, msg, wParam, lParam);
}

static void RtfEd_SetToolTip(HWND hCtrl, const wchar_t* text)
{
    SetPropW(hCtrl, L"rtfeTipText", (HANDLE)(void*)text);
    WNDPROC prev = (WNDPROC)SetWindowLongPtrW(hCtrl, GWLP_WNDPROC, (LONG_PTR)RtfEd_ToolbarBtnProc);
    SetPropW(hCtrl, L"rtfeTipProc", (HANDLE)(void*)(LONG_PTR)prev);
}

// ─── RichEdit dll ─────────────────────────────────────────────────────────────

static HMODULE s_hRtfEditDll = NULL;

// ─── Window proc ─────────────────────────────────────────────────────────────

static LRESULT CALLBACK RtfEditorWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg)
    {
    // ── WM_CREATE ────────────────────────────────────────────────────────────
    case WM_CREATE: {
        CREATESTRUCTW*   cs    = (CREATESTRUCTW*)lParam;
        HINSTANCE        hInst = cs->hInstance;
        RtfEditorData*   pData = (RtfEditorData*)cs->lpCreateParams;

        // Allocate internal state (freed in WM_DESTROY).
        RtfEdState* st = new RtfEdState{};
        st->pData           = pData;
        st->pad             = S(8);
        st->updatingToolbar = false;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)st);

        // Initialise the project tooltip system (safe to call multiple times).
        InitTooltipSystem(hInst);

        // Load RichEdit: Msftedit.dll (4.1) preferred, Riched20.dll (2.0) fallback.
        if (!s_hRtfEditDll) {
            s_hRtfEditDll = LoadLibraryW(L"Msftedit.dll");
            if (!s_hRtfEditDll) s_hRtfEditDll = LoadLibraryW(L"Riched20.dll");
        }
        WNDCLASSEXW wce = {}; wce.cbSize = sizeof(wce);
        const wchar_t* reClass =
            (s_hRtfEditDll && GetClassInfoExW(s_hRtfEditDll, L"RICHEDIT50W", &wce))
            ? L"RICHEDIT50W" : L"RichEdit20W";

        RECT rcC; GetClientRect(hwnd, &rcC);
        const int cW  = rcC.right;
        const int pad = st->pad;
        const int bSz = S(26);   // toolbar button size
        const int bG  = S(3);    // button gap
        const int sG  = S(8);    // separator gap

        // ── Toolbar — Row 1: character formatting ─────────────────────────────
        int x = pad, row1Y = pad;

        HWND hBold = CreateWindowExW(0, L"BUTTON", L"B",
            WS_CHILD|WS_VISIBLE|BS_AUTOCHECKBOX|BS_PUSHLIKE,
            x, row1Y, bSz, bSz, hwnd, (HMENU)IDC_RTFE_BOLD, hInst, NULL);
        x += bSz + bG;

        HWND hItalic = CreateWindowExW(0, L"BUTTON", L"I",
            WS_CHILD|WS_VISIBLE|BS_AUTOCHECKBOX|BS_PUSHLIKE,
            x, row1Y, bSz, bSz, hwnd, (HMENU)IDC_RTFE_ITALIC, hInst, NULL);
        x += bSz + bG;

        HWND hUnder = CreateWindowExW(0, L"BUTTON", L"U",
            WS_CHILD|WS_VISIBLE|BS_AUTOCHECKBOX|BS_PUSHLIKE,
            x, row1Y, bSz, bSz, hwnd, (HMENU)IDC_RTFE_UNDERLINE, hInst, NULL);
        x += bSz + bG;

        CreateWindowExW(0, L"BUTTON", L"S\u0336",  // S + combining strikethrough
            WS_CHILD|WS_VISIBLE|BS_AUTOCHECKBOX|BS_PUSHLIKE,
            x, row1Y, bSz, bSz, hwnd, (HMENU)IDC_RTFE_STRIKE, hInst, NULL);
        x += bSz + sG;

        CreateWindowExW(0, L"BUTTON", L"X\u2082",  // X₂
            WS_CHILD|WS_VISIBLE|BS_AUTOCHECKBOX|BS_PUSHLIKE,
            x, row1Y, bSz+S(4), bSz, hwnd, (HMENU)IDC_RTFE_SUBSCRIPT, hInst, NULL);
        x += bSz+S(4) + bG;

        CreateWindowExW(0, L"BUTTON", L"X\u00B2",  // X²
            WS_CHILD|WS_VISIBLE|BS_AUTOCHECKBOX|BS_PUSHLIKE,
            x, row1Y, bSz+S(4), bSz, hwnd, (HMENU)IDC_RTFE_SUPERSCRIPT, hInst, NULL);
        x += bSz+S(4) + sG;

        // Font face combobox — populated with sorted system fonts.
        int faceW = S(170);
        HWND hFace = CreateWindowExW(0, L"COMBOBOX", L"",
            WS_CHILD|WS_VISIBLE|CBS_DROPDOWNLIST|WS_VSCROLL,
            x, row1Y, faceW, S(280), hwnd, (HMENU)IDC_RTFE_FONTFACE, hInst, NULL);
        {
            std::vector<std::wstring> fonts;
            LOGFONTW lf = {}; lf.lfCharSet = DEFAULT_CHARSET;
            HDC hdc = GetDC(hwnd);
            EnumFontFamiliesExW(hdc, &lf, RtfEd_FontEnumProc, (LPARAM)&fonts, 0);
            ReleaseDC(hwnd, hdc);
            std::sort(fonts.begin(), fonts.end(), [](const std::wstring& a, const std::wstring& b){
                return _wcsicmp(a.c_str(), b.c_str()) < 0;
            });
            // Insert default font at top then the rest.
            const wchar_t* defFace = L"Segoe UI";
            SendMessageW(hFace, CB_ADDSTRING, 0, (LPARAM)defFace);
            for (auto& f : fonts)
                if (_wcsicmp(f.c_str(), defFace) != 0)
                    SendMessageW(hFace, CB_ADDSTRING, 0, (LPARAM)f.c_str());
            SendMessageW(hFace, CB_SETCURSEL, 0, 0);
        }
        x += faceW + bG;

        // Font size combobox.
        int szW = S(56);
        HWND hSzCombo = CreateWindowExW(0, L"COMBOBOX", L"",
            WS_CHILD|WS_VISIBLE|CBS_DROPDOWNLIST|WS_VSCROLL,
            x, row1Y, szW, S(280), hwnd, (HMENU)IDC_RTFE_FONTSIZE, hInst, NULL);
        for (int i = 0; i < s_rtfFontCount; i++) {
            wchar_t sz[8]; swprintf(sz, 8, L"%d", s_rtfFontSizes[i]);
            SendMessageW(hSzCombo, CB_ADDSTRING, 0, (LPARAM)sz);
        }
        SendMessageW(hSzCombo, CB_SETCURSEL, s_rtfFontDefault, 0);

        // ── Toolbar — Row 2: paragraph formatting + colour ────────────────────
        x = pad;
        int row2Y = row1Y + bSz + S(4);

        CreateWindowExW(0, L"BUTTON", L"\u2261L",   // ≡L
            WS_CHILD|WS_VISIBLE|BS_AUTOCHECKBOX|BS_PUSHLIKE,
            x, row2Y, bSz+S(4), bSz, hwnd, (HMENU)IDC_RTFE_ALIGN_L, hInst, NULL);
        x += bSz+S(4) + bG;

        CreateWindowExW(0, L"BUTTON", L"\u2261C",
            WS_CHILD|WS_VISIBLE|BS_AUTOCHECKBOX|BS_PUSHLIKE,
            x, row2Y, bSz+S(4), bSz, hwnd, (HMENU)IDC_RTFE_ALIGN_C, hInst, NULL);
        x += bSz+S(4) + bG;

        CreateWindowExW(0, L"BUTTON", L"\u2261R",
            WS_CHILD|WS_VISIBLE|BS_AUTOCHECKBOX|BS_PUSHLIKE,
            x, row2Y, bSz+S(4), bSz, hwnd, (HMENU)IDC_RTFE_ALIGN_R, hInst, NULL);
        x += bSz+S(4) + bG;

        CreateWindowExW(0, L"BUTTON", L"\u2261J",   // ≡J = Justify
            WS_CHILD|WS_VISIBLE|BS_AUTOCHECKBOX|BS_PUSHLIKE,
            x, row2Y, bSz+S(4), bSz, hwnd, (HMENU)IDC_RTFE_ALIGN_J, hInst, NULL);
        x += bSz+S(4) + sG;

        CreateWindowExW(0, L"BUTTON", L"\u2022",    // • Bullet
            WS_CHILD|WS_VISIBLE|BS_AUTOCHECKBOX|BS_PUSHLIKE,
            x, row2Y, bSz+S(4), bSz, hwnd, (HMENU)IDC_RTFE_BULLET, hInst, NULL);
        x += bSz+S(4) + bG;

        CreateWindowExW(0, L"BUTTON", L"1.",
            WS_CHILD|WS_VISIBLE|BS_AUTOCHECKBOX|BS_PUSHLIKE,
            x, row2Y, bSz+S(4), bSz, hwnd, (HMENU)IDC_RTFE_NUMBERED, hInst, NULL);
        x += bSz+S(4) + sG;

        HWND hColor = CreateWindowExW(0, L"BUTTON", L"A\u25BC",  // A▼ text colour
            WS_CHILD|WS_VISIBLE|BS_PUSHBUTTON,
            x, row2Y, bSz+S(10), bSz, hwnd, (HMENU)IDC_RTFE_COLOR, hInst, NULL);
        x += bSz+S(10) + bG;

        CreateWindowExW(0, L"BUTTON", L"H\u25BC",  // H▼ highlight
            WS_CHILD|WS_VISIBLE|BS_PUSHBUTTON,
            x, row2Y, bSz+S(10), bSz, hwnd, (HMENU)IDC_RTFE_HIGHLIGHT, hInst, NULL);
        x += bSz+S(10) + sG;

        HWND hImgBtn = CreateWindowExW(0, L"BUTTON", L"\U0001F5BC",  // 🖼
            WS_CHILD|WS_VISIBLE|BS_PUSHBUTTON,
            x, row2Y, bSz+S(10), bSz, hwnd, (HMENU)IDC_RTFE_IMAGE, hInst, NULL);

        // ── RichEdit ──────────────────────────────────────────────────────────
        int editY   = row2Y + bSz + S(6);
        st->editY   = editY;

        // If char limit is set, reserve space for status bar above OK/Cancel.
        int statusH  = (pData->maxChars > 0) ? S(20) : 0;
        st->statusH  = statusH;
        int btnRowH  = S(38) + pad;
        st->bottomBarH = btnRowH + (statusH > 0 ? statusH + S(6) : 0);

        int editH = rcC.bottom - editY - S(6) - st->bottomBarH;

        HWND hEdit = CreateWindowExW(WS_EX_CLIENTEDGE, reClass, L"",
            WS_CHILD|WS_VISIBLE|WS_VSCROLL|ES_MULTILINE|ES_WANTRETURN|ES_AUTOVSCROLL,
            pad, editY, cW - 2*pad, editH,
            hwnd, (HMENU)IDC_RTFE_EDIT, hInst, NULL);

        // Char limit — only applied when maxChars > 0.
        if (pData->maxChars > 0)
            SendMessageW(hEdit, EM_EXLIMITTEXT, 0, pData->maxChars);

        // Default font: Segoe UI 12pt, auto colour (prevents black-on-black).
        CHARFORMAT2W cfD = {}; cfD.cbSize = sizeof(cfD);
        cfD.dwMask    = CFM_FACE | CFM_SIZE | CFM_CHARSET | CFM_COLOR | CFM_EFFECTS;
        cfD.dwEffects = CFE_AUTOCOLOR;
        cfD.yHeight   = s_rtfFontSizes[s_rtfFontDefault] * 20; // twips
        cfD.bCharSet  = DEFAULT_CHARSET;
        wcsncpy(cfD.szFaceName, L"Segoe UI", LF_FACESIZE - 1);
        SendMessageW(hEdit, EM_SETCHARFORMAT, SCF_ALL, (LPARAM)&cfD);
        SendMessageW(hEdit, EM_SETBKGNDCOLOR, 0, RGB(255, 255, 255));

        // Subscribe to both change and selection-change notifications.
        SendMessageW(hEdit, EM_SETEVENTMASK, 0, ENM_CHANGE | ENM_SELCHANGE);

        if (!pData->initRtf.empty()) RtfEd_StreamIn(hEdit, pData->initRtf);

        // ── Optional status bar ───────────────────────────────────────────────
        if (statusH > 0) {
            int statusY = rcC.bottom - btnRowH - statusH - S(6);
            CreateWindowExW(WS_EX_STATICEDGE, L"STATIC", L"",
                WS_CHILD|WS_VISIBLE|SS_LEFT|SS_CENTERIMAGE,
                pad, statusY, cW - 2*pad, statusH,
                hwnd, (HMENU)IDC_RTFE_STATUSBAR, hInst, NULL);
            RtfEd_UpdateStatus(hwnd, hEdit, pData);
        }

        // ── Save / Cancel (custom buttons — MeasureButtonWidth) ───────────────
        const wchar_t* okTxt  = pData->okText.empty()     ? L"Save"   : pData->okText.c_str();
        const wchar_t* canTxt = pData->cancelText.empty() ? L"Cancel" : pData->cancelText.c_str();
        int wOK  = MeasureButtonWidth(okTxt,  true);
        int wCnl = MeasureButtonWidth(canTxt, true);
        int btnY = rcC.bottom - pad - S(38);
        int startX = (cW - wOK - S(10) - wCnl) / 2;
        CreateCustomButtonWithIcon(hwnd, IDC_RTFE_OK,     okTxt,  ButtonColor::Green,
            L"imageres.dll", 89,  startX,           btnY, wOK,  S(38), hInst);
        CreateCustomButtonWithIcon(hwnd, IDC_RTFE_CANCEL, canTxt, ButtonColor::Red,
            L"shell32.dll",  131, startX+wOK+S(10), btnY, wCnl, S(38), hInst);

        // ── Custom multilingual tooltips for toolbar buttons ─────────────────
        RtfEd_SetToolTip(hBold,                                   L"Bold  (Ctrl+B)");
        RtfEd_SetToolTip(hItalic,                                 L"Italic  (Ctrl+I)");
        RtfEd_SetToolTip(hUnder,                                  L"Underline  (Ctrl+U)");
        RtfEd_SetToolTip(GetDlgItem(hwnd, IDC_RTFE_STRIKE),      L"Strikethrough");
        RtfEd_SetToolTip(GetDlgItem(hwnd, IDC_RTFE_SUBSCRIPT),   L"Subscript  (e.g. H\u2082O)");
        RtfEd_SetToolTip(GetDlgItem(hwnd, IDC_RTFE_SUPERSCRIPT), L"Superscript  (e.g. m\u00B2)");
        RtfEd_SetToolTip(hFace,                                   L"Font face");
        RtfEd_SetToolTip(hSzCombo,                                L"Font size (pt)");
        RtfEd_SetToolTip(hColor,                                  L"Text colour");
        RtfEd_SetToolTip(GetDlgItem(hwnd, IDC_RTFE_HIGHLIGHT),   L"Highlight colour");
        RtfEd_SetToolTip(GetDlgItem(hwnd, IDC_RTFE_ALIGN_L),     L"Align left");
        RtfEd_SetToolTip(GetDlgItem(hwnd, IDC_RTFE_ALIGN_C),     L"Align centre");
        RtfEd_SetToolTip(GetDlgItem(hwnd, IDC_RTFE_ALIGN_R),     L"Align right");
        RtfEd_SetToolTip(GetDlgItem(hwnd, IDC_RTFE_ALIGN_J),     L"Justify");
        RtfEd_SetToolTip(GetDlgItem(hwnd, IDC_RTFE_BULLET),      L"Bullet list");
        RtfEd_SetToolTip(GetDlgItem(hwnd, IDC_RTFE_NUMBERED),    L"Numbered list");
        RtfEd_SetToolTip(hImgBtn,                                 L"Insert image  (PNG / JPEG)");

        // ── Fonts ─────────────────────────────────────────────────────────────
        NONCLIENTMETRICSW ncm = {}; ncm.cbSize = sizeof(ncm);
        SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(ncm), &ncm, 0);
        if (ncm.lfMessageFont.lfHeight < 0)
            ncm.lfMessageFont.lfHeight = (LONG)(ncm.lfMessageFont.lfHeight * 1.2f);
        ncm.lfMessageFont.lfQuality = CLEARTYPE_QUALITY;
        HFONT hF = CreateFontIndirectW(&ncm.lfMessageFont);
        if (hF) {
            // Apply to all children except the RichEdit (manages its own font).
            auto applyFont = [](HWND hC, LPARAM lp) -> BOOL {
                wchar_t cls[64] = {};
                GetClassNameW(hC, cls, 64);
                if (_wcsicmp(cls, L"RichEdit20W")  != 0 &&
                    _wcsicmp(cls, L"RICHEDIT50W") != 0)
                    SendMessageW(hC, WM_SETFONT, lp, TRUE);
                return TRUE;
            };
            EnumChildWindows(hwnd, applyFont, (LPARAM)hF);

            // Bold font for the "B" button.
            NONCLIENTMETRICSW ncmB = ncm; ncmB.lfMessageFont.lfWeight = FW_BOLD;
            HFONT hFB = CreateFontIndirectW(&ncmB.lfMessageFont);
            if (hFB) {
                SendMessageW(hBold, WM_SETFONT, (WPARAM)hFB, TRUE);
                SetPropW(hwnd, L"rtfeBoldFont", hFB);
            }
            // Italic font for the "I" button.
            NONCLIENTMETRICSW ncmI = ncm; ncmI.lfMessageFont.lfItalic = TRUE;
            HFONT hFI = CreateFontIndirectW(&ncmI.lfMessageFont);
            if (hFI) {
                SendMessageW(hItalic, WM_SETFONT, (WPARAM)hFI, TRUE);
                SetPropW(hwnd, L"rtfeItalicFont", hFI);
            }
            // Strikethrough font for the "S̶" button.
            NONCLIENTMETRICSW ncmS = ncm; ncmS.lfMessageFont.lfStrikeOut = TRUE;
            HFONT hFS = CreateFontIndirectW(&ncmS.lfMessageFont);
            if (hFS) {
                SendMessageW(GetDlgItem(hwnd, IDC_RTFE_STRIKE), WM_SETFONT, (WPARAM)hFS, TRUE);
                SetPropW(hwnd, L"rtfeStrikeFont", hFS);
            }
            SetPropW(hwnd, L"rtfeFont", hF);
        }

        SetFocus(hEdit);
        // Initial toolbar state sync.
        RtfEd_SyncToolbar(hwnd, hEdit);
        return 0;
    }

    // ── WM_SIZE — resize the RichEdit and recentre OK/Cancel ─────────────────
    case WM_SIZE: {
        RtfEdState* st = (RtfEdState*)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
        if (!st) break;
        int cW = LOWORD(lParam), cH = HIWORD(lParam);
        int pad = st->pad;

        HWND hEdit = GetDlgItem(hwnd, IDC_RTFE_EDIT);
        HWND hStat = GetDlgItem(hwnd, IDC_RTFE_STATUSBAR);
        HWND hOK   = GetDlgItem(hwnd, IDC_RTFE_OK);
        HWND hCnl  = GetDlgItem(hwnd, IDC_RTFE_CANCEL);

        int bottomBarH = st->bottomBarH;
        int statusH    = st->statusH;

        // Edit control fills the gap.
        int editH = cH - st->editY - S(6) - bottomBarH;
        if (hEdit && editH > 0)
            SetWindowPos(hEdit, NULL, pad, st->editY, cW - 2*pad, editH, SWP_NOZORDER);

        // Status bar (optional).
        if (hStat && statusH > 0) {
            int statusY = cH - S(38) - pad - S(6) - statusH;
            SetWindowPos(hStat, NULL, pad, statusY, cW - 2*pad, statusH, SWP_NOZORDER);
        }

        // OK / Cancel — recalculate widths and recentre.
        if (hOK && hCnl) {
            RECT rOK = {}, rCnl = {};
            GetWindowRect(hOK,  &rOK);
            GetWindowRect(hCnl, &rCnl);
            int wOK  = rOK.right  - rOK.left;
            int wCnl = rCnl.right - rCnl.left;
            int startX = (cW - wOK - S(10) - wCnl) / 2;
            int btnY   = cH - pad - S(38);
            SetWindowPos(hOK,  NULL, startX,           btnY, 0, 0, SWP_NOZORDER|SWP_NOSIZE);
            SetWindowPos(hCnl, NULL, startX+wOK+S(10), btnY, 0, 0, SWP_NOZORDER|SWP_NOSIZE);
        }
        return 0;
    }

    // ── WM_COMMAND ────────────────────────────────────────────────────────────
    case WM_COMMAND: {
        RtfEdState*    st    = (RtfEdState*)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
        RtfEditorData* pData = st ? st->pData : nullptr;
        HWND hEdit = GetDlgItem(hwnd, IDC_RTFE_EDIT);
        int  wmId  = LOWORD(wParam);
        int  wmEv  = HIWORD(wParam);

        // ── Save ──────────────────────────────────────────────────────────────
        if (wmId == IDC_RTFE_OK) {
            if (pData) {
                pData->outRtf    = hEdit ? RtfEd_StreamOut(hEdit) : L"";
                pData->okClicked = true;
            }
            DestroyWindow(hwnd); return 0;
        }

        // ── Cancel ────────────────────────────────────────────────────────────
        if (wmId == IDC_RTFE_CANCEL || wmId == IDCANCEL) {
            DestroyWindow(hwnd); return 0;
        }

        if (!hEdit) break;

        // ── Character format toggles ──────────────────────────────────────────
        if (wmId == IDC_RTFE_BOLD) {
            CHARRANGE cr = {}; SendMessageW(hEdit, EM_EXGETSEL, 0, (LPARAM)&cr);
            RtfEd_ToggleEffect(hEdit, CFM_BOLD, CFE_BOLD);
            SendMessageW(hEdit, EM_EXSETSEL, 0, (LPARAM)&cr);
            RtfEd_SyncToolbar(hwnd, hEdit); SetFocus(hEdit); return 0;
        }
        if (wmId == IDC_RTFE_ITALIC) {
            CHARRANGE cr = {}; SendMessageW(hEdit, EM_EXGETSEL, 0, (LPARAM)&cr);
            RtfEd_ToggleEffect(hEdit, CFM_ITALIC, CFE_ITALIC);
            SendMessageW(hEdit, EM_EXSETSEL, 0, (LPARAM)&cr);
            RtfEd_SyncToolbar(hwnd, hEdit); SetFocus(hEdit); return 0;
        }
        if (wmId == IDC_RTFE_UNDERLINE) {
            CHARRANGE cr = {}; SendMessageW(hEdit, EM_EXGETSEL, 0, (LPARAM)&cr);
            RtfEd_ToggleEffect(hEdit, CFM_UNDERLINE, CFE_UNDERLINE);
            SendMessageW(hEdit, EM_EXSETSEL, 0, (LPARAM)&cr);
            RtfEd_SyncToolbar(hwnd, hEdit); SetFocus(hEdit); return 0;
        }
        if (wmId == IDC_RTFE_STRIKE) {
            CHARRANGE cr = {}; SendMessageW(hEdit, EM_EXGETSEL, 0, (LPARAM)&cr);
            RtfEd_ToggleEffect(hEdit, CFM_STRIKEOUT, CFE_STRIKEOUT);
            SendMessageW(hEdit, EM_EXSETSEL, 0, (LPARAM)&cr);
            RtfEd_SyncToolbar(hwnd, hEdit); SetFocus(hEdit); return 0;
        }
        if (wmId == IDC_RTFE_SUBSCRIPT) {
            CHARRANGE cr = {}; SendMessageW(hEdit, EM_EXGETSEL, 0, (LPARAM)&cr);
            RtfEd_ToggleScript(hEdit, CFE_SUBSCRIPT);
            SendMessageW(hEdit, EM_EXSETSEL, 0, (LPARAM)&cr);
            RtfEd_SyncToolbar(hwnd, hEdit); SetFocus(hEdit); return 0;
        }
        if (wmId == IDC_RTFE_SUPERSCRIPT) {
            CHARRANGE cr = {}; SendMessageW(hEdit, EM_EXGETSEL, 0, (LPARAM)&cr);
            RtfEd_ToggleScript(hEdit, CFE_SUPERSCRIPT);
            SendMessageW(hEdit, EM_EXSETSEL, 0, (LPARAM)&cr);
            RtfEd_SyncToolbar(hwnd, hEdit); SetFocus(hEdit); return 0;
        }

        // ── Text colour ───────────────────────────────────────────────────────
        if (wmId == IDC_RTFE_COLOR) {
            CHARRANGE cr = {}; SendMessageW(hEdit, EM_EXGETSEL, 0, (LPARAM)&cr);
            static COLORREF s_custColors[16] = {};
            CHOOSECOLORW cc = {};
            cc.lStructSize  = sizeof(cc);
            cc.hwndOwner    = hwnd;
            cc.lpCustColors = s_custColors;
            cc.Flags        = CC_FULLOPEN | CC_RGBINIT;
            CHARFORMAT2W cfC = {}; cfC.cbSize = sizeof(cfC); cfC.dwMask = CFM_COLOR;
            SendMessageW(hEdit, EM_GETCHARFORMAT, SCF_SELECTION, (LPARAM)&cfC);
            cc.rgbResult = (cfC.dwEffects & CFE_AUTOCOLOR) ? RGB(0,0,0) : cfC.crTextColor;
            if (ChooseColorW(&cc)) {
                CHARFORMAT2W cfSet = {}; cfSet.cbSize = sizeof(cfSet);
                cfSet.dwMask = CFM_COLOR; cfSet.dwEffects = 0;
                cfSet.crTextColor = cc.rgbResult;
                SendMessageW(hEdit, EM_SETCHARFORMAT, SCF_SELECTION, (LPARAM)&cfSet);
            }
            SendMessageW(hEdit, EM_EXSETSEL, 0, (LPARAM)&cr);
            SetFocus(hEdit); return 0;
        }

        // ── Highlight colour ──────────────────────────────────────────────────
        if (wmId == IDC_RTFE_HIGHLIGHT) {
            CHARRANGE cr = {}; SendMessageW(hEdit, EM_EXGETSEL, 0, (LPARAM)&cr);
            static COLORREF s_hlCustColors[16] = {};
            CHOOSECOLORW cc = {};
            cc.lStructSize  = sizeof(cc);
            cc.hwndOwner    = hwnd;
            cc.lpCustColors = s_hlCustColors;
            cc.Flags        = CC_FULLOPEN | CC_RGBINIT;
            cc.rgbResult    = RGB(255, 255, 0); // default yellow
            CHARFORMAT2W cfH = {}; cfH.cbSize = sizeof(cfH); cfH.dwMask = CFM_BACKCOLOR;
            SendMessageW(hEdit, EM_GETCHARFORMAT, SCF_SELECTION, (LPARAM)&cfH);
            if (!(cfH.dwEffects & CFE_AUTOBACKCOLOR)) cc.rgbResult = cfH.crBackColor;
            if (ChooseColorW(&cc)) {
                CHARFORMAT2W cfSet = {}; cfSet.cbSize = sizeof(cfSet);
                cfSet.dwMask    = CFM_BACKCOLOR;
                cfSet.dwEffects = 0;
                cfSet.crBackColor = cc.rgbResult;
                SendMessageW(hEdit, EM_SETCHARFORMAT, SCF_SELECTION, (LPARAM)&cfSet);
            }
            SendMessageW(hEdit, EM_EXSETSEL, 0, (LPARAM)&cr);
            SetFocus(hEdit); return 0;
        }

        // ── Alignment ─────────────────────────────────────────────────────────
        if (wmId == IDC_RTFE_ALIGN_L) { RtfEd_SetAlignment(hEdit, PFA_LEFT);    RtfEd_SyncToolbar(hwnd, hEdit); SetFocus(hEdit); return 0; }
        if (wmId == IDC_RTFE_ALIGN_C) { RtfEd_SetAlignment(hEdit, PFA_CENTER);  RtfEd_SyncToolbar(hwnd, hEdit); SetFocus(hEdit); return 0; }
        if (wmId == IDC_RTFE_ALIGN_R) { RtfEd_SetAlignment(hEdit, PFA_RIGHT);   RtfEd_SyncToolbar(hwnd, hEdit); SetFocus(hEdit); return 0; }
        if (wmId == IDC_RTFE_ALIGN_J) { RtfEd_SetAlignment(hEdit, PFA_JUSTIFY); RtfEd_SyncToolbar(hwnd, hEdit); SetFocus(hEdit); return 0; }

        // ── Lists ─────────────────────────────────────────────────────────────
        if (wmId == IDC_RTFE_BULLET)   { RtfEd_ToggleBullet(hEdit);   RtfEd_SyncToolbar(hwnd, hEdit); SetFocus(hEdit); return 0; }
        if (wmId == IDC_RTFE_NUMBERED) { RtfEd_ToggleNumbered(hEdit); RtfEd_SyncToolbar(hwnd, hEdit); SetFocus(hEdit); return 0; }

        // ── Insert image ──────────────────────────────────────────────────────
        if (wmId == IDC_RTFE_IMAGE) { RtfEd_InsertImage(hwnd, hEdit); return 0; }

        // ── Font face ─────────────────────────────────────────────────────────
        if (wmId == IDC_RTFE_FONTFACE && wmEv == CBN_SELCHANGE && !st->updatingToolbar) {
            CHARRANGE cr = {}; SendMessageW(hEdit, EM_EXGETSEL, 0, (LPARAM)&cr);
            HWND hFace = GetDlgItem(hwnd, IDC_RTFE_FONTFACE);
            int sel = (int)SendMessageW(hFace, CB_GETCURSEL, 0, 0);
            if (sel >= 0) {
                wchar_t face[LF_FACESIZE] = {};
                SendMessageW(hFace, CB_GETLBTEXT, sel, (LPARAM)face);
                CHARFORMAT2W cfF = {}; cfF.cbSize = sizeof(cfF);
                cfF.dwMask = CFM_FACE | CFM_CHARSET;
                cfF.bCharSet = DEFAULT_CHARSET;
                wcsncpy(cfF.szFaceName, face, LF_FACESIZE - 1);
                SendMessageW(hEdit, EM_SETCHARFORMAT, SCF_SELECTION, (LPARAM)&cfF);
            }
            SendMessageW(hEdit, EM_EXSETSEL, 0, (LPARAM)&cr);
            SetFocus(hEdit); return 0;
        }

        // ── Font size ─────────────────────────────────────────────────────────
        if (wmId == IDC_RTFE_FONTSIZE && wmEv == CBN_SELCHANGE && !st->updatingToolbar) {
            CHARRANGE cr = {}; SendMessageW(hEdit, EM_EXGETSEL, 0, (LPARAM)&cr);
            HWND hSzC = GetDlgItem(hwnd, IDC_RTFE_FONTSIZE);
            int sel   = (int)SendMessageW(hSzC, CB_GETCURSEL, 0, 0);
            if (sel >= 0 && sel < s_rtfFontCount) {
                CHARFORMAT2W cfSz = {}; cfSz.cbSize = sizeof(cfSz);
                cfSz.dwMask  = CFM_SIZE;
                cfSz.yHeight = s_rtfFontSizes[sel] * 20;
                SendMessageW(hEdit, EM_SETCHARFORMAT, SCF_SELECTION, (LPARAM)&cfSz);
            }
            SendMessageW(hEdit, EM_EXSETSEL, 0, (LPARAM)&cr);
            SetFocus(hEdit); return 0;
        }

        // ── RichEdit notifications ─────────────────────────────────────────────
        if (wmId == IDC_RTFE_EDIT) {
            if (wmEv == EN_CHANGE)    RtfEd_UpdateStatus(hwnd, hEdit, pData);
            if (wmEv == EN_SELCHANGE) RtfEd_SyncToolbar(hwnd, hEdit);
        }
        break;
    }

    // ── WM_KEYDOWN — Escape closes without saving ─────────────────────────────
    case WM_KEYDOWN:
        if (wParam == VK_ESCAPE) { DestroyWindow(hwnd); return 0; }
        break;

    // ── WM_DRAWITEM — custom Save / Cancel buttons ────────────────────────────
    case WM_DRAWITEM: {
        LPDRAWITEMSTRUCT dis = (LPDRAWITEMSTRUCT)lParam;
        if (dis->CtlID == IDC_RTFE_OK || dis->CtlID == IDC_RTFE_CANCEL) {
            ButtonColor col = (ButtonColor)GetWindowLongPtr(dis->hwndItem, GWLP_USERDATA);
            NONCLIENTMETRICSW ncm = {}; ncm.cbSize = sizeof(ncm);
            SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(ncm), &ncm, 0);
            if (ncm.lfMessageFont.lfHeight < 0)
                ncm.lfMessageFont.lfHeight = (LONG)(ncm.lfMessageFont.lfHeight * 1.2f);
            ncm.lfMessageFont.lfWeight  = FW_BOLD;
            ncm.lfMessageFont.lfQuality = CLEARTYPE_QUALITY;
            HFONT hFont = CreateFontIndirectW(&ncm.lfMessageFont);
            LRESULT res = DrawCustomButton(dis, col, hFont);
            if (hFont) DeleteObject(hFont);
            return res;
        }
        break;
    }

    // ── Background colours — keep toolbar and status areas white ─────────────
    case WM_CTLCOLORBTN: {
        HDC hdc = (HDC)wParam;
        SetBkColor(hdc, GetSysColor(COLOR_WINDOW));
        return (LRESULT)GetSysColorBrush(COLOR_WINDOW);
    }
    case WM_CTLCOLORSTATIC: {
        HDC hdc = (HDC)wParam;
        SetBkColor(hdc, GetSysColor(COLOR_WINDOW));
        SetTextColor(hdc, GetSysColor(COLOR_WINDOWTEXT));
        return (LRESULT)GetSysColorBrush(COLOR_WINDOW);
    }

    // ── Cleanup ───────────────────────────────────────────────────────────────
    case WM_DESTROY: {
        auto freeProp = [&](const wchar_t* name) {
            HFONT h = (HFONT)GetPropW(hwnd, name);
            if (h) { DeleteObject(h); RemovePropW(hwnd, name); }
        };
        freeProp(L"rtfeFont");
        freeProp(L"rtfeBoldFont");
        freeProp(L"rtfeItalicFont");
        freeProp(L"rtfeStrikeFont");
        // Hide the shared project tooltip if this editor was the one showing it.
        HideTooltip();
        // Internal state.
        RtfEdState* st = (RtfEdState*)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
        if (st) { delete st; SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0); }
        break;
    }

    case WM_CLOSE:
        DestroyWindow(hwnd); return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

// ─── Public API ───────────────────────────────────────────────────────────────

bool OpenRtfEditor(HWND hwndParent, RtfEditorData& data)
{
    HINSTANCE hInst = GetModuleHandleW(NULL);

    // Register window class once.
    WNDCLASSEXW wc = {}; wc.cbSize = sizeof(wc);
    if (!GetClassInfoExW(hInst, L"RtfEditorWindow", &wc)) {
        wc.lpfnWndProc   = RtfEditorWndProc;
        wc.hInstance     = hInst;
        wc.lpszClassName = L"RtfEditorWindow";
        wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
        wc.hCursor       = LoadCursorW(NULL, IDC_ARROW);
        wc.hIcon         = LoadIconW(NULL, IDI_APPLICATION);
        wc.style         = CS_DBLCLKS;
        RegisterClassExW(&wc);
    }

    int dlgW = data.preferredW > 0 ? data.preferredW : S(660);
    int dlgH = data.preferredH > 0 ? data.preferredH : S(520);

    // Centre over parent, or screen if no parent.
    int x, y;
    if (hwndParent) {
        RECT rcPar = {}; GetWindowRect(hwndParent, &rcPar);
        x = rcPar.left + (rcPar.right  - rcPar.left - dlgW) / 2;
        y = rcPar.top  + (rcPar.bottom - rcPar.top  - dlgH) / 2;
    } else {
        x = (GetSystemMetrics(SM_CXSCREEN) - dlgW) / 2;
        y = (GetSystemMetrics(SM_CYSCREEN) - dlgH) / 2;
    }

    // Clamp to work area.
    RECT rcWork = {}; SystemParametersInfoW(SPI_GETWORKAREA, 0, &rcWork, 0);
    if (x + dlgW > rcWork.right)  x = rcWork.right  - dlgW;
    if (y + dlgH > rcWork.bottom) y = rcWork.bottom - dlgH;
    if (x < rcWork.left) x = rcWork.left;
    if (y < rcWork.top)  y = rcWork.top;

    const wchar_t* title = data.titleText.empty() ? L"Edit Text" : data.titleText.c_str();

    HWND hDlg = CreateWindowExW(
        WS_EX_TOPMOST,
        L"RtfEditorWindow", title,
        WS_OVERLAPPEDWINDOW | WS_VISIBLE,
        x, y, dlgW, dlgH,
        hwndParent, NULL, hInst, &data);

    if (!hDlg) return false;

    // Modal message loop — identical to notes_editor (IsWindow guard + WM_QUIT re-post).
    MSG m;
    while (GetMessageW(&m, NULL, 0, 0) > 0) {
        if (!IsWindow(hDlg)) break;
        if (m.message == WM_QUIT) { PostQuitMessage((int)m.wParam); break; }
        TranslateMessage(&m);
        DispatchMessageW(&m);
    }
    return data.okClicked;
}
