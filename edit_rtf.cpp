#include "edit_rtf.h"
#include "dpi.h"
#include "button.h"
#include "tooltip.h"
#include "my_scrollbar.h"
#define _RICHEDIT_VER 0x0800   // enable TABLEROWPARMS / TABLECELLPARMS
#include <richedit.h>
#include <commdlg.h>
#include <commctrl.h>
#include <windowsx.h>   // GET_X_LPARAM / GET_Y_LPARAM
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

// ─── Locale helper ───────────────────────────────────────────────────────────
// Returns the locale string for <key> or <fb> if the map is null or key absent.
static std::wstring RtfLS(const RtfEditorData* data,
                          const wchar_t* key, const wchar_t* fb)
{
    if (!data || !data->pLocale) return fb;
    auto it = data->pLocale->find(key);
    return (it != data->pLocale->end()) ? it->second : fb;
}

static void RtfEd_InsertImage(HWND hwnd, HWND hEdit, const RtfEditorData* pD)
{
    // Build the NUL-delimited filter string from locale look-ups.
    std::wstring filterAll  = RtfLS(pD, L"rtfe_img_filter_all",  L"Image files");
    std::wstring filterPng  = RtfLS(pD, L"rtfe_img_filter_png",  L"PNG files (*.png)");
    std::wstring filterJpeg = RtfLS(pD, L"rtfe_img_filter_jpeg", L"JPEG files (*.jpg;*.jpeg)");
    std::wstring filter = filterAll  + L'\0' + L"*.png;*.jpg;*.jpeg" + L'\0'
                        + filterPng  + L'\0' + L"*.png"              + L'\0'
                        + filterJpeg + L'\0' + L"*.jpg;*.jpeg"       + L'\0' + L'\0';
    std::wstring dlgTitle = RtfLS(pD, L"rtfe_img_dlg_title", L"Insert Image");

    wchar_t path[MAX_PATH] = {};
    OPENFILENAMEW ofn = {}; ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner   = hwnd;
    ofn.lpstrFilter = filter.c_str();
    ofn.lpstrFile   = path;
    ofn.nMaxFile    = MAX_PATH;
    ofn.Flags       = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
    ofn.lpstrTitle  = dlgTitle.c_str();
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

// ─── Table insertion / properties ────────────────────────────────────────────
// Uses EM_INSERTTABLE (RICHEDIT_VER >= 0x0800) so the cells are native RichEdit
// objects with proper word-wrap.  Requires Msftedit.dll (RichEdit 4.1+).
//
// Border style codes stored in TABLECELLPARMS::dxBrdrLeft etc. use the RTF
// \brdr* mapping from MSDN (negative = RTF style index):
//  0   = no border       -1  = single (\brdrs)   -2  = thick (\brdrth)
//  -3  = double (\brdrdb) -4  = dotted (\brdrdot) -5  = dashed (\brdrdash)
// We store these as app-level indices 0..5 and map before calling EM_INSERTTABLE.

// --- helpers ---

// Convert our 0-based border-style UI index to TABLECELLPARMS border px value.
// RichEdit expects negative values for styled borders; 0 = none.
static SHORT RtfEd_BorderStylePx(int uiIdx, int widthPx)
{
    // uiIdx: 0=none 1=single 2=thick 3=double 4=dotted 5=dashed
    if (uiIdx == 0 || widthPx == 0) return 0;
    // RichEdit uses negative short codes; magnitude is style, not width.
    // Actual width is encoded separately per MSDN (it uses width in twips).
    // We return width in twips (1 px = 15 twips at 96 DPI, but we cap at 32767).
    SHORT twips = (SHORT)(widthPx * 15);
    return (twips > 0) ? twips : 1;
}

struct RtfTableParams {
    int  rows     = 2;
    int  cols     = 2;
    int  widthPct = 100;   // table width as % of editor (used to compute auto cell width)
    int  borderW  = 1;     // border width in px
    int  borderStyle = 1;  // 0=none 1=single 2=thick 3=double 4=dotted 5=dashed
    COLORREF borderColor = RGB(0,0,0);
    int  rowHeightPx = 0;  // 0 = auto
    int  colWidthPx  = 0;   // 0 = auto-derive; >0 = absolute px per column (fixed)
    int  colWidthPct = 50;  // 0 = not in pct-mode; >0 = % of editor width at apply time
    // colWidthPct and colWidthPx are mutually exclusive (colWidthPct takes priority).
    // Pct mode recalculates the absolute cell width from the current editor width each
    // time the table is inserted or edited — so the user's "50%" always means half the
    // current editor width, not half the editor width at the time the table was created.
    int  hAlign   = 0;     // 0=left 1=center 2=right  (table row alignment on page)
    int  vAlign   = 0;     // 0=top  1=middle 2=bottom (cell vertical alignment)
    int  cellHAlign = 0;   // 0=left 1=center 2=right  (cell content text alignment)
};

// Global table dialog border colour (persists across opens).
static COLORREF s_tblBorderColor = RGB(0, 0, 0);
static COLORREF s_tblCustomColors[16] = {};

// ─── Per-RichEdit state for table live resize ─────────────────────────────────
// Allocated on the heap and stored as window prop L"rtfeSt" on the subclassed
// RichEdit.  Enables proportional re-scaling of table rows when the editor
// is resized.
struct RtfEditState {
    int colWidthPct = 0;  // >0: pct-mode column width — recalculated on resize
    int cols        = 0;  // column count; 0 = no pct-mode table, don't auto-resize
};

// ─── Table-properties dialog ─────────────────────────────────────────────────
// A self-contained Win32 dialog that collects table parameters.
// Pass a pointer to RtfTableParams in CREATESTRUCT::lpCreateParams;
// it is filled on OK and the dialog is a modal loop caller drives.

struct TblDlgState {
    RtfTableParams* p;       // in/out
    bool  isEdit;            // true = editing existing table (hides rows/cols)
    HBRUSH hBrColorBtn;      // for the colour preview button
    int   edWidthPx;         // editor client width for px<->pct conversion
    bool  unitIsPct;         // true = % mode active; false = px mode
    HWND  hColWSpin;         // updown buddy for the column-width edit (range updated on unit change)
};

static LRESULT CALLBACK TblDlgProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    TblDlgState* st = (TblDlgState*)(LONG_PTR)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
    switch (msg) {
    case WM_CREATE: {
        CREATESTRUCTW* cs = (CREATESTRUCTW*)lParam;
        st = (TblDlgState*)cs->lpCreateParams;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)st);
        st->hBrColorBtn = CreateSolidBrush(st->p->borderColor);

        HINSTANCE hi = cs->hInstance;
        int x = S(10), y = S(10);
        const int lw = S(130), ew = S(60), eh = S(22), gap = S(6), row = eh + gap;

        auto L = [&](const wchar_t* t, int cx, int cy, int cw, int ch) {
            CreateWindowExW(0, L"STATIC", t, WS_CHILD|WS_VISIBLE|SS_LEFT,
                cx, cy + S(3), cw, ch - S(3), hwnd, NULL, hi, NULL);
        };
        auto E = [&](int id, int cx, int cy, int cw, int ch, const wchar_t* t = L"") {
            return CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", t,
                WS_CHILD|WS_VISIBLE|ES_NUMBER|ES_AUTOHSCROLL,
                cx, cy, cw, ch, hwnd, (HMENU)(LONG_PTR)id, hi, NULL);
        };
        auto Sp = [&](int editId, int cx, int cy, HWND hEdit, int lo, int hi2) {
            HWND hSpin = CreateWindowExW(0, UPDOWN_CLASS, NULL,
                WS_CHILD|WS_VISIBLE|UDS_ARROWKEYS|UDS_SETBUDDYINT|UDS_ALIGNRIGHT,
                0, 0, 0, 0, hwnd, NULL, hi, NULL);
            SendMessageW(hSpin, UDM_SETBUDDY,    (WPARAM)hEdit, 0);
            SendMessageW(hSpin, UDM_SETRANGE32,  lo, hi2);
            SendMessageW(hSpin, UDM_SETPOS32,    0,
                (LPARAM)*(int*)((char*)st->p + (editId == IDC_RTFE_TD_ROWS ? offsetof(RtfTableParams,rows)
                              : editId == IDC_RTFE_TD_COLS  ? offsetof(RtfTableParams,cols)
                              : editId == IDC_RTFE_TD_WIDTH ? offsetof(RtfTableParams,widthPct)
                              : editId == IDC_RTFE_TD_BWIDTH? offsetof(RtfTableParams,borderW)
                              : editId == IDC_RTFE_TD_COLW  ? offsetof(RtfTableParams,widthPct) /* unused */
                              : offsetof(RtfTableParams,rowHeightPx))));
            return hSpin;
        };

        int col2 = x + lw + S(6);

        if (!st->isEdit) {
            // Rows
            L(L"Rows:", x, y, lw, eh);
            HWND eRows = E(IDC_RTFE_TD_ROWS, col2, y, ew, eh);
            SetWindowTextW(eRows, std::to_wstring(st->p->rows).c_str());
            CreateWindowExW(0, UPDOWN_CLASS, NULL,
                WS_CHILD|WS_VISIBLE|UDS_ARROWKEYS|UDS_SETBUDDYINT|UDS_ALIGNRIGHT,
                0,0,0,0, hwnd, NULL, hi, NULL);
            {HWND hs = FindWindowExW(hwnd,NULL,UPDOWN_CLASS,NULL);
             SendMessageW(hs,UDM_SETBUDDY,(WPARAM)eRows,0);
             SendMessageW(hs,UDM_SETRANGE32,1,99);
             SendMessageW(hs,UDM_SETPOS32,0,(LPARAM)st->p->rows);}
            y += row;

            // Cols
            L(L"Columns:", x, y, lw, eh);
            HWND eCols = E(IDC_RTFE_TD_COLS, col2, y, ew, eh);
            SetWindowTextW(eCols, std::to_wstring(st->p->cols).c_str());
            {HWND hs2 = CreateWindowExW(0, UPDOWN_CLASS, NULL,
                WS_CHILD|WS_VISIBLE|UDS_ARROWKEYS|UDS_SETBUDDYINT|UDS_ALIGNRIGHT,
                0,0,0,0, hwnd, NULL, hi, NULL);
             SendMessageW(hs2,UDM_SETBUDDY,(WPARAM)eCols,0);
             SendMessageW(hs2,UDM_SETRANGE32,1,32);
             SendMessageW(hs2,UDM_SETPOS32,0,(LPARAM)st->p->cols);}
            y += row;
        }

        // Table width %
        L(L"Table width (%):", x, y, lw, eh);
        HWND eW = E(IDC_RTFE_TD_WIDTH, col2, y, ew, eh);
        SetWindowTextW(eW, std::to_wstring(st->p->widthPct).c_str());
        {HWND hs3 = CreateWindowExW(0, UPDOWN_CLASS, NULL,
            WS_CHILD|WS_VISIBLE|UDS_ARROWKEYS|UDS_SETBUDDYINT|UDS_ALIGNRIGHT,
            0,0,0,0, hwnd, NULL, hi, NULL);
         SendMessageW(hs3,UDM_SETBUDDY,(WPARAM)eW,0);
         SendMessageW(hs3,UDM_SETRANGE32,10,100);
         SendMessageW(hs3,UDM_SETPOS32,0,(LPARAM)st->p->widthPct);}
        y += row;

        // Row height (0=auto)
        L(L"Row height (px, 0=auto):", x, y, lw, eh);
        HWND eRH = E(IDC_RTFE_TD_ROWH, col2, y, ew, eh);
        SetWindowTextW(eRH, std::to_wstring(st->p->rowHeightPx).c_str());
        {HWND hs4 = CreateWindowExW(0, UPDOWN_CLASS, NULL,
            WS_CHILD|WS_VISIBLE|UDS_ARROWKEYS|UDS_SETBUDDYINT|UDS_ALIGNRIGHT,
            0,0,0,0, hwnd, NULL, hi, NULL);
         SendMessageW(hs4,UDM_SETBUDDY,(WPARAM)eRH,0);
         SendMessageW(hs4,UDM_SETRANGE32,0,999);
         SendMessageW(hs4,UDM_SETPOS32,0,(LPARAM)st->p->rowHeightPx);}
        y += row;

        // Column width — single field with px/% unit picker button.
        // unitIsPct tracks which mode is active (set from initial params in TblDlgState init).
        L(L"Column width (0=auto):", x, y, lw, eh);
        {
            int initVal = st->unitIsPct ? st->p->colWidthPct : st->p->colWidthPx;
            int spinHi  = st->unitIsPct ? 100 : 9999;
            HWND eCW = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT",
                std::to_wstring(initVal).c_str(),
                WS_CHILD|WS_VISIBLE|ES_NUMBER|ES_AUTOHSCROLL,
                col2, y, ew, eh, hwnd, (HMENU)IDC_RTFE_TD_COLWPX, hi, NULL);
            st->hColWSpin = CreateWindowExW(0, UPDOWN_CLASS, NULL,
                WS_CHILD|WS_VISIBLE|UDS_ARROWKEYS|UDS_SETBUDDYINT|UDS_ALIGNRIGHT,
                0,0,0,0, hwnd, NULL, hi, NULL);
            SendMessageW(st->hColWSpin, UDM_SETBUDDY,   (WPARAM)eCW, 0);
            SendMessageW(st->hColWSpin, UDM_SETRANGE32, 0, spinHi);
            SendMessageW(st->hColWSpin, UDM_SETPOS32,   0, (LPARAM)initVal);
            // Unit picker: small button showing current unit with a dropdown arrow.
            CreateWindowExW(0, L"BUTTON",
                st->unitIsPct ? L"% \u25bc" : L"px \u25bc",
                WS_CHILD|WS_VISIBLE|BS_PUSHBUTTON,
                col2 + ew + S(4), y, S(38), eh,
                hwnd, (HMENU)IDC_RTFE_TD_COLWUNIT, hi, NULL);
            (void)eCW;
        }
        y += row;

        // Border width
        L(L"Border width (px):", x, y, lw, eh);
        HWND eBW = E(IDC_RTFE_TD_BWIDTH, col2, y, ew, eh);
        SetWindowTextW(eBW, std::to_wstring(st->p->borderW).c_str());
        {HWND hs5 = CreateWindowExW(0, UPDOWN_CLASS, NULL,
            WS_CHILD|WS_VISIBLE|UDS_ARROWKEYS|UDS_SETBUDDYINT|UDS_ALIGNRIGHT,
            0,0,0,0, hwnd, NULL, hi, NULL);
         SendMessageW(hs5,UDM_SETBUDDY,(WPARAM)eBW,0);
         SendMessageW(hs5,UDM_SETRANGE32,0,20);
         SendMessageW(hs5,UDM_SETPOS32,0,(LPARAM)st->p->borderW);}
        y += row;

        // Border style
        L(L"Border style:", x, y, lw, eh);
        HWND hBS = CreateWindowExW(0, L"COMBOBOX", L"",
            WS_CHILD|WS_VISIBLE|CBS_DROPDOWNLIST|WS_VSCROLL,
            col2, y, ew + S(40), S(140), hwnd, (HMENU)IDC_RTFE_TD_BTYPE, hi, NULL);
        const wchar_t* bstyles[] = { L"None", L"Single", L"Thick", L"Double", L"Dotted", L"Dashed" };
        for (auto s : bstyles) SendMessageW(hBS, CB_ADDSTRING, 0, (LPARAM)s);
        SendMessageW(hBS, CB_SETCURSEL, st->p->borderStyle, 0);
        y += row;

        // Border colour
        L(L"Border colour:", x, y, lw, eh);
        CreateWindowExW(0, L"BUTTON", L"",
            WS_CHILD|WS_VISIBLE|BS_OWNERDRAW,
            col2, y, ew, eh, hwnd, (HMENU)IDC_RTFE_TD_BCOLOR, hi, NULL);
        y += row;

        // Table alignment (row on page)
        L(L"Table alignment:", x, y, lw, eh);
        HWND hHA = CreateWindowExW(0, L"COMBOBOX", L"",
            WS_CHILD|WS_VISIBLE|CBS_DROPDOWNLIST|WS_VSCROLL,
            col2, y, ew + S(40), S(110), hwnd, (HMENU)IDC_RTFE_TD_HALIGN, hi, NULL);
        const wchar_t* haligns[] = { L"Left", L"Centre", L"Right" };
        for (auto s : haligns) SendMessageW(hHA, CB_ADDSTRING, 0, (LPARAM)s);
        SendMessageW(hHA, CB_SETCURSEL, st->p->hAlign, 0);
        y += row;

        // V alignment
        L(L"Cell V alignment:", x, y, lw, eh);
        HWND hVA = CreateWindowExW(0, L"COMBOBOX", L"",
            WS_CHILD|WS_VISIBLE|CBS_DROPDOWNLIST|WS_VSCROLL,
            col2, y, ew + S(40), S(110), hwnd, (HMENU)IDC_RTFE_TD_VALIGN, hi, NULL);
        const wchar_t* valigns[] = { L"Top", L"Middle", L"Bottom" };
        for (auto s : valigns) SendMessageW(hVA, CB_ADDSTRING, 0, (LPARAM)s);
        SendMessageW(hVA, CB_SETCURSEL, st->p->vAlign, 0);
        y += row;

        // Cell H alignment (text alignment in cells)
        L(L"Cell H alignment:", x, y, lw, eh);
        HWND hCHA = CreateWindowExW(0, L"COMBOBOX", L"",
            WS_CHILD|WS_VISIBLE|CBS_DROPDOWNLIST|WS_VSCROLL,
            col2, y, ew + S(40), S(110), hwnd, (HMENU)IDC_RTFE_TD_CHALIGN, hi, NULL);
        const wchar_t* chaligns[] = { L"Left", L"Centre", L"Right" };
        for (auto s : chaligns) SendMessageW(hCHA, CB_ADDSTRING, 0, (LPARAM)s);
        SendMessageW(hCHA, CB_SETCURSEL, st->p->cellHAlign, 0);
        y += row + S(4);

        // OK / Cancel
        int bw = S(80), bh = S(28);
        int totalW = col2 + ew + S(40) + x;
        int bx = (totalW - bw*2 - S(8)) / 2;
        CreateWindowExW(0, L"BUTTON", L"OK",
            WS_CHILD|WS_VISIBLE|BS_PUSHBUTTON|BS_DEFPUSHBUTTON,
            bx, y, bw, bh, hwnd, (HMENU)IDC_RTFE_TD_OK, hi, NULL);
        CreateWindowExW(0, L"BUTTON", L"Cancel",
            WS_CHILD|WS_VISIBLE|BS_PUSHBUTTON,
            bx+bw+S(8), y, bw, bh, hwnd, (HMENU)IDC_RTFE_TD_CANCEL, hi, NULL);

        // Set a NONCLIENTMETRICS font on all children
        NONCLIENTMETRICSW ncm = {}; ncm.cbSize = sizeof(ncm);
        SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(ncm), &ncm, 0);
        if (ncm.lfMessageFont.lfHeight < 0)
            ncm.lfMessageFont.lfHeight = (LONG)(ncm.lfMessageFont.lfHeight * 1.1f);
        HFONT hF = CreateFontIndirectW(&ncm.lfMessageFont);
        if (hF) {
            SetPropW(hwnd, L"tblDlgFont", hF);
            EnumChildWindows(hwnd, [](HWND hC, LPARAM lp) -> BOOL {
                SendMessageW(hC, WM_SETFONT, lp, TRUE); return TRUE;
            }, (LPARAM)hF);
        }
        return 0;
    }

    case WM_COMMAND: {
        int id = LOWORD(wParam);
        int ev = HIWORD(wParam);
        if (id == IDC_RTFE_TD_CANCEL || id == IDCANCEL) {
            DestroyWindow(hwnd); return 0;
        }

        // Unit picker button: show px / % popup and convert the current value.
        if (id == IDC_RTFE_TD_COLWUNIT && ev == BN_CLICKED) {
            HMENU hPop = CreatePopupMenu();
            AppendMenuW(hPop, MF_STRING | (!st->unitIsPct ? MF_CHECKED : 0), 1, L"px");
            AppendMenuW(hPop, MF_STRING | ( st->unitIsPct ? MF_CHECKED : 0), 2, L"%");
            HWND hBtn = GetDlgItem(hwnd, IDC_RTFE_TD_COLWUNIT);
            RECT rc; GetWindowRect(hBtn, &rc);
            int pick = TrackPopupMenu(hPop,
                TPM_LEFTALIGN | TPM_TOPALIGN | TPM_RETURNCMD | TPM_NONOTIFY,
                rc.left, rc.bottom, 0, hwnd, NULL);
            DestroyMenu(hPop);
            bool wantPct = (pick == 2);
            if (pick > 0 && wantPct != st->unitIsPct) {
                // Read current value and convert between units where possible.
                wchar_t buf[16] = {};
                GetDlgItemTextW(hwnd, IDC_RTFE_TD_COLWPX, buf, 16);
                int val = _wtoi(buf);
                if (st->edWidthPx > 0 && val > 0) {
                    val = wantPct
                        ? std::min(100,  (int)((long long)val * 100 / st->edWidthPx))  // px → %
                        : std::min(9999, (int)((long long)val * st->edWidthPx / 100)); // % → px
                } else {
                    val = 0;  // no editor size known — reset to 0 (auto)
                }
                st->unitIsPct = wantPct;
                SetWindowTextW(hBtn, wantPct ? L"% \u25bc" : L"px \u25bc");
                SendMessageW(st->hColWSpin, UDM_SETRANGE32, 0, wantPct ? 100 : 9999);
                SendMessageW(st->hColWSpin, UDM_SETPOS32,   0, (LPARAM)val);
                SetDlgItemTextW(hwnd, IDC_RTFE_TD_COLWPX, std::to_wstring(val).c_str());
            }
            break;
        }

        if (id == IDC_RTFE_TD_OK || id == IDOK) {
            // Read all fields back into st->p
            auto getInt = [&](int ctrlId, int def) -> int {
                wchar_t buf[16] = {};
                GetDlgItemTextW(hwnd, ctrlId, buf, 16);
                int v = _wtoi(buf);
                return v;
            };
            if (!st->isEdit) {
                st->p->rows  = std::max(1, std::min(99, getInt(IDC_RTFE_TD_ROWS, 2)));
                st->p->cols  = std::max(1, std::min(32, getInt(IDC_RTFE_TD_COLS, 2)));
            }
            st->p->widthPct    = std::max(10, std::min(100, getInt(IDC_RTFE_TD_WIDTH, 100)));
            st->p->rowHeightPx = std::max(0, std::min(999, getInt(IDC_RTFE_TD_ROWH, 0)));
            {
                // Save the value into the correct field based on the active unit mode.
                int val = std::max(0, getInt(IDC_RTFE_TD_COLWPX, 0));
                if (st->unitIsPct) {
                    st->p->colWidthPct = std::min(100,  val);
                    st->p->colWidthPx  = 0;
                } else {
                    st->p->colWidthPx  = std::min(9999, val);
                    st->p->colWidthPct = 0;
                }
            }
            st->p->borderW     = std::max(0, std::min(20,  getInt(IDC_RTFE_TD_BWIDTH, 1)));
            st->p->borderStyle = (int)SendDlgItemMessageW(hwnd, IDC_RTFE_TD_BTYPE,   CB_GETCURSEL, 0, 0);
            st->p->hAlign      = (int)SendDlgItemMessageW(hwnd, IDC_RTFE_TD_HALIGN,  CB_GETCURSEL, 0, 0);
            st->p->vAlign      = (int)SendDlgItemMessageW(hwnd, IDC_RTFE_TD_VALIGN,  CB_GETCURSEL, 0, 0);
            st->p->cellHAlign  = (int)SendDlgItemMessageW(hwnd, IDC_RTFE_TD_CHALIGN, CB_GETCURSEL, 0, 0);
            st->p->borderColor = s_tblBorderColor;
            st->p->rows = (st->p->rows > 0 ? st->p->rows : 2); // safety
            DestroyWindow(hwnd); return 0;
        }
        if (id == IDC_RTFE_TD_BCOLOR) {
            CHOOSECOLORW cc = {};
            cc.lStructSize  = sizeof(cc);
            cc.hwndOwner    = hwnd;
            cc.lpCustColors = s_tblCustomColors;
            cc.rgbResult    = s_tblBorderColor;
            cc.Flags        = CC_FULLOPEN | CC_RGBINIT;
            if (ChooseColorW(&cc)) {
                s_tblBorderColor = cc.rgbResult;
                if (st->hBrColorBtn) DeleteObject(st->hBrColorBtn);
                st->hBrColorBtn = CreateSolidBrush(s_tblBorderColor);
                InvalidateRect(GetDlgItem(hwnd, IDC_RTFE_TD_BCOLOR), NULL, TRUE);
            }
            return 0;
        }
        break;
    }

    case WM_DRAWITEM: {
        LPDRAWITEMSTRUCT dis = (LPDRAWITEMSTRUCT)lParam;
        if (dis->CtlID == IDC_RTFE_TD_BCOLOR) {
            HBRUSH hBr = st ? st->hBrColorBtn : NULL;
            if (!hBr) hBr = (HBRUSH)GetStockObject(BLACK_BRUSH);
            FillRect(dis->hDC, &dis->rcItem, hBr);
            FrameRect(dis->hDC, &dis->rcItem, (HBRUSH)GetStockObject(BLACK_BRUSH));
            return TRUE;
        }
        break;
    }

    case WM_KEYDOWN:
        if (wParam == VK_ESCAPE) { DestroyWindow(hwnd); return 0; }
        if (wParam == VK_RETURN)  { SendMessageW(hwnd, WM_COMMAND, IDC_RTFE_TD_OK, 0); return 0; }
        break;

    case WM_DESTROY: {
        HFONT hF = (HFONT)GetPropW(hwnd, L"tblDlgFont");
        if (hF) { DeleteObject(hF); RemovePropW(hwnd, L"tblDlgFont"); }
        if (st && st->hBrColorBtn) { DeleteObject(st->hBrColorBtn); st->hBrColorBtn = NULL; }
        break;
    }
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

