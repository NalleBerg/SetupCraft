/*
 * file_assoc_dialog.cpp — Add/Edit File Association modal dialog for SetupCraft.
 *
 * Flat form layout: label/edit pair for each FileAssocRow field plus an
 * "Enabled" checkbox. Follows the CreateWindowEx + private message-loop pattern
 * used by sc_shortcut_dialog.cpp and dep_edit_dialog.cpp.
 */

#include "file_assoc_dialog.h"
#include "button.h"     // CreateCustomButtonWithIcon, MeasureButtonWidth
#include "checkbox.h"   // CreateCustomCheckbox, DrawCustomCheckbox
#include "ctrlw.h"      // ShowValidationDialog
#include "dpi.h"        // S()
#include <commctrl.h>
#include <commdlg.h>    // GetOpenFileNameW

// ── Layout constants (design pixels at 96 DPI) ────────────────────────────────
static const int FADLG_PAD_H   = 20;
static const int FADLG_PAD_T   = 20;
static const int FADLG_PAD_B   = 20;
static const int FADLG_GAP     = 10;
static const int FADLG_GAP_SM  =  4;
static const int FADLG_BTN_H   = 34;
static const int FADLG_BTN_GAP = 15;
static const int FADLG_CONT_W  = 520;
static const int FADLG_LABEL_H = 18;
static const int FADLG_EDIT_H  = 26;
static const int FADLG_CB_H    = 22;
static const int FADLG_BROWSE_W= 42;
static const int FADLG_TITLE_H = 28;

// ── Control IDs ───────────────────────────────────────────────────────────────
#define FADLG_IDC_TITLE     3200
#define FADLG_IDC_ENABLED   3201
#define FADLG_IDC_EXT       3202
#define FADLG_IDC_DESC      3203
#define FADLG_IDC_PROGID    3204
#define FADLG_IDC_ICONPATH  3205
#define FADLG_IDC_ICONBROWSE 3206
#define FADLG_IDC_ICONIDX   3207
#define FADLG_IDC_OPENCMD   3208
#define FADLG_IDC_EDITCMD   3209
#define FADLG_IDC_PRINTCMD  3210
#define FADLG_IDC_MIME      3211
#define FADLG_IDC_OK        3212
#define FADLG_IDC_CANCEL    3213
// Help icon IDs (one per field row)
#define FADLG_IDC_HELP_ENABLED  3220
#define FADLG_IDC_HELP_EXT      3221
#define FADLG_IDC_HELP_DESC     3222
#define FADLG_IDC_HELP_PROGID   3223
#define FADLG_IDC_HELP_ICONPATH 3224
#define FADLG_IDC_HELP_ICONIDX  3225
#define FADLG_IDC_HELP_OPENCMD  3226
#define FADLG_IDC_HELP_EDITCMD  3227
#define FADLG_IDC_HELP_PRINTCMD 3228
#define FADLG_IDC_HELP_MIME     3229

// ── Per-invocation heap struct ────────────────────────────────────────────────
struct FaDlgData {
    FileAssocRow row;
    bool isNew;
    HINSTANCE hInst;
    const std::map<std::wstring, std::wstring>* pLocale;
    bool okPressed;
    HFONT hFont;
};

static bool s_fadlgOk = false;

// ── Helpers ───────────────────────────────────────────────────────────────────
static std::wstring FAL(const FaDlgData* d, const wchar_t* key, const wchar_t* fb)
{
    auto it = d->pLocale->find(key);
    return (it != d->pLocale->end()) ? it->second : fb;
}

static std::wstring GetEditW(HWND hDlg, int id)
{
    int len = GetWindowTextLengthW(GetDlgItem(hDlg, id));
    if (len <= 0) return L"";
    std::wstring s(len + 1, L'\0');
    GetWindowTextW(GetDlgItem(hDlg, id), &s[0], len + 1);
    s.resize(len);
    return s;
}

