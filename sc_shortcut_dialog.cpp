/*
 * sc_shortcut_dialog.cpp — "Configure Shortcut" modal dialog for SetupCraft.
 *
 * See sc_shortcut_dialog.h for the public API and sc_shortcut_dialog_INTERNALS.txt
 * for architecture notes, layout constants, and the modal message-loop pattern.
 *
 * Layout rules: all pixel values through S(), all strings through locale.
 */

#include "sc_shortcut_dialog.h"
#include "shortcuts.h"     // SCT_* constants, IDC_SCDLG_* IDs
#include "button.h"        // CreateCustomButtonWithIcon, DrawCustomButton, SetButtonTooltip
#include "checkbox.h"      // CreateCustomCheckbox, DrawCustomCheckbox
#include "dpi.h"           // S() DPI-scale macro

#include <commctrl.h>
#include "vfs_picker.h"       // ShowVfsPicker, VfsPicker_IsExecutable
#include <vector>

// PrivateExtractIconsW — same declaration as buttons.cpp / shortcuts.cpp.
extern "C" __declspec(dllimport) UINT WINAPI PrivateExtractIconsW(
    LPCWSTR szFileName, int nIconIndex, int cxIcon, int cyIcon,
    HICON* phicon, UINT* piconid, UINT nIcons, UINT flags);

// ── Layout constants (design-time pixels at 96 DPI) ──────────────────────────
static const int SCDLG_PAD_H    = 20;  // left/right padding
static const int SCDLG_PAD_T    = 20;  // top padding
static const int SCDLG_PAD_B    = 15;  // bottom padding (below buttons)
static const int SCDLG_GAP      = 10;  // standard inter-row gap
static const int SCDLG_GAP_SM   =  4;  // label-to-control gap
static const int SCDLG_BTN_H    = 34;  // OK / Cancel button height
static const int SCDLG_BTN_W    = 120; // OK / Cancel button width each
static const int SCDLG_BTN_GAP  = 15;  // gap between OK and Cancel
static const int SCDLG_CONT_W   = 380; // inner content column width
static const int SCDLG_ICON_SZ  = 48;  // icon preview square side
static const int SCDLG_LABEL_H  = 18;  // single-line static label height
static const int SCDLG_EDIT_H   = 26;  // edit-control height
static const int SCDLG_CB_H     = 22;  // checkbox row height
static const int SCDLG_BROWSE_W = 42;  // width of the "..." browse buttons

// ── Per-invocation data (heap-allocated, passed via lpCreateParams) ───────────
struct ScDlgData {
    // initials (also written back on OK)
    int          type;
    std::wstring smPath;
    std::wstring name;
    std::wstring exePath;
    std::wstring workingDir;
    bool         workingDirIsAuto; // true = still tracking exe directory; auto-update on exe pick
    std::wstring iconPath;
    int          iconIndex;
    bool         runAsAdmin;
    HINSTANCE    hInst;
    const std::map<std::wstring, std::wstring>* pLocale;
    // runtime
    HICON        hCurrentIcon;   // icon shown in the preview; owned here
    bool         okPressed;
};

// Module-level result flag toggled from WM_COMMAND before DestroyWindow.
static bool s_scDlgOk = false;

// Safe locale lookup inside the dialog proc.
static std::wstring DLoc(const ScDlgData* d, const wchar_t* key, const wchar_t* fb) {
    auto it = d->pLocale->find(key);
    return (it != d->pLocale->end()) ? it->second : fb;
}

// ── Icon preview subclass ─────────────────────────────────────────────────────
// Paints the HICON stored in GWLP_USERDATA, or a placeholder frame if NULL.
static LRESULT CALLBACK ScIconPreviewProc(
    HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam,
    UINT_PTR /*uid*/, DWORD_PTR /*ref*/)
{
    switch (msg) {
    case WM_ERASEBKGND: {
        RECT rc; GetClientRect(hwnd, &rc);
        FillRect((HDC)wParam, &rc, GetSysColorBrush(COLOR_WINDOW));
        return 1;
    }
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);
        HICON hIco = (HICON)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
        RECT rc; GetClientRect(hwnd, &rc);
        if (hIco) {
            DrawIconEx(hdc, 0, 0, hIco, rc.right, rc.bottom, 0, NULL, DI_NORMAL);
        } else {
            // Empty placeholder: white fill + grey border.
            FillRect(hdc, &rc, GetSysColorBrush(COLOR_WINDOW));
            HPEN hPen = CreatePen(PS_SOLID, 1, GetSysColor(COLOR_BTNSHADOW));
            HPEN hOld = (HPEN)SelectObject(hdc, hPen);
            Rectangle(hdc, rc.left, rc.top, rc.right - 1, rc.bottom - 1);
            SelectObject(hdc, hOld);
            DeleteObject(hPen);
        }
        EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_NCDESTROY:
        RemoveWindowSubclass(hwnd, ScIconPreviewProc, 0);
        return DefSubclassProc(hwnd, msg, wParam, lParam);
    }
    return DefSubclassProc(hwnd, msg, wParam, lParam);
}