// Returns true if the user clicked OK; fills *p.  Parent = editor hwnd.
// edWidthPx = editor client pixel width (used to convert col width px<->pct).
static bool RtfEd_ShowTableDialog(HWND parent, RtfTableParams* p, bool isEdit, int edWidthPx = 0)
{
    HINSTANCE hInst = GetModuleHandleW(NULL);
    s_tblBorderColor = p->borderColor;

    // Register class once.
    WNDCLASSEXW wc = {}; wc.cbSize = sizeof(wc);
    if (!GetClassInfoExW(hInst, L"RtfTblDlg", &wc)) {
        wc.lpfnWndProc   = TblDlgProc;
        wc.hInstance     = hInst;
        wc.lpszClassName = L"RtfTblDlg";
        wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
        wc.hCursor       = LoadCursorW(NULL, IDC_ARROW);
        RegisterClassExW(&wc);
    }

    const int dlgW = S(260), dlgH = isEdit ? S(382) : S(462);
    RECT rp = {}; GetWindowRect(parent, &rp);
    int cx = rp.left + (rp.right - rp.left - dlgW) / 2;
    int cy = rp.top  + (rp.bottom - rp.top  - dlgH) / 2;

    TblDlgState st{ p, isEdit, NULL, edWidthPx, (p->colWidthPct > 0), NULL };
    const wchar_t* title = isEdit ? L"Table Properties" : L"Insert Table";

    HWND hDlg = CreateWindowExW(
        WS_EX_TOPMOST | WS_EX_DLGMODALFRAME,
        L"RtfTblDlg", title,
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_VISIBLE,
        cx, cy, dlgW, dlgH,
        parent, NULL, hInst, &st);
    if (!hDlg) return false;

    // Block the editor.
    EnableWindow(parent, FALSE);

    // Simple modal loop.
    MSG m;
    while (GetMessageW(&m, NULL, 0, 0) > 0) {
        if (!IsWindow(hDlg)) break;
        if (m.message == WM_QUIT) { PostQuitMessage((int)m.wParam); break; }
        if (!IsDialogMessageW(hDlg, &m)) {
            TranslateMessage(&m);
            DispatchMessageW(&m);
        }
    }
    if (IsWindow(parent)) EnableWindow(parent, TRUE);
    SetForegroundWindow(parent);

    // OK was pressed iff all fields were written back (rows > 0 check).
    return (p->rows > 0 && !IsWindow(hDlg));
}