// ── Label + edit-with-optional-browse row helper ──────────────────────────────
static void MakeRow(HWND hDlg, HINSTANCE hInst, HFONT hFont,
                    int x0, int cntW, int& y,
                    const std::wstring& label, int editId,
                    const std::wstring& initVal,
                    bool hasBrowse = false, int browseId = 0,
                    bool hasHelp = false, int helpId = 0, const std::wstring& helpTip = L"")
{
    {
        HWND h = CreateWindowExW(0, L"STATIC", label.c_str(),
            WS_CHILD | WS_VISIBLE | SS_LEFT,
            x0, y, cntW, S(FADLG_LABEL_H),
            hDlg, NULL, hInst, NULL);
        SendMessageW(h, WM_SETFONT, (WPARAM)hFont, TRUE);
    }
    y += S(FADLG_LABEL_H) + S(FADLG_GAP_SM);

    int editW = cntW;
    if (hasBrowse) editW -= S(FADLG_BROWSE_W) + S(FADLG_GAP_SM);
    if (hasHelp)   editW -= S(FADLG_EDIT_H)   + S(FADLG_GAP_SM);
    {
        HWND hEdit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", initVal.c_str(),
            WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL,
            x0, y, editW, S(FADLG_EDIT_H),
            hDlg, (HMENU)(UINT_PTR)editId, hInst, NULL);
        SendMessageW(hEdit, WM_SETFONT, (WPARAM)hFont, TRUE);
    }
    int nextX = x0 + editW + S(FADLG_GAP_SM);
    if (hasBrowse && browseId) {
        CreateCustomButtonWithIcon(
            hDlg, browseId, L"…", ButtonColor::Blue,
            L"shell32.dll", 4,
            nextX, y, S(FADLG_BROWSE_W), S(FADLG_EDIT_H), hInst);
        nextX += S(FADLG_BROWSE_W) + S(FADLG_GAP_SM);
    }
    if (hasHelp && helpId && !helpTip.empty()) {
        HWND hHelp = CreateCustomButtonWithIcon(
            hDlg, helpId, L"", ButtonColor::Blue,
            L"shell32.dll", 23,
            nextX, y, S(FADLG_EDIT_H), S(FADLG_EDIT_H), hInst);
        SetButtonTooltip(hHelp, helpTip.c_str());
    }
    y += S(FADLG_EDIT_H) + S(FADLG_GAP);
}

