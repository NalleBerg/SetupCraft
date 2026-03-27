#include "notes_editor.h"
#include "dpi.h"
#include "button.h"
#include <richedit.h>
#include <commdlg.h>
#include <string>
#include <wchar.h>

// ─── Internal helpers ─────────────────────────────────────────────────────────

struct RtfStreamBuf { const std::string *src; size_t pos; };

static DWORD CALLBACK NotesRtfReadCb(DWORD_PTR cookie, LPBYTE buf, LONG cb, LONG *pcb)
{
    RtfStreamBuf *rb = (RtfStreamBuf*)cookie;
    size_t rem = rb->src->size() - rb->pos;
    LONG   n   = (LONG)(rem < (size_t)cb ? rem : (size_t)cb);
    if (n > 0) { memcpy(buf, rb->src->c_str() + rb->pos, n); rb->pos += n; }
    *pcb = n;
    return 0;
}

static DWORD CALLBACK NotesRtfWriteCb(DWORD_PTR cookie, LPBYTE buf, LONG cb, LONG *pcb)
{
    std::string *s = (std::string*)cookie;
    s->append((char*)buf, cb);
    *pcb = cb;
    return 0;
}

// Stream RTF wstring into a RichEdit control.
static void NotesEditor_StreamIn(HWND hEdit, const std::wstring& wrtf)
{
    if (wrtf.empty()) { SetWindowTextW(hEdit, L""); return; }
    std::string rtf;
    int n = WideCharToMultiByte(CP_UTF8, 0, wrtf.c_str(), -1, NULL, 0, NULL, NULL);
    if (n > 1) { rtf.resize(n - 1); WideCharToMultiByte(CP_UTF8, 0, wrtf.c_str(), -1, &rtf[0], n, NULL, NULL); }
    RtfStreamBuf rb = { &rtf, 0 };
    EDITSTREAM es   = { (DWORD_PTR)&rb, 0, NotesRtfReadCb };
    SendMessageW(hEdit, EM_STREAMIN, SF_RTF, (LPARAM)&es);
}

// Dump RichEdit content as RTF wstring.
static std::wstring NotesEditor_StreamOut(HWND hEdit)
{
    std::string rtf;
    EDITSTREAM es = { (DWORD_PTR)&rtf, 0, NotesRtfWriteCb };
    SendMessageW(hEdit, EM_STREAMOUT, SF_RTF, (LPARAM)&es);
    if (rtf.empty()) return L"";
    int n = MultiByteToWideChar(CP_UTF8, 0, rtf.c_str(), -1, NULL, 0);
    if (n <= 1) return L"";
    std::wstring out(n - 1, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, rtf.c_str(), -1, &out[0], n);
    return out;
}

// Count plain-text characters (for the 500-char limit display).
static int NotesEditor_PlainLen(HWND hEdit)
{
    GETTEXTLENGTHEX gtl = {};
    gtl.flags    = GTL_NUMCHARS | GTL_PRECISE;
    gtl.codepage = 1200; // Unicode
    return (int)SendMessageW(hEdit, EM_GETTEXTLENGTHEX, (WPARAM)&gtl, 0);
}

// Toggle a CHARFORMAT2 effect flag on the current selection.
// Uses read-modify-write: only the target bit is changed; all other effect bits
// (especially CFE_AUTOCOLOR) and the explicit colour are preserved.
static void NotesEditor_ToggleEffect(HWND hEdit, DWORD maskBit, DWORD effectBit)
{
    CHARFORMAT2W cf = {}; cf.cbSize = sizeof(cf);
    cf.dwMask = maskBit | CFM_COLOR;          // fetch colour state as well
    SendMessageW(hEdit, EM_GETCHARFORMAT, SCF_SELECTION, (LPARAM)&cf);
    if (cf.dwEffects & effectBit)
        cf.dwEffects &= ~effectBit;
    else
        cf.dwEffects |= effectBit;
    // dwMask includes CFM_COLOR → CFE_AUTOCOLOR and crTextColor are written back unchanged
    SendMessageW(hEdit, EM_SETCHARFORMAT, SCF_SELECTION, (LPARAM)&cf);
}