// ─── Build and insert table via EM_INSERTTABLE ────────────────────────────────
// Editor window width is used to compute absolute cell widths from widthPct.
static void RtfEd_InsertTableNative(HWND hwnd, HWND hEdit, const RtfTableParams& tp)
{
    // Use EM_GETRECT to get the actual text-area width (formatting rectangle),
    // which excludes RichEdit's internal left/right margin insets that
    // GetClientRect includes — using GetClientRect causes the table to be
    // fractionally wider than the visible text area ("101%" appearance).
    RECT fmtRc = {};
    SendMessageW(hEdit, EM_GETRECT, 0, (LPARAM)&fmtRc);
    int physPx = fmtRc.right - fmtRc.left;
    if (physPx < 10) { RECT cr; GetClientRect(hEdit, &cr); physPx = cr.right - cr.left; }
    const float twipsPerPhysPx = 15.0f / g_dpiScale;
    int edTwips = (int)(physPx * twipsPerPhysPx + 0.5f);

    // dxCellMargin is the text-inset padding inside each cell — it does NOT
    // add to the outer cell width.  dxWidth in TABLECELLPARMS is the outer
    // (border-to-border) cell width.  So the total table width = sum of all
    // dxWidth values; no margin overhead needs to be subtracted.
    const int dxCellMarginTwips = 4 * 15;   // 60 twips — must match row.dxCellMargin

    int cellTwips;
    if (tp.colWidthPct > 0) {
        // Percentage mode: each column = pct% of the editor text-area.
        // N columns at 50% each = 100% total.  Always recalculates from the
        // current editor width so the percentage is honoured after resize.
        cellTwips = (int)((long long)edTwips * tp.colWidthPct / 100);
    } else if (tp.colWidthPx > 0) {
        // Absolute mode: logical px value from dialog → twips (DPI-independent).
        cellTwips = tp.colWidthPx * 15;
    } else {
        // Auto: distribute the requested table width equally across columns.
        int tblTwips = (int)((long long)edTwips * tp.widthPct / 100);
        cellTwips = (tp.cols > 0) ? (tblTwips / tp.cols) : tblTwips;
    }
    if (cellTwips < 100) cellTwips = 100; // minimum

    // Build cell params array (all cells identical).
    std::vector<TABLECELLPARMS> cells((size_t)(tp.cols));
    SHORT brdr = RtfEd_BorderStylePx(tp.borderStyle, tp.borderW);
    for (auto& c : cells) {
        c.dxWidth    = cellTwips;
        c.nVertAlign = (WORD)(tp.vAlign < 3 ? tp.vAlign : 0);
        c.fMergeTop = c.fMergePrev = c.fVertical = 0;
        c.fMergeStart = c.fMergeCont = 0;
        c.wShading   = 0;
        c.dxBrdrLeft  = brdr; c.dyBrdrTop    = brdr;
        c.dxBrdrRight = brdr; c.dyBrdrBottom = brdr;
        c.crBrdrLeft  = tp.borderColor; c.crBrdrTop    = tp.borderColor;
        c.crBrdrRight = tp.borderColor; c.crBrdrBottom = tp.borderColor;
        c.crBackPat   = RGB(255,255,255);
        c.crForePat   = RGB(0,0,0);
    }

    TABLEROWPARMS row = {};
    row.cbRow      = sizeof(TABLEROWPARMS);
    row.cbCell     = sizeof(TABLECELLPARMS);
    row.cCell      = (BYTE)tp.cols;
    row.cRow       = (BYTE)tp.rows;
    row.dxCellMargin = dxCellMarginTwips;  // 4 logical px per side in twips
    row.dxIndent   = 0;
    row.dyHeight   = (tp.rowHeightPx > 0) ? (LONG)(tp.rowHeightPx * 15) : 0;
    row.nAlignment = (tp.hAlign == 1) ? 2 : (tp.hAlign == 2) ? 3 : 1; // 1=L 2=C 3=R
    row.fRTL       = 0;
    row.fKeep      = 0; row.fKeepFollow = 0;
    row.fWrap      = 1; // word-wrap cells — always on
    row.fIdentCells = 1;
    row.cpStartRow = -1;  // insert at current selection
    row.bTableLevel = 1;
    row.iCell      = 0;

    SendMessageW(hEdit, EM_INSERTTABLE, (WPARAM)&row, (LPARAM)cells.data());

    // Save pct-mode params so the WM_SIZE handler can rescale the table.
    {
        RtfEditState* es = (RtfEditState*)(LONG_PTR)GetPropW(hEdit, L"rtfeSt");
        if (es) { es->colWidthPct = tp.colWidthPct; es->cols = tp.cols; }
    }

    // Apply cell text alignment to the entire table just inserted.
    // Select forward enough chars to cover all cells, then apply.
    if (tp.cellHAlign != 0) {
        WORD pfa = (tp.cellHAlign == 1) ? PFA_CENTER : PFA_RIGHT;
        // Get the extent of the newly inserted table by querying para format.
        CHARRANGE crAll = { 0, -1 }; // will narrow via EM_GETTABLEPARMS
        TABLEROWPARMS trq = {}; trq.cbRow = sizeof(trq); trq.cpStartRow = -1;
        TABLECELLPARMS tcq = {}; tcq.dxWidth = sizeof(tcq);
        SendMessageW(hEdit, EM_GETTABLEPARMS, (WPARAM)&trq, (LPARAM)&tcq);
        // trq.cpStartRow is set to the start CP of the table row.
        if (trq.cpStartRow >= 0) {
            // Select a large range covering all rows; EM_SETPARAFORMAT clips to table.
            CHARRANGE crTbl = { trq.cpStartRow, trq.cpStartRow + 65535 };
            SendMessageW(hEdit, EM_EXSETSEL, 0, (LPARAM)&crTbl);
            PARAFORMAT2 pf = {}; pf.cbSize = sizeof(pf);
            pf.dwMask  = PFM_ALIGNMENT;
            pf.wAlignment = pfa;
            SendMessageW(hEdit, EM_SETPARAFORMAT, 0, (LPARAM)&pf);
            // Collapse selection to end.
            CHARRANGE crEnd = { crTbl.cpMin, crTbl.cpMin };
            SendMessageW(hEdit, EM_EXSETSEL, 0, (LPARAM)&crEnd);
        }
    }
    SetFocus(hEdit);
}

