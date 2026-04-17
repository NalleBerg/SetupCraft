/*
 * script_edit_dialog.cpp — "Add/Edit Script" modal dialog for SetupCraft.
 *
 * Follows the measure-then-create pattern from dep_edit_dialog.cpp:
 *   - RegisterClassExW once (static guard)
 *   - CreateWindowExW with CREATESTRUCT passing ScriptDlgData*
 *   - EnableWindow(parent, FALSE) + private message loop
 *   - DestroyWindow on OK/Cancel
 */

#include "script_edit_dialog.h"
#include "scripts.h"        // ScrWhenToRun enum, SCR_TYPE_BAT/PS1
#include "button.h"         // CreateCustomButtonWithIcon(), MeasureButtonWidth()
#include "checkbox.h"       // CreateCustomCheckbox(), DrawCustomCheckbox()
#include "ctrlw.h"          // ShowValidationDialog()
#include "dpi.h"            // S()
#include <commdlg.h>        // GetOpenFileNameW()
#include <shellapi.h>       // ShellExecuteW()
#include <fstream>          // std::ifstream (ReadScriptFile)

// ── Scintilla runtime integration ────────────────────────────────────────────────
// Loaded at runtime from Scintilla.dll + Lexilla.dll — no import lib required.
// Constants taken from ScintillaMessages.h (numeric to avoid enum-class casts).
#define SCI_SETCODEPAGE            2037
#define SCI_SETEOLMODE             2031
#define SCI_SETILEXER              4033
#define SCI_STYLECLEARALL          2050
#define SCI_STYLESETFORE           2051
#define SCI_STYLESETBACK           2052
#define SCI_STYLESETBOLD           2053
#define SCI_STYLESETSIZE           2055
#define SCI_STYLESETFONT           2056
#define SCI_SETTEXT                2181
#define SCI_GETLENGTH              2006
#define SCI_GETTEXT                2182
#define SCI_SETTABWIDTH            2036
#define SCI_SETCARETLINEVISIBLE    2096
#define SCI_SETCARETLINEBACK       2098
#define SCI_SETMARGINTYPEN         2240
#define SCI_SETMARGINWIDTHN        2242
#define SCI_SETSCROLLWIDTH         2274
#define SCI_SETSCROLLWIDTHTRACKING 2516
#define SC_CP_UTF8                 65001
#define SC_EOL_CRLF                0

static HMODULE s_hSciDll = NULL;
static HMODULE s_hLexDll = NULL;
typedef int   (*SciRegFn)(void*);
typedef void* (*CreateLexerFn)(const char* name);
static CreateLexerFn s_pfnCreateLexer = nullptr;

static bool SciLoad(HINSTANCE hInst)
{
    if (s_hSciDll) return true;
    s_hSciDll = LoadLibraryW(L"Scintilla.dll");
    s_hLexDll = LoadLibraryW(L"Lexilla.dll");
    if (!s_hSciDll) return false;
    auto pfnReg = (SciRegFn)GetProcAddress(s_hSciDll, "Scintilla_RegisterClasses");
    if (pfnReg) pfnReg(hInst);
    if (s_hLexDll)
        s_pfnCreateLexer = (CreateLexerFn)GetProcAddress(s_hLexDll, "CreateLexer");
    return true;
}

static std::string SciW2A(const std::wstring& ws)
{
    if (ws.empty()) return "";
    int n = WideCharToMultiByte(CP_UTF8, 0, ws.c_str(), (int)ws.size(), nullptr, 0, nullptr, nullptr);
    std::string s(n, '\0');
    WideCharToMultiByte(CP_UTF8, 0, ws.c_str(), (int)ws.size(), &s[0], n, nullptr, nullptr);
    return s;
}

static std::wstring SciA2W(const std::string& s)
{
    if (s.empty()) return L"";
    int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0);
    std::wstring ws(n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), &ws[0], n);
    return ws;
}

static void SciSetText(HWND hSci, const std::wstring& text)
{
    std::string u = SciW2A(text);
    SendMessageA(hSci, SCI_SETTEXT, 0, (LPARAM)u.c_str());
}

static std::wstring SciGetText(HWND hSci)
{
    int len = (int)SendMessageW(hSci, SCI_GETLENGTH, 0, 0);
    if (len <= 0) return L"";
    std::string buf(len + 1, '\0');
    SendMessageA(hSci, SCI_GETTEXT, (WPARAM)(len + 1), (LPARAM)buf.data());
    buf.resize(len);
    return SciA2W(buf);
}

static void SciApplyHighlighting(HWND hSci, int scrType)
{
    if (s_pfnCreateLexer) {
        void* lexer = s_pfnCreateLexer(scrType == SCR_TYPE_PS1 ? "powershell" : "batch");
        if (lexer) SendMessageW(hSci, SCI_SETILEXER, 0, (LPARAM)lexer);
    }
    // Set default style (32 = STYLE_DEFAULT), then propagate to all 256 styles
    SendMessageA(hSci, SCI_STYLESETFONT, 32, (LPARAM)"Consolas");
    SendMessageW(hSci, SCI_STYLESETSIZE, 32, 11);
    SendMessageW(hSci, SCI_STYLESETBACK, 32, RGB(255, 255, 255));
    SendMessageW(hSci, SCI_STYLESETFORE, 32, RGB(  0,   0,   0));
    SendMessageW(hSci, SCI_STYLECLEARALL, 0, 0);
    // Line number margin (margin 0), hide symbol margin (margin 1)
    SendMessageW(hSci, SCI_SETMARGINTYPEN,  0, 1);   // SC_MARGIN_NUMBER
    SendMessageW(hSci, SCI_SETMARGINWIDTHN, 0, 44);
    SendMessageW(hSci, SCI_SETMARGINWIDTHN, 1, 0);   // hide
    SendMessageW(hSci, SCI_STYLESETBACK, 33, RGB(240, 240, 240)); // 33 = STYLE_LINENUMBER
    SendMessageW(hSci, SCI_STYLESETFORE, 33, RGB(110, 110, 110));
    SendMessageA(hSci, SCI_STYLESETFONT, 33, (LPARAM)"Consolas");
    SendMessageW(hSci, SCI_STYLESETSIZE, 33, 9);
    // Highlight current line
    SendMessageW(hSci, SCI_SETCARETLINEVISIBLE, TRUE, 0);
    SendMessageW(hSci, SCI_SETCARETLINEBACK, RGB(232, 242, 254), 0);
    // Scroll width auto-tracking
    SendMessageW(hSci, SCI_SETSCROLLWIDTHTRACKING, TRUE, 0);
    SendMessageW(hSci, SCI_SETSCROLLWIDTH, 1, 0);

    if (scrType == SCR_TYPE_BAT) {
        // SCE_BAT_COMMENT=1  SCE_BAT_WORD=2  SCE_BAT_LABEL=3
        // SCE_BAT_HIDE=4     SCE_BAT_COMMAND=5  SCE_BAT_IDENTIFIER=6
        SendMessageW(hSci, SCI_STYLESETFORE, 1, RGB(  0, 128,   0)); // REM — green
        SendMessageW(hSci, SCI_STYLESETFORE, 2, RGB(  0,   0, 200)); // echo/if/set — blue
        SendMessageW(hSci, SCI_STYLESETBOLD, 2, TRUE);
        SendMessageW(hSci, SCI_STYLESETFORE, 3, RGB(100,   0, 150)); // :label — purple
        SendMessageW(hSci, SCI_STYLESETBOLD, 3, TRUE);
        SendMessageW(hSci, SCI_STYLESETFORE, 4, RGB(130, 130, 130)); // @echo — grey
        SendMessageW(hSci, SCI_STYLESETFORE, 5, RGB(  0, 100, 200)); // external command
        SendMessageW(hSci, SCI_STYLESETFORE, 6, RGB(180,  60,   0)); // %variable% — orange
        SendMessageW(hSci, SCI_STYLESETBOLD, 6, TRUE);
    } else {
        // SCE_POWERSHELL_COMMENT=1  SCE_POWERSHELL_STRING=2   SCE_POWERSHELL_CHARACTER=3
        // SCE_POWERSHELL_NUMBER=4   SCE_POWERSHELL_VARIABLE=5  SCE_POWERSHELL_KEYWORD=8
        // SCE_POWERSHELL_CMDLET=9
        SendMessageW(hSci, SCI_STYLESETFORE, 1, RGB(  0, 128,   0)); // # comment — green
        SendMessageW(hSci, SCI_STYLESETFORE, 2, RGB(163,  21,  21)); // "string" — rust
        SendMessageW(hSci, SCI_STYLESETFORE, 3, RGB(163,  21,  21)); // 'string'
        SendMessageW(hSci, SCI_STYLESETFORE, 4, RGB(  9, 134,  88)); // number — teal
        SendMessageW(hSci, SCI_STYLESETFORE, 5, RGB(  0,  16, 128)); // $variable — navy
        SendMessageW(hSci, SCI_STYLESETFORE, 8, RGB(  0,   0, 220)); // keyword — blue
        SendMessageW(hSci, SCI_STYLESETBOLD, 8, TRUE);
        SendMessageW(hSci, SCI_STYLESETFORE, 9, RGB(  0,  90, 180)); // cmdlet
    }
}