// ── Dialog procedure ──────────────────────────────────────────────────────────
static LRESULT CALLBACK FaDlgProc(HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg) {

    case WM_CREATE: {
        CREATESTRUCTW* cs = (CREATESTRUCTW*)lParam;
        FaDlgData* pD = (FaDlgData*)cs->lpCreateParams;
        SetWindowLongPtrW(hDlg, GWLP_USERDATA, (LONG_PTR)pD);

        HINSTANCE hInst = cs->hInstance;

        // Build dialog font from NONCLIENTMETRICS.
        NONCLIENTMETRICSW ncm = {}; ncm.cbSize = sizeof(ncm);
        SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(ncm), &ncm, 0);
        if (ncm.lfMessageFont.lfHeight < 0)
            ncm.lfMessageFont.lfHeight = (LONG)(ncm.lfMessageFont.lfHeight * 1.2f);
        ncm.lfMessageFont.lfQuality = CLEARTYPE_QUALITY;
        pD->hFont = CreateFontIndirectW(&ncm.lfMessageFont);
        HFONT hFont = pD->hFont;

        int x0   = S(FADLG_PAD_H);
        int cntW = S(FADLG_CONT_W);
        int y    = S(FADLG_PAD_T);

        // ── Title ─────────────────────────────────────────────────────────────
        std::wstring dlgTitle = pD->isNew
            ? FAL(pD, L"fa_dlg_title_add",  L"Add File Association")
            : FAL(pD, L"fa_dlg_title_edit", L"Edit File Association");
        {
            HWND h = CreateWindowExW(0, L"STATIC", dlgTitle.c_str(),
                WS_CHILD | WS_VISIBLE | SS_LEFT,
                x0, y, cntW, S(FADLG_TITLE_H),
                hDlg, (HMENU)FADLG_IDC_TITLE, hInst, NULL);
            // Build a slightly larger font for the title.
            LOGFONTW lf = ncm.lfMessageFont;
            lf.lfHeight = (LONG)(lf.lfHeight * 1.35f);
            lf.lfWeight = FW_BOLD;
            HFONT hBig = CreateFontIndirectW(&lf);
            if (hBig) SendMessageW(h, WM_SETFONT, (WPARAM)hBig, TRUE);
        }
        y += S(FADLG_TITLE_H) + S(FADLG_GAP);

        // ── Enabled checkbox ──────────────────────────────────────────────────
        std::wstring enabledLbl = FAL(pD, L"fa_enabled", L"Enabled");
        {
            int cbW = cntW - S(FADLG_EDIT_H) - S(FADLG_GAP_SM);
            CreateCustomCheckbox(hDlg, FADLG_IDC_ENABLED, enabledLbl,
                pD->row.enabled != 0, x0, y, cbW, S(FADLG_CB_H), hInst);
            HWND hHelp = CreateCustomButtonWithIcon(
                hDlg, FADLG_IDC_HELP_ENABLED, L"", ButtonColor::Blue,
                L"shell32.dll", 23,
                x0 + cbW + S(FADLG_GAP_SM), y,
                S(FADLG_CB_H), S(FADLG_CB_H), hInst);
            SetButtonTooltip(hHelp,
                FAL(pD, L"fa_tip_enabled",
                    L"Enable or disable this file type association. Disabled associations are excluded from the installer.").c_str());
        }
        y += S(FADLG_CB_H) + S(FADLG_GAP);

        // ── Extension ─────────────────────────────────────────────────────────
        MakeRow(hDlg, hInst, hFont, x0, cntW, y,
            FAL(pD, L"fa_lbl_ext", L"Extension (.myext):"),
            FADLG_IDC_EXT, pD->row.extension,
            false, 0, true, FADLG_IDC_HELP_EXT,
            FAL(pD, L"fa_tip_ext", L"File extension to register, including the leading dot. Example: .pdf  or  .mydata"));

        // ── Description ───────────────────────────────────────────────────────
        MakeRow(hDlg, hInst, hFont, x0, cntW, y,
            FAL(pD, L"fa_lbl_desc", L"Description:"),
            FADLG_IDC_DESC, pD->row.description,
            false, 0, true, FADLG_IDC_HELP_DESC,
            FAL(pD, L"fa_tip_desc", L"Human-readable label shown in Windows Explorer for files of this type. Example: PDF Document"));

        // ── ProgID ────────────────────────────────────────────────────────────
        MakeRow(hDlg, hInst, hFont, x0, cntW, y,
            FAL(pD, L"fa_lbl_progid", L"ProgID (auto if blank):"),
            FADLG_IDC_PROGID, pD->row.prog_id,
            false, 0, true, FADLG_IDC_HELP_PROGID,
            FAL(pD, L"fa_tip_progid", L"Unique registry identifier for this file type. Leave blank to auto-generate as AppName.Extension. Example: MyApp.pdf"));

        // ── Icon path (with browse) ────────────────────────────────────────────
        MakeRow(hDlg, hInst, hFont, x0, cntW, y,
            FAL(pD, L"fa_lbl_icon", L"Icon path:"),
            FADLG_IDC_ICONPATH, pD->row.icon_path,
            true, FADLG_IDC_ICONBROWSE,
            true, FADLG_IDC_HELP_ICONPATH,
            FAL(pD, L"fa_tip_iconpath", L"Path to an .exe, .dll or .ico file that contains the icon for this file type. Use \u2026 to browse."));

        // ── Icon index ────────────────────────────────────────────────────────
        MakeRow(hDlg, hInst, hFont, x0, cntW, y,
            FAL(pD, L"fa_lbl_icon_idx", L"Icon index:"),
            FADLG_IDC_ICONIDX, std::to_wstring(pD->row.icon_index),
            false, 0, true, FADLG_IDC_HELP_ICONIDX,
            FAL(pD, L"fa_tip_iconidx", L"Zero-based index of the icon inside the icon file. Use 0 for a standalone .ico or for the first icon in a .dll or .exe."));

        // ── Open command ──────────────────────────────────────────────────────
        MakeRow(hDlg, hInst, hFont, x0, cntW, y,
            FAL(pD, L"fa_lbl_open", L"Open command:"),
            FADLG_IDC_OPENCMD, pD->row.open_cmd,
            false, 0, true, FADLG_IDC_HELP_OPENCMD,
            FAL(pD, L"fa_tip_opencmd", L"Command run when the user double-clicks a file. Use %1 as the file path placeholder. Example: \"C:\\MyApp\\MyApp.exe\" \"%1\""));

        // ── Edit command ──────────────────────────────────────────────────────
        MakeRow(hDlg, hInst, hFont, x0, cntW, y,
            FAL(pD, L"fa_lbl_edit", L"Edit command:"),
            FADLG_IDC_EDITCMD, pD->row.edit_cmd,
            false, 0, true, FADLG_IDC_HELP_EDITCMD,
            FAL(pD, L"fa_tip_editcmd", L"Command added to the right-click Edit menu. Use %1 as the file path placeholder. Leave blank to omit this menu entry."));

        // ── Print command ─────────────────────────────────────────────────────
        MakeRow(hDlg, hInst, hFont, x0, cntW, y,
            FAL(pD, L"fa_lbl_print", L"Print command:"),
            FADLG_IDC_PRINTCMD, pD->row.print_cmd,
            false, 0, true, FADLG_IDC_HELP_PRINTCMD,
            FAL(pD, L"fa_tip_printcmd", L"Command added to the right-click Print menu. Use %1 as the file path placeholder. Leave blank to omit this menu entry."));

        // ── MIME type ─────────────────────────────────────────────────────────
        MakeRow(hDlg, hInst, hFont, x0, cntW, y,
            FAL(pD, L"fa_lbl_mime", L"MIME type:"),
            FADLG_IDC_MIME, pD->row.content_type,
            false, 0, true, FADLG_IDC_HELP_MIME,
            FAL(pD, L"fa_tip_mime", L"MIME content type for this extension. Used by browsers and servers. Leave blank if not needed. Example: application/pdf"));

        y += S(FADLG_GAP);

        // ── OK / Cancel buttons ───────────────────────────────────────────────
        std::wstring okTxt  = FAL(pD, L"ok",     L"OK");
        std::wstring canTxt = FAL(pD, L"cancel", L"Cancel");
        int wOK  = MeasureButtonWidth(okTxt,  true);
        int wCan = MeasureButtonWidth(canTxt, true);
        int totalW = S(FADLG_CONT_W) + 2 * S(FADLG_PAD_H);
        int okX  = totalW - S(FADLG_PAD_H) - wCan - S(FADLG_BTN_GAP) - wOK;
        int canX = totalW - S(FADLG_PAD_H) - wCan;

        CreateCustomButtonWithIcon(hDlg, FADLG_IDC_OK, okTxt.c_str(),
            ButtonColor::Green, L"shell32.dll", 80,
            okX,  y, wOK,  S(FADLG_BTN_H), hInst);
        CreateCustomButtonWithIcon(hDlg, FADLG_IDC_CANCEL, canTxt.c_str(),
            ButtonColor::Red,   L"shell32.dll", 131,
            canX, y, wCan, S(FADLG_BTN_H), hInst);
        y += S(FADLG_BTN_H) + S(FADLG_PAD_B);

        // Resize window to content.
        SetWindowPos(hDlg, NULL, 0, 0,
            totalW,
            y + GetSystemMetrics(SM_CYCAPTION) + 2 * GetSystemMetrics(SM_CYFIXEDFRAME),
            SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);

        // Centre on parent.
        HWND hParent = GetParent(hDlg);
        if (hParent) {
            RECT rp, rd;
            GetWindowRect(hParent, &rp);
            GetWindowRect(hDlg, &rd);
            int cx = (rp.left + rp.right  - (rd.right - rd.left)) / 2;
            int cy = (rp.top  + rp.bottom - (rd.bottom - rd.top)) / 2;
            SetWindowPos(hDlg, NULL, cx, cy, 0, 0, SWP_NOSIZE | SWP_NOZORDER);
        }

        return 0;
    }

    case WM_CTLCOLORSTATIC:
    case WM_CTLCOLOREDIT:
    case WM_CTLCOLORBTN: {
        HDC hdc = (HDC)wParam;
        SetBkColor(hdc, GetSysColor(COLOR_WINDOW));
        SetTextColor(hdc, GetSysColor(COLOR_WINDOWTEXT));
        return (LRESULT)GetSysColorBrush(COLOR_WINDOW);
    }

    case WM_DRAWITEM: {
        DRAWITEMSTRUCT* dis = (DRAWITEMSTRUCT*)lParam;
        if (DrawCustomCheckbox(dis)) return TRUE;
        {
            ButtonColor color = (ButtonColor)GetWindowLongPtrW(dis->hwndItem, GWLP_USERDATA);
            HFONT hFont = CreateFontW(-S(12), 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
                DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
            LRESULT r = DrawCustomButton(dis, color, hFont);
            if (hFont) DeleteObject(hFont);
            return r;
        }
    }

    case WM_COMMAND: {
        int id    = LOWORD(wParam);
        int event = HIWORD(wParam);

        if (id == FADLG_IDC_CANCEL || (id == IDCANCEL)) {
            s_fadlgOk = false;
            DestroyWindow(hDlg);
            return 0;
        }

        if (id == FADLG_IDC_OK) {
            FaDlgData* pD = (FaDlgData*)GetWindowLongPtrW(hDlg, GWLP_USERDATA);

            std::wstring ext = GetEditW(hDlg, FADLG_IDC_EXT);
            if (ext.empty()) {
                std::wstring title = FAL(pD, L"validation_error", L"Validation Error");
                std::wstring body  = FAL(pD, L"fa_err_ext_empty", L"Extension must not be empty.");
                ShowValidationDialog(hDlg, title, body, pD ? *pD->pLocale : std::map<std::wstring,std::wstring>{});
                SetFocus(GetDlgItem(hDlg, FADLG_IDC_EXT));
                return 0;
            }

            // Collect all fields back into row.
            pD->row.enabled      = (IsDlgButtonChecked(hDlg, FADLG_IDC_ENABLED) == BST_CHECKED) ? 1 : 0;
            pD->row.extension    = ext;
            pD->row.description  = GetEditW(hDlg, FADLG_IDC_DESC);
            pD->row.prog_id      = GetEditW(hDlg, FADLG_IDC_PROGID);
            pD->row.icon_path    = GetEditW(hDlg, FADLG_IDC_ICONPATH);
            pD->row.icon_index   = _wtoi(GetEditW(hDlg, FADLG_IDC_ICONIDX).c_str());
            pD->row.open_cmd     = GetEditW(hDlg, FADLG_IDC_OPENCMD);
            pD->row.edit_cmd     = GetEditW(hDlg, FADLG_IDC_EDITCMD);
            pD->row.print_cmd    = GetEditW(hDlg, FADLG_IDC_PRINTCMD);
            pD->row.content_type = GetEditW(hDlg, FADLG_IDC_MIME);

            s_fadlgOk = true;
            DestroyWindow(hDlg);
            return 0;
        }

        if (id == FADLG_IDC_ICONBROWSE && event == BN_CLICKED) {
            wchar_t buf[MAX_PATH] = {};
            std::wstring cur = GetEditW(hDlg, FADLG_IDC_ICONPATH);
            if (!cur.empty()) wcsncpy_s(buf, cur.c_str(), MAX_PATH - 1);
            OPENFILENAMEW ofn = {};
            ofn.lStructSize  = sizeof(ofn);
            ofn.hwndOwner    = hDlg;
            ofn.lpstrFilter  = L"Icon files (*.exe;*.dll;*.ico)\0*.exe;*.dll;*.ico\0All files (*.*)\0*.*\0";
            ofn.lpstrFile    = buf;
            ofn.nMaxFile     = MAX_PATH;
            ofn.Flags        = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_HIDEREADONLY;
            if (GetOpenFileNameW(&ofn))
                SetWindowTextW(GetDlgItem(hDlg, FADLG_IDC_ICONPATH), buf);
            return 0;
        }

        return 0;
    }

    case WM_KEYDOWN:
        if ((int)wParam == VK_ESCAPE) {
            s_fadlgOk = false;
            DestroyWindow(hDlg);
        }
        return 0;

    case WM_DESTROY: {
        FaDlgData* pD = (FaDlgData*)GetWindowLongPtrW(hDlg, GWLP_USERDATA);
        if (pD && pD->hFont) { DeleteObject(pD->hFont); pD->hFont = NULL; }
        PostQuitMessage(0);
        return 0;
    }
    }
    return DefWindowProcW(hDlg, msg, wParam, lParam);
}