// ─── Update existing table properties via EM_SETTABLEPARMS ───────────────────
// Gets params from hEdit at the caret, updates all rows with new values.
static void RtfEd_ApplyTableProps(HWND hEdit, const RtfTableParams& tp)
{
    // Read current table row/cell params to know cCell/cRow.
    TABLEROWPARMS curRow = {}; curRow.cbRow = sizeof(curRow);
    TABLECELLPARMS curCell = {}; curCell.dxWidth = sizeof(curCell); // cbCell not used directly
    curRow.cpStartRow = -1; // caret row

    // EM_GETTABLEPARMS fills row/cell for the row where the caret is.
    SendMessageW(hEdit, EM_GETTABLEPARMS, (WPARAM)&curRow, (LPARAM)&curCell);

    int cols = curRow.cCell > 0 ? curRow.cCell : tp.cols;

    RECT fmtRc2 = {};
    SendMessageW(hEdit, EM_GETRECT, 0, (LPARAM)&fmtRc2);
    int physPx2 = fmtRc2.right - fmtRc2.left;
    if (physPx2 < 10) { RECT cr2; GetClientRect(hEdit, &cr2); physPx2 = cr2.right - cr2.left; }
    const float twipsPerPhysPx2 = 15.0f / g_dpiScale;
    int edTwips = (int)(physPx2 * twipsPerPhysPx2 + 0.5f);

    const int dxCellMarginTwips = 4 * 15;   // must match insert path

    int cellTwips;
    if (tp.colWidthPct > 0) {
        cellTwips = (int)((long long)edTwips * tp.colWidthPct / 100);
    } else if (tp.colWidthPx > 0) {
        cellTwips = tp.colWidthPx * 15;
    } else {
        int tblTwips = (int)((long long)edTwips * tp.widthPct / 100);
        cellTwips = cols > 0 ? tblTwips / cols : tblTwips;
    }
    if (cellTwips < 100) cellTwips = 100;

    std::vector<TABLECELLPARMS> cells((size_t)cols);
    SHORT brdr = RtfEd_BorderStylePx(tp.borderStyle, tp.borderW);
    for (auto& c : cells) {
        c.dxWidth    = cellTwips;
        c.nVertAlign = (WORD)(tp.vAlign < 3 ? tp.vAlign : 0);
        c.fMergeTop = c.fMergePrev = c.fVertical = 0;
        c.fMergeStart = c.fMergeCont = 0;
        c.wShading   = 0;
        c.dxBrdrLeft  = brdr; c.dyBrdrTop    = brdr;
        c.dxBrdrRight = brdr; c.dyBrdrBottom = brdr;
        c.crBrdrLeft  = tp.borderColor; c.crBrdrTop    = tp.borderColor;
        c.crBrdrRight = tp.borderColor; c.crBrdrBottom = tp.borderColor;
        c.crBackPat   = RGB(255,255,255);
        c.crForePat   = RGB(0,0,0);
    }

    TABLEROWPARMS row = {};
    row.cbRow      = sizeof(TABLEROWPARMS);
    row.cbCell     = sizeof(TABLECELLPARMS);
    row.cCell      = (BYTE)cols;
    row.cRow       = curRow.cRow > 0 ? curRow.cRow : 1;
    row.dxCellMargin = dxCellMarginTwips;
    row.dxIndent   = 0;
    row.dyHeight   = (tp.rowHeightPx > 0) ? (LONG)(tp.rowHeightPx * 15) : 0;
    row.nAlignment = (tp.hAlign == 1) ? 2 : (tp.hAlign == 2) ? 3 : 1;
    row.fRTL       = 0; row.fKeep = 0; row.fKeepFollow = 0;
    row.fWrap      = 1;
    row.fIdentCells = 1;
    row.cpStartRow = -1;
    row.bTableLevel = 1; row.iCell = 0;

    SendMessageW(hEdit, EM_SETTABLEPARMS, (WPARAM)&row, (LPARAM)cells.data());

    // Save pct-mode params so the WM_SIZE handler can rescale the table.
    {
        RtfEditState* es = (RtfEditState*)(LONG_PTR)GetPropW(hEdit, L"rtfeSt");
        if (es) { es->colWidthPct = tp.colWidthPct; es->cols = cols; }
    }

    // Apply cell text alignment to the entire table.
    if (tp.cellHAlign != 0) {
        WORD pfa = (tp.cellHAlign == 1) ? PFA_CENTER : PFA_RIGHT;
        TABLEROWPARMS trq = {}; trq.cbRow = sizeof(trq); trq.cpStartRow = -1;
        TABLECELLPARMS tcq = {}; tcq.dxWidth = sizeof(tcq);
        SendMessageW(hEdit, EM_GETTABLEPARMS, (WPARAM)&trq, (LPARAM)&tcq);
        if (trq.cpStartRow >= 0) {
            CHARRANGE crTbl = { trq.cpStartRow, trq.cpStartRow + 65535 };
            SendMessageW(hEdit, EM_EXSETSEL, 0, (LPARAM)&crTbl);
            PARAFORMAT2 pf = {}; pf.cbSize = sizeof(pf);
            pf.dwMask  = PFM_ALIGNMENT;
            pf.wAlignment = pfa;
            SendMessageW(hEdit, EM_SETPARAFORMAT, 0, (LPARAM)&pf);
            CHARRANGE crEnd = { trq.cpStartRow, trq.cpStartRow };
            SendMessageW(hEdit, EM_EXSETSEL, 0, (LPARAM)&crEnd);
        }
    }
    SetFocus(hEdit);
}