static HWND SciCreateEditor(HWND hParent, int ctlId, HINSTANCE hInst,
                             int x, int y, int w, int h)
{
    if (!SciLoad(hInst)) return NULL;
    HWND hSci = CreateWindowExW(
        WS_EX_CLIENTEDGE, L"Scintilla", L"",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_VSCROLL,
        x, y, w, h, hParent, (HMENU)(UINT_PTR)ctlId, hInst, NULL);
    if (!hSci) return NULL;
    SendMessageW(hSci, SCI_SETCODEPAGE, SC_CP_UTF8, 0);
    SendMessageW(hSci, SCI_SETEOLMODE,  SC_EOL_CRLF, 0);
    SendMessageW(hSci, SCI_SETTABWIDTH, 4, 0);
    return hSci;
}

// ── Layout constants (design pixels @ 96 DPI) ─────────────────────────────────
static const int SD_PAD_H    = 20;
static const int SD_PAD_T    = 20;
static const int SD_PAD_B    = 24;
static const int SD_GAP      = 10;
static const int SD_GAP_SM   =  4;
static const int SD_BTN_H    = 34;
static const int SD_BTN_GAP  = 15;
static const int SD_CONT_W   = 700;
static const int SD_LABEL_H  = 18;
static const int SD_EDIT_H   = 26;
static const int SD_CB_H     = 22;
static const int SD_COMBO_H  = 26;
static const int SD_CONTENT_H= 300;   // multiline script editor height (auto-stretched)

// ── Per-invocation state ──────────────────────────────────────────────────────
struct ScriptDlgData {
    DB::ScriptRow scr;
    HINSTANCE     hInst;
    const std::map<std::wstring, std::wstring>* pLocale;
    const std::vector<DB::ScriptRow>*           pExisting; // for dup-name check
    bool    okPressed;
    HWND    hSci;   // Scintilla editor window (NULL if DLLs unavailable)
};

static bool s_scrDlgOk = false;

// ── Locale helper ─────────────────────────────────────────────────────────────
static std::wstring SL(const ScriptDlgData* d, const wchar_t* key, const wchar_t* fb)
{
    auto it = d->pLocale->find(key);
    return (it != d->pLocale->end()) ? it->second : fb;
}

// ── GetEditText helper ────────────────────────────────────────────────────────
static std::wstring GetEditText(HWND hDlg, int id)
{
    HWND h = GetDlgItem(hDlg, id);
    if (!h) return L"";
    int len = GetWindowTextLengthW(h);
    if (len <= 0) return L"";
    std::wstring s(len + 1, L'\0');
    GetWindowTextW(h, &s[0], len + 1);
    s.resize(len);
    return s;
}

// ── UpdateWhenVisibility — show/hide Finish-label row ─────────────────────────
// The "label on Finish page" field is only relevant for SWR_FINISH_OPTOUT.
static void UpdateWhenVisibility(HWND hDlg)
{
    HWND hCombo = GetDlgItem(hDlg, IDC_SCRDLG_WHEN_COMBO);
    int sel = hCombo ? (int)SendMessageW(hCombo, CB_GETCURSEL, 0, 0) : 0;
    // CB item data directly stores the ScrWhenToRun value (0-3)
    int when = SWR_AFTER_FILES;
    if (sel >= 0) {
        LRESULT v = SendMessageW(hCombo, CB_GETITEMDATA, (WPARAM)sel, 0);
        if (v != CB_ERR) when = (int)v;
    }
    bool showFinish  = (when == (int)SWR_FINISH_OPTOUT);
    bool showUninstall = true;   // "also_uninstall" is always shown

    HWND hFlbl = GetDlgItem(hDlg, IDC_SCRDLG_FINISH_LABEL_LBL);
    HWND hFed  = GetDlgItem(hDlg, IDC_SCRDLG_FINISH_LABEL);
    if (hFlbl) ShowWindow(hFlbl, showFinish ? SW_SHOW : SW_HIDE);
    if (hFed)  ShowWindow(hFed,  showFinish ? SW_SHOW : SW_HIDE);
}

// ── IsDupName — true if any script other than editingId already has this name ──
static bool IsDupName(const std::wstring& name,
                       const std::vector<DB::ScriptRow>& existing,
                       int editingId)
{
    for (const auto& s : existing)
        if (s.id != editingId && s.name == name) return true;
    return false;
}

// ── Rename-on-duplicate dialog ────────────────────────────────────────────────
// A small modal that lets the developer choose a different name when a conflict
// is detected.  Controls use the same custom-button pattern as the main dialog.

struct RenameDlgCtx {
    HINSTANCE   hInst;
    const std::vector<DB::ScriptRow>*           pExisting;
    int         editingId;     // the script being edited — excluded from dup check
    std::wstring result;       // written on OK
    bool        ok;
    const std::map<std::wstring,std::wstring>* pLocale;
};

static std::wstring RLoc(const RenameDlgCtx* c, const wchar_t* key, const wchar_t* fb)
{
    auto it = c->pLocale->find(key);
    return (it != c->pLocale->end()) ? it->second : fb;
}