// ── Helper: load icon at preview size from an icon-capable file ───────────────
static HICON LoadPreviewIcon(const std::wstring& path, int idx)
{
    if (path.empty()) return NULL;
    HICON hIco = NULL;
    UINT dummy = 0;
    PrivateExtractIconsW(path.c_str(), idx,
                         S(SCDLG_ICON_SZ), S(SCDLG_ICON_SZ),
                         &hIco, &dummy, 1, 0);
    if (!hIco)
        ExtractIconExW(path.c_str(), idx, &hIco, NULL, 1);
    return hIco;
}

// ── Dialog procedure ──────────────────────────────────────────────────────────
static LRESULT CALLBACK ScShortcutDlgProc(HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg) {

    // ── Build all controls ────────────────────────────────────────────────────
    case WM_CREATE: {
        CREATESTRUCTW* cs = (CREATESTRUCTW*)lParam;
        ScDlgData*     pD = (ScDlgData*)cs->lpCreateParams;
        SetWindowLongPtrW(hDlg, GWLP_USERDATA, (LONG_PTR)pD);

        HINSTANCE hInst = cs->hInstance;
        RECT rcC; GetClientRect(hDlg, &rcC);
        int cW = rcC.right;
        int x0 = S(SCDLG_PAD_H);
        int cntW = cW - 2 * S(SCDLG_PAD_H);

        // Dialog font — scaled NONCLIENTMETRICS message font.
        NONCLIENTMETRICSW ncm = {}; ncm.cbSize = sizeof(ncm);
        SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(ncm), &ncm, 0);
        if (ncm.lfMessageFont.lfHeight < 0)
            ncm.lfMessageFont.lfHeight = (LONG)(ncm.lfMessageFont.lfHeight * 1.2f);
        ncm.lfMessageFont.lfQuality = CLEARTYPE_QUALITY;
        HFONT hFont = CreateFontIndirectW(&ncm.lfMessageFont);

        int y = S(SCDLG_PAD_T);
        // Pre-compute widths used for edit+browse rows.
        int editWithBrowseW = cntW - S(SCDLG_BROWSE_W) - S(SCDLG_GAP_SM);

        // ── Executable path label + edit + browse button ──────────────────────
        {
            std::wstring lbl = DLoc(pD, L"scdlg_exe_label", L"Executable:");
            HWND h = CreateWindowExW(0, L"STATIC", lbl.c_str(),
                WS_CHILD | WS_VISIBLE | SS_LEFT,
                x0, y, cntW, S(SCDLG_LABEL_H),
                hDlg, NULL, hInst, NULL);
            SendMessageW(h, WM_SETFONT, (WPARAM)hFont, TRUE);
        }
        y += S(SCDLG_LABEL_H) + S(SCDLG_GAP_SM);
        {
            HWND hEdit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", pD->exePath.c_str(),
                WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL,
                x0, y, editWithBrowseW, S(SCDLG_EDIT_H),
                hDlg, (HMENU)IDC_SCDLG_EXE, hInst, NULL);
            SendMessageW(hEdit, WM_SETFONT, (WPARAM)hFont, TRUE);

            HWND hBtn = CreateCustomButtonWithIcon(
                hDlg, IDC_SCDLG_EXE_BROWSE, L"...", ButtonColor::Blue,
                L"shell32.dll", 4,
                x0 + editWithBrowseW + S(SCDLG_GAP_SM), y,
                S(SCDLG_BROWSE_W), S(SCDLG_EDIT_H), hInst);
            SetButtonTooltip(hBtn,
                DLoc(pD, L"scdlg_exe_tooltip",
                    L"Browse for the target executable (.exe)").c_str());
        }
        y += S(SCDLG_EDIT_H) + S(SCDLG_GAP);

        // ── Name label + edit ─────────────────────────────────────────────────
        {
            std::wstring lbl = DLoc(pD, L"scdlg_name_label", L"Shortcut name:");
            HWND h = CreateWindowExW(0, L"STATIC", lbl.c_str(),
                WS_CHILD | WS_VISIBLE | SS_LEFT,
                x0, y, cntW, S(SCDLG_LABEL_H),
                hDlg, NULL, hInst, NULL);
            SendMessageW(h, WM_SETFONT, (WPARAM)hFont, TRUE);
        }
        y += S(SCDLG_LABEL_H) + S(SCDLG_GAP_SM);
        {
            HWND h = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", pD->name.c_str(),
                WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL,
                x0, y, cntW, S(SCDLG_EDIT_H),
                hDlg, (HMENU)IDC_SCDLG_NAME, hInst, NULL);
            SendMessageW(h, WM_SETFONT, (WPARAM)hFont, TRUE);
            SendMessageW(h, EM_SETSEL, 0, -1);  // select all
        }
        y += S(SCDLG_EDIT_H) + S(SCDLG_GAP);

        // ── Working directory label + edit + browse button ────────────────────
        {
            std::wstring lbl = DLoc(pD, L"scdlg_workdir_label", L"Run in folder:");
            HWND h = CreateWindowExW(0, L"STATIC", lbl.c_str(),
                WS_CHILD | WS_VISIBLE | SS_LEFT,
                x0, y, cntW, S(SCDLG_LABEL_H),
                hDlg, NULL, hInst, NULL);
            SendMessageW(h, WM_SETFONT, (WPARAM)hFont, TRUE);
        }
        y += S(SCDLG_LABEL_H) + S(SCDLG_GAP_SM);
        {
            HWND hEdit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", pD->workingDir.c_str(),
                WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL,
                x0, y, editWithBrowseW, S(SCDLG_EDIT_H),
                hDlg, (HMENU)IDC_SCDLG_WORKDIR, hInst, NULL);
            SendMessageW(hEdit, WM_SETFONT, (WPARAM)hFont, TRUE);

            HWND hBtn = CreateCustomButtonWithIcon(
                hDlg, IDC_SCDLG_WORKDIR_BROWSE, L"...", ButtonColor::Blue,
                L"shell32.dll", 4,
                x0 + editWithBrowseW + S(SCDLG_GAP_SM), y,
                S(SCDLG_BROWSE_W), S(SCDLG_EDIT_H), hInst);
            SetButtonTooltip(hBtn,
                DLoc(pD, L"scdlg_workdir_tooltip",
                    L"Browse for the folder the shortcut should start in").c_str());
        }
        y += S(SCDLG_EDIT_H) + S(SCDLG_GAP);

        // ── Start Menu path (SCT_STARTMENU only) ──────────────────────────────
        if (pD->type == SCT_STARTMENU && !pD->smPath.empty()) {
            {
                std::wstring lbl = DLoc(pD, L"scdlg_sm_path_label", L"Start Menu location:");
                HWND h = CreateWindowExW(0, L"STATIC", lbl.c_str(),
                    WS_CHILD | WS_VISIBLE | SS_LEFT,
                    x0, y, cntW, S(SCDLG_LABEL_H),
                    hDlg, NULL, hInst, NULL);
                SendMessageW(h, WM_SETFONT, (WPARAM)hFont, TRUE);
            }
            y += S(SCDLG_LABEL_H) + S(SCDLG_GAP_SM);
            {
                HWND h = CreateWindowExW(0, L"STATIC", pD->smPath.c_str(),
                    WS_CHILD | WS_VISIBLE | SS_LEFT | SS_NOPREFIX,
                    x0, y, cntW, S(SCDLG_LABEL_H),
                    hDlg, NULL, hInst, NULL);
                SendMessageW(h, WM_SETFONT, (WPARAM)hFont, TRUE);
            }
            y += S(SCDLG_LABEL_H) + S(SCDLG_GAP);
        }

        // ── Icon label ────────────────────────────────────────────────────────
        {
            std::wstring lbl = DLoc(pD, L"scdlg_icon_label", L"Icon:");
            HWND h = CreateWindowExW(0, L"STATIC", lbl.c_str(),
                WS_CHILD | WS_VISIBLE | SS_LEFT,
                x0, y, cntW, S(SCDLG_LABEL_H),
                hDlg, NULL, hInst, NULL);
            SendMessageW(h, WM_SETFONT, (WPARAM)hFont, TRUE);
        }
        y += S(SCDLG_LABEL_H) + S(SCDLG_GAP_SM);

        // ── Icon row: 48×48 preview + "Change Icon…" button ──────────────────
        {
            int iconSz = S(SCDLG_ICON_SZ);

            // Subclassed STATIC preview.
            HWND hPrev = CreateWindowExW(0, L"STATIC", NULL,
                WS_CHILD | WS_VISIBLE | SS_NOTIFY,
                x0, y, iconSz, iconSz,
                hDlg, (HMENU)IDC_SCDLG_ICON_PREVIEW, hInst, NULL);

            HICON hIco          = LoadPreviewIcon(pD->iconPath, pD->iconIndex);
            pD->hCurrentIcon    = hIco;
            SetWindowLongPtrW(hPrev, GWLP_USERDATA, (LONG_PTR)hIco);
            SetWindowSubclass(hPrev, ScIconPreviewProc, 0, 0);

            // "Change Icon…" button — vertically centred beside the preview square.
            int btnX  = x0   + iconSz + S(SCDLG_GAP);
            int btnW  = cntW - iconSz - S(SCDLG_GAP);
            int btnH  = S(SCDLG_BTN_H);
            int btnY  = y + (iconSz - btnH) / 2;
            std::wstring chgTxt = DLoc(pD, L"scdlg_change_icon", L"Change Icon\u2026");
            HWND hBtn = CreateCustomButtonWithIcon(
                hDlg, IDC_SCDLG_ICON_ADD, chgTxt, ButtonColor::Blue,
                L"shell32.dll", 23,
                btnX, btnY, btnW, btnH, hInst);
            SetButtonTooltip(hBtn,
                DLoc(pD, L"scdlg_change_icon_tooltip",
                    L"Choose an .ico, .exe, or .dll file to extract the icon from").c_str());
        }
        y += S(SCDLG_ICON_SZ) + S(SCDLG_GAP);

        // ── Run as administrator checkbox ─────────────────────────────────────
        {
            std::wstring lbl = DLoc(pD, L"scdlg_run_as_admin",
                L"Run as administrator at launch");
            HWND hCb = CreateCustomCheckbox(
                hDlg, IDC_SCDLG_RUN_AS_ADMIN, lbl,
                pD->runAsAdmin,
                x0, y, cntW, S(SCDLG_CB_H), hInst);
            SendMessageW(hCb, WM_SETFONT, (WPARAM)hFont, TRUE);
            SetButtonTooltip(hCb,
                DLoc(pD, L"scdlg_run_as_admin_tooltip",
                    L"The installer will launch this shortcut with administrator privileges").c_str());
        }

        // ── OK and Cancel ─────────────────────────────────────────────────────
        {
            int totalBtnW = 2 * S(SCDLG_BTN_W) + S(SCDLG_BTN_GAP);
            int startX    = (cW - totalBtnW) / 2;
            int btnY      = rcC.bottom - S(SCDLG_PAD_B) - S(SCDLG_BTN_H);

            std::wstring okTxt  = DLoc(pD, L"scdlg_ok",     L"OK");
            std::wstring cnlTxt = DLoc(pD, L"scdlg_cancel", L"Cancel");

            CreateCustomButtonWithIcon(
                hDlg, IDC_SCDLG_OK, okTxt, ButtonColor::Green,
                L"shell32.dll", 112,
                startX, btnY, S(SCDLG_BTN_W), S(SCDLG_BTN_H), hInst);
            CreateCustomButtonWithIcon(
                hDlg, IDC_SCDLG_CANCEL, cnlTxt, ButtonColor::Red,
                L"shell32.dll", 131,
                startX + S(SCDLG_BTN_W) + S(SCDLG_BTN_GAP), btnY,
                S(SCDLG_BTN_W), S(SCDLG_BTN_H), hInst);
        }

        // Move focus to the name field.
        SetFocus(GetDlgItem(hDlg, IDC_SCDLG_NAME));
        return 0;
    }

    // ── Button / checkbox commands ────────────────────────────────────────────
    case WM_COMMAND: {
        int ctrlId = LOWORD(wParam);
        ScDlgData* pD = (ScDlgData*)GetWindowLongPtrW(hDlg, GWLP_USERDATA);
        if (!pD) break;

        if (ctrlId == IDC_SCDLG_OK) {
            // Harvest exe path.
            HWND hExeEdit = GetDlgItem(hDlg, IDC_SCDLG_EXE);
            if (hExeEdit) {
                int len = GetWindowTextLengthW(hExeEdit);
                pD->exePath.assign(len, L'\0');
                if (len > 0) GetWindowTextW(hExeEdit, &pD->exePath[0], len + 1);
            }
            // Harvest working directory.
            HWND hWdEdit = GetDlgItem(hDlg, IDC_SCDLG_WORKDIR);
            if (hWdEdit) {
                int len = GetWindowTextLengthW(hWdEdit);
                pD->workingDir.assign(len, L'\0');
                if (len > 0) GetWindowTextW(hWdEdit, &pD->workingDir[0], len + 1);
            }
            // Harvest name from edit control.
            HWND hEdit = GetDlgItem(hDlg, IDC_SCDLG_NAME);
            if (hEdit) {
                int len = GetWindowTextLengthW(hEdit);
                pD->name.assign(len, L'\0');
                if (len > 0) GetWindowTextW(hEdit, &pD->name[0], len + 1);
            }
            // Harvest run-as-admin checkbox state.
            HWND hCb = GetDlgItem(hDlg, IDC_SCDLG_RUN_AS_ADMIN);
            if (hCb) pD->runAsAdmin = (SendMessageW(hCb, BM_GETCHECK, 0, 0) == BST_CHECKED);
            pD->okPressed = true;
            s_scDlgOk     = true;
            DestroyWindow(hDlg);
            return 0;
        }

        if (ctrlId == IDC_SCDLG_CANCEL) {
            s_scDlgOk = false;
            DestroyWindow(hDlg);
            return 0;
        }

        if (ctrlId == IDC_SCDLG_EXE_BROWSE) {
            // VFS-backed file picker: show only executable file types.
            VfsPickerParams p;
            p.title       = DLoc(pD, L"scdlg_exe_picker_title",      L"Select executable");
            p.okText      = DLoc(pD, L"scdlg_ok",                     L"OK");
            p.cancelText  = DLoc(pD, L"scdlg_cancel",                 L"Cancel");
            p.foldersLabel  = DLoc(pD, L"vfspicker_folders_label",    L"Folders");
            p.filesLabel    = DLoc(pD, L"vfspicker_exe_files_label",  L"Files in selected folder");
            p.colFileName   = DLoc(pD, L"vfspicker_col_name",         L"Name");
            p.colFilePath   = DLoc(pD, L"vfspicker_col_path",         L"Path");
            p.noSelMessage  = DLoc(pD, L"vfspicker_exe_no_sel",
                L"Please select an executable file from the right pane.");
            p.rootLabel_ProgramFiles  = DLoc(pD, L"vfspicker_root_program_files", L"Program Files");
            p.rootLabel_ProgramData   = DLoc(pD, L"vfspicker_root_program_data",  L"ProgramData");
            p.rootLabel_AppData       = DLoc(pD, L"vfspicker_root_appdata",       L"AppData (Roaming)");
            p.rootLabel_AskAtInstall  = DLoc(pD, L"vfspicker_root_ask_install",   L"Ask at install");
            p.singleSelect    = true;
            p.showFilePane    = true;
            p.allowFolderPick = false;
            p.fileFilter      = VfsPicker_IsExecutable;

            std::vector<VfsPickerResult> picks;
            if (ShowVfsPicker(hDlg, pD->hInst, p, *pD->pLocale, picks)) {
                pD->exePath = picks[0].sourcePath;
                HWND hExeEdit = GetDlgItem(hDlg, IDC_SCDLG_EXE);
                if (hExeEdit) SetWindowTextW(hExeEdit, pD->exePath.c_str());

                // Auto-fill name from exe base name if name field is empty.
                HWND hNameEdit = GetDlgItem(hDlg, IDC_SCDLG_NAME);
                if (hNameEdit && GetWindowTextLengthW(hNameEdit) == 0) {
                    std::wstring base = picks[0].displayName;
                    size_t dot = base.rfind(L'.');
                    if (dot != std::wstring::npos) base = base.substr(0, dot);
                    SetWindowTextW(hNameEdit, base.c_str());
                }

                // Auto-fill workdir from exe directory if still tracking automatically.
                if (pD->workingDirIsAuto) {
                    std::wstring dir = pD->exePath;
                    size_t sep = dir.rfind(L'\\');
                    if (sep != std::wstring::npos) dir = dir.substr(0, sep);
                    pD->workingDir = dir;
                    HWND hWdEdit = GetDlgItem(hDlg, IDC_SCDLG_WORKDIR);
                    if (hWdEdit) SetWindowTextW(hWdEdit, dir.c_str());
                }
            }
            return 0;
        }

        if (ctrlId == IDC_SCDLG_WORKDIR_BROWSE) {
            // VFS-backed folder picker.
            VfsPickerParams p;
            p.title       = DLoc(pD, L"scdlg_workdir_picker_title",   L"Select working directory");
            p.okText      = DLoc(pD, L"scdlg_ok",                      L"OK");
            p.cancelText  = DLoc(pD, L"scdlg_cancel",                  L"Cancel");
            p.foldersLabel  = DLoc(pD, L"vfspicker_folders_label",     L"Folders");
            p.noSelMessage  = DLoc(pD, L"vfspicker_folder_no_sel",
                L"Please select a folder from the left pane.");
            p.rootLabel_ProgramFiles  = DLoc(pD, L"vfspicker_root_program_files", L"Program Files");
            p.rootLabel_ProgramData   = DLoc(pD, L"vfspicker_root_program_data",  L"ProgramData");
            p.rootLabel_AppData       = DLoc(pD, L"vfspicker_root_appdata",       L"AppData (Roaming)");
            p.rootLabel_AskAtInstall  = DLoc(pD, L"vfspicker_root_ask_install",   L"Ask at install");
            p.singleSelect    = true;
            p.showFilePane    = false;
            p.allowFolderPick = true;

            std::vector<VfsPickerResult> picks;
            if (ShowVfsPicker(hDlg, pD->hInst, p, *pD->pLocale, picks)) {
                pD->workingDir       = picks[0].sourcePath;
                pD->workingDirIsAuto = false; // user explicitly picked
                HWND hWdEdit = GetDlgItem(hDlg, IDC_SCDLG_WORKDIR);
                if (hWdEdit) SetWindowTextW(hWdEdit, pD->workingDir.c_str());
            }
            return 0;
        }

        if (ctrlId == IDC_SCDLG_ICON_ADD) {
            // Build the GetOpenFileName filter:
            //   "Icon files (*.ico, *.exe, *.dll)\0*.ico;*.exe;*.dll\0\0"
            // The display label comes from locale; the pattern is always fixed.
            std::wstring filterLabel = DLoc(pD, L"scdlg_icon_filter_label",
                L"Icon files (*.ico, *.exe, *.dll)");
            const wchar_t* pattern = L"*.ico;*.exe;*.dll";

            // Allocate a buffer large enough: label + NUL + pattern + NUL + NUL
            std::vector<wchar_t> filterBuf;
            filterBuf.reserve(filterLabel.size() + 1 + wcslen(pattern) + 2);
            for (wchar_t ch : filterLabel) filterBuf.push_back(ch);
            filterBuf.push_back(L'\0');
            for (const wchar_t* p = pattern; *p; ++p) filterBuf.push_back(*p);
            filterBuf.push_back(L'\0');
            filterBuf.push_back(L'\0');  // double-null terminator

            // Pre-fill the file path box with the current icon path (if any).
            wchar_t filePath[MAX_PATH] = {};
            if (!pD->iconPath.empty())
                wcsncpy_s(filePath, pD->iconPath.c_str(), MAX_PATH - 1);

            std::wstring pickerTitle = DLoc(pD, L"scdlg_icon_picker_title",
                L"Select icon file");

            OPENFILENAMEW ofn     = {};
            ofn.lStructSize       = sizeof(ofn);
            ofn.hwndOwner         = hDlg;
            ofn.lpstrFilter       = filterBuf.data();
            ofn.nFilterIndex      = 1;
            ofn.lpstrFile         = filePath;
            ofn.nMaxFile          = MAX_PATH;
            ofn.lpstrTitle        = pickerTitle.c_str();
            ofn.Flags             = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_HIDEREADONLY;

            if (GetOpenFileNameW(&ofn)) {
                // Destroy previously loaded preview icon.
                if (pD->hCurrentIcon) { DestroyIcon(pD->hCurrentIcon); pD->hCurrentIcon = NULL; }

                pD->iconPath  = filePath;
                pD->iconIndex = 0;  // always use first icon from the picked file

                HICON hNew       = LoadPreviewIcon(pD->iconPath, 0);
                pD->hCurrentIcon = hNew;

                HWND hPrev = GetDlgItem(hDlg, IDC_SCDLG_ICON_PREVIEW);
                if (hPrev) {
                    SetWindowLongPtrW(hPrev, GWLP_USERDATA, (LONG_PTR)hNew);
                    InvalidateRect(hPrev, NULL, TRUE);
                    UpdateWindow(hPrev);
                }
            }
            return 0;
        }
        break;
    }

    // ── Owner-draw: checkboxes then buttons ───────────────────────────────────
    case WM_DRAWITEM: {
        LPDRAWITEMSTRUCT dis = (LPDRAWITEMSTRUCT)lParam;
        if (DrawCustomCheckbox(dis)) return TRUE;  // checkbox handled

        // Button drawing: retrieve colour stored by CreateCustomButtonWithIcon.
        NONCLIENTMETRICSW ncm = {}; ncm.cbSize = sizeof(ncm);
        SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(ncm), &ncm, 0);
        if (ncm.lfMessageFont.lfHeight < 0)
            ncm.lfMessageFont.lfHeight = (LONG)(ncm.lfMessageFont.lfHeight * 1.2f);
        ncm.lfMessageFont.lfWeight  = FW_BOLD;
        ncm.lfMessageFont.lfQuality = CLEARTYPE_QUALITY;
        HFONT hFont = CreateFontIndirectW(&ncm.lfMessageFont);
        ButtonColor color = (ButtonColor)GetWindowLongPtrW(dis->hwndItem, GWLP_USERDATA);
        LRESULT r = DrawCustomButton(dis, color, hFont);
        if (hFont) DeleteObject(hFont);
        return r;
    }

    // ── White background for static controls ──────────────────────────────────
    case WM_CTLCOLORSTATIC: {
        HDC hdc = (HDC)wParam;
        SetBkColor  (hdc, GetSysColor(COLOR_WINDOW));
        SetTextColor(hdc, GetSysColor(COLOR_WINDOWTEXT));
        return (LRESULT)GetSysColorBrush(COLOR_WINDOW);
    }

    // ── Keyboard shortcuts: Escape = Cancel ───────────────────────────────────
    case WM_KEYDOWN:
        if (wParam == VK_ESCAPE) {
            s_scDlgOk = false;
            DestroyWindow(hDlg);
            return 0;
        }
        break;

    case WM_CLOSE:
        s_scDlgOk = false;
        DestroyWindow(hDlg);
        return 0;

    // ── Clean up preview icon on destroy ──────────────────────────────────────
    case WM_DESTROY: {
        ScDlgData* pD = (ScDlgData*)GetWindowLongPtrW(hDlg, GWLP_USERDATA);
        if (pD && pD->hCurrentIcon) {
            DestroyIcon(pD->hCurrentIcon);
            pD->hCurrentIcon = NULL;
        }
        return 0;
    }
    }
    return DefWindowProcW(hDlg, msg, wParam, lParam);
}