// ─── Proportional rescale of pct-mode table rows ─────────────────────────────
// Called (debounced) after the editor window is resized.  Loops every line,
// moves the caret to each table row so EM_GETTABLEPARMS(cpStartRow=-1) is
// guaranteed to fill the struct, then sets dxWidth = pct% of the current
// formatting-rect width via EM_SETTABLEPARMS.
static void RtfEd_RescalePctTables(HWND hEdit)
{
    RtfEditState* es = (RtfEditState*)(LONG_PTR)GetPropW(hEdit, L"rtfeSt");
    if (!es || es->colWidthPct <= 0 || es->cols <= 0) return;

    RECT fmtRc = {};
    SendMessageW(hEdit, EM_GETRECT, 0, (LPARAM)&fmtRc);
    int physPx = fmtRc.right - fmtRc.left;
    if (physPx < 10) { RECT cr; GetClientRect(hEdit, &cr); physPx = cr.right - cr.left; }
    int newEdTwips = (int)(physPx * 15.0f / g_dpiScale + 0.5f);
    SHORT newCellTwips = (SHORT)std::max(100, newEdTwips * es->colWidthPct / 100);

    CHARRANGE crSaved = {};
    SendMessageW(hEdit, EM_EXGETSEL, 0, (LPARAM)&crSaved);

    int lines = (int)SendMessageW(hEdit, EM_GETLINECOUNT, 0, 0);
    LONG lastRowCP = -2;
    for (int ln = 0; ln < lines; ln++) {
        LONG cp = (LONG)SendMessageW(hEdit, EM_LINEINDEX, (WPARAM)ln, 0);
        if (cp < 0) continue;
        CHARRANGE crLine = { cp, cp };
        SendMessageW(hEdit, EM_EXSETSEL, 0, (LPARAM)&crLine);

        TABLEROWPARMS trp = {}; trp.cbRow = sizeof(trp); trp.cpStartRow = -1;
        TABLECELLPARMS tcp = {};
        if (!SendMessageW(hEdit, EM_GETTABLEPARMS, (WPARAM)&trp, (LPARAM)&tcp)) continue;
        if (trp.cCell == 0) continue;
        if (trp.cpStartRow == lastRowCP) continue;
        lastRowCP = trp.cpStartRow;

        tcp.dxWidth = newCellTwips;
        std::vector<TABLECELLPARMS> cv((size_t)trp.cCell, tcp);
        TABLEROWPARMS nr = trp;
        nr.cbCell     = sizeof(TABLECELLPARMS);
        nr.cRow       = 1;
        nr.iCell      = 0;
        nr.cpStartRow = -1;  // caret is in this row
        SendMessageW(hEdit, EM_SETTABLEPARMS, (WPARAM)&nr, (LPARAM)cv.data());
    }

    SendMessageW(hEdit, EM_EXSETSEL, 0, (LPARAM)&crSaved);
}

// ─── Check if caret is inside a table ────────────────────────────────────────
static bool RtfEd_CaretInTable(HWND hEdit)
{
    TABLEROWPARMS  trp = {}; trp.cbRow  = sizeof(trp); trp.cpStartRow = -1;
    TABLECELLPARMS tcp = {}; tcp.dxWidth = sizeof(tcp);
    LRESULT r = SendMessageW(hEdit, EM_GETTABLEPARMS, (WPARAM)&trp, (LPARAM)&tcp);
    return (r != 0 && trp.cCell > 0);
}

// ─── Default table params (last-used, persist per session) ────────────────────
static RtfTableParams s_lastTableParams;

// ─── Table context-menu helper ────────────────────────────────────────────────
// Shows the table context menu at the given screen position and handles the
// chosen command.  Called from both the subclass proc (mouse right-click and
// keyboard invoke) so the logic lives in one place.
static void RtfEd_ShowTableContextMenu(HWND hEditor, HWND hEdit, POINT screen)
{
    HMENU hMenu = CreatePopupMenu();
    AppendMenuW(hMenu, MF_STRING, IDM_RTFE_TABLE_PROPS, L"Table properties\u2026");
    AppendMenuW(hMenu, MF_SEPARATOR, 0, NULL);
    HMENU hAlignSub = CreatePopupMenu();
    AppendMenuW(hAlignSub, MF_STRING, IDM_RTFE_CELL_ALIGN_L, L"Align left");
    AppendMenuW(hAlignSub, MF_STRING, IDM_RTFE_CELL_ALIGN_C, L"Align centre");
    AppendMenuW(hAlignSub, MF_STRING, IDM_RTFE_CELL_ALIGN_R, L"Align right");
    AppendMenuW(hMenu, MF_POPUP, (UINT_PTR)hAlignSub, L"Cell alignment");

    int cmd = TrackPopupMenu(hMenu, TPM_RIGHTBUTTON | TPM_RETURNCMD | TPM_NONOTIFY,
                             screen.x, screen.y, 0, hEditor, NULL);
    DestroyMenu(hMenu);   // also destroys hAlignSub

    if      (cmd == IDM_RTFE_CELL_ALIGN_L) { RtfEd_SetAlignment(hEdit, PFA_LEFT);   }
    else if (cmd == IDM_RTFE_CELL_ALIGN_C) { RtfEd_SetAlignment(hEdit, PFA_CENTER); }
    else if (cmd == IDM_RTFE_CELL_ALIGN_R) { RtfEd_SetAlignment(hEdit, PFA_RIGHT);  }
    else if (cmd == IDM_RTFE_TABLE_PROPS) {
        // Read current table state from the RichEdit.
        TABLEROWPARMS  trp = {}; trp.cbRow    = sizeof(trp); trp.cpStartRow = -1;
        TABLECELLPARMS tcp = {}; tcp.dxWidth  = sizeof(tcp);
        SendMessageW(hEdit, EM_GETTABLEPARMS, (WPARAM)&trp, (LPARAM)&tcp);

        RECT rc; GetClientRect(hEdit, &rc);
        int edPx  = rc.right > 0 ? rc.right : S(500);
        int twips = edPx * 15;
        // Cell width in twips; fall back to equal distribution if not set.
        int cellT = tcp.dxWidth > 0 ? tcp.dxWidth
                                    : (twips / std::max(1, (int)trp.cCell));
        int cols  = trp.cCell > 0 ? (int)trp.cCell : 1;
        int totalT = cellT * cols;
        int pct = totalT > 0 ? (int)((long long)totalT * 100 / twips) : 100;
        if (pct < 10) pct = 10; if (pct > 100) pct = 100;

        // Seed from last-used params; override with live values from EM_GETTABLEPARMS.
        // We only have absolute twips here, so we show px mode (colWidthPct = 0);
        // the user can switch to pct mode in the dialog if they want.
        RtfTableParams tp = s_lastTableParams;
        tp.rows       = trp.cRow  > 0 ? (int)trp.cRow  : 1;
        tp.cols       = cols;
        tp.widthPct   = pct;
        tp.colWidthPx = cellT > 0 ? cellT / 15 : 0;
        tp.colWidthPct = 0;   // absolute twips — no stored pct available
        tp.rowHeightPx = trp.dyHeight > 0 ? (int)(trp.dyHeight / 15) : 0;
        tp.hAlign     = (trp.nAlignment == 2) ? 1 : (trp.nAlignment == 3) ? 2 : 0;
        tp.vAlign     = tcp.nVertAlign < 3 ? tcp.nVertAlign : 0;
        tp.borderW    = (tcp.dxBrdrLeft > 0) ? std::max(1, (int)(tcp.dxBrdrLeft / 15)) : 0;
        tp.borderColor = tcp.crBrdrLeft;

        if (RtfEd_ShowTableDialog(hEditor, &tp, true, edPx)) {
            s_lastTableParams = tp;
            RtfEd_ApplyTableProps(hEdit, tp);
        }
    }
    SetFocus(hEdit);
}

