// NSBEdit — standalone RTF notepad
// Full-featured RTF editor window with File menu, toolbar, and status bar.
// Icon: shell32.dll index 70  (set on taskbar, title bar, and status bar).
// Tooltips: English-only via project tooltip system.

#include "NSBEdit.h"
#include <windows.h>
#include <richedit.h>
#include <commdlg.h>
#include <commctrl.h>
#include <shellapi.h>
#include <string>
#include <vector>
#include <algorithm>
#include <stdio.h>
#include "../dpi.h"
#include "../tooltip.h"

// ── Control and menu IDs ───────────────────────────────────────────────────────
#define IDM_NEW             101
#define IDM_OPEN            102
#define IDM_SAVE            103
#define IDM_SAVEAS          104
#define IDM_EXIT            105

#define IDC_NE_EDIT         201
#define IDC_NE_BOLD         202
#define IDC_NE_ITALIC       203
#define IDC_NE_UNDERLINE    204
#define IDC_NE_STRIKE       205
#define IDC_NE_SUBSCRIPT    206
#define IDC_NE_SUPERSCRIPT  207
#define IDC_NE_FONTFACE     208
#define IDC_NE_FONTSIZE     209
#define IDC_NE_COLOR        210
#define IDC_NE_HIGHLIGHT    211
#define IDC_NE_ALIGN_L      212
#define IDC_NE_ALIGN_C      213
#define IDC_NE_ALIGN_R      214
#define IDC_NE_ALIGN_J      215
#define IDC_NE_BULLET       216
#define IDC_NE_NUMBERED     217
#define IDC_NE_IMAGE        218
#define IDC_NE_STATUSBAR    219

// ── Internal state ─────────────────────────────────────────────────────────────
struct NeState {
    std::wstring currentPath;
    bool  modified       = false;
    bool  suppressChange = false;
    bool  updatingToolbar = false;
    int   editY          = 0;
    int   statusH        = 0;
    int   pad            = 0;
    HICON hIconLarge     = NULL;
    HICON hIconSmall     = NULL;
};

// ── RichEdit DLL ───────────────────────────────────────────────────────────────
static HMODULE s_neRtfDll = NULL;

// ── Global main hwnd (for message-loop keyboard intercept) ─────────────────────
static HWND s_hwndMain = NULL;

// ── RTF stream helpers ─────────────────────────────────────────────────────────
struct NeStreamBuf { const std::string* src; size_t pos; };

static DWORD CALLBACK Ne_ReadCb(DWORD_PTR cookie, LPBYTE buf, LONG cb, LONG* pcb)
{
    NeStreamBuf* rb = (NeStreamBuf*)cookie;
    size_t rem = rb->src->size() - rb->pos;
    LONG   n   = (LONG)(rem < (size_t)cb ? rem : (size_t)cb);
    if (n > 0) { memcpy(buf, rb->src->c_str() + rb->pos, n); rb->pos += n; }
    *pcb = n;
    return 0;
}

static DWORD CALLBACK Ne_WriteCb(DWORD_PTR cookie, LPBYTE buf, LONG cb, LONG* pcb)
{
    std::string* s = (std::string*)cookie;
    s->append((char*)buf, cb);
    *pcb = cb;
    return 0;
}

static void Ne_StreamIn(HWND hEdit, const std::string& bytes, bool asRtf)
{
    NeStreamBuf rb = { &bytes, 0 };
    EDITSTREAM  es = { (DWORD_PTR)&rb, 0, Ne_ReadCb };
    SendMessageW(hEdit, EM_STREAMIN, asRtf ? SF_RTF : (SF_TEXT | SF_UNICODE), (LPARAM)&es);
}

static std::string Ne_StreamOut(HWND hEdit, bool asRtf)
{
    std::string s;
    EDITSTREAM  es = { (DWORD_PTR)&s, 0, Ne_WriteCb };
    SendMessageW(hEdit, EM_STREAMOUT, asRtf ? SF_RTF : (SF_TEXT | SF_UNICODE), (LPARAM)&es);
    return s;
}

// ── Character format helpers ───────────────────────────────────────────────────
static void Ne_ToggleEffect(HWND hEdit, DWORD maskBit, DWORD effectBit)
{
    CHARFORMAT2W cf = {}; cf.cbSize = sizeof(cf);
    cf.dwMask = maskBit | CFM_COLOR;
    SendMessageW(hEdit, EM_GETCHARFORMAT, SCF_SELECTION, (LPARAM)&cf);
    if (cf.dwEffects & effectBit) cf.dwEffects &= ~effectBit;
    else                          cf.dwEffects |=  effectBit;
    SendMessageW(hEdit, EM_SETCHARFORMAT, SCF_SELECTION, (LPARAM)&cf);
}

static void Ne_ToggleScript(HWND hEdit, DWORD wantBit)
{
    CHARFORMAT2W cf = {}; cf.cbSize = sizeof(cf);
    cf.dwMask = CFM_SUBSCRIPT | CFM_COLOR;
    SendMessageW(hEdit, EM_GETCHARFORMAT, SCF_SELECTION, (LPARAM)&cf);
    bool wasSet = (cf.dwEffects & wantBit) != 0;
    cf.dwEffects &= ~(CFE_SUBSCRIPT | CFE_SUPERSCRIPT);
    if (!wasSet) cf.dwEffects |= wantBit;
    SendMessageW(hEdit, EM_SETCHARFORMAT, SCF_SELECTION, (LPARAM)&cf);
}

static void Ne_SetAlignment(HWND hEdit, WORD align)
{
    PARAFORMAT2 pf = {}; pf.cbSize = sizeof(pf);
    pf.dwMask     = PFM_ALIGNMENT;
    pf.wAlignment = align;
    SendMessageW(hEdit, EM_SETPARAFORMAT, 0, (LPARAM)&pf);
}

static void Ne_ToggleBullet(HWND hEdit)
{
    PARAFORMAT2 pf = {}; pf.cbSize = sizeof(pf);
    pf.dwMask = PFM_NUMBERING;
    SendMessageW(hEdit, EM_GETPARAFORMAT, 0, (LPARAM)&pf);
    pf.dwMask     = PFM_NUMBERING | PFM_OFFSET;
    if (pf.wNumbering == PFN_BULLET) { pf.wNumbering = 0;          pf.dxOffset = 0;   }
    else                              { pf.wNumbering = PFN_BULLET;  pf.dxOffset = 360; }
    SendMessageW(hEdit, EM_SETPARAFORMAT, 0, (LPARAM)&pf);
}

static void Ne_ToggleNumbered(HWND hEdit)
{
    PARAFORMAT2 pf = {}; pf.cbSize = sizeof(pf);
    pf.dwMask = PFM_NUMBERING;
    SendMessageW(hEdit, EM_GETPARAFORMAT, 0, (LPARAM)&pf);
    pf.dwMask          = PFM_NUMBERING | PFM_OFFSET | PFM_NUMBERINGSTYLE | PFM_NUMBERINGSTART;
    if (pf.wNumbering == PFN_ARABIC) {
        pf.wNumbering = 0;          pf.dxOffset        = 0;
        pf.wNumberingStyle = 0;     pf.wNumberingStart  = 1;
    } else {
        pf.wNumbering      = PFN_ARABIC;    pf.dxOffset        = 360;
        pf.wNumberingStyle = PFNS_PERIOD;   pf.wNumberingStart = 1;
    }
    SendMessageW(hEdit, EM_SETPARAFORMAT, 0, (LPARAM)&pf);
}