// Toggle subscript / superscript (mutually exclusive; toggling off clears both).
// Preserves CFE_AUTOCOLOR and explicit colour via read-modify-write.
static void NotesEditor_ToggleScript(HWND hEdit, DWORD wantBit)
{
    CHARFORMAT2W cf = {}; cf.cbSize = sizeof(cf);
    cf.dwMask = CFM_SUBSCRIPT | CFM_COLOR;    // fetch colour state as well
    SendMessageW(hEdit, EM_GETCHARFORMAT, SCF_SELECTION, (LPARAM)&cf);
    bool wasSet = (cf.dwEffects & wantBit) != 0;
    cf.dwEffects &= ~(CFE_SUBSCRIPT | CFE_SUPERSCRIPT); // clear both
    if (!wasSet) cf.dwEffects |= wantBit;     // set the wanted one if it wasn’t on
    SendMessageW(hEdit, EM_SETCHARFORMAT, SCF_SELECTION, (LPARAM)&cf);
}

// Refresh the character-count status label.
static void NotesEditor_UpdateStatus(HWND hwnd, HWND hEdit, const std::wstring& fmtStr)
{
    int left = 500 - NotesEditor_PlainLen(hEdit);
    if (left < 0) left = 0;
    wchar_t buf[80];
    swprintf(buf, 80, fmtStr.empty() ? L"%d characters left" : fmtStr.c_str(), left);
    HWND hStatus = GetDlgItem(hwnd, IDC_NOTES_STATUS);
    if (hStatus) SetWindowTextW(hStatus, buf);
}

// Font sizes (pt) offered in the combo.
static const int s_notesFontSizes[] = { 8, 9, 10, 11, 12, 14, 16, 18, 20, 24, 28, 36 };
static const int s_notesFontCount   = (int)(sizeof(s_notesFontSizes)/sizeof(s_notesFontSizes[0]));
static const int s_notesFontDefault = 2; // index of 10 pt

// ─── Dialog window proc ───────────────────────────────────────────────────────

static HMODULE s_hRichEditDll = NULL;