// ─── RichEdit subclass for right-click table menu ────────────────────────────
// Stored on the RichEdit as window prop L"rtfeEditProc".
//
// Why WM_RBUTTONUP (not WM_CONTEXTMENU)?
//   Msftedit.dll intercepts WM_RBUTTONUP internally and shows its own cut/copy/
//   paste context menu via TrackPopupMenu without ever sending WM_CONTEXTMENU to
//   the window proc.  Subclassing WM_CONTEXTMENU therefore does not intercept it
//   reliably.  Instead we intercept WM_RBUTTONUP: if the caret is in a table we
//   show our custom menu and return without calling the prev proc (suppressing
//   Msftedit's menu); otherwise we fall through and Msftedit shows its standard
//   menu as usual.
//
//   Keyboard-triggered context menus (Menu key / Shift+F10) still arrive as
//   WM_CONTEXTMENU with lParam == -1; we handle that separately.

static LRESULT CALLBACK RtfEd_EditSubclassProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    WNDPROC prev = (WNDPROC)(LONG_PTR)GetPropW(hwnd, L"rtfeEditProc");
    if (!prev) return DefWindowProcW(hwnd, msg, wParam, lParam);

    // WM_RBUTTONDOWN: move the caret to the click point before we test whether
    // the caret is inside a table on WM_RBUTTONUP.
    if (msg == WM_RBUTTONDOWN) {
        LRESULT r = CallWindowProcW(prev, hwnd, msg, wParam, lParam);
        POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
        POINTL ptl = { pt.x, pt.y };
        LONG cp = (LONG)SendMessageW(hwnd, EM_CHARFROMPOS, 0, (LPARAM)&ptl);
        if (cp >= 0) {
            CHARRANGE cr = { cp, cp };
            SendMessageW(hwnd, EM_EXSETSEL, 0, (LPARAM)&cr);
        }
        return r;
    }

    // WM_RBUTTONUP: lParam carries CLIENT coordinates (LOWORD=x, HIWORD=y).
    // Show the table menu if the caret is in a table cell; otherwise let
    // Msftedit handle the message (shows its standard cut/copy/paste menu).
    if (msg == WM_RBUTTONUP) {
        if (RtfEd_CaretInTable(hwnd)) {
            POINT screen = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
            ClientToScreen(hwnd, &screen);
            RtfEd_ShowTableContextMenu(GetParent(hwnd), hwnd, screen);
            return 0;  // suppress Msftedit's own menu
        }
        // Fall through: Msftedit shows cut/copy/paste.
    }

    // WM_CONTEXTMENU: only handle the keyboard-triggered case (lParam == -1).
    // Mouse right-clicks are handled above in WM_RBUTTONUP.
    if (msg == WM_CONTEXTMENU && lParam == (LPARAM)-1) {
        if (RtfEd_CaretInTable(hwnd)) {
            CHARRANGE cr = {};
            SendMessageW(hwnd, EM_EXGETSEL, 0, (LPARAM)&cr);
            POINTL ptl = {};
            SendMessageW(hwnd, EM_POSFROMCHAR, (WPARAM)&ptl, (LPARAM)cr.cpMin);
            POINT screen = { ptl.x, ptl.y };
            ClientToScreen(hwnd, &screen);
            RtfEd_ShowTableContextMenu(GetParent(hwnd), hwnd, screen);
            return 0;
        }
        // Fall through: Msftedit shows its standard menu.
    }

    if (msg == WM_NCDESTROY) {
        RemovePropW(hwnd, L"rtfeEditProc");
        auto* es = (RtfEditState*)(LONG_PTR)GetPropW(hwnd, L"rtfeSt");
        RemovePropW(hwnd, L"rtfeSt");
        delete es;
        return CallWindowProcW(prev, hwnd, msg, wParam, lParam);
    }

    return CallWindowProcW(prev, hwnd, msg, wParam, lParam);
}

static void RtfEd_SubclassEdit(HWND hEdit)
{
    WNDPROC prev = (WNDPROC)SetWindowLongPtrW(hEdit, GWLP_WNDPROC, (LONG_PTR)RtfEd_EditSubclassProc);
    SetPropW(hEdit, L"rtfeEditProc", (HANDLE)(LONG_PTR)prev);
    SetPropW(hEdit, L"rtfeSt",      (HANDLE)(LONG_PTR)new RtfEditState{});
}

// Forward declaration — RtfEd_SyncToolbar is defined after RtfEdState (below).
static void RtfEd_SyncToolbar(HWND hwnd, HWND hEdit);

// ─── Open file ────────────────────────────────────────────────────────────────
// Opens an RTF or plain-text file and streams its content into the editor,
// replacing the current document.  Plain-text files are streamed as SF_TEXT
// so RichEdit handles the conversion; RTF files use SF_RTF.
static void RtfEd_OpenFile(HWND hwnd, HWND hEdit, const RtfEditorData* pD)
{
    std::wstring fAll  = RtfLS(pD, L"rtfe_open_filter_all",  L"Supported files (*.rtf;*.txt;*.md)");
    std::wstring fRtf  = RtfLS(pD, L"rtfe_open_filter_rtf",  L"RTF files (*.rtf)");
    std::wstring fTxt  = RtfLS(pD, L"rtfe_open_filter_txt",  L"Text files (*.txt)");
    std::wstring fMd   = RtfLS(pD, L"rtfe_open_filter_md",   L"Markdown files (*.md)");
    std::wstring fAny  = RtfLS(pD, L"rtfe_open_filter_any",  L"All files");
    std::wstring title = RtfLS(pD, L"rtfe_open_dlg_title",   L"Open File");
    std::wstring filter = fAll + L'\0' + L"*.rtf;*.txt;*.md" + L'\0'
                        + fRtf + L'\0' + L"*.rtf"            + L'\0'
                        + fTxt + L'\0' + L"*.txt"            + L'\0'
                        + fMd  + L'\0' + L"*.md"             + L'\0'
                        + fAny + L'\0' + L"*.*"              + L'\0' + L'\0';

    wchar_t path[MAX_PATH] = {};
    OPENFILENAMEW ofn = {}; ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner   = hwnd;
    ofn.lpstrFilter = filter.c_str();
    ofn.lpstrFile   = path;
    ofn.nMaxFile    = MAX_PATH;
    ofn.Flags       = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
    ofn.lpstrTitle  = title.c_str();
    if (!GetOpenFileNameW(&ofn)) return;

    FILE* f = nullptr;
    if (_wfopen_s(&f, path, L"rb") != 0 || !f) return;
    fseek(f, 0, SEEK_END); long fsz = ftell(f); rewind(f);
    std::string raw(fsz, '\0');
    fread(&raw[0], 1, fsz, f);
    fclose(f);

    // Detect RTF by leading "{\\rtf" signature; otherwise treat as plain text.
    bool isRtf = (raw.size() >= 5 && raw.substr(0, 5) == "{\\rtf");
    UINT sfFlag = isRtf ? SF_RTF : (SF_TEXT | SF_UNICODE);
    if (!isRtf) {
        // Convert plain text (assumed UTF-8 or ANSI) to wstring for SF_TEXT|SF_UNICODE.
        int wlen = MultiByteToWideChar(CP_UTF8, 0, raw.c_str(), (int)raw.size(), NULL, 0);
        std::wstring wraw(wlen, L'\0');
        MultiByteToWideChar(CP_UTF8, 0, raw.c_str(), (int)raw.size(), &wraw[0], wlen);
        std::string narrow(wraw.size() * sizeof(wchar_t), '\0');
        memcpy(&narrow[0], wraw.data(), wraw.size() * sizeof(wchar_t));
        raw = narrow;
    }

    RtfEdStreamBuf rb = { &raw, 0 };
    EDITSTREAM esIn   = { (DWORD_PTR)&rb, 0, RtfEdReadCb };
    SendMessageW(hEdit, EM_STREAMIN, sfFlag, (LPARAM)&esIn);
    RtfEd_SyncToolbar(hwnd, hEdit);
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
    HMSB hSbV;         // custom vertical scrollbar (my_scrollbar)
    HMSB hSbH;         // custom horizontal scrollbar (my_scrollbar)
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
    case WM_NCDESTROY: {
        wchar_t* tipCopy = (wchar_t*)(void*)GetPropW(hwnd, L"rtfeTipText");
        delete[] tipCopy;
        RemovePropW(hwnd, L"rtfeTipProc");
        RemovePropW(hwnd, L"rtfeTipText");
        SetWindowLongPtrW(hwnd, GWLP_WNDPROC, (LONG_PTR)prev);
        return CallWindowProcW(prev, hwnd, msg, wParam, lParam);
    }
    }
    return CallWindowProcW(prev, hwnd, msg, wParam, lParam);
}