// ── Image insertion (PNG / JPEG → \pict hex in RTF stream) ────────────────────
enum class NeImgFmt { Unknown, PNG, JPEG };
struct NeImgInfo { NeImgFmt fmt; int w, h; };

static NeImgInfo Ne_ClassifyImage(const std::vector<unsigned char>& d)
{
    NeImgInfo info = { NeImgFmt::Unknown, 0, 0 };
    if (d.size() < 24) return info;
    if (d[0]==0x89 && d[1]==0x50 && d[2]==0x4E && d[3]==0x47 &&
        d[4]==0x0D && d[5]==0x0A && d[6]==0x1A && d[7]==0x0A) {
        info.fmt = NeImgFmt::PNG;
        info.w = (d[16]<<24)|(d[17]<<16)|(d[18]<<8)|d[19];
        info.h = (d[20]<<24)|(d[21]<<16)|(d[22]<<8)|d[23];
        return info;
    }
    if (d[0]==0xFF && d[1]==0xD8) {
        info.fmt = NeImgFmt::JPEG;
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

static void Ne_InsertImage(HWND hwnd, HWND hEdit)
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
        MessageBoxW(hwnd, L"Could not open image file.", L"Insert Image", MB_OK | MB_ICONERROR);
        return;
    }
    fseek(f, 0, SEEK_END); long fsz = ftell(f); fseek(f, 0, SEEK_SET);
    std::vector<unsigned char> raw((size_t)fsz);
    fread(raw.data(), 1, (size_t)fsz, f);
    fclose(f);

    NeImgInfo info = Ne_ClassifyImage(raw);
    if (info.fmt == NeImgFmt::Unknown || info.w <= 0 || info.h <= 0) {
        MessageBoxW(hwnd, L"Unsupported format. Please use PNG or JPEG.", L"Insert Image", MB_OK | MB_ICONWARNING);
        return;
    }

    int goalW = info.w * 15;
    int goalH = info.h * 15;
    if (goalW > 5760) { goalH = (int)((long long)5760 * info.h / info.w); goalW = 5760; }
    if (goalH > 11520) { goalW = (int)((long long)11520 * info.w / info.h); goalH = 11520; }

    static const char hx[] = "0123456789ABCDEF";
    std::string hex;
    hex.reserve(raw.size() * 2 + raw.size() / 64 + 2);
    for (size_t i = 0; i < raw.size(); i++) {
        hex += hx[(raw[i] >> 4) & 0xF];
        hex += hx[raw[i] & 0xF];
        if ((i + 1) % 64 == 0) hex += '\n';
    }

    const char* blip = (info.fmt == NeImgFmt::PNG) ? "\\pngblip" : "\\jpegblip";
    std::string snippet = "{\\rtf1\\ansi {\\pict";
    snippet += blip;
    snippet += "\\picw"     + std::to_string(info.w);
    snippet += "\\pich"     + std::to_string(info.h);
    snippet += "\\picwgoal" + std::to_string(goalW);
    snippet += "\\pichgoal" + std::to_string(goalH);
    snippet += "\r\n" + hex + "}}";

    NeStreamBuf rb = { &snippet, 0 };
    EDITSTREAM  es = { (DWORD_PTR)&rb, 0, Ne_ReadCb };
    SendMessageW(hEdit, EM_STREAMIN, SF_RTF | SFF_SELECTION, (LPARAM)&es);
    SetFocus(hEdit);
}

// ── Font sizes ─────────────────────────────────────────────────────────────────
static const int s_neFontSizes[] = { 8,9,10,11,12,14,16,18,20,22,24,28,32,36,48,72 };
static const int s_neFontCount   = (int)(sizeof(s_neFontSizes) / sizeof(s_neFontSizes[0]));
static const int s_neFontDefault = 4; // index of 12 pt

static int CALLBACK Ne_FontEnumProc(const LOGFONTW* lf, const TEXTMETRICW*, DWORD, LPARAM lp)
{
    if (lf->lfFaceName[0] == L'@') return 1;
    std::vector<std::wstring>* fonts = (std::vector<std::wstring>*)lp;
    for (auto& f : *fonts) if (_wcsicmp(f.c_str(), lf->lfFaceName) == 0) return 1;
    fonts->push_back(lf->lfFaceName);
    return 1;
}

// ── Toolbar sync ───────────────────────────────────────────────────────────────
static void Ne_SyncToolbar(HWND hwnd, HWND hEdit)
{
    NeState* st = (NeState*)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
    if (!st || st->updatingToolbar) return;
    st->updatingToolbar = true;

    CHARFORMAT2W cf = {}; cf.cbSize = sizeof(cf);
    cf.dwMask = CFM_BOLD | CFM_ITALIC | CFM_UNDERLINE | CFM_STRIKEOUT |
                CFM_SUBSCRIPT | CFM_FACE | CFM_SIZE;
    SendMessageW(hEdit, EM_GETCHARFORMAT, SCF_SELECTION, (LPARAM)&cf);

    HWND hFace = GetDlgItem(hwnd, IDC_NE_FONTFACE);
    if (hFace && cf.szFaceName[0]) {
        int cnt = (int)SendMessageW(hFace, CB_GETCOUNT, 0, 0);
        for (int i = 0; i < cnt; i++) {
            wchar_t buf[LF_FACESIZE] = {};
            SendMessageW(hFace, CB_GETLBTEXT, i, (LPARAM)buf);
            if (_wcsicmp(buf, cf.szFaceName) == 0) { SendMessageW(hFace, CB_SETCURSEL, (WPARAM)i, 0); break; }
        }
    }

    HWND hSzC = GetDlgItem(hwnd, IDC_NE_FONTSIZE);
    if (hSzC && cf.yHeight > 0) {
        int pt = cf.yHeight / 20;
        int cnt = (int)SendMessageW(hSzC, CB_GETCOUNT, 0, 0);
        int best = 0, bestDiff = INT_MAX;
        for (int i = 0; i < cnt; i++) {
            wchar_t buf[16] = {};
            SendMessageW(hSzC, CB_GETLBTEXT, i, (LPARAM)buf);
            int diff = abs(_wtoi(buf) - pt);
            if (diff < bestDiff) { bestDiff = diff; best = i; }
        }
        SendMessageW(hSzC, CB_SETCURSEL, (WPARAM)best, 0);
    }

    PARAFORMAT2 pf = {}; pf.cbSize = sizeof(pf);
    pf.dwMask = PFM_ALIGNMENT | PFM_NUMBERING;
    SendMessageW(hEdit, EM_GETPARAFORMAT, 0, (LPARAM)&pf);

    auto setCheck = [&](int id, bool on) {
        HWND h = GetDlgItem(hwnd, id);
        if (h) SendMessageW(h, BM_SETCHECK, on ? BST_CHECKED : BST_UNCHECKED, 0);
    };

    setCheck(IDC_NE_BOLD,        (cf.dwEffects & CFE_BOLD)        != 0);
    setCheck(IDC_NE_ITALIC,      (cf.dwEffects & CFE_ITALIC)      != 0);
    setCheck(IDC_NE_UNDERLINE,   (cf.dwEffects & CFE_UNDERLINE)   != 0);
    setCheck(IDC_NE_STRIKE,      (cf.dwEffects & CFE_STRIKEOUT)   != 0);
    setCheck(IDC_NE_SUBSCRIPT,   (cf.dwEffects & CFE_SUBSCRIPT)   != 0);
    setCheck(IDC_NE_SUPERSCRIPT, (cf.dwEffects & CFE_SUPERSCRIPT) != 0);

    setCheck(IDC_NE_ALIGN_L, pf.wAlignment == PFA_LEFT || pf.wAlignment == 0);
    setCheck(IDC_NE_ALIGN_C, pf.wAlignment == PFA_CENTER);
    setCheck(IDC_NE_ALIGN_R, pf.wAlignment == PFA_RIGHT);
    setCheck(IDC_NE_ALIGN_J, pf.wAlignment == PFA_JUSTIFY);
    setCheck(IDC_NE_BULLET,   pf.wNumbering == PFN_BULLET);
    setCheck(IDC_NE_NUMBERED, pf.wNumbering == PFN_ARABIC);

    st->updatingToolbar = false;
}