static LRESULT CALLBACK RenameDlgProc(HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam)
{
    RenameDlgCtx* ctx = (RenameDlgCtx*)GetWindowLongPtrW(hDlg, GWLP_USERDATA);
    switch (msg) {
    case WM_CREATE: {
        CREATESTRUCTW* cs = (CREATESTRUCTW*)lParam;
        ctx = (RenameDlgCtx*)cs->lpCreateParams;
        SetWindowLongPtrW(hDlg, GWLP_USERDATA, (LONG_PTR)ctx);
        return 0;
    }
    case WM_DRAWITEM: {
        LPDRAWITEMSTRUCT dis = (LPDRAWITEMSTRUCT)lParam;
        if (dis->CtlID == IDC_SCRDLG_RENAME_OK ||
            dis->CtlID == IDC_SCRDLG_RENAME_CANCEL) {
            ButtonColor color = (ButtonColor)GetWindowLongPtr(dis->hwndItem, GWLP_USERDATA);
            HFONT hF = CreateFontW(-S(12), 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
                DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
            LRESULT r = DrawCustomButton(dis, color, hF);
            if (hF) DeleteObject(hF);
            return r;
        }
        return FALSE;
    }
    case WM_COMMAND: {
        int id = LOWORD(wParam);
        if (id == IDC_SCRDLG_RENAME_OK) {
            HWND hEd = GetDlgItem(hDlg, IDC_SCRDLG_RENAME_EDIT);
            int len = hEd ? GetWindowTextLengthW(hEd) : 0;
            std::wstring name;
            if (len > 0) {
                name.resize(len + 1);
                GetWindowTextW(hEd, &name[0], len + 1);
                name.resize(len);
            }
            // Trim leading/trailing whitespace
            size_t first = name.find_first_not_of(L" \t");
            size_t last  = name.find_last_not_of(L" \t");
            name = (first == std::wstring::npos) ? L"" : name.substr(first, last - first + 1);

            if (name.empty()) {
                ShowValidationDialog(hDlg,
                    RLoc(ctx, L"validation_error",       L"Validation Error"),
                    RLoc(ctx, L"scr_dlg_dup_err_empty",  L"Please enter a name."),
                    *ctx->pLocale);
                SetFocus(hEd);
                return 0;
            }
            if (IsDupName(name, *ctx->pExisting, ctx->editingId)) {
                std::wstring tmpl = RLoc(ctx, L"scr_dlg_dup_err_taken",
                    L"A script named \u201c<<Name>>\u201d is also taken.\nPlease choose a different name.");
                std::wstring m = tmpl;
                auto p = m.find(L"<<Name>>");
                if (p != std::wstring::npos) m.replace(p, 8, name);
                ShowValidationDialog(hDlg,
                    RLoc(ctx, L"validation_error", L"Validation Error"),
                    m, *ctx->pLocale);
                SetFocus(hEd);
                return 0;
            }
            ctx->result = name;
            ctx->ok = true;
            DestroyWindow(hDlg);
            return 0;
        }
        if (id == IDC_SCRDLG_RENAME_CANCEL) {
            ctx->ok = false;
            DestroyWindow(hDlg);
            return 0;
        }
        return 0;
    }
    case WM_CTLCOLORSTATIC: {
        HDC hdc = (HDC)wParam;
        SetBkColor  (hdc, GetSysColor(COLOR_WINDOW));
        SetTextColor(hdc, GetSysColor(COLOR_WINDOWTEXT));
        return (LRESULT)GetSysColorBrush(COLOR_WINDOW);
    }
    case WM_KEYDOWN:
        if (wParam == VK_ESCAPE) { if (ctx) { ctx->ok = false; } DestroyWindow(hDlg); return 0; }
        break;
    case WM_CLOSE:
        if (ctx) ctx->ok = false;
        DestroyWindow(hDlg);
        return 0;
    }
    return DefWindowProcW(hDlg, msg, wParam, lParam);
}

// Returns the chosen replacement name, or empty string if the developer cancelled.
static std::wstring ShowRenameDialog(
    HWND hwndParent, HINSTANCE hInst,
    const std::wstring& conflictName,
    const std::vector<DB::ScriptRow>& existing,
    int editingId,
    const std::map<std::wstring,std::wstring>& locale)
{
    static bool s_renameClassReg = false;
    if (!s_renameClassReg) {
        WNDCLASSEXW wc = {};
        wc.cbSize        = sizeof(wc);
        wc.style         = CS_HREDRAW | CS_VREDRAW;
        wc.lpfnWndProc   = RenameDlgProc;
        wc.hInstance     = hInst;
        wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
        wc.lpszClassName = L"ScrRenameDialog";
        wc.hCursor       = LoadCursor(NULL, IDC_ARROW);
        RegisterClassExW(&wc);
        s_renameClassReg = true;
    }

    // Locale helpers
    auto RL = [&](const wchar_t* k, const wchar_t* fb) {
        auto it = locale.find(k); return it != locale.end() ? it->second : std::wstring(fb);
    };

    // Build conflict message text, replacing <<Name>> placeholder
    std::wstring msgTmpl = RL(L"scr_dlg_dup_msg",
        L"A script named \u201c<<Name>>\u201d already exists.\nEnter a different name:");
    std::wstring msg = msgTmpl;
    { auto p = msg.find(L"<<Name>>"); if (p != std::wstring::npos) msg.replace(p, 8, conflictName); }

    std::wstring title  = RL(L"scr_dlg_dup_title",  L"Duplicate Script Name");
    std::wstring lblTxt = RL(L"scr_dlg_dup_lbl",    L"New name:");
    std::wstring okTxt  = RL(L"scr_dlg_dup_ok",     L"Rename");
    std::wstring canTxt = RL(L"scr_dlg_dup_cancel",  L"Cancel");

    const int pad  = S(16);
    const int gap  = S(8);
    const int cW   = S(300);
    const int msgH = S(44);
    const int lblH = S(16);
    const int edH  = S(24);
    const int btnH = S(28);
    const int padB = S(16);

    int totalH = pad + msgH + gap + lblH + S(4) + edH + gap + btnH + padB;

    const DWORD style   = WS_POPUP | WS_CAPTION | WS_SYSMENU;
    const DWORD exStyle = WS_EX_DLGMODALFRAME;

    RECT rc = { 0, 0, cW + pad * 2, S(totalH) };
    AdjustWindowRectEx(&rc, style, FALSE, exStyle);
    int dlgW = rc.right  - rc.left;
    int dlgH = rc.bottom - rc.top;

    // Centre over the parent (the script edit dialog)
    RECT rcP; GetWindowRect(hwndParent, &rcP);
    int dlgX = rcP.left + (rcP.right  - rcP.left - dlgW) / 2;
    int dlgY = rcP.top  + (rcP.bottom - rcP.top  - dlgH) / 2;

    RenameDlgCtx ctx;
    ctx.hInst     = hInst;
    ctx.pExisting = &existing;
    ctx.editingId = editingId;
    ctx.ok        = false;
    ctx.pLocale   = &locale;

    // Disable parent BEFORE creating the dialog (standard modal pattern)
    EnableWindow(hwndParent, FALSE);

    HWND hDlg = CreateWindowExW(exStyle, L"ScrRenameDialog", title.c_str(),
        style, dlgX, dlgY, dlgW, dlgH, hwndParent, NULL, hInst, &ctx);
    if (!hDlg) {
        EnableWindow(hwndParent, TRUE);
        return L"";
    }

    // Build controls
    RECT rcC; GetClientRect(hDlg, &rcC);
    int cw = rcC.right - pad * 2;
    int y  = pad;

    // Body font from system metrics
    NONCLIENTMETRICSW ncm = {}; ncm.cbSize = sizeof(ncm);
    SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(ncm), &ncm, 0);
    if (ncm.lfMessageFont.lfHeight < 0)
        ncm.lfMessageFont.lfHeight = (LONG)(ncm.lfMessageFont.lfHeight * 1.1f);
    ncm.lfMessageFont.lfQuality = CLEARTYPE_QUALITY;
    HFONT hFont = CreateFontIndirectW(&ncm.lfMessageFont);

    // Message label
    { HWND h = CreateWindowExW(0, L"STATIC", msg.c_str(),
          WS_CHILD | WS_VISIBLE | SS_LEFT,
          pad, y, cw, msgH, hDlg,
          (HMENU)(UINT_PTR)IDC_SCRDLG_RENAME_MSG, hInst, NULL);
      if (hFont) SendMessageW(h, WM_SETFONT, (WPARAM)hFont, FALSE); }
    y += msgH + gap;

    // "New name:" label
    { HWND h = CreateWindowExW(0, L"STATIC", lblTxt.c_str(),
          WS_CHILD | WS_VISIBLE | SS_LEFT,
          pad, y, cw, lblH, hDlg, NULL, hInst, NULL);
      if (hFont) SendMessageW(h, WM_SETFONT, (WPARAM)hFont, FALSE); }
    y += lblH + S(4);

    // Name edit — pre-filled and fully selected so typing immediately replaces it
    { HWND hEd = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", conflictName.c_str(),
          WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
          pad, y, cw, edH, hDlg,
          (HMENU)(UINT_PTR)IDC_SCRDLG_RENAME_EDIT, hInst, NULL);
      if (hFont) SendMessageW(hEd, WM_SETFONT, (WPARAM)hFont, FALSE);
      SendMessageW(hEd, EM_SETSEL, 0, -1); }
    y += edH + gap;

    // OK / Cancel buttons (centred)
    { int wOk  = MeasureButtonWidth(okTxt,  true) + S(16);
      int wCan = MeasureButtonWidth(canTxt, true) + S(16);
      int bx   = pad + (cw - wOk - S(10) - wCan) / 2;
      CreateCustomButtonWithIcon(hDlg, IDC_SCRDLG_RENAME_OK, okTxt.c_str(),
          ButtonColor::Blue, L"shell32.dll", 258, bx, y, wOk, btnH, hInst);
      CreateCustomButtonWithIcon(hDlg, IDC_SCRDLG_RENAME_CANCEL, canTxt.c_str(),
          ButtonColor::Red, L"shell32.dll", 131, bx + wOk + S(10), y, wCan, btnH, hInst); }

    ShowWindow(hDlg, SW_SHOW);
    // Set focus to the edit control so the developer can type immediately
    SetFocus(GetDlgItem(hDlg, IDC_SCRDLG_RENAME_EDIT));

    MSG m;
    while (IsWindow(hDlg) && GetMessageW(&m, NULL, 0, 0) > 0) {
        if (!IsDialogMessageW(hDlg, &m)) {
            TranslateMessage(&m);
            DispatchMessageW(&m);
        }
    }

    EnableWindow(hwndParent, TRUE);
    SetForegroundWindow(hwndParent);

    if (hFont) DeleteObject(hFont);
    return ctx.ok ? ctx.result : L"";
}