static LRESULT CALLBACK NotesEditorDlgProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg) {
    case WM_CREATE: {
        CREATESTRUCTW*   cs    = (CREATESTRUCTW*)lParam;
        HINSTANCE        hInst = cs->hInstance;
        NotesEditorData* pData = (NotesEditorData*)cs->lpCreateParams;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)pData);

        // Load RichEdit (Msftedit.dll = RichEdit 4.1, fall back to Riched20.dll = 2.0)
        if (!s_hRichEditDll) {
            s_hRichEditDll = LoadLibraryW(L"Msftedit.dll");
            if (!s_hRichEditDll) s_hRichEditDll = LoadLibraryW(L"Riched20.dll");
        }
        WNDCLASSEXW wce = {}; wce.cbSize = sizeof(wce);
        const wchar_t* reClass = (s_hRichEditDll && GetClassInfoExW(s_hRichEditDll, L"RICHEDIT50W", &wce))
                                 ? L"RICHEDIT50W" : L"RichEdit20W";

        RECT rcC; GetClientRect(hwnd, &rcC);
        int cW = rcC.right, cH = rcC.bottom;
        int pad   = S(10);
        int btnSz = S(28);
        int btnGap= S(4);
        int x = pad, tbY = pad;

        // ── Toolbar row ───────────────────────────────────────────────────────
        HWND hBold = CreateWindowExW(0, L"BUTTON", L"B",
            WS_CHILD|WS_VISIBLE|BS_PUSHBUTTON,
            x, tbY, btnSz, btnSz, hwnd, (HMENU)IDC_NOTES_BOLD, hInst, NULL);
        x += btnSz + btnGap;
        HWND hItalic = CreateWindowExW(0, L"BUTTON", L"I",
            WS_CHILD|WS_VISIBLE|BS_PUSHBUTTON,
            x, tbY, btnSz, btnSz, hwnd, (HMENU)IDC_NOTES_ITALIC, hInst, NULL);
        x += btnSz + btnGap;
        HWND hUnder = CreateWindowExW(0, L"BUTTON", L"U",
            WS_CHILD|WS_VISIBLE|BS_PUSHBUTTON,
            x, tbY, btnSz, btnSz, hwnd, (HMENU)IDC_NOTES_UNDERLINE, hInst, NULL);
        x += btnSz + S(10);

        HWND hSub = CreateWindowExW(0, L"BUTTON", L"X\u2082",  // X₂
            WS_CHILD|WS_VISIBLE|BS_PUSHBUTTON,
            x, tbY, btnSz+S(6), btnSz, hwnd, (HMENU)IDC_NOTES_SUBSCRIPT, hInst, NULL);
        x += btnSz+S(6) + btnGap;
        HWND hSup = CreateWindowExW(0, L"BUTTON", L"X\u00B2",  // X²
            WS_CHILD|WS_VISIBLE|BS_PUSHBUTTON,
            x, tbY, btnSz+S(6), btnSz, hwnd, (HMENU)IDC_NOTES_SUPERSCRIPT, hInst, NULL);
        x += btnSz+S(6) + S(10);

        int comboW = S(70);
        HWND hFontSize = CreateWindowExW(0, L"COMBOBOX", L"",
            WS_CHILD|WS_VISIBLE|CBS_DROPDOWNLIST|WS_VSCROLL,
            x, tbY, comboW, S(200), hwnd, (HMENU)IDC_NOTES_FONTSIZE, hInst, NULL);
        for (int i = 0; i < s_notesFontCount; i++) {
            wchar_t sz[8]; swprintf(sz, 8, L"%d", s_notesFontSizes[i]);
            SendMessageW(hFontSize, CB_ADDSTRING, 0, (LPARAM)sz);
        }
        SendMessageW(hFontSize, CB_SETCURSEL, s_notesFontDefault, 0);
        x += comboW + S(6);

        HWND hColor = CreateWindowExW(0, L"BUTTON", L"A\u25BC",  // A▼
            WS_CHILD|WS_VISIBLE|BS_PUSHBUTTON,
            x, tbY, btnSz+S(10), btnSz, hwnd, (HMENU)IDC_NOTES_COLOR, hInst, NULL);
        x += btnSz+S(10) + S(10);

        HWND hTable = CreateWindowExW(0, L"BUTTON", L"\u229E",  // ⊞ table glyph
            WS_CHILD|WS_VISIBLE|BS_PUSHBUTTON,
            x, tbY, btnSz+S(10), btnSz, hwnd, (HMENU)IDC_NOTES_TABLE, hInst, NULL);
        (void)hTable;

        // ── RichEdit ──────────────────────────────────────────────────────────
        int editY   = tbY + btnSz + btnGap;
        int statusH = S(22);
        int btnRowH = S(38);
        int editH   = cH - editY - S(6) - statusH - S(6) - btnRowH - pad;
        HWND hEdit  = CreateWindowExW(WS_EX_CLIENTEDGE, reClass, L"",
            WS_CHILD|WS_VISIBLE|WS_VSCROLL|ES_MULTILINE|ES_WANTRETURN|ES_AUTOVSCROLL,
            pad, editY, cW - 2*pad, editH,
            hwnd, (HMENU)IDC_NOTES_EDIT, hInst, NULL);

        // Cap at 500 plain-text characters.
        SendMessageW(hEdit, EM_EXLIMITTEXT, 0, 500);

        // Default font: Segoe UI 10 pt, auto text colour.
        CHARFORMAT2W cfD = {}; cfD.cbSize = sizeof(cfD);
        cfD.dwMask    = CFM_FACE | CFM_SIZE | CFM_CHARSET | CFM_COLOR | CFM_EFFECTS;
        cfD.dwEffects = CFE_AUTOCOLOR; // auto text colour — prevents black-on-black
        cfD.yHeight   = 10 * 20;      // twips (20 twips per point)
        cfD.bCharSet  = DEFAULT_CHARSET;
        wcsncpy(cfD.szFaceName, L"Segoe UI", LF_FACESIZE - 1);
        SendMessageW(hEdit, EM_SETCHARFORMAT, SCF_ALL, (LPARAM)&cfD);
        SendMessageW(hEdit, EM_SETBKGNDCOLOR, 0, RGB(255, 255, 255)); // explicit white

        if (!pData->initRtf.empty()) NotesEditor_StreamIn(hEdit, pData->initRtf);
        SendMessageW(hEdit, EM_SETEVENTMASK, 0, ENM_CHANGE);

        // ── Char-count status bar ─────────────────────────────────────────────
        int statusY = editY + editH + S(6);
        CreateWindowExW(WS_EX_STATICEDGE, L"STATIC", L"",
            WS_CHILD|WS_VISIBLE|SS_LEFT|SS_CENTERIMAGE,
            pad, statusY, cW - 2*pad, statusH,
            hwnd, (HMENU)IDC_NOTES_STATUS, hInst, NULL);
        NotesEditor_UpdateStatus(hwnd, hEdit, pData->charsLeftFmt);

        // ── Save / Cancel buttons ─────────────────────────────────────────────
        int btnY2  = statusY + statusH + S(6);
        const wchar_t* okTxt  = pData->okText.empty()     ? L"Save"   : pData->okText.c_str();
        const wchar_t* canTxt = pData->cancelText.empty() ? L"Cancel" : pData->cancelText.c_str();
        int wOK_n  = MeasureButtonWidth(okTxt,  true);
        int wCnl_n = MeasureButtonWidth(canTxt, true);
        int startX = (cW - wOK_n - S(10) - wCnl_n) / 2;
        CreateCustomButtonWithIcon(hwnd, IDC_NOTES_OK,     okTxt,  ButtonColor::Green,
            L"imageres.dll", 89,  startX,              btnY2, wOK_n,  S(38), hInst);
        CreateCustomButtonWithIcon(hwnd, IDC_NOTES_CANCEL, canTxt, ButtonColor::Red,
            L"shell32.dll",  131, startX+wOK_n+S(10),  btnY2, wCnl_n, S(38), hInst);

        // ── Tooltips ──────────────────────────────────────────────────────────
        SetButtonTooltip(hBold,     L"Bold");
        SetButtonTooltip(hItalic,   L"Italic");
        SetButtonTooltip(hUnder,    L"Underline");
        SetButtonTooltip(hSub,      L"Subscript  (e.g. H\u2082O)");
        SetButtonTooltip(hSup,      L"Superscript  (e.g. m\u00B2)");
        SetButtonTooltip(hFontSize, L"Font size");
        SetButtonTooltip(hColor,    L"Text color");
        SetButtonTooltip(GetDlgItem(hwnd, IDC_NOTES_TABLE), L"Insert table (2 \u00d7 2)");

        // ── Fonts ─────────────────────────────────────────────────────────────
        NONCLIENTMETRICSW ncm = {}; ncm.cbSize = sizeof(ncm);
        SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(ncm), &ncm, 0);
        ncm.lfMessageFont.lfQuality = CLEARTYPE_QUALITY;
        HFONT hF = CreateFontIndirectW(&ncm.lfMessageFont);
        if (hF) {
            // Apply to every child except the RichEdit (manages its own font).
            auto applyFont = [](HWND hC, LPARAM lp) -> BOOL {
                wchar_t cls[64] = {};
                GetClassNameW(hC, cls, 64);
                if (_wcsicmp(cls, L"RichEdit20W") != 0 && _wcsicmp(cls, L"RICHEDIT50W") != 0)
                    SendMessageW(hC, WM_SETFONT, (WPARAM)(HFONT)lp, TRUE);
                return TRUE;
            };
            EnumChildWindows(hwnd, applyFont, (LPARAM)hF);

            // Bold font for the "B" button.
            NONCLIENTMETRICSW ncmB = ncm; ncmB.lfMessageFont.lfWeight = FW_BOLD;
            HFONT hFB = CreateFontIndirectW(&ncmB.lfMessageFont);
            if (hFB) { SendMessageW(hBold,   WM_SETFONT, (WPARAM)hFB, TRUE); SetPropW(hwnd, L"hNotesBoldFont",   hFB); }

            // Italic font for the "I" button.
            NONCLIENTMETRICSW ncmI = ncm; ncmI.lfMessageFont.lfItalic = TRUE;
            HFONT hFI = CreateFontIndirectW(&ncmI.lfMessageFont);
            if (hFI) { SendMessageW(hItalic, WM_SETFONT, (WPARAM)hFI, TRUE); SetPropW(hwnd, L"hNotesItalicFont", hFI); }

            SetPropW(hwnd, L"hNotesFont", hF);
        }
        SetFocus(hEdit);
        return 0;
    }

    case WM_COMMAND: {
        NotesEditorData* pData = (NotesEditorData*)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
        if (!pData) break;
        HWND hEdit = GetDlgItem(hwnd, IDC_NOTES_EDIT);
        int  wmId  = LOWORD(wParam);
        int  wmEv  = HIWORD(wParam);

        if (wmId == IDC_NOTES_CANCEL || wmId == IDCANCEL) {
            EnableWindow(GetWindow(hwnd, GW_OWNER), TRUE);
            DestroyWindow(hwnd); return 0;
        }
        if (wmId == IDC_NOTES_OK) {
            pData->outRtf    = hEdit ? NotesEditor_StreamOut(hEdit) : L"";
            pData->okClicked = true;
            EnableWindow(GetWindow(hwnd, GW_OWNER), TRUE);
            DestroyWindow(hwnd); return 0;
        }
        if (wmId == IDC_NOTES_BOLD && hEdit) {
            CHARRANGE cr = {}; SendMessageW(hEdit, EM_EXGETSEL, 0, (LPARAM)&cr);
            NotesEditor_ToggleEffect(hEdit, CFM_BOLD,      CFE_BOLD);
            SendMessageW(hEdit, EM_EXSETSEL, 0, (LPARAM)&cr); SetFocus(hEdit); return 0;
        }
        if (wmId == IDC_NOTES_ITALIC && hEdit) {
            CHARRANGE cr = {}; SendMessageW(hEdit, EM_EXGETSEL, 0, (LPARAM)&cr);
            NotesEditor_ToggleEffect(hEdit, CFM_ITALIC,    CFE_ITALIC);
            SendMessageW(hEdit, EM_EXSETSEL, 0, (LPARAM)&cr); SetFocus(hEdit); return 0;
        }
        if (wmId == IDC_NOTES_UNDERLINE && hEdit) {
            CHARRANGE cr = {}; SendMessageW(hEdit, EM_EXGETSEL, 0, (LPARAM)&cr);
            NotesEditor_ToggleEffect(hEdit, CFM_UNDERLINE, CFE_UNDERLINE);
            SendMessageW(hEdit, EM_EXSETSEL, 0, (LPARAM)&cr); SetFocus(hEdit); return 0;
        }
        if (wmId == IDC_NOTES_SUBSCRIPT && hEdit) {
            CHARRANGE cr = {}; SendMessageW(hEdit, EM_EXGETSEL, 0, (LPARAM)&cr);
            NotesEditor_ToggleScript(hEdit, CFE_SUBSCRIPT);
            SendMessageW(hEdit, EM_EXSETSEL, 0, (LPARAM)&cr); SetFocus(hEdit); return 0;
        }
        if (wmId == IDC_NOTES_SUPERSCRIPT && hEdit) {
            CHARRANGE cr = {}; SendMessageW(hEdit, EM_EXGETSEL, 0, (LPARAM)&cr);
            NotesEditor_ToggleScript(hEdit, CFE_SUPERSCRIPT);
            SendMessageW(hEdit, EM_EXSETSEL, 0, (LPARAM)&cr); SetFocus(hEdit); return 0;
        }

        if (wmId == IDC_NOTES_TABLE && hEdit) {
            // Insert a 2-column, 2-row table at the current cursor position.
            // Stream the RTF fragment into the selection (SFF_SELECTION replaces selection).
            const char kTableRtf[] =
                "{\\rtf1"
                "\\trowd\\trgaph108\\trleft0"
                "\\cellx3240\\cellx6840"
                "\\intbl\\cell\\intbl\\cell\\row"
                "\\trowd\\trgaph108\\trleft0"
                "\\cellx3240\\cellx6840"
                "\\intbl\\cell\\intbl\\cell\\row}";
            // Conversion to bytes for EDITSTREAM
            struct { const char* p; LONG remaining; } ctx{ kTableRtf, (LONG)sizeof(kTableRtf)-1 };
            EDITSTREAM es{};
            es.pfnCallback = [](DWORD_PTR cookie, LPBYTE buf, LONG cb, LONG* done) -> DWORD {
                auto* c = reinterpret_cast<decltype(ctx)*>(cookie);
                *done = std::min(cb, c->remaining);
                memcpy(buf, c->p, *done);
                c->p += *done; c->remaining -= *done;
                return 0;
            };
            es.dwCookie = (DWORD_PTR)&ctx;
            SendMessageW(hEdit, EM_STREAMIN, SF_RTF | SFF_SELECTION, (LPARAM)&es);
            SetFocus(hEdit);
            return 0;
        }
        if (wmId == IDC_NOTES_COLOR && hEdit) {
            CHARRANGE cr = {}; SendMessageW(hEdit, EM_EXGETSEL, 0, (LPARAM)&cr);
            CHOOSECOLORW cc = {};
            static COLORREF s_custColors[16] = {};
            cc.lStructSize  = sizeof(cc);
            cc.hwndOwner    = hwnd;
            cc.lpCustColors = s_custColors;
            cc.Flags        = CC_FULLOPEN | CC_RGBINIT;
            // Seed with the current selection's colour.
            CHARFORMAT2W cfC = {}; cfC.cbSize = sizeof(cfC); cfC.dwMask = CFM_COLOR;
            SendMessageW(hEdit, EM_GETCHARFORMAT, SCF_SELECTION, (LPARAM)&cfC);
            cc.rgbResult = cfC.crTextColor;
            if (ChooseColorW(&cc)) {
                CHARFORMAT2W cfSet = {}; cfSet.cbSize = sizeof(cfSet);
                cfSet.dwMask = CFM_COLOR; cfSet.dwEffects = 0; cfSet.crTextColor = cc.rgbResult;
                SendMessageW(hEdit, EM_SETCHARFORMAT, SCF_SELECTION, (LPARAM)&cfSet);
            }
            SendMessageW(hEdit, EM_EXSETSEL, 0, (LPARAM)&cr);
            SetFocus(hEdit);
            return 0;
        }

        if (wmId == IDC_NOTES_FONTSIZE && wmEv == CBN_SELCHANGE) {
            CHARRANGE cr = {}; SendMessageW(hEdit, EM_EXGETSEL, 0, (LPARAM)&cr);
            HWND hCombo = GetDlgItem(hwnd, IDC_NOTES_FONTSIZE);
            int  sel    = (int)SendMessageW(hCombo, CB_GETCURSEL, 0, 0);
            if (sel >= 0 && sel < s_notesFontCount) {
                CHARFORMAT2W cfSz = {}; cfSz.cbSize = sizeof(cfSz);
                cfSz.dwMask  = CFM_SIZE;
                cfSz.yHeight = s_notesFontSizes[sel] * 20; // twips
                SendMessageW(hEdit, EM_SETCHARFORMAT, SCF_SELECTION, (LPARAM)&cfSz);
            }
            SendMessageW(hEdit, EM_EXSETSEL, 0, (LPARAM)&cr);
            SetFocus(hEdit);
            return 0;
        }

        // EN_CHANGE from the RichEdit → refresh status.
        if (wmId == IDC_NOTES_EDIT && wmEv == EN_CHANGE && hEdit)
            NotesEditor_UpdateStatus(hwnd, hEdit, pData->charsLeftFmt);
        break;
    }

    case WM_DRAWITEM: {
        LPDRAWITEMSTRUCT dis = (LPDRAWITEMSTRUCT)lParam;
        if (dis->CtlID == IDC_NOTES_OK || dis->CtlID == IDC_NOTES_CANCEL) {
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

    case WM_CTLCOLORBTN: {
        // Make push-button backgrounds white so the toolbar area matches the dialog.
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

    case WM_DESTROY: {
        HFONT hF  = (HFONT)GetPropW(hwnd, L"hNotesFont");       if (hF)  { DeleteObject(hF);  RemovePropW(hwnd, L"hNotesFont"); }
        HFONT hFB = (HFONT)GetPropW(hwnd, L"hNotesBoldFont");   if (hFB) { DeleteObject(hFB); RemovePropW(hwnd, L"hNotesBoldFont"); }
        HFONT hFI = (HFONT)GetPropW(hwnd, L"hNotesItalicFont"); if (hFI) { DeleteObject(hFI); RemovePropW(hwnd, L"hNotesItalicFont"); }
        break;
    }

    case WM_CLOSE:
        EnableWindow(GetWindow(hwnd, GW_OWNER), TRUE);
        DestroyWindow(hwnd); return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

// ─── Public API ───────────────────────────────────────────────────────────────

bool OpenNotesEditor(HWND hwndParent, NotesEditorData& data)
{
    WNDCLASSEXW wc = {}; wc.cbSize = sizeof(wc);
    if (!GetClassInfoExW(GetModuleHandleW(NULL), L"CompNotesEditor", &wc)) {
        wc.lpfnWndProc   = NotesEditorDlgProc;
        wc.hInstance     = GetModuleHandleW(NULL);
        wc.lpszClassName = L"CompNotesEditor";
        wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
        wc.hCursor       = LoadCursor(NULL, IDC_ARROW);
        RegisterClassExW(&wc);
    }

    int  dlgW   = S(560), dlgH = S(440);
    RECT rcPar  = {}; GetWindowRect(hwndParent, &rcPar);
    int  x      = rcPar.left + (rcPar.right  - rcPar.left - dlgW) / 2;
    int  y      = rcPar.top  + (rcPar.bottom - rcPar.top  - dlgH) / 2;
    const wchar_t* title = data.titleText.empty() ? L"Edit Notes" : data.titleText.c_str();

    HWND hDlg = CreateWindowExW(WS_EX_TOOLWINDOW,
        L"CompNotesEditor", title,
        WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_VISIBLE,
        x, y, dlgW, dlgH, hwndParent, NULL, GetModuleHandleW(NULL), &data);

    EnableWindow(hwndParent, FALSE);

    MSG m;
    while (GetMessageW(&m, NULL, 0, 0) > 0) {
        if (!IsWindow(hDlg)) break;
        TranslateMessage(&m); DispatchMessageW(&m);
    }

    // Re-enable owner (safe no-op if WM_CLOSE/WM_COMMAND already did it),
    // then bring it to the foreground via a TOPMOST flash.
    EnableWindow(hwndParent, TRUE);
    SetWindowPos(hwndParent, HWND_TOPMOST,   0,0,0,0, SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE);
    SetWindowPos(hwndParent, HWND_NOTOPMOST, 0,0,0,0, SWP_NOMOVE|SWP_NOSIZE);
    return data.okClicked;
}