// ── Title and status bar ───────────────────────────────────────────────────────
static void Ne_UpdateTitle(HWND hwnd)
{
    NeState* st = (NeState*)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
    if (!st) return;
    std::wstring title;
    if (st->modified) title += L"* ";
    if (st->currentPath.empty()) {
        title += L"Untitled \u2014 NSBEdit";
    } else {
        size_t pos = st->currentPath.find_last_of(L"\\/");
        title += (pos == std::wstring::npos ? st->currentPath : st->currentPath.substr(pos + 1));
        title += L" \u2014 NSBEdit";
    }
    SetWindowTextW(hwnd, title.c_str());
}

static void Ne_UpdateStatusText(HWND hwnd)
{
    HWND hSb = GetDlgItem(hwnd, IDC_NE_STATUSBAR);
    if (!hSb) return;
    NeState* st = (NeState*)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
    if (!st) return;
    const wchar_t* txt = st->currentPath.empty() ? L"Untitled" : st->currentPath.c_str();
    SendMessageW(hSb, SB_SETTEXT, 0, (LPARAM)txt);
}

// ── File operations ────────────────────────────────────────────────────────────
static bool Ne_PromptSaveIfModified(HWND hwnd); // forward

static bool Ne_IsRtf(const std::string& bytes)
{
    return bytes.size() >= 5 && bytes.compare(0, 5, "{\\rtf") == 0;
}

static void Ne_New(HWND hwnd)
{
    if (!Ne_PromptSaveIfModified(hwnd)) return;
    HWND hEdit = GetDlgItem(hwnd, IDC_NE_EDIT);
    NeState* st = (NeState*)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
    if (st) st->suppressChange = true;
    if (hEdit) SetWindowTextW(hEdit, L"");
    if (st) { st->suppressChange = false; st->currentPath.clear(); st->modified = false; }
    Ne_UpdateTitle(hwnd);
    Ne_UpdateStatusText(hwnd);
    if (hEdit) SetFocus(hEdit);
}

static void Ne_Open(HWND hwnd)
{
    if (!Ne_PromptSaveIfModified(hwnd)) return;
    wchar_t path[MAX_PATH] = {};
    OPENFILENAMEW ofn = {}; ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner   = hwnd;
    ofn.lpstrFilter = L"RTF files (*.rtf)\0*.rtf\0"
                      L"Text files (*.txt)\0*.txt\0"
                      L"All files (*.*)\0*.*\0\0";
    ofn.lpstrFile   = path;
    ofn.nMaxFile    = MAX_PATH;
    ofn.Flags       = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
    ofn.lpstrTitle  = L"Open";
    if (!GetOpenFileNameW(&ofn)) return;

    FILE* f = nullptr;
    if (_wfopen_s(&f, path, L"rb") != 0 || !f) {
        MessageBoxW(hwnd, L"Could not open file.", L"NSBEdit", MB_OK | MB_ICONERROR);
        return;
    }
    fseek(f, 0, SEEK_END); long sz = ftell(f); fseek(f, 0, SEEK_SET);
    std::string bytes(sz, '\0');
    if (sz > 0) fread(&bytes[0], 1, (size_t)sz, f);
    fclose(f);

    HWND hEdit = GetDlgItem(hwnd, IDC_NE_EDIT);
    NeState* st = (NeState*)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
    if (hEdit && st) {
        st->suppressChange = true;
        Ne_StreamIn(hEdit, bytes, Ne_IsRtf(bytes));
        st->suppressChange = false;
        st->currentPath = path;
        st->modified    = false;
    }
    Ne_UpdateTitle(hwnd);
    Ne_UpdateStatusText(hwnd);
    if (hEdit) SetFocus(hEdit);
}

static bool Ne_SaveToPath(HWND hwnd, const std::wstring& path)
{
    HWND hEdit = GetDlgItem(hwnd, IDC_NE_EDIT);
    if (!hEdit) return false;

    bool asRtf = true;
    size_t dot = path.rfind(L'.');
    if (dot != std::wstring::npos) {
        std::wstring ext = path.substr(dot + 1);
        for (auto& c : ext) c = (wchar_t)towlower(c);
        if (ext == L"txt") asRtf = false;
    }

    std::string bytes = Ne_StreamOut(hEdit, asRtf);
    FILE* f = nullptr;
    if (_wfopen_s(&f, path.c_str(), L"wb") != 0 || !f) {
        MessageBoxW(hwnd, L"Could not save file.", L"NSBEdit", MB_OK | MB_ICONERROR);
        return false;
    }
    fwrite(bytes.c_str(), 1, bytes.size(), f);
    fclose(f);
    return true;
}

static bool Ne_SaveAs(HWND hwnd)
{
    wchar_t path[MAX_PATH] = {};
    NeState* st = (NeState*)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
    if (st && !st->currentPath.empty())
        wcsncpy_s(path, st->currentPath.c_str(), _TRUNCATE);

    OPENFILENAMEW ofn = {}; ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner   = hwnd;
    ofn.lpstrFilter = L"RTF files (*.rtf)\0*.rtf\0"
                      L"Text files (*.txt)\0*.txt\0"
                      L"All files (*.*)\0*.*\0\0";
    ofn.lpstrFile   = path;
    ofn.nMaxFile    = MAX_PATH;
    ofn.lpstrDefExt = L"rtf";
    ofn.Flags       = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST;
    ofn.lpstrTitle  = L"Save As";
    if (!GetSaveFileNameW(&ofn)) return false;

    if (!Ne_SaveToPath(hwnd, path)) return false;
    if (st) { st->currentPath = path; st->modified = false; }
    Ne_UpdateTitle(hwnd);
    Ne_UpdateStatusText(hwnd);
    return true;
}

static bool Ne_Save(HWND hwnd)
{
    NeState* st = (NeState*)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
    if (!st || st->currentPath.empty()) return Ne_SaveAs(hwnd);
    if (!Ne_SaveToPath(hwnd, st->currentPath)) return false;
    st->modified = false;
    Ne_UpdateTitle(hwnd);
    return true;
}

static bool Ne_PromptSaveIfModified(HWND hwnd)
{
    NeState* st = (NeState*)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
    if (!st || !st->modified) return true;
    const wchar_t* name = st->currentPath.empty() ? L"Untitled" : st->currentPath.c_str();
    wchar_t msg[MAX_PATH + 64];
    swprintf_s(msg, L"Save changes to \"%s\"?", name);
    int r = MessageBoxW(hwnd, msg, L"NSBEdit", MB_YESNOCANCEL | MB_ICONQUESTION);
    if (r == IDCANCEL) return false;
    if (r == IDYES)    return Ne_Save(hwnd);
    return true; // IDNO — discard
}