// ── ReadScriptFile (duplicate from scripts.cpp — keeps dialog self-contained) ──
static bool ReadScriptFile(const wchar_t* path, std::wstring& out)
{
    std::ifstream ifs(path, std::ios::binary);
    if (!ifs) return false;
    std::string bytes((std::istreambuf_iterator<char>(ifs)), {});
    int wlen = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
                                   bytes.c_str(), (int)bytes.size(), NULL, 0);
    if (wlen > 0) {
        out.resize(wlen);
        MultiByteToWideChar(CP_UTF8, 0, bytes.c_str(), (int)bytes.size(), &out[0], wlen);
    } else {
        int alen = MultiByteToWideChar(CP_ACP, 0,
                                       bytes.c_str(), (int)bytes.size(), NULL, 0);
        out.resize(alen);
        MultiByteToWideChar(CP_ACP, 0, bytes.c_str(), (int)bytes.size(), &out[0], alen);
    }
    // Normalise to CRLF
    std::wstring norm; norm.reserve(out.size() + 64);
    for (size_t i = 0; i < out.size(); ++i) {
        if (out[i] == L'\n' && (i == 0 || out[i-1] != L'\r')) norm += L'\r';
        norm += out[i];
    }
    out = std::move(norm);
    return true;
}

// ── Dialog procedure ──────────────────────────────────────────────────────────
static LRESULT CALLBACK ScrDlgProc(HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam)
{
    ScriptDlgData* pData = (ScriptDlgData*)GetWindowLongPtrW(hDlg, GWLP_USERDATA);

    switch (msg) {
    case WM_CREATE: {
        CREATESTRUCTW* cs = (CREATESTRUCTW*)lParam;
        pData = (ScriptDlgData*)cs->lpCreateParams;
        SetWindowLongPtrW(hDlg, GWLP_USERDATA, (LONG_PTR)pData);
        return 0;
    }

    case WM_DRAWITEM: {
        LPDRAWITEMSTRUCT dis = (LPDRAWITEMSTRUCT)lParam;
        if (DrawCustomCheckbox(dis)) return TRUE;
        if (dis->CtlID == IDC_SCRDLG_OK     ||
            dis->CtlID == IDC_SCRDLG_CANCEL  ||
            dis->CtlID == IDC_SCRDLG_LOAD    ||
            dis->CtlID == IDC_SCRDLG_TEST) {
            ButtonColor color = (ButtonColor)GetWindowLongPtr(dis->hwndItem, GWLP_USERDATA);
            HFONT hFont = CreateFontW(-S(12), 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
                DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
            LRESULT r = DrawCustomButton(dis, color, hFont);
            if (hFont) DeleteObject(hFont);
            return r;
        }
        return FALSE;
    }

    case WM_COMMAND: {
        int wmId    = LOWORD(wParam);
        int wmEvent = HIWORD(wParam);

        if (wmId == IDC_SCRDLG_WHEN_COMBO && wmEvent == CBN_SELCHANGE) {
            UpdateWhenVisibility(hDlg);
            return 0;
        }

        // Type radio changes: re-apply syntax highlighting for new language
        if ((wmId == IDC_SCRDLG_TYPE_BAT || wmId == IDC_SCRDLG_TYPE_PS1) &&
             wmEvent == BN_CLICKED) {
            if (pData && pData->hSci)
                SciApplyHighlighting(pData->hSci,
                    wmId == IDC_SCRDLG_TYPE_BAT ? SCR_TYPE_BAT : SCR_TYPE_PS1);
            return 0;
        }

        // Load from file
        if (wmId == IDC_SCRDLG_LOAD && wmEvent == BN_CLICKED) {
            OPENFILENAMEW ofn = {};
            wchar_t szFile[MAX_PATH] = {};
            ofn.lStructSize  = sizeof(OPENFILENAMEW);
            ofn.hwndOwner    = hDlg;
            ofn.lpstrFile    = szFile;
            ofn.nMaxFile     = MAX_PATH;
            ofn.lpstrFilter  = L"Script Files (*.bat;*.cmd;*.ps1)\0*.bat;*.cmd;*.ps1\0All Files\0*.*\0";
            ofn.nFilterIndex = 1;
            ofn.Flags = OFN_EXPLORER | OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST | OFN_HIDEREADONLY;
            if (GetOpenFileNameW(&ofn)) {
                std::wstring content;
                if (ReadScriptFile(szFile, content)) {
                    // Detect type from extension, update radios
                    std::wstring p(szFile);
                    auto dot = p.rfind(L'.');
                    bool isBat = true;
                    if (dot != std::wstring::npos) {
                        std::wstring ext = p.substr(dot);
                        for (auto& c : ext) c = (wchar_t)towlower(c);
                        isBat = (ext == L".bat" || ext == L".cmd");
                        SendDlgItemMessageW(hDlg, IDC_SCRDLG_TYPE_BAT,
                            BM_SETCHECK, isBat ? BST_CHECKED : BST_UNCHECKED, 0);
                        SendDlgItemMessageW(hDlg, IDC_SCRDLG_TYPE_PS1,
                            BM_SETCHECK, isBat ? BST_UNCHECKED : BST_CHECKED, 0);
                    }
                    // Apply correct lexer then load text
                    if (pData && pData->hSci) {
                        SciApplyHighlighting(pData->hSci, isBat ? SCR_TYPE_BAT : SCR_TYPE_PS1);
                        SciSetText(pData->hSci, content);
                    }
                }
            }
            return 0;
        }

        // Test in terminal — write a temp file and ShellExecute it
        if (wmId == IDC_SCRDLG_TEST && wmEvent == BN_CLICKED) {
            std::wstring content = GetEditText(hDlg, IDC_SCRDLG_CONTENT);
            if (content.empty()) {
                if (pData) {
                    std::wstring t = SL(pData, L"scr_test_title",      L"Test Script");
                    std::wstring m = SL(pData, L"scr_test_no_content", L"No script content to test.");
                    MessageBoxW(hDlg, m.c_str(), t.c_str(), MB_ICONINFORMATION | MB_OK);
                }
                return 0;
            }
            bool isPs1 = (SendDlgItemMessageW(hDlg, IDC_SCRDLG_TYPE_PS1,
                            BM_GETCHECK, 0, 0) == BST_CHECKED);
            wchar_t tmpDir[MAX_PATH];
            GetTempPathW(MAX_PATH, tmpDir);
            wchar_t tmpFile[MAX_PATH];
            GetTempFileNameW(tmpDir, L"scr", 0, tmpFile);
            // Rename extension
            std::wstring tmpPath(tmpFile);
            auto dot = tmpPath.rfind(L'.');
            if (dot != std::wstring::npos)
                tmpPath = tmpPath.substr(0, dot) + (isPs1 ? L".ps1" : L".bat");
            else
                tmpPath += isPs1 ? L".ps1" : L".bat";
            // Write UTF-8 BOM + content via _wfopen (std::ofstream wstring ctor unavailable on MinGW)
            std::string utfContent;
            int needed = WideCharToMultiByte(CP_UTF8, 0, content.c_str(), -1, NULL, 0, NULL, NULL);
            utfContent.resize(needed - 1);
            WideCharToMultiByte(CP_UTF8, 0, content.c_str(), -1, &utfContent[0], needed, NULL, NULL);
            {
                FILE* fout = _wfopen(tmpPath.c_str(), L"wb");
                if (fout) {
                    if (isPs1) {
                        const char bom[] = "\xEF\xBB\xBF";
                        fwrite(bom, 1, 3, fout);
                    }
                    fwrite(utfContent.c_str(), 1, utfContent.size(), fout);
                    fclose(fout);
                }
            }
            // Open with appropriate host
            if (isPs1) {
                std::wstring args = L"-NoExit -ExecutionPolicy Bypass -File \"" + tmpPath + L"\"";
                ShellExecuteW(hDlg, L"open", L"powershell.exe", args.c_str(), NULL, SW_SHOW);
            } else {
                std::wstring args = L"/k \"" + tmpPath + L"\"";
                ShellExecuteW(hDlg, L"open", L"cmd.exe", args.c_str(), NULL, SW_SHOW);
            }
            return 0;
        }

        // OK — validate then collect
        if (wmId == IDC_SCRDLG_OK && wmEvent == BN_CLICKED) {
            std::wstring name = GetEditText(hDlg, IDC_SCRDLG_NAME);
            if (name.empty()) {
                std::wstring t = SL(pData, L"validation_error",     L"Validation Error");
                std::wstring m = SL(pData, L"scr_dlg_err_no_name",  L"Please enter a name for the script.");
                ShowValidationDialog(hDlg, t, m, *pData->pLocale);
                SetFocus(GetDlgItem(hDlg, IDC_SCRDLG_NAME));
                return 0;
            }
            // Duplicate name check — let the developer rename on the spot
            if (pData->pExisting) {
                for (const auto& s : *pData->pExisting) {
                    if (s.id != pData->scr.id && s.name == name) {
                        std::wstring newName = ShowRenameDialog(
                            hDlg, pData->hInst, name, *pData->pExisting,
                            pData->scr.id, *pData->pLocale);
                        if (newName.empty()) return 0;  // user cancelled
                        name = newName;
                        SetDlgItemTextW(hDlg, IDC_SCRDLG_NAME, name.c_str());
                        break;
                    }
                }
            }
            std::wstring content = (pData && pData->hSci)
                ? SciGetText(pData->hSci)
                : GetEditText(hDlg, IDC_SCRDLG_CONTENT);
            if (content.empty()) {
                std::wstring t = SL(pData, L"validation_error",       L"Validation Error");
                std::wstring m = SL(pData, L"scr_dlg_err_no_content", L"Please enter or load script content.");
                ShowValidationDialog(hDlg, t, m, *pData->pLocale);
                SetFocus(pData && pData->hSci ? pData->hSci : GetDlgItem(hDlg, IDC_SCRDLG_CONTENT));
                return 0;
            }

            if (pData) {
                pData->scr.name    = name;
                pData->scr.content = content;
                pData->scr.type    =
                    (SendDlgItemMessageW(hDlg, IDC_SCRDLG_TYPE_BAT, BM_GETCHECK, 0, 0) == BST_CHECKED)
                    ? SCR_TYPE_BAT : SCR_TYPE_PS1;

                // "When to run" combo: item data holds ScrWhenToRun value
                { HWND hCb = GetDlgItem(hDlg, IDC_SCRDLG_WHEN_COMBO);
                  int sel = hCb ? (int)SendMessageW(hCb, CB_GETCURSEL, 0, 0) : 0;
                  LRESULT v = sel >= 0
                      ? SendMessageW(hCb, CB_GETITEMDATA, (WPARAM)sel, 0) : CB_ERR;
                  pData->scr.when_to_run = (v != CB_ERR) ? (int)v : (int)SWR_AFTER_FILES; }

                pData->scr.run_hidden =
                    (SendDlgItemMessageW(hDlg, IDC_SCRDLG_RUN_HIDDEN, BM_GETCHECK, 0, 0) == BST_CHECKED) ? 1 : 0;
                pData->scr.wait_for_completion =
                    (SendDlgItemMessageW(hDlg, IDC_SCRDLG_WAIT, BM_GETCHECK, 0, 0) == BST_CHECKED) ? 1 : 0;
                pData->scr.also_uninstall =
                    (SendDlgItemMessageW(hDlg, IDC_SCRDLG_ALSO_UNINSTALL, BM_GETCHECK, 0, 0) == BST_CHECKED) ? 1 : 0;
                pData->scr.description = GetEditText(hDlg, IDC_SCRDLG_FINISH_LABEL);
                pData->okPressed = true;
            }
            s_scrDlgOk = true;
            DestroyWindow(hDlg);
            return 0;
        }

        if (wmId == IDC_SCRDLG_CANCEL && wmEvent == BN_CLICKED) {
            s_scrDlgOk = false;
            DestroyWindow(hDlg);
            return 0;
        }
        return 0;
    }

    case WM_CTLCOLORSTATIC: {
        HDC hdc = (HDC)wParam;
        SetBkColor  (hdc, GetSysColor(COLOR_WINDOW));
        SetTextColor(hdc, GetSysColor(COLOR_WINDOWTEXT));
        return (LRESULT)GetSysColorBrush(COLOR_WINDOW);
    }

    case WM_KEYDOWN:
        if (wParam == VK_ESCAPE) {
            s_scrDlgOk = false;
            DestroyWindow(hDlg);
            return 0;
        }
        break;

    case WM_CLOSE:
        s_scrDlgOk = false;
        DestroyWindow(hDlg);
        return 0;

    case WM_DESTROY:
        return 0;
    }
    return DefWindowProcW(hDlg, msg, wParam, lParam);
}

// ── SCR_EditDialog ────────────────────────────────────────────────────────────
bool SCR_EditDialog(HWND hwndParent, HINSTANCE hInst,
                    const std::map<std::wstring, std::wstring>& locale,
                    const std::vector<DB::ScriptRow>& existing,
                    DB::ScriptRow& scr)
{
    s_scrDlgOk = false;

    ScriptDlgData data;
    data.scr       = scr;
    data.hInst     = hInst;
    data.pLocale   = &locale;
    data.pExisting = &existing;
    data.okPressed = false;
    data.hSci      = NULL;

    // ── Register window class once ────────────────────────────────────────────
    static bool s_classRegistered = false;
    if (!s_classRegistered) {
        WNDCLASSEXW wc = {};
        wc.cbSize        = sizeof(wc);
        wc.style         = CS_HREDRAW | CS_VREDRAW;
        wc.lpfnWndProc   = ScrDlgProc;
        wc.hInstance     = hInst;
        wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
        wc.lpszClassName = L"ScrEditDialog";
        wc.hCursor       = LoadCursor(NULL, IDC_ARROW);
        RegisterClassExW(&wc);
        s_classRegistered = true;
    }

    // ── Measure window height ─────────────────────────────────────────────────
    // Two-column compact block: left = name + type radios, right = 3 checkboxes.
    // Rows that are always visible in the compact block:
    //   Row A: name label+edit  (full width)
    //   Row B: [type label | <blank>]  (two columns, same height)
    //   Row C: [Bat radio + PS1 radio stacked | checkboxes stacked]
    // Then: When combo (full width)  → optional Finish-label  → editor  → load/test  → OK/Cancel
    const int SD_BLOCK_H = SD_CB_H * 3 + SD_GAP_SM * 2; // height of the 3-row right column

    int contentH = SD_PAD_T;
    contentH += SD_LABEL_H + SD_GAP_SM + SD_EDIT_H + SD_GAP;  // name
    contentH += SD_LABEL_H + SD_GAP_SM;                        // type label row
    contentH += SD_BLOCK_H + SD_GAP;                           // radios|checkboxes block
    contentH += SD_LABEL_H + SD_GAP_SM + SD_COMBO_H + SD_GAP; // when combo
    // Finish label overlays the top of the editor when visible (no reserved height)
    contentH += SD_CONTENT_H + SD_GAP; // script content (no label row)
    contentH += SD_BTN_H - 6 + SD_GAP;                          // load/test buttons row
    contentH += SD_GAP + SD_BTN_H + SD_PAD_B + 10;             // OK/Cancel (extra breathing room)

    const DWORD dlgStyle   = WS_POPUP | WS_CAPTION | WS_SYSMENU;
    const DWORD dlgExStyle = WS_EX_DLGMODALFRAME;

    RECT rc = { 0, 0, S(SD_CONT_W) + S(SD_PAD_H) * 2, S(contentH) };
    AdjustWindowRectEx(&rc, dlgStyle, FALSE, dlgExStyle);
    int dlgW = rc.right  - rc.left;
    int dlgH = rc.bottom - rc.top;

    // Centre H/V on the monitor that contains hwndParent
    HMONITOR hMon = MonitorFromWindow(hwndParent, MONITOR_DEFAULTTONEAREST);
    MONITORINFO mi = {}; mi.cbSize = sizeof(mi);
    if (hMon && GetMonitorInfoW(hMon, &mi)) {
        RECT& wa = mi.rcWork;
        int maxH = wa.bottom - wa.top - S(20);
        if (dlgH > maxH) dlgH = maxH;
        int dlgX = wa.left + (wa.right  - wa.left - dlgW) / 2;
        int dlgY = wa.top  + (wa.bottom - wa.top  - dlgH) / 2;
        if (dlgX < wa.left)              dlgX = wa.left;
        if (dlgY < wa.top)               dlgY = wa.top;
        if (dlgX + dlgW > wa.right)      dlgX = wa.right  - dlgW;
        if (dlgY + dlgH > wa.bottom)     dlgY = wa.bottom - dlgH;
        rc = { dlgX, dlgY, dlgX + dlgW, dlgY + dlgH }; // reuse rc for position
    } else {
        RECT rcWork; SystemParametersInfoW(SPI_GETWORKAREA, 0, &rcWork, 0);
        int maxH = (rcWork.bottom - rcWork.top) - S(20);
        if (dlgH > maxH) dlgH = maxH;
        rc.left = rcWork.left + (rcWork.right  - rcWork.left - dlgW) / 2;
        rc.top  = rcWork.top  + (rcWork.bottom - rcWork.top  - dlgH) / 2;
        rc.right  = rc.left + dlgW;
        rc.bottom = rc.top  + dlgH;
    }
    int dlgX = rc.left, dlgY = rc.top;

    bool isNew = (scr.id == 0);
    std::wstring dlgTitle;
    { auto it = locale.find(isNew ? L"scr_dlg_add_title" : L"scr_dlg_edit_title");
      dlgTitle = (it != locale.end()) ? it->second : (isNew ? L"Add Script" : L"Edit Script"); }

    HWND hDlg = CreateWindowExW(
        dlgExStyle, L"ScrEditDialog", dlgTitle.c_str(),
        dlgStyle, dlgX, dlgY, dlgW, dlgH,
        hwndParent, NULL, hInst, &data);
    if (!hDlg) return false;

    // ── Build controls ────────────────────────────────────────────────────────
    RECT rcC; GetClientRect(hDlg, &rcC);
    int cW = rcC.right;
    int lx = S(SD_PAD_H);
    int cw = cW - lx * 2;
    int y  = S(SD_PAD_T);

    // Get the body font from the non-client metrics (same as rest of app)
    NONCLIENTMETRICSW ncm = {}; ncm.cbSize = sizeof(ncm);
    SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(ncm), &ncm, 0);
    if (ncm.lfMessageFont.lfHeight < 0)
        ncm.lfMessageFont.lfHeight = (LONG)(ncm.lfMessageFont.lfHeight * 1.1f);
    ncm.lfMessageFont.lfQuality = CLEARTYPE_QUALITY;
    HFONT hFont = CreateFontIndirectW(&ncm.lfMessageFont);

    auto Lbl = [&](const wchar_t* txt, int ht = SD_LABEL_H) {
        HWND h = CreateWindowExW(0, L"STATIC", txt,
            WS_CHILD | WS_VISIBLE | SS_LEFT,
            lx, y, cw, S(ht), hDlg, NULL, hInst, NULL);
        if (hFont) SendMessageW(h, WM_SETFONT, (WPARAM)hFont, FALSE);
        y += S(ht) + S(SD_GAP_SM);
    };
    auto Edit1 = [&](int ctlId, const wchar_t* text = L"") {
        HWND h = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", text,
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
            lx, y, cw, S(SD_EDIT_H), hDlg, (HMENU)(UINT_PTR)ctlId, hInst, NULL);
        if (hFont) SendMessageW(h, WM_SETFONT, (WPARAM)hFont, FALSE);
        y += S(SD_EDIT_H) + S(SD_GAP);
        return h;
    };

    // ── Name ──────────────────────────────────────────────────────────────────
    { auto it = locale.find(L"scr_dlg_name");
      Lbl(it != locale.end() ? it->second.c_str() : L"Script name:"); }
    Edit1(IDC_SCRDLG_NAME, scr.name.c_str());

    // ── Two-column block: type radios (left) | checkboxes (right) ─────────────
    // Column split: left ~45%, right ~55% (checkboxes need more text space)
    {
        const int colGap = S(20);
        const int colLW  = cw * 45 / 100;          // left column width
        const int colRX  = lx + colLW + colGap;    // right column x
        const int colRW  = cw - colLW - colGap;    // right column width
        const int rH     = S(SD_CB_H);
        int yL = y, yR = y;                         // independent y cursors

        // Left: "Script type:" label
        { auto it = locale.find(L"scr_dlg_type");
          const wchar_t* txt = (it != locale.end() ? it->second.c_str() : L"Script type:");
          HWND h = CreateWindowExW(0, L"STATIC", txt, WS_CHILD | WS_VISIBLE | SS_LEFT,
              lx, yL, colLW, S(SD_LABEL_H), hDlg, NULL, hInst, NULL);
          if (hFont) SendMessageW(h, WM_SETFONT, (WPARAM)hFont, FALSE);
          yL += S(SD_LABEL_H) + S(SD_GAP_SM); }

        // Left: Batch radio
        { auto it = locale.find(L"scr_dlg_type_bat");
          HWND hBat = CreateWindowExW(0, L"BUTTON",
              (it != locale.end() ? it->second.c_str() : L"Batch (.bat / .cmd)"),
              WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTORADIOBUTTON | BS_NOTIFY,
              lx, yL, colLW, rH, hDlg, (HMENU)(UINT_PTR)IDC_SCRDLG_TYPE_BAT, hInst, NULL);
          if (hFont) SendMessageW(hBat, WM_SETFONT, (WPARAM)hFont, FALSE);
          SendMessageW(hBat, BM_SETCHECK, scr.type == SCR_TYPE_BAT ? BST_CHECKED : BST_UNCHECKED, 0);
          yL += rH + S(SD_GAP_SM); }

        // Left: PowerShell radio
        { auto it = locale.find(L"scr_dlg_type_ps1");
          HWND hPs1 = CreateWindowExW(0, L"BUTTON",
              (it != locale.end() ? it->second.c_str() : L"PowerShell (.ps1)"),
              WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTORADIOBUTTON | BS_NOTIFY,
              lx, yL, colLW, rH, hDlg, (HMENU)(UINT_PTR)IDC_SCRDLG_TYPE_PS1, hInst, NULL);
          if (hFont) SendMessageW(hPs1, WM_SETFONT, (WPARAM)hFont, FALSE);
          SendMessageW(hPs1, BM_SETCHECK, scr.type == SCR_TYPE_PS1 ? BST_CHECKED : BST_UNCHECKED, 0);
          yL += rH + S(SD_GAP_SM); }

        // Right: 3 checkboxes (no header label — uses same vertical space)
        auto lc = [&](const wchar_t* k, const wchar_t* fb) {
            auto it = locale.find(k); return it != locale.end() ? it->second : std::wstring(fb);
        };
        { HWND hCb = CreateCustomCheckbox(hDlg, IDC_SCRDLG_RUN_HIDDEN,
            lc(L"scr_dlg_run_hidden", L"Run hidden (no console window)"),
            scr.run_hidden != 0, colRX, yR, colRW, rH, hInst);
          if (hFont) SendMessageW(hCb, WM_SETFONT, (WPARAM)hFont, FALSE); }
        yR += rH + S(SD_GAP_SM);
        { HWND hCb = CreateCustomCheckbox(hDlg, IDC_SCRDLG_WAIT,
            lc(L"scr_dlg_wait", L"Wait for script to complete"),
            scr.wait_for_completion != 0, colRX, yR, colRW, rH, hInst);
          if (hFont) SendMessageW(hCb, WM_SETFONT, (WPARAM)hFont, FALSE); }
        yR += rH + S(SD_GAP_SM);
        { HWND hCb = CreateCustomCheckbox(hDlg, IDC_SCRDLG_ALSO_UNINSTALL,
            lc(L"scr_dlg_also_uninstall", L"Also run at uninstall"),
            scr.also_uninstall != 0, colRX, yR, colRW, rH, hInst);
          if (hFont) SendMessageW(hCb, WM_SETFONT, (WPARAM)hFont, FALSE); }
        yR += rH;

        // Advance y past whichever column is taller
        y = (yL > yR ? yL : yR) + S(SD_GAP);
    }

    // ── When to run combo ─────────────────────────────────────────────────────
    { auto it = locale.find(L"scr_dlg_when");
      Lbl(it != locale.end() ? it->second.c_str() : L"When to run:"); }
    {
        HWND hCb = CreateWindowExW(0, L"COMBOBOX", L"",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | CBS_DROPDOWNLIST | CBS_HASSTRINGS,
            lx, y, cw, S(SD_COMBO_H) * 6,
            hDlg, (HMENU)(UINT_PTR)IDC_SCRDLG_WHEN_COMBO, hInst, NULL);
        if (hFont) SendMessageW(hCb, WM_SETFONT, (WPARAM)hFont, FALSE);

        struct { ScrWhenToRun val; const wchar_t* key; const wchar_t* fb; } items[] = {
            { SWR_BEFORE_FILES,  L"scr_when_before_files",  L"Before files are installed [Code]"   },
            { SWR_AFTER_FILES,   L"scr_when_after_files",   L"After files are installed [Run]"     },
            { SWR_FINISH_OPTOUT, L"scr_when_finish_optout", L"Finish page (optional, user can skip) [Run]" },
            { SWR_UNINSTALL,     L"scr_when_uninstall",     L"At uninstall [UninstallRun]"          },
        };
        int selIdx = 0;
        for (int i = 0; i < 4; i++) {
            auto it2 = locale.find(items[i].key);
            const wchar_t* txt = (it2 != locale.end()) ? it2->second.c_str() : items[i].fb;
            int idx = (int)SendMessageW(hCb, CB_ADDSTRING, 0, (LPARAM)txt);
            SendMessageW(hCb, CB_SETITEMDATA, (WPARAM)idx, (LPARAM)(int)items[i].val);
            if ((int)items[i].val == scr.when_to_run) selIdx = idx;
        }
        SendMessageW(hCb, CB_SETCURSEL, (WPARAM)selIdx, 0);
        y += S(SD_COMBO_H) + S(SD_GAP);
    }

    // ── Finish-page label — overlays the top of the editor when visible ────────
    // Positioned at the editor's top y; no space is reserved in the dialog height.
    // When SWR_FINISH_OPTOUT is selected the controls float on top of the editor.
    {
        int yOvr = y;  // same as editor start — no y advancement
        auto it = locale.find(L"scr_dlg_finish_label_lbl");
        HWND hFlbl = CreateWindowExW(0, L"STATIC",
            (it != locale.end() ? it->second.c_str()
                                : L"Label on Finish page (leave blank for default):"),
            WS_CHILD | SS_LEFT,
            lx, yOvr, cw, S(SD_LABEL_H),
            hDlg, (HMENU)(UINT_PTR)IDC_SCRDLG_FINISH_LABEL_LBL, hInst, NULL);
        if (hFont) SendMessageW(hFlbl, WM_SETFONT, (WPARAM)hFont, FALSE);
        yOvr += S(SD_LABEL_H) + S(SD_GAP_SM);
        HWND hFed = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT",
            scr.description.c_str(),
            WS_CHILD | WS_TABSTOP | ES_AUTOHSCROLL,
            lx, yOvr, cw, S(SD_EDIT_H),
            hDlg, (HMENU)(UINT_PTR)IDC_SCRDLG_FINISH_LABEL, hInst, NULL);
        if (hFont) SendMessageW(hFed, WM_SETFONT, (WPARAM)hFont, FALSE);
        // Visibility driven by selected "when" combo value
        UpdateWhenVisibility(hDlg);
    }

    // ── Script content — no label row; editor fills from here to load/test row ──
    {
        HWND hSciEd = SciCreateEditor(hDlg, IDC_SCRDLG_CONTENT, hInst,
                                       lx, y, cw, S(SD_CONTENT_H));
        if (hSciEd) {
            SciApplyHighlighting(hSciEd, scr.type);
            SciSetText(hSciEd, scr.content);
        } else {
            // Fallback: plain EDIT if Scintilla DLLs unavailable
            HWND hEd = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT",
                scr.content.c_str(),
                WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_VSCROLL |
                ES_MULTILINE | ES_AUTOVSCROLL | ES_WANTRETURN,
                lx, y, cw, S(SD_CONTENT_H),
                hDlg, (HMENU)(UINT_PTR)IDC_SCRDLG_CONTENT, hInst, NULL);
            if (hFont) SendMessageW(hEd, WM_SETFONT, (WPARAM)hFont, FALSE);
        }
        data.hSci = hSciEd;
        y += S(SD_CONTENT_H) + S(SD_GAP);
    }

    // ── Load from file + Test in terminal buttons ─────────────────────────────
    {
        auto lc = [&](const wchar_t* k, const wchar_t* fb) {
            auto it = locale.find(k); return it != locale.end() ? it->second : std::wstring(fb);
        };
        std::wstring loadTxt = lc(L"scr_dlg_load", L"Load from file\u2026");
        std::wstring testTxt = lc(L"scr_dlg_test", L"Test in terminal");
        int wLoad = MeasureButtonWidth(loadTxt, true);
        int wTest = MeasureButtonWidth(testTxt, true);
        int btnH  = S(SD_BTN_H) - S(6);   // slightly smaller than the OK/Cancel pair

        HWND hLoad = CreateCustomButtonWithIcon(hDlg, IDC_SCRDLG_LOAD, loadTxt.c_str(),
            ButtonColor::Blue, L"shell32.dll", 3, lx, y, wLoad, btnH, hInst);
        SetButtonTooltip(hLoad, lc(L"scr_dlg_load_tip",
            L"Open a .bat, .cmd or .ps1 file from disk").c_str());

        HWND hTest = CreateCustomButtonWithIcon(hDlg, IDC_SCRDLG_TEST, testTxt.c_str(),
            ButtonColor::Blue, L"shell32.dll", 25, lx + wLoad + S(SD_GAP), y, wTest, btnH, hInst);
        SetButtonTooltip(hTest, lc(L"scr_dlg_test_tip",
            L"Write a temporary copy and run it in a terminal window").c_str());
        (void)hTest;
        y += btnH + S(SD_GAP);
    }

    // ── OK / Cancel buttons ───────────────────────────────────────────────────
    y += S(SD_GAP);
    {
        auto lc = [&](const wchar_t* k, const wchar_t* fb) {
            auto it = locale.find(k); return it != locale.end() ? it->second : std::wstring(fb);
        };
        std::wstring okTxt  = lc(L"scr_dlg_ok",     L"Save");
        std::wstring canTxt = lc(L"scr_dlg_cancel",  L"Cancel");
        int wOk  = MeasureButtonWidth(okTxt,  true) + S(20);
        int wCan = MeasureButtonWidth(canTxt, true) + S(20);
        int bAreaW = wOk + S(SD_BTN_GAP) + wCan;
        int bx     = lx + (cw - bAreaW) / 2;

        CreateCustomButtonWithIcon(hDlg, IDC_SCRDLG_OK, okTxt.c_str(),
            ButtonColor::Blue, L"shell32.dll", 258,
            bx, y, wOk, S(SD_BTN_H), hInst);
        CreateCustomButtonWithIcon(hDlg, IDC_SCRDLG_CANCEL, canTxt.c_str(),
            ButtonColor::Red, L"shell32.dll", 131,
            bx + wOk + S(SD_BTN_GAP), y, wCan, S(SD_BTN_H), hInst);
    }

    // ── Run modal loop ────────────────────────────────────────────────────────
    EnableWindow(hwndParent, FALSE);
    ShowWindow(hDlg, SW_SHOW);
    UpdateWindow(hDlg);

    MSG m = {};
    while (IsWindow(hDlg)) {
        BOOL bRet = GetMessageW(&m, NULL, 0, 0);
        if (bRet == 0) { PostQuitMessage((int)m.wParam); break; }
        if (bRet == -1) break;
        if (!IsWindow(hDlg)) break;
        TranslateMessage(&m);
        DispatchMessageW(&m);
    }

    EnableWindow(hwndParent, TRUE);
    SetForegroundWindow(hwndParent);

    if (s_scrDlgOk) {
        scr = data.scr;
        return true;
    }
    return false;
}