static void RtfEd_SetToolTip(HWND hCtrl, const wchar_t* text)
{
    // Heap-copy so the pointer stays valid for the lifetime of the button.
    // Freed in RtfEd_ToolbarBtnProc WM_NCDESTROY.
    size_t len = wcslen(text) + 1;
    wchar_t* copy = new wchar_t[len];
    wcscpy(copy, text);
    SetPropW(hCtrl, L"rtfeTipText", (HANDLE)(void*)copy);
    WNDPROC prev = (WNDPROC)SetWindowLongPtrW(hCtrl, GWLP_WNDPROC, (LONG_PTR)RtfEd_ToolbarBtnProc);
    SetPropW(hCtrl, L"rtfeTipProc", (HANDLE)(void*)(LONG_PTR)prev);
}

// ─── Responsive toolbar layout ───────────────────────────────────────────────
// Positions all 17 toolbar controls into one row (if cW is wide enough)
// or two rows. Returns the Y coordinate where the RichEdit should start.
static int RtfEd_LayoutToolbar(HWND hwnd, int cW)
{
    const int pad   = S(8);
    const int bSz   = S(26);
    const int bG    = S(3);
    const int sG    = S(8);
    const int wXs   = bSz + S(4);   // subscript / superscript
    const int wAl   = bSz + S(4);   // alignment / list buttons
    const int wCol  = bSz + S(10);  // colour / highlight / image
    const int wFace = S(170);
    const int wSize = S(56);

    // Total pixel width needed to fit everything in one row (both pads included).
    int oneRowW = pad
        + bSz+bG + bSz+bG + bSz+bG + bSz+sG   // B I U S
        + wXs+bG + wXs+sG                       // X2 X^2
        + wFace+bG + wSize                      // FaceCombo SizeCombo
        + sG                                    // separator before row-2 items
        + wAl+bG + wAl+bG + wAl+bG + wAl+sG    // align L C R J
        + wAl+bG + wAl+sG                       // bullet numbered
        + wCol+bG + wCol+sG + wCol+sG           // colour highlight image
        + S(28)+sG + wCol                       // open table
        + pad;

    bool oneRow = (cW >= oneRowW);
    int  y1 = pad;
    int  y2 = oneRow ? pad : (pad + bSz + S(4));

    int x = pad;
    auto P = [&](int id, int w, int gap, int ry) {
        HWND h = GetDlgItem(hwnd, id);
        if (h) SetWindowPos(h, NULL, x, ry, w, bSz, SWP_NOZORDER | SWP_NOACTIVATE);
        x += w + gap;
    };

    // Row 1 items.
    P(IDC_RTFE_BOLD,        bSz,   bG, y1);
    P(IDC_RTFE_ITALIC,      bSz,   bG, y1);
    P(IDC_RTFE_UNDERLINE,   bSz,   bG, y1);
    P(IDC_RTFE_STRIKE,      bSz,   sG, y1);
    P(IDC_RTFE_SUBSCRIPT,   wXs,   bG, y1);
    P(IDC_RTFE_SUPERSCRIPT, wXs,   sG, y1);
    P(IDC_RTFE_FONTFACE,    wFace, bG, y1);
    P(IDC_RTFE_FONTSIZE,    wSize, 0,  y1);

    // One-row: continue on same line; two-row: reset x for second row.
    if (oneRow) x += sG;
    else        x  = pad;

    // Row 2 items.
    P(IDC_RTFE_ALIGN_L,   wAl,  bG, y2);
    P(IDC_RTFE_ALIGN_C,   wAl,  bG, y2);
    P(IDC_RTFE_ALIGN_R,   wAl,  bG, y2);
    P(IDC_RTFE_ALIGN_J,   wAl,  sG, y2);
    P(IDC_RTFE_BULLET,    wAl,  bG, y2);
    P(IDC_RTFE_NUMBERED,  wAl,  sG, y2);
    P(IDC_RTFE_COLOR,     wCol, bG, y2);
    P(IDC_RTFE_HIGHLIGHT, wCol, sG, y2);
    P(IDC_RTFE_IMAGE,     wCol, sG, y2);
    P(IDC_RTFE_OPEN,      S(28), sG, y2);
    P(IDC_RTFE_TABLE,     wCol, 0,  y2);

    return y2 + bSz + S(6);  // top of RichEdit
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
        x += bSz+S(10) + sG;

        // "Open file" button — shell32.dll icon 38 (folder/open).
        HWND hOpenBtn = CreateCustomButtonWithIcon(
            hwnd, IDC_RTFE_OPEN, L"",
            ButtonColor::Blue,
            L"shell32.dll", 38,
            x, row2Y, S(28), bSz, hInst);
        x += S(28) + sG;

        // Insert table button
        CreateWindowExW(0, L"BUTTON", L"\u229E",  // ⊞
            WS_CHILD|WS_VISIBLE|BS_PUSHBUTTON,
            x, row2Y, bSz+S(10), bSz, hwnd, (HMENU)IDC_RTFE_TABLE, hInst, NULL);

        // ── RichEdit ──────────────────────────────────────────────────────────
        // Reflow toolbar now (may become one row if the window starts wide enough).
        int editY = RtfEd_LayoutToolbar(hwnd, cW);
        st->editY = editY;

        // If char limit is set, reserve space for status bar above OK/Cancel.
        int statusH  = (pData->maxChars > 0) ? S(20) : 0;
        st->statusH  = statusH;
        int btnRowH  = S(38) + pad;
        st->bottomBarH = btnRowH + (statusH > 0 ? statusH + S(6) : 0);

        int editH = rcC.bottom - editY - S(6) - st->bottomBarH;

        HWND hEdit = CreateWindowExW(WS_EX_CLIENTEDGE, reClass, L"",
            WS_CHILD|WS_VISIBLE|WS_VSCROLL|WS_HSCROLL|ES_MULTILINE|ES_WANTRETURN|ES_AUTOVSCROLL,
            pad, editY, cW - 2*pad, editH,
            hwnd, (HMENU)IDC_RTFE_EDIT, hInst, NULL);

        // Disable word wrap — set a target line width of 32767 px (in twips:
        // 32767 × 15 = 491 505).  This makes every line infinitely wide from
        // the user's viewpoint while keeping SCROLLINFO nMax in a sane range.
        SendMessageW(hEdit, EM_SETTARGETDEVICE, (WPARAM)NULL, 32767 * 15);

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

        // ── Enable advanced typography (needed for table word-wrap in cells) ──
        SendMessageW(hEdit, EM_SETTYPOGRAPHYOPTIONS, TO_ADVANCEDTYPOGRAPHY, TO_ADVANCEDTYPOGRAPHY);

        // ── Subclass the RichEdit first (innermost proc — only handles right-click).
        RtfEd_SubclassEdit(hEdit);

        // ── Attach custom scrollbars on top of the subclass so that
        //    my_scrollbar's subclass proc sees WM_VSCROLL/WM_HSCROLL/WM_MOUSEWHEEL
        //    before the right-click subclass passes them on to the RichEdit.
        //    Auto-hide mode (no MSB_NOHIDE): bars stay invisible until content
        //    overflows the viewport, then fade in on hover.
        st->hSbV = msb_attach(hEdit, MSB_VERTICAL);
        st->hSbH = msb_attach(hEdit, MSB_HORIZONTAL);

        if (!pData->initRtf.empty()) {
            RtfEd_StreamIn(hEdit, pData->initRtf);
            // Measure actual content width now that RTF is loaded.
            msb_notify_content_changed(st->hSbH);
        }

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
        // All strings are looked up via the locale map in pData.  English
        // fallbacks are used when pData->pLocale is null (e.g. standalone test).
        auto T = [&](const wchar_t* key, const wchar_t* fb) {
            return RtfLS(pData, key, fb);
        };
        RtfEd_SetToolTip(hBold,                                   T(L"rtfe_tip_bold",        L"Bold  (Ctrl+B)").c_str());
        RtfEd_SetToolTip(hItalic,                                  T(L"rtfe_tip_italic",      L"Italic  (Ctrl+I)").c_str());
        RtfEd_SetToolTip(hUnder,                                   T(L"rtfe_tip_underline",   L"Underline  (Ctrl+U)").c_str());
        RtfEd_SetToolTip(GetDlgItem(hwnd, IDC_RTFE_STRIKE),       T(L"rtfe_tip_strike",      L"Strikethrough").c_str());
        RtfEd_SetToolTip(GetDlgItem(hwnd, IDC_RTFE_SUBSCRIPT),    T(L"rtfe_tip_subscript",   L"Subscript  (e.g. H\u2082O)").c_str());
        RtfEd_SetToolTip(GetDlgItem(hwnd, IDC_RTFE_SUPERSCRIPT),  T(L"rtfe_tip_superscript", L"Superscript  (e.g. m\u00B2)").c_str());
        RtfEd_SetToolTip(hFace,                                    T(L"rtfe_tip_fontface",    L"Font face").c_str());
        RtfEd_SetToolTip(hSzCombo,                                 T(L"rtfe_tip_fontsize",    L"Font size (pt)").c_str());
        RtfEd_SetToolTip(hColor,                                   T(L"rtfe_tip_color",       L"Text colour").c_str());
        RtfEd_SetToolTip(GetDlgItem(hwnd, IDC_RTFE_HIGHLIGHT),    T(L"rtfe_tip_highlight",   L"Highlight colour").c_str());
        RtfEd_SetToolTip(GetDlgItem(hwnd, IDC_RTFE_ALIGN_L),      T(L"rtfe_tip_align_l",     L"Align left").c_str());
        RtfEd_SetToolTip(GetDlgItem(hwnd, IDC_RTFE_ALIGN_C),      T(L"rtfe_tip_align_c",     L"Align centre").c_str());
        RtfEd_SetToolTip(GetDlgItem(hwnd, IDC_RTFE_ALIGN_R),      T(L"rtfe_tip_align_r",     L"Align right").c_str());
        RtfEd_SetToolTip(GetDlgItem(hwnd, IDC_RTFE_ALIGN_J),      T(L"rtfe_tip_align_j",     L"Justify").c_str());
        RtfEd_SetToolTip(GetDlgItem(hwnd, IDC_RTFE_BULLET),       T(L"rtfe_tip_bullet",      L"Bullet list").c_str());
        RtfEd_SetToolTip(GetDlgItem(hwnd, IDC_RTFE_NUMBERED),     T(L"rtfe_tip_numbered",    L"Numbered list").c_str());
        RtfEd_SetToolTip(hImgBtn,                                  T(L"rtfe_tip_image",       L"Insert image  (PNG / JPEG)").c_str());
        RtfEd_SetToolTip(hOpenBtn,                                 T(L"rtfe_tip_open",        L"Open file\u2026  (RTF / TXT)").c_str());
        RtfEd_SetToolTip(GetDlgItem(hwnd, IDC_RTFE_TABLE),        T(L"rtfe_tip_table",       L"Insert table\u2026").c_str());

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

        // Reflow toolbar — one row if wide enough, two rows otherwise.
        st->editY = RtfEd_LayoutToolbar(hwnd, cW);

        // Edit control fills the gap.
        int editH = cH - st->editY - S(6) - bottomBarH;
        if (hEdit && editH > 0)
            SetWindowPos(hEdit, NULL, pad, st->editY, cW - 2*pad, editH, SWP_NOZORDER);

        // Schedule a debounced pct-table rescale on this (parent) window.
        // Firing the timer here rather than on the subclassed RichEdit proc
        // ensures the message is processed by a proc we fully own, and that
        // EM_GETRECT is serviced after the RichEdit has finished its own layout.
        SetTimer(hwnd, 9901, 80, NULL);

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

    // ── WM_TIMER 9901 — debounced post-resize table rescale ─────────────────
    case WM_TIMER: {
        if (wParam == 9901) {
            KillTimer(hwnd, 9901);
            HWND hEd = GetDlgItem(hwnd, IDC_RTFE_EDIT);
            if (hEd) RtfEd_RescalePctTables(hEd);
        }
        return 0;
    }

    // ── WM_COMMAND ──────────────────────────────────────────────────────────────────
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
            { HWND hOw = GetWindow(hwnd, GW_OWNER); if (hOw) { EnableWindow(hOw, TRUE); SetForegroundWindow(hOw); } }
            DestroyWindow(hwnd); return 0;
        }

        // ── Cancel ────────────────────────────────────────────────────────────
        if (wmId == IDC_RTFE_CANCEL || wmId == IDCANCEL) {
            { HWND hOw = GetWindow(hwnd, GW_OWNER); if (hOw) { EnableWindow(hOw, TRUE); SetForegroundWindow(hOw); } }
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

        // ── Insert image / open file ──────────────────────────────────────────
        if (wmId == IDC_RTFE_IMAGE) { RtfEd_InsertImage(hwnd, hEdit, st->pData); return 0; }
        if (wmId == IDC_RTFE_OPEN)  { RtfEd_OpenFile(hwnd, hEdit, st->pData);   return 0; }

        // ── Insert table ──────────────────────────────────────────────────────
        if (wmId == IDC_RTFE_TABLE) {
            RECT rcE = {}; GetClientRect(hEdit, &rcE);
            RtfTableParams tp = s_lastTableParams;
            if (RtfEd_ShowTableDialog(hwnd, &tp, false, rcE.right)) {
                s_lastTableParams = tp;
                RtfEd_InsertTableNative(hwnd, hEdit, tp);
            } else {
                SetFocus(hEdit);
            }
            return 0;
        }

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
            if (wmEv == EN_CHANGE) {
                RtfEd_UpdateStatus(hwnd, hEdit, pData);
                // Sync scrollbars — content may have changed size.
                // Vertical: plain sync (SCROLLINFO nMax is correct for vert).
                // Horizontal: notify content changed so the width is re-measured.
                if (st->hSbV) msb_sync(st->hSbV);
                if (st->hSbH) msb_notify_content_changed(st->hSbH);
            }
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
        if (dis->CtlID == IDC_RTFE_OK || dis->CtlID == IDC_RTFE_CANCEL || dis->CtlID == IDC_RTFE_OPEN) {
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
        // Cancel any pending rescale timer.
        KillTimer(hwnd, 9901);
        // Internal state.
        RtfEdState* st = (RtfEdState*)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
        if (st) {
            msb_detach(st->hSbV);
            msb_detach(st->hSbH);
            delete st;
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
        }
        break;
    }

    case WM_CLOSE:
        { HWND hOw = GetWindow(hwnd, GW_OWNER); if (hOw) { EnableWindow(hOw, TRUE); SetForegroundWindow(hOw); } }
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

    // Window width: just enough for a single-row toolbar (computed from the same
    // constants as RtfEd_LayoutToolbar so it is always exact at any DPI).
    // A caller may override via data.preferredW (e.g. notes_editor does this).
    int dlgW, dlgH;
    if (data.preferredW > 0) {
        dlgW = data.preferredW;
    } else {
        const int _pad   = S(8);
        const int _bSz   = S(26);
        const int _bG    = S(3);
        const int _sG    = S(8);
        const int _wXs   = _bSz + S(4);
        const int _wAl   = _bSz + S(4);
        const int _wCol  = _bSz + S(10);
        const int _wFace = S(170);
        const int _wSize = S(56);
        int clientW = _pad
            + _bSz+_bG + _bSz+_bG + _bSz+_bG + _bSz+_sG
            + _wXs+_bG + _wXs+_sG
            + _wFace+_bG + _wSize + _sG
            + _wAl+_bG + _wAl+_bG + _wAl+_bG + _wAl+_sG
            + _wAl+_bG + _wAl+_sG
            + _wCol+_bG + _wCol+_sG + _wCol+_sG
            + S(28)+_sG + _wCol
            + _pad;
        RECT adjW = { 0, 0, clientW, 0 };
        AdjustWindowRectEx(&adjW, WS_OVERLAPPEDWINDOW, FALSE, 0);
        dlgW = adjW.right - adjW.left;
    }
    dlgH = data.preferredH > 0 ? data.preferredH : S(520);

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

    if (hwndParent) EnableWindow(hwndParent, FALSE);
    HWND hDlg = CreateWindowExW(
        0,
        L"RtfEditorWindow", title,
        WS_OVERLAPPEDWINDOW | WS_VISIBLE,
        x, y, dlgW, dlgH,
        hwndParent, NULL, hInst, &data);

    if (!hDlg) return false;

    // Modal message loop — IsWindow guard + WM_QUIT re-post.
    // Ctrl+B/I/U are NOT handled natively by RichEdit; we intercept them here
    // and forward to the parent's WM_COMMAND handlers so the toolbar also syncs.
    MSG m;
    HWND hEdit = GetDlgItem(hDlg, IDC_RTFE_EDIT);
    while (GetMessageW(&m, NULL, 0, 0) > 0) {
        if (!IsWindow(hDlg)) break;
        if (m.message == WM_QUIT) { PostQuitMessage((int)m.wParam); break; }
        // ── Keyboard shortcuts when the RichEdit has focus ────────────────────
        if (m.message == WM_KEYDOWN && m.hwnd == hEdit &&
            (GetKeyState(VK_CONTROL) & 0x8000))
        {
            bool handled = true;
            switch ((int)m.wParam) {
            case 'B': SendMessageW(hDlg, WM_COMMAND, IDC_RTFE_BOLD,      0); break;
            case 'I': SendMessageW(hDlg, WM_COMMAND, IDC_RTFE_ITALIC,    0); break;
            case 'U': SendMessageW(hDlg, WM_COMMAND, IDC_RTFE_UNDERLINE, 0); break;
            default:  handled = false; break;
            }
            if (handled) continue; // skip TranslateMessage / DispatchMessage
        }
        TranslateMessage(&m);
        DispatchMessageW(&m);
    }
    // Re-enable parent (safe no-op if OK/Cancel already did it) and bring
    // it to the foreground via a TOPMOST flash — the only reliable way on
    // modern Windows to raise a window after a modal child closes.
    if (hwndParent && IsWindow(hwndParent)) {
        EnableWindow(hwndParent, TRUE);
        SetWindowPos(hwndParent, HWND_TOPMOST,   0,0,0,0, SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE);
        SetWindowPos(hwndParent, HWND_NOTOPMOST, 0,0,0,0, SWP_NOMOVE|SWP_NOSIZE);
    }
    return data.okClicked;
}