// ── Tooltip subclass (English-only, project tooltip system) ───────────────────
static bool s_neTipTracking = false;
static HWND  s_neTipHwnd    = NULL;

static LRESULT CALLBACK Ne_TipSubclassProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    WNDPROC prev = (WNDPROC)(LONG_PTR)GetPropW(hwnd, L"neTipProc");
    if (!prev) return DefWindowProcW(hwnd, msg, wParam, lParam);

    switch (msg) {
    case WM_MOUSEMOVE:
        if (!IsTooltipVisible() || s_neTipHwnd != hwnd) {
            const wchar_t* txt = (const wchar_t*)(void*)GetPropW(hwnd, L"neTipText");
            if (txt && *txt) {
                RECT rc; GetWindowRect(hwnd, &rc);
                std::vector<TooltipEntry> entries = { {L"", txt} };
                ShowMultilingualTooltip(entries, rc.left, rc.bottom + 4, GetParent(hwnd));
                s_neTipHwnd = hwnd;
            }
        }
        if (!s_neTipTracking) {
            TRACKMOUSEEVENT tme = { sizeof(tme), TME_LEAVE, hwnd, 0 };
            TrackMouseEvent(&tme);
            s_neTipTracking = true;
        }
        break;
    case WM_MOUSELEAVE:
        HideTooltip();
        s_neTipHwnd     = NULL;
        s_neTipTracking = false;
        break;
    case WM_NCDESTROY:
        RemovePropW(hwnd, L"neTipProc");
        RemovePropW(hwnd, L"neTipText");
        SetWindowLongPtrW(hwnd, GWLP_WNDPROC, (LONG_PTR)prev);
        return CallWindowProcW(prev, hwnd, msg, wParam, lParam);
    }
    return CallWindowProcW(prev, hwnd, msg, wParam, lParam);
}

static void Ne_SetTip(HWND hCtrl, const wchar_t* text)
{
    SetPropW(hCtrl, L"neTipText", (HANDLE)(void*)text);
    WNDPROC prev = (WNDPROC)SetWindowLongPtrW(hCtrl, GWLP_WNDPROC, (LONG_PTR)Ne_TipSubclassProc);
    SetPropW(hCtrl, L"neTipProc", (HANDLE)(void*)(LONG_PTR)prev);
}