// ── Public function ───────────────────────────────────────────────────────────

bool SC_EditShortcutDialog(
    HWND hwndParent,
    HINSTANCE hInst,
    int type,
    const std::wstring& smPath,
    const std::wstring& initName,
    const std::wstring& initExePath,
    const std::wstring& initWorkingDir,
    const std::wstring& initIconPath,
    int initIconIndex,
    bool initRunAsAdmin,
    const std::map<std::wstring, std::wstring>& locale,
    ScDlgResult& out)
{
    // Register the window class once per process lifetime.
    static bool s_classRegistered = false;
    if (!s_classRegistered) {
        WNDCLASSEXW wc   = {};
        wc.cbSize         = sizeof(wc);
        wc.style          = CS_HREDRAW | CS_VREDRAW;
        wc.lpfnWndProc    = ScShortcutDlgProc;
        wc.hInstance      = hInst;
        wc.hCursor        = LoadCursor(NULL, IDC_ARROW);
        wc.hbrBackground  = (HBRUSH)(COLOR_WINDOW + 1);
        wc.lpszClassName  = L"ScShortcutDlgClass";
        RegisterClassExW(&wc);
        s_classRegistered = true;
    }

    // Compute client area size.
    int clientW = S(SCDLG_CONT_W) + 2 * S(SCDLG_PAD_H);
    // Two-row formula per field: LABEL_H + GAP_SM + EDIT_H + GAP
    static const int ROW_H = SCDLG_LABEL_H + SCDLG_GAP_SM + SCDLG_EDIT_H + SCDLG_GAP;
    int clientH = S(SCDLG_PAD_T)
                + S(ROW_H)                                           // exe
                + S(ROW_H)                                           // name
                + S(ROW_H)                                           // workdir
                + S(SCDLG_LABEL_H) + S(SCDLG_GAP_SM)               // icon label
                + S(SCDLG_ICON_SZ) + S(SCDLG_GAP)                   // icon row
                + S(SCDLG_CB_H)    + S(SCDLG_GAP)                   // run-as-admin
                + S(SCDLG_BTN_H)   + S(SCDLG_PAD_B);                // buttons

    // SM path rows add extra height.
    if (type == SCT_STARTMENU && !smPath.empty())
        clientH += S(SCDLG_LABEL_H) + S(SCDLG_GAP_SM)  // SM path label
                +  S(SCDLG_LABEL_H) + S(SCDLG_GAP);     // SM path text

    // Convert client size → outer window size using the exact same styles.
    RECT wrc = { 0, 0, clientW, clientH };
    AdjustWindowRectEx(&wrc,
        WS_POPUP | WS_CAPTION | WS_SYSMENU,
        FALSE, WS_EX_DLGMODALFRAME);
    int wW = wrc.right  - wrc.left;
    int wH = wrc.bottom - wrc.top;

    // Centre on parent (fall back to work area if no parent or screen query fails).
    RECT rcRef;
    if (!hwndParent || !GetWindowRect(hwndParent, &rcRef))
        SystemParametersInfoW(SPI_GETWORKAREA, 0, &rcRef, 0);
    int wx = rcRef.left + (rcRef.right  - rcRef.left - wW) / 2;
    int wy = rcRef.top  + (rcRef.bottom - rcRef.top  - wH) / 2;

    // Clamp so the dialog is always fully visible.
    RECT rcWork;
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &rcWork, 0);
    if (wx < rcWork.left)              wx = rcWork.left;
    if (wy < rcWork.top)               wy = rcWork.top;
    if (wx + wW > rcWork.right)        wx = rcWork.right  - wW;
    if (wy + wH > rcWork.bottom)       wy = rcWork.bottom - wH;

    // Title from locale.
    auto itTitle = locale.find(L"scdlg_title");
    std::wstring title = (itTitle != locale.end()) ? itTitle->second : L"Configure Shortcut";

    // Build per-invocation data on the heap; retrieved via GWLP_USERDATA in proc.
    // Derive initial workingDir from exe directory when caller passes an empty string.
    std::wstring resolvedWorkDir = initWorkingDir;
    bool autoWorkDir = resolvedWorkDir.empty();
    if (autoWorkDir && !initExePath.empty()) {
        size_t sep = initExePath.rfind(L'\\');
        resolvedWorkDir = (sep != std::wstring::npos) ? initExePath.substr(0, sep) : initExePath;
    }

    ScDlgData* pData         = new ScDlgData{};
    pData->type               = type;
    pData->smPath             = smPath;
    pData->name               = initName;
    pData->exePath            = initExePath;
    pData->workingDir         = resolvedWorkDir;
    pData->workingDirIsAuto   = autoWorkDir;
    pData->iconPath           = initIconPath;
    pData->iconIndex          = initIconIndex;
    pData->runAsAdmin         = initRunAsAdmin;
    pData->hInst              = hInst;
    pData->pLocale            = &locale;   // safe: locale outlives the modal loop
    pData->hCurrentIcon       = NULL;
    pData->okPressed          = false;

    s_scDlgOk = false;

    HWND hDlg = CreateWindowExW(
        WS_EX_DLGMODALFRAME,
        L"ScShortcutDlgClass",
        title.c_str(),
        WS_POPUP | WS_CAPTION | WS_SYSMENU,
        wx, wy, wW, wH,
        hwndParent, NULL, hInst, pData);

    if (!hDlg) { delete pData; return false; }

    // Standard modal pattern: disable parent while dialog is alive.
    if (hwndParent) EnableWindow(hwndParent, FALSE);
    ShowWindow(hDlg, SW_SHOW);
    UpdateWindow(hDlg);

    MSG msg = {};
    while (IsWindow(hDlg)) {
        BOOL bRet = GetMessageW(&msg, NULL, 0, 0);
        if (bRet == 0) { PostQuitMessage((int)msg.wParam); break; }  // WM_QUIT: re-post
        if (bRet == -1) break;                                        // error
        if (!IsWindow(hDlg)) break;
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    if (hwndParent) { EnableWindow(hwndParent, TRUE); SetForegroundWindow(hwndParent); }

    // Populate caller's output struct if OK was pressed.
    bool accepted = s_scDlgOk;
    if (accepted) {
        out.name        = pData->name;
        out.exePath     = pData->exePath;
        out.workingDir  = pData->workingDir;
        out.iconPath    = pData->iconPath;
        out.iconIndex   = pData->iconIndex;
        out.runAsAdmin  = pData->runAsAdmin;
    }

    delete pData;
    return accepted;
}