// ── Public entry point ────────────────────────────────────────────────────────
bool FA_ShowEditDialog(HWND hwndParent,
                       HINSTANCE hInst,
                       const std::map<std::wstring, std::wstring>& locale,
                       FileAssocRow& row,
                       bool isNew)
{
    // Register window class once.
    static bool s_classReg = false;
    if (!s_classReg) {
        WNDCLASSEXW wc = {};
        wc.cbSize        = sizeof(wc);
        wc.style         = CS_HREDRAW | CS_VREDRAW;
        wc.lpfnWndProc   = FaDlgProc;
        wc.hInstance     = hInst;
        wc.hCursor       = LoadCursorW(NULL, IDC_ARROW);
        wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
        wc.lpszClassName = L"SetupCraftFaDialog";
        RegisterClassExW(&wc);
        s_classReg = true;
    }

    FaDlgData data;
    data.row     = row;
    data.isNew   = isNew;
    data.hInst   = hInst;
    data.pLocale = &locale;
    data.okPressed = false;
    data.hFont   = NULL;

    s_fadlgOk = false;

    std::wstring title = isNew
        ? (locale.count(L"fa_dlg_title_add")  ? locale.at(L"fa_dlg_title_add")  : L"Add File Association")
        : (locale.count(L"fa_dlg_title_edit") ? locale.at(L"fa_dlg_title_edit") : L"Edit File Association");

    HWND hDlg = CreateWindowExW(
        WS_EX_DLGMODALFRAME | WS_EX_WINDOWEDGE,
        L"SetupCraftFaDialog",
        title.c_str(),
        WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_CLIPSIBLINGS,
        CW_USEDEFAULT, CW_USEDEFAULT,
        S(FADLG_CONT_W) + 2 * S(FADLG_PAD_H),
        400,  // preliminary; WM_CREATE resizes
        hwndParent, NULL, hInst, &data);

    if (!hDlg) return false;

    EnableWindow(hwndParent, FALSE);
    ShowWindow(hDlg, SW_SHOW);
    UpdateWindow(hDlg);

    MSG m;
    while (GetMessageW(&m, NULL, 0, 0)) {
        if (!IsDialogMessageW(hDlg, &m)) {
            TranslateMessage(&m);
            DispatchMessageW(&m);
        }
    }

    EnableWindow(hwndParent, TRUE);
    SetForegroundWindow(hwndParent);

    if (s_fadlgOk) {
        row = data.row;
        return true;
    }
    return false;
}