// ── Window procedure ───────────────────────────────────────────────────────────
static LRESULT CALLBACK Ne_WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg)
    {
    // ── WM_CREATE ─────────────────────────────────────────────────────────────
    case WM_CREATE: {
        CREATESTRUCTW* cs    = (CREATESTRUCTW*)lParam;
        HINSTANCE      hInst = cs->hInstance;

        NeState* st  = new NeState{};
        st->pad      = S(8);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)st);

        // ── Tooltip system ────────────────────────────────────────────────────
        InitTooltipSystem(hInst);

        // ── Shell32 index 70 icon ─────────────────────────────────────────────
        {
            wchar_t shell32[MAX_PATH];
            GetSystemDirectoryW(shell32, MAX_PATH);
            wcscat_s(shell32, L"\\shell32.dll");
            ExtractIconExW(shell32, 70, &st->hIconLarge, &st->hIconSmall, 1);
            if (st->hIconLarge) SendMessageW(hwnd, WM_SETICON, ICON_BIG,   (LPARAM)st->hIconLarge);
            if (st->hIconSmall) SendMessageW(hwnd, WM_SETICON, ICON_SMALL, (LPARAM)st->hIconSmall);
        }

        // ── File menu ─────────────────────────────────────────────────────────
        HMENU hMenu  = CreateMenu();
        HMENU hFile  = CreatePopupMenu();
        AppendMenuW(hFile, MF_STRING, IDM_NEW,    L"&New\tCtrl+N");
        AppendMenuW(hFile, MF_STRING, IDM_OPEN,   L"&Open...\tCtrl+O");
        AppendMenuW(hFile, MF_STRING, IDM_SAVE,   L"&Save\tCtrl+S");
        AppendMenuW(hFile, MF_STRING, IDM_SAVEAS, L"Save &As...\tCtrl+Shift+S");
        AppendMenuW(hFile, MF_SEPARATOR, 0, NULL);
        AppendMenuW(hFile, MF_STRING, IDM_EXIT,   L"E&xit");
        AppendMenuW(hMenu, MF_POPUP, (UINT_PTR)hFile, L"&File");
        SetMenu(hwnd, hMenu);

        // ── Load RichEdit DLL ─────────────────────────────────────────────────
        if (!s_neRtfDll) {
            s_neRtfDll = LoadLibraryW(L"Msftedit.dll");
            if (!s_neRtfDll) s_neRtfDll = LoadLibraryW(L"Riched20.dll");
        }
        WNDCLASSEXW wce = {}; wce.cbSize = sizeof(wce);
        const wchar_t* reClass =
            (s_neRtfDll && GetClassInfoExW(s_neRtfDll, L"RICHEDIT50W", &wce))
            ? L"RICHEDIT50W" : L"RichEdit20W";

        RECT rcC; GetClientRect(hwnd, &rcC);
        const int cW  = rcC.right;
        const int pad = st->pad;
        const int bSz = S(26);
        const int bG  = S(3);
        const int sG  = S(8);

        // ── Toolbar — Row 1: character formatting ─────────────────────────────
        int x = pad, row1Y = pad;

        HWND hBold = CreateWindowExW(0, L"BUTTON", L"B",
            WS_CHILD|WS_VISIBLE|BS_AUTOCHECKBOX|BS_PUSHLIKE,
            x, row1Y, bSz, bSz, hwnd, (HMENU)(UINT_PTR)IDC_NE_BOLD, hInst, NULL);
        x += bSz + bG;

        HWND hItalic = CreateWindowExW(0, L"BUTTON", L"I",
            WS_CHILD|WS_VISIBLE|BS_AUTOCHECKBOX|BS_PUSHLIKE,
            x, row1Y, bSz, bSz, hwnd, (HMENU)(UINT_PTR)IDC_NE_ITALIC, hInst, NULL);
        x += bSz + bG;

        HWND hUnder = CreateWindowExW(0, L"BUTTON", L"U",
            WS_CHILD|WS_VISIBLE|BS_AUTOCHECKBOX|BS_PUSHLIKE,
            x, row1Y, bSz, bSz, hwnd, (HMENU)(UINT_PTR)IDC_NE_UNDERLINE, hInst, NULL);
        x += bSz + bG;

        CreateWindowExW(0, L"BUTTON", L"S\u0336",
            WS_CHILD|WS_VISIBLE|BS_AUTOCHECKBOX|BS_PUSHLIKE,
            x, row1Y, bSz, bSz, hwnd, (HMENU)(UINT_PTR)IDC_NE_STRIKE, hInst, NULL);
        x += bSz + sG;

        CreateWindowExW(0, L"BUTTON", L"X\u2082",
            WS_CHILD|WS_VISIBLE|BS_AUTOCHECKBOX|BS_PUSHLIKE,
            x, row1Y, bSz+S(4), bSz, hwnd, (HMENU)(UINT_PTR)IDC_NE_SUBSCRIPT, hInst, NULL);
        x += bSz+S(4) + bG;

        CreateWindowExW(0, L"BUTTON", L"X\u00B2",
            WS_CHILD|WS_VISIBLE|BS_AUTOCHECKBOX|BS_PUSHLIKE,
            x, row1Y, bSz+S(4), bSz, hwnd, (HMENU)(UINT_PTR)IDC_NE_SUPERSCRIPT, hInst, NULL);
        x += bSz+S(4) + sG;

        int faceW = S(170);
        HWND hFace = CreateWindowExW(0, L"COMBOBOX", L"",
            WS_CHILD|WS_VISIBLE|CBS_DROPDOWNLIST|WS_VSCROLL,
            x, row1Y, faceW, S(280), hwnd, (HMENU)(UINT_PTR)IDC_NE_FONTFACE, hInst, NULL);
        {
            std::vector<std::wstring> fonts;
            LOGFONTW lf = {}; lf.lfCharSet = DEFAULT_CHARSET;
            HDC hdc = GetDC(hwnd);
            EnumFontFamiliesExW(hdc, &lf, Ne_FontEnumProc, (LPARAM)&fonts, 0);
            ReleaseDC(hwnd, hdc);
            std::sort(fonts.begin(), fonts.end(), [](const std::wstring& a, const std::wstring& b){
                return _wcsicmp(a.c_str(), b.c_str()) < 0;
            });
            const wchar_t* defFace = L"Segoe UI";
            SendMessageW(hFace, CB_ADDSTRING, 0, (LPARAM)defFace);
            for (auto& fn : fonts)
                if (_wcsicmp(fn.c_str(), defFace) != 0)
                    SendMessageW(hFace, CB_ADDSTRING, 0, (LPARAM)fn.c_str());
            SendMessageW(hFace, CB_SETCURSEL, 0, 0);
        }
        x += faceW + bG;

        int szW = S(56);
        HWND hSzCombo = CreateWindowExW(0, L"COMBOBOX", L"",
            WS_CHILD|WS_VISIBLE|CBS_DROPDOWNLIST|WS_VSCROLL,
            x, row1Y, szW, S(280), hwnd, (HMENU)(UINT_PTR)IDC_NE_FONTSIZE, hInst, NULL);
        for (int i = 0; i < s_neFontCount; i++) {
            wchar_t sz[8]; swprintf_s(sz, L"%d", s_neFontSizes[i]);
            SendMessageW(hSzCombo, CB_ADDSTRING, 0, (LPARAM)sz);
        }
        SendMessageW(hSzCombo, CB_SETCURSEL, s_neFontDefault, 0);

        // ── Toolbar — Row 2: paragraph + colour + image ───────────────────────
        x = pad;
        int row2Y = row1Y + bSz + S(4);

        CreateWindowExW(0, L"BUTTON", L"\u2261L",
            WS_CHILD|WS_VISIBLE|BS_AUTOCHECKBOX|BS_PUSHLIKE,
            x, row2Y, bSz+S(4), bSz, hwnd, (HMENU)(UINT_PTR)IDC_NE_ALIGN_L, hInst, NULL);
        x += bSz+S(4) + bG;

        CreateWindowExW(0, L"BUTTON", L"\u2261C",
            WS_CHILD|WS_VISIBLE|BS_AUTOCHECKBOX|BS_PUSHLIKE,
            x, row2Y, bSz+S(4), bSz, hwnd, (HMENU)(UINT_PTR)IDC_NE_ALIGN_C, hInst, NULL);
        x += bSz+S(4) + bG;

        CreateWindowExW(0, L"BUTTON", L"\u2261R",
            WS_CHILD|WS_VISIBLE|BS_AUTOCHECKBOX|BS_PUSHLIKE,
            x, row2Y, bSz+S(4), bSz, hwnd, (HMENU)(UINT_PTR)IDC_NE_ALIGN_R, hInst, NULL);
        x += bSz+S(4) + bG;

        CreateWindowExW(0, L"BUTTON", L"\u2261J",
            WS_CHILD|WS_VISIBLE|BS_AUTOCHECKBOX|BS_PUSHLIKE,
            x, row2Y, bSz+S(4), bSz, hwnd, (HMENU)(UINT_PTR)IDC_NE_ALIGN_J, hInst, NULL);
        x += bSz+S(4) + sG;

        CreateWindowExW(0, L"BUTTON", L"\u2022",
            WS_CHILD|WS_VISIBLE|BS_AUTOCHECKBOX|BS_PUSHLIKE,
            x, row2Y, bSz+S(4), bSz, hwnd, (HMENU)(UINT_PTR)IDC_NE_BULLET, hInst, NULL);
        x += bSz+S(4) + bG;

        CreateWindowExW(0, L"BUTTON", L"1.",
            WS_CHILD|WS_VISIBLE|BS_AUTOCHECKBOX|BS_PUSHLIKE,
            x, row2Y, bSz+S(4), bSz, hwnd, (HMENU)(UINT_PTR)IDC_NE_NUMBERED, hInst, NULL);
        x += bSz+S(4) + sG;

        HWND hColor = CreateWindowExW(0, L"BUTTON", L"A\u25BC",
            WS_CHILD|WS_VISIBLE|BS_PUSHBUTTON,
            x, row2Y, bSz+S(10), bSz, hwnd, (HMENU)(UINT_PTR)IDC_NE_COLOR, hInst, NULL);
        x += bSz+S(10) + bG;

        CreateWindowExW(0, L"BUTTON", L"H\u25BC",
            WS_CHILD|WS_VISIBLE|BS_PUSHBUTTON,
            x, row2Y, bSz+S(10), bSz, hwnd, (HMENU)(UINT_PTR)IDC_NE_HIGHLIGHT, hInst, NULL);
        x += bSz+S(10) + sG;

        HWND hImgBtn = CreateWindowExW(0, L"BUTTON", L"\U0001F5BC",
            WS_CHILD|WS_VISIBLE|BS_PUSHBUTTON,
            x, row2Y, bSz+S(10), bSz, hwnd, (HMENU)(UINT_PTR)IDC_NE_IMAGE, hInst, NULL);

        // ── Status bar ────────────────────────────────────────────────────────
        HWND hSb = CreateWindowExW(0, STATUSCLASSNAME, NULL,
            WS_CHILD|WS_VISIBLE|SBARS_SIZEGRIP,
            0, 0, 0, 0, hwnd, (HMENU)(UINT_PTR)IDC_NE_STATUSBAR, hInst, NULL);
        {
            int parts[1] = { -1 };
            SendMessageW(hSb, SB_SETPARTS, 1, (LPARAM)parts);
            SendMessageW(hSb, SB_SETTEXT,  0, (LPARAM)L"Untitled");
            if (st->hIconSmall)
                SendMessageW(hSb, SB_SETICON, 0, (LPARAM)st->hIconSmall);
        }
        {
            RECT rcSb; GetClientRect(hSb, &rcSb);
            st->statusH = rcSb.bottom > 0 ? rcSb.bottom : S(22);
        }

        // ── RichEdit ──────────────────────────────────────────────────────────
        int editY = row2Y + bSz + S(6);
        st->editY = editY;
        int editH = rcC.bottom - editY - st->statusH - S(2);

        HWND hEdit = CreateWindowExW(WS_EX_CLIENTEDGE, reClass, L"",
            WS_CHILD|WS_VISIBLE|WS_VSCROLL|ES_MULTILINE|ES_WANTRETURN|ES_AUTOVSCROLL,
            pad, editY, cW - 2*pad, std::max(1, editH),
            hwnd, (HMENU)(UINT_PTR)IDC_NE_EDIT, hInst, NULL);

        // Default font: Segoe UI 12pt, auto colour.
        CHARFORMAT2W cfD = {}; cfD.cbSize = sizeof(cfD);
        cfD.dwMask    = CFM_FACE | CFM_SIZE | CFM_CHARSET | CFM_COLOR | CFM_EFFECTS;
        cfD.dwEffects = CFE_AUTOCOLOR;
        cfD.yHeight   = s_neFontSizes[s_neFontDefault] * 20;
        cfD.bCharSet  = DEFAULT_CHARSET;
        wcsncpy_s(cfD.szFaceName, L"Segoe UI", LF_FACESIZE - 1);
        SendMessageW(hEdit, EM_SETCHARFORMAT, SCF_ALL, (LPARAM)&cfD);
        SendMessageW(hEdit, EM_SETBKGNDCOLOR, 0, RGB(255, 255, 255));
        SendMessageW(hEdit, EM_SETEVENTMASK, 0, ENM_CHANGE | ENM_SELCHANGE);

        // ── Apply system font to toolbar buttons / combos ─────────────────────
        NONCLIENTMETRICSW ncm = {}; ncm.cbSize = sizeof(ncm);
        SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(ncm), &ncm, 0);
        if (ncm.lfMessageFont.lfHeight < 0)
            ncm.lfMessageFont.lfHeight = (LONG)(ncm.lfMessageFont.lfHeight * 1.2f);
        ncm.lfMessageFont.lfQuality = CLEARTYPE_QUALITY;
        HFONT hF = CreateFontIndirectW(&ncm.lfMessageFont);
        if (hF) {
            auto applyFont = [](HWND hC, LPARAM lp) -> BOOL {
                wchar_t cls[64] = {};
                GetClassNameW(hC, cls, 64);
                if (_wcsicmp(cls, L"RichEdit20W") != 0 &&
                    _wcsicmp(cls, L"RICHEDIT50W") != 0)
                    SendMessageW(hC, WM_SETFONT, lp, TRUE);
                return TRUE;
            };
            EnumChildWindows(hwnd, applyFont, (LPARAM)hF);

            // Bold/Italic/Strike variants.
            NONCLIENTMETRICSW ncmB = ncm; ncmB.lfMessageFont.lfWeight = FW_BOLD;
            HFONT hFB = CreateFontIndirectW(&ncmB.lfMessageFont);
            if (hFB) { SendMessageW(hBold, WM_SETFONT, (WPARAM)hFB, TRUE); SetPropW(hwnd, L"neFontBold", hFB); }

            NONCLIENTMETRICSW ncmI = ncm; ncmI.lfMessageFont.lfItalic = TRUE;
            HFONT hFI = CreateFontIndirectW(&ncmI.lfMessageFont);
            if (hFI) { SendMessageW(hItalic, WM_SETFONT, (WPARAM)hFI, TRUE); SetPropW(hwnd, L"neFontItalic", hFI); }

            NONCLIENTMETRICSW ncmS = ncm; ncmS.lfMessageFont.lfStrikeOut = TRUE;
            HFONT hFS = CreateFontIndirectW(&ncmS.lfMessageFont);
            if (hFS) {
                SendMessageW(GetDlgItem(hwnd, IDC_NE_STRIKE), WM_SETFONT, (WPARAM)hFS, TRUE);
                SetPropW(hwnd, L"neFontStrike", hFS);
            }
            SetPropW(hwnd, L"neFont", hF);
        }

        // ── Tooltips (English only) ───────────────────────────────────────────
        Ne_SetTip(hBold,                                    L"Bold  (Ctrl+B)");
        Ne_SetTip(hItalic,                                  L"Italic  (Ctrl+I)");
        Ne_SetTip(hUnder,                                   L"Underline  (Ctrl+U)");
        Ne_SetTip(GetDlgItem(hwnd, IDC_NE_STRIKE),         L"Strikethrough");
        Ne_SetTip(GetDlgItem(hwnd, IDC_NE_SUBSCRIPT),      L"Subscript  (e.g. H\u2082O)");
        Ne_SetTip(GetDlgItem(hwnd, IDC_NE_SUPERSCRIPT),    L"Superscript  (e.g. m\u00B2)");
        Ne_SetTip(hFace,                                    L"Font face");
        Ne_SetTip(hSzCombo,                                 L"Font size (pt)");
        Ne_SetTip(hColor,                                   L"Text colour");
        Ne_SetTip(GetDlgItem(hwnd, IDC_NE_HIGHLIGHT),      L"Highlight colour");
        Ne_SetTip(GetDlgItem(hwnd, IDC_NE_ALIGN_L),        L"Align left");
        Ne_SetTip(GetDlgItem(hwnd, IDC_NE_ALIGN_C),        L"Align centre");
        Ne_SetTip(GetDlgItem(hwnd, IDC_NE_ALIGN_R),        L"Align right");
        Ne_SetTip(GetDlgItem(hwnd, IDC_NE_ALIGN_J),        L"Justify");
        Ne_SetTip(GetDlgItem(hwnd, IDC_NE_BULLET),         L"Bullet list");
        Ne_SetTip(GetDlgItem(hwnd, IDC_NE_NUMBERED),       L"Numbered list");
        Ne_SetTip(hImgBtn,                                  L"Insert image  (PNG / JPEG)");

        SetFocus(hEdit);
        Ne_SyncToolbar(hwnd, hEdit);
        return 0;
    }

    // ── WM_SIZE ───────────────────────────────────────────────────────────────
    case WM_SIZE: {
        NeState* st = (NeState*)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
        if (!st) break;
        int cW = LOWORD(lParam), cH = HIWORD(lParam);
        int pad = st->pad;

        // Reposition status bar.
        HWND hSb = GetDlgItem(hwnd, IDC_NE_STATUSBAR);
        if (hSb) {
            SendMessageW(hSb, WM_SIZE, wParam, lParam);
            RECT rcSb; GetClientRect(hSb, &rcSb);
            if (rcSb.bottom > 0) st->statusH = rcSb.bottom;
        }

        // Resize RichEdit.
        HWND hEdit = GetDlgItem(hwnd, IDC_NE_EDIT);
        int editH  = cH - st->editY - st->statusH - S(2);
        if (hEdit && editH > 0)
            SetWindowPos(hEdit, NULL, pad, st->editY, cW - 2*pad, editH, SWP_NOZORDER);
        return 0;
    }

    // ── WM_COMMAND ────────────────────────────────────────────────────────────
    case WM_COMMAND: {
        NeState* st    = (NeState*)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
        HWND     hEdit = GetDlgItem(hwnd, IDC_NE_EDIT);
        int wmId  = LOWORD(wParam);
        int wmEv  = HIWORD(wParam);

        // ── File menu ─────────────────────────────────────────────────────────
        if (wmId == IDM_NEW)    { Ne_New(hwnd);    return 0; }
        if (wmId == IDM_OPEN)   { Ne_Open(hwnd);   return 0; }
        if (wmId == IDM_SAVE)   { Ne_Save(hwnd);   return 0; }
        if (wmId == IDM_SAVEAS) { Ne_SaveAs(hwnd); return 0; }
        if (wmId == IDM_EXIT)   { PostMessageW(hwnd, WM_CLOSE, 0, 0); return 0; }

        if (!hEdit) break;

        // ── Character formatting buttons ──────────────────────────────────────
        if (wmId == IDC_NE_BOLD) {
            CHARRANGE cr = {}; SendMessageW(hEdit, EM_EXGETSEL, 0, (LPARAM)&cr);
            Ne_ToggleEffect(hEdit, CFM_BOLD, CFE_BOLD);
            SendMessageW(hEdit, EM_EXSETSEL, 0, (LPARAM)&cr);
            Ne_SyncToolbar(hwnd, hEdit); SetFocus(hEdit); return 0;
        }
        if (wmId == IDC_NE_ITALIC) {
            CHARRANGE cr = {}; SendMessageW(hEdit, EM_EXGETSEL, 0, (LPARAM)&cr);
            Ne_ToggleEffect(hEdit, CFM_ITALIC, CFE_ITALIC);
            SendMessageW(hEdit, EM_EXSETSEL, 0, (LPARAM)&cr);
            Ne_SyncToolbar(hwnd, hEdit); SetFocus(hEdit); return 0;
        }
        if (wmId == IDC_NE_UNDERLINE) {
            CHARRANGE cr = {}; SendMessageW(hEdit, EM_EXGETSEL, 0, (LPARAM)&cr);
            Ne_ToggleEffect(hEdit, CFM_UNDERLINE, CFE_UNDERLINE);
            SendMessageW(hEdit, EM_EXSETSEL, 0, (LPARAM)&cr);
            Ne_SyncToolbar(hwnd, hEdit); SetFocus(hEdit); return 0;
        }
        if (wmId == IDC_NE_STRIKE) {
            CHARRANGE cr = {}; SendMessageW(hEdit, EM_EXGETSEL, 0, (LPARAM)&cr);
            Ne_ToggleEffect(hEdit, CFM_STRIKEOUT, CFE_STRIKEOUT);
            SendMessageW(hEdit, EM_EXSETSEL, 0, (LPARAM)&cr);
            Ne_SyncToolbar(hwnd, hEdit); SetFocus(hEdit); return 0;
        }
        if (wmId == IDC_NE_SUBSCRIPT) {
            CHARRANGE cr = {}; SendMessageW(hEdit, EM_EXGETSEL, 0, (LPARAM)&cr);
            Ne_ToggleScript(hEdit, CFE_SUBSCRIPT);
            SendMessageW(hEdit, EM_EXSETSEL, 0, (LPARAM)&cr);
            Ne_SyncToolbar(hwnd, hEdit); SetFocus(hEdit); return 0;
        }
        if (wmId == IDC_NE_SUPERSCRIPT) {
            CHARRANGE cr = {}; SendMessageW(hEdit, EM_EXGETSEL, 0, (LPARAM)&cr);
            Ne_ToggleScript(hEdit, CFE_SUPERSCRIPT);
            SendMessageW(hEdit, EM_EXSETSEL, 0, (LPARAM)&cr);
            Ne_SyncToolbar(hwnd, hEdit); SetFocus(hEdit); return 0;
        }

        // ── Text colour ───────────────────────────────────────────────────────
        if (wmId == IDC_NE_COLOR) {
            CHARRANGE cr = {}; SendMessageW(hEdit, EM_EXGETSEL, 0, (LPARAM)&cr);
            static COLORREF s_custColors[16] = {};
            CHOOSECOLORW cc = {}; cc.lStructSize = sizeof(cc);
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
        if (wmId == IDC_NE_HIGHLIGHT) {
            CHARRANGE cr = {}; SendMessageW(hEdit, EM_EXGETSEL, 0, (LPARAM)&cr);
            static COLORREF s_hlColors[16] = {};
            CHOOSECOLORW cc = {}; cc.lStructSize = sizeof(cc);
            cc.hwndOwner    = hwnd;
            cc.lpCustColors = s_hlColors;
            cc.Flags        = CC_FULLOPEN | CC_RGBINIT;
            cc.rgbResult    = RGB(255, 255, 0);
            CHARFORMAT2W cfH = {}; cfH.cbSize = sizeof(cfH); cfH.dwMask = CFM_BACKCOLOR;
            SendMessageW(hEdit, EM_GETCHARFORMAT, SCF_SELECTION, (LPARAM)&cfH);
            if (!(cfH.dwEffects & CFE_AUTOBACKCOLOR)) cc.rgbResult = cfH.crBackColor;
            if (ChooseColorW(&cc)) {
                CHARFORMAT2W cfSet = {}; cfSet.cbSize = sizeof(cfSet);
                cfSet.dwMask    = CFM_BACKCOLOR; cfSet.dwEffects = 0;
                cfSet.crBackColor = cc.rgbResult;
                SendMessageW(hEdit, EM_SETCHARFORMAT, SCF_SELECTION, (LPARAM)&cfSet);
            }
            SendMessageW(hEdit, EM_EXSETSEL, 0, (LPARAM)&cr);
            SetFocus(hEdit); return 0;
        }

        // ── Alignment ─────────────────────────────────────────────────────────
        if (wmId == IDC_NE_ALIGN_L) { Ne_SetAlignment(hEdit, PFA_LEFT);    Ne_SyncToolbar(hwnd, hEdit); SetFocus(hEdit); return 0; }
        if (wmId == IDC_NE_ALIGN_C) { Ne_SetAlignment(hEdit, PFA_CENTER);  Ne_SyncToolbar(hwnd, hEdit); SetFocus(hEdit); return 0; }
        if (wmId == IDC_NE_ALIGN_R) { Ne_SetAlignment(hEdit, PFA_RIGHT);   Ne_SyncToolbar(hwnd, hEdit); SetFocus(hEdit); return 0; }
        if (wmId == IDC_NE_ALIGN_J) { Ne_SetAlignment(hEdit, PFA_JUSTIFY); Ne_SyncToolbar(hwnd, hEdit); SetFocus(hEdit); return 0; }

        // ── Lists ─────────────────────────────────────────────────────────────
        if (wmId == IDC_NE_BULLET)   { Ne_ToggleBullet(hEdit);   Ne_SyncToolbar(hwnd, hEdit); SetFocus(hEdit); return 0; }
        if (wmId == IDC_NE_NUMBERED) { Ne_ToggleNumbered(hEdit); Ne_SyncToolbar(hwnd, hEdit); SetFocus(hEdit); return 0; }

        // ── Insert image ──────────────────────────────────────────────────────
        if (wmId == IDC_NE_IMAGE) { Ne_InsertImage(hwnd, hEdit); return 0; }

        // ── Font face ─────────────────────────────────────────────────────────
        if (wmId == IDC_NE_FONTFACE && wmEv == CBN_SELCHANGE && st && !st->updatingToolbar) {
            CHARRANGE cr = {}; SendMessageW(hEdit, EM_EXGETSEL, 0, (LPARAM)&cr);
            HWND hFaceCtrl = GetDlgItem(hwnd, IDC_NE_FONTFACE);
            int sel = (int)SendMessageW(hFaceCtrl, CB_GETCURSEL, 0, 0);
            if (sel >= 0) {
                wchar_t face[LF_FACESIZE] = {};
                SendMessageW(hFaceCtrl, CB_GETLBTEXT, sel, (LPARAM)face);
                CHARFORMAT2W cfF = {}; cfF.cbSize = sizeof(cfF);
                cfF.dwMask = CFM_FACE | CFM_CHARSET;
                cfF.bCharSet = DEFAULT_CHARSET;
                wcsncpy_s(cfF.szFaceName, face, LF_FACESIZE - 1);
                SendMessageW(hEdit, EM_SETCHARFORMAT, SCF_SELECTION, (LPARAM)&cfF);
            }
            SendMessageW(hEdit, EM_EXSETSEL, 0, (LPARAM)&cr);
            SetFocus(hEdit); return 0;
        }

        // ── Font size ─────────────────────────────────────────────────────────
        if (wmId == IDC_NE_FONTSIZE && wmEv == CBN_SELCHANGE && st && !st->updatingToolbar) {
            CHARRANGE cr = {}; SendMessageW(hEdit, EM_EXGETSEL, 0, (LPARAM)&cr);
            HWND hSzCtrl = GetDlgItem(hwnd, IDC_NE_FONTSIZE);
            int sel = (int)SendMessageW(hSzCtrl, CB_GETCURSEL, 0, 0);
            if (sel >= 0 && sel < s_neFontCount) {
                CHARFORMAT2W cfSz = {}; cfSz.cbSize = sizeof(cfSz);
                cfSz.dwMask  = CFM_SIZE;
                cfSz.yHeight = s_neFontSizes[sel] * 20;
                SendMessageW(hEdit, EM_SETCHARFORMAT, SCF_SELECTION, (LPARAM)&cfSz);
            }
            SendMessageW(hEdit, EM_EXSETSEL, 0, (LPARAM)&cr);
            SetFocus(hEdit); return 0;
        }

        // ── RichEdit notifications ────────────────────────────────────────────
        if (wmId == IDC_NE_EDIT) {
            if (wmEv == EN_CHANGE && st && !st->suppressChange) {
                st->modified = true;
                Ne_UpdateTitle(hwnd);
            }
            if (wmEv == EN_SELCHANGE) Ne_SyncToolbar(hwnd, hEdit);
        }
        break;
    }

    // ── Background colours ────────────────────────────────────────────────────
    case WM_CTLCOLORBTN:
    case WM_CTLCOLORSTATIC: {
        HDC hdc = (HDC)wParam;
        SetBkColor(hdc, GetSysColor(COLOR_BTNFACE));
        SetTextColor(hdc, GetSysColor(COLOR_BTNTEXT));
        return (LRESULT)GetSysColorBrush(COLOR_BTNFACE);
    }

    // ── WM_CLOSE — prompt if modified ────────────────────────────────────────
    case WM_CLOSE:
        if (!Ne_PromptSaveIfModified(hwnd)) return 0;
        DestroyWindow(hwnd);
        return 0;

    // ── WM_DESTROY ────────────────────────────────────────────────────────────
    case WM_DESTROY: {
        auto freeProp = [&](const wchar_t* name) {
            HFONT h = (HFONT)GetPropW(hwnd, name);
            if (h) { DeleteObject(h); RemovePropW(hwnd, name); }
        };
        freeProp(L"neFont");
        freeProp(L"neFontBold");
        freeProp(L"neFontItalic");
        freeProp(L"neFontStrike");

        HideTooltip();
        CleanupTooltipSystem();

        NeState* st = (NeState*)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
        if (st) {
            if (st->hIconLarge) DestroyIcon(st->hIconLarge);
            if (st->hIconSmall) DestroyIcon(st->hIconSmall);
            delete st;
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
        }
        PostQuitMessage(0);
        return 0;
    }
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

// ── WinMain ───────────────────────────────────────────────────────────────────
int WINAPI wWinMain(HINSTANCE hInst, HINSTANCE, LPWSTR lpCmdLine, int nCmdShow)
{
    SetProcessDPIAware();
    {
        HDC hdc = GetDC(NULL);
        g_dpiScale = GetDeviceCaps(hdc, LOGPIXELSX) / 96.0f;
        ReleaseDC(NULL, hdc);
    }

    INITCOMMONCONTROLSEX icc = { sizeof(icc), ICC_WIN95_CLASSES | ICC_BAR_CLASSES };
    InitCommonControlsEx(&icc);

    WNDCLASSEXW wc  = {};
    wc.cbSize       = sizeof(wc);
    wc.lpfnWndProc  = Ne_WndProc;
    wc.hInstance    = hInst;
    wc.hCursor      = LoadCursorW(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    wc.lpszClassName = L"NSBEditWnd";
    // Window class icon loaded inside WM_CREATE via WM_SETICON.
    RegisterClassExW(&wc);

    // Centre window on primary monitor.
    int sw = GetSystemMetrics(SM_CXSCREEN);
    int sh = GetSystemMetrics(SM_CYSCREEN);
    int ww = S(800), wh = S(640);
    int wx = (sw - ww) / 2, wy = (sh - wh) / 2;

    s_hwndMain = CreateWindowExW(0, L"NSBEditWnd", L"Untitled \u2014 NSBEdit",
        WS_OVERLAPPEDWINDOW,
        wx, wy, ww, wh, NULL, NULL, hInst, NULL);

    if (!s_hwndMain) return 1;

    // If a file path was passed on the command line, open it.
    if (lpCmdLine && *lpCmdLine) {
        std::wstring arg = lpCmdLine;
        // Strip surrounding quotes if present.
        if (!arg.empty() && arg.front() == L'"') {
            arg.erase(0, 1);
            size_t q = arg.find(L'"');
            if (q != std::wstring::npos) arg.erase(q);
        }
        if (!arg.empty()) {
            FILE* f = nullptr;
            if (_wfopen_s(&f, arg.c_str(), L"rb") == 0 && f) {
                fseek(f, 0, SEEK_END); long sz = ftell(f); fseek(f, 0, SEEK_SET);
                std::string bytes((size_t)std::max(0L, sz), '\0');
                if (sz > 0) fread(&bytes[0], 1, (size_t)sz, f);
                fclose(f);
                HWND hEdit = GetDlgItem(s_hwndMain, IDC_NE_EDIT);
                NeState* st = (NeState*)GetWindowLongPtrW(s_hwndMain, GWLP_USERDATA);
                if (hEdit && st) {
                    st->suppressChange = true;
                    Ne_StreamIn(hEdit, bytes, Ne_IsRtf(bytes));
                    st->suppressChange = false;
                    st->currentPath = arg;
                    st->modified    = false;
                    Ne_UpdateTitle(s_hwndMain);
                    Ne_UpdateStatusText(s_hwndMain);
                }
            }
        }
    }

    ShowWindow(s_hwndMain, nCmdShow);
    UpdateWindow(s_hwndMain);

    MSG msg;
    while (GetMessageW(&msg, NULL, 0, 0)) {
        // Intercept Ctrl+N/O/S/Shift+S before the RichEdit consumes them.
        if (msg.message == WM_KEYDOWN && (GetKeyState(VK_CONTROL) & 0x8000)) {
            bool shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
            if (msg.wParam == 'N') { SendMessageW(s_hwndMain, WM_COMMAND, IDM_NEW, 0);    continue; }
            if (msg.wParam == 'O') { SendMessageW(s_hwndMain, WM_COMMAND, IDM_OPEN, 0);   continue; }
            if (msg.wParam == 'S') {
                SendMessageW(s_hwndMain, WM_COMMAND, shift ? IDM_SAVEAS : IDM_SAVE, 0);
                continue;
            }
        }
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    return (int)msg.wParam;
}
