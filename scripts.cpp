/*
 * scripts.cpp — Scripts page implementation for SetupCraft (page index 8).
 *
 * Variable-length list of scripts, each with type (.bat/.ps1), when-to-run,
 * and a full content body.  Displayed as a large-icon ListView tile grid
 * (shell32.dll-1 at 64×64).  A pop-up editor (script_edit_dialog.cpp) is
 * opened for add/edit via toolbar buttons or double-click.
 *
 * Layout (top → bottom):
 *   Page title
 *   Master enable checkbox  +  hint label
 *   [separator]
 *   Toolbar: [+ Add Script]  [Load from file…]  |  [Edit]  [Delete]
 *   ListView: large-icon tiles (icon + name)
 *   Empty-hint label (hidden when ListView has items)
 */

#include "scripts.h"
#include "script_edit_dialog.h"
#include "mainwindow.h"         // MarkAsModified()
#include "db.h"
#include "dpi.h"                // S()
#include "button.h"             // CreateCustomButtonWithIcon(), MeasureButtonWidth()
#include "checkbox.h"           // CreateCustomCheckbox()
#include <shellapi.h>           // ShellExecuteW()
#include <commdlg.h>            // GetOpenFileNameW()
#include <fstream>
#include <vector>

// Declare PrivateExtractIconsW (same as button.cpp — undocumented but widely available)
extern "C" __declspec(dllimport) UINT WINAPI PrivateExtractIconsW(
    LPCWSTR lpszFile, int nIconIndex, int cxIcon, int cyIcon,
    HICON *phicon, UINT *piconid, UINT nIcons, UINT flags);

// ── Module-private state ──────────────────────────────────────────────────────
static bool s_scrEnabled = false;
static std::vector<DB::ScriptRow> s_scripts;
static int  s_nextScrId  = 1;

static HIMAGELIST s_hImgList  = NULL;   // 64×64 icon list for the ListView
static HWND       s_hScrList  = NULL;   // the ListView itself

static HFONT      s_hGuiFont  = NULL;
static HINSTANCE  s_hInst     = NULL;
static const std::map<std::wstring, std::wstring>* s_pLocale = NULL;

// ── Locale helper ─────────────────────────────────────────────────────────────
static std::wstring loc(const wchar_t* key, const wchar_t* fallback)
{
    if (!s_pLocale) return fallback;
    auto it = s_pLocale->find(key);
    return (it != s_pLocale->end()) ? it->second : fallback;
}

// ── RefreshList — rebuild ListView items from s_scripts ───────────────────────
static void RefreshList(HWND hwnd)
{
    if (!s_hScrList) return;
    ListView_DeleteAllItems(s_hScrList);

    for (int i = 0; i < (int)s_scripts.size(); i++) {
        LVITEMW lvi = {};
        lvi.mask    = LVIF_TEXT | LVIF_IMAGE | LVIF_PARAM;
        lvi.iItem   = i;
        lvi.iImage  = 0;          // single icon in the list; image 0 for all
        lvi.lParam  = (LPARAM)i;
        lvi.pszText = const_cast<wchar_t*>(s_scripts[i].name.c_str());
        ListView_InsertItem(s_hScrList, &lvi);
    }

    // Show/hide the empty hint
    HWND hHint = GetDlgItem(hwnd, IDC_SCR_EMPTY_HINT);
    if (hHint)
        ShowWindow(hHint, s_scripts.empty() ? SW_SHOW : SW_HIDE);
}

// ── UpdateToolbar — enable/disable Edit+Delete based on selection ──────────────
static void UpdateToolbar(HWND hwnd)
{
    bool masterOn = s_scrEnabled;
    HWND hAdd    = GetDlgItem(hwnd, IDC_SCR_TOOLBAR_ADD);
    HWND hLoad   = GetDlgItem(hwnd, IDC_SCR_TOOLBAR_LOAD);
    HWND hEdit   = GetDlgItem(hwnd, IDC_SCR_TOOLBAR_EDIT);
    HWND hDelete = GetDlgItem(hwnd, IDC_SCR_TOOLBAR_DELETE);

    if (hAdd)  EnableWindow(hAdd,  masterOn ? TRUE : FALSE);
    if (hLoad) EnableWindow(hLoad, masterOn ? TRUE : FALSE);

    bool hasSel = s_hScrList
               && (ListView_GetNextItem(s_hScrList, -1, LVNI_SELECTED) >= 0);
    if (hEdit)   EnableWindow(hEdit,   (masterOn && hasSel) ? TRUE : FALSE);
    if (hDelete) EnableWindow(hDelete, (masterOn && hasSel) ? TRUE : FALSE);
}

// ── Helper: read a script file from disk into a wstring ───────────────────────
static bool ReadScriptFile(const wchar_t* path, std::wstring& outContent)
{
    std::ifstream ifs(path, std::ios::binary);
    if (!ifs) return false;
    std::string bytes((std::istreambuf_iterator<char>(ifs)),
                       std::istreambuf_iterator<char>());
    // Try UTF-8 first, fall back to system code page
    int wlen = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
                                   bytes.c_str(), (int)bytes.size(), NULL, 0);
    std::wstring wtext;
    if (wlen > 0) {
        wtext.resize(wlen);
        MultiByteToWideChar(CP_UTF8, 0, bytes.c_str(), (int)bytes.size(), &wtext[0], wlen);
    } else {
        int alen = MultiByteToWideChar(CP_ACP, 0,
                                       bytes.c_str(), (int)bytes.size(), NULL, 0);
        wtext.resize(alen);
        MultiByteToWideChar(CP_ACP, 0, bytes.c_str(), (int)bytes.size(), &wtext[0], alen);
    }
    // Normalise line endings to CRLF
    std::wstring norm;
    norm.reserve(wtext.size() + 64);
    for (size_t i = 0; i < wtext.size(); ++i) {
        if (wtext[i] == L'\n' && (i == 0 || wtext[i-1] != L'\r'))
            norm += L'\r';
        norm += wtext[i];
    }
    outContent = std::move(norm);
    return true;
}

// ── SCR_Reset ─────────────────────────────────────────────────────────────────
void SCR_Reset()
{
    s_scrEnabled = false;
    s_scripts.clear();
    s_nextScrId  = 1;
    if (s_hImgList) { ImageList_Destroy(s_hImgList); s_hImgList = NULL; }
    s_hScrList  = NULL;
    s_hGuiFont  = NULL;
    s_hInst     = NULL;
    s_pLocale   = NULL;
}

// ── SCR_BuildPage ─────────────────────────────────────────────────────────────
void SCR_BuildPage(HWND hwnd, HINSTANCE hInst,
                   int pageY, int clientWidth,
                   HFONT hPageTitleFont, HFONT hGuiFont,
                   const std::map<std::wstring, std::wstring>& locale)
{
    s_hInst   = hInst;
    s_hGuiFont = hGuiFont;
    s_pLocale  = &locale;

    const int padH   = S(20);
    const int gap    = S(10);
    const int titleH = S(38);
    const int cbH    = S(22);
    const int btnH   = S(30);

    int y = pageY + S(20);

    // ── Page title ────────────────────────────────────────────────────────────
    HWND hTitle = CreateWindowExW(0, L"STATIC",
        loc(L"scr_page_title", L"Run Scripts").c_str(),
        WS_CHILD | WS_VISIBLE | SS_LEFT,
        padH, y, clientWidth - padH * 2, titleH,
        hwnd, (HMENU)(UINT_PTR)IDC_SCR_PAGE_TITLE, hInst, NULL);
    if (hPageTitleFont) SendMessageW(hTitle, WM_SETFONT, (WPARAM)hPageTitleFont, TRUE);
    y += titleH + gap;

    // ── Master enable checkbox ────────────────────────────────────────────────
    CreateCustomCheckbox(hwnd, IDC_SCR_MASTER_ENABLE,
        loc(L"scr_enable", L"Enable scripts for this project"),
        s_scrEnabled, padH, y, clientWidth - padH * 2, cbH, hInst);
    y += cbH + S(4);

    // Hint label shown when master is off
    HWND hHint = CreateWindowExW(0, L"STATIC",
        loc(L"scr_enable_hint",
            L"When unchecked, no scripts will be packaged or run by the installer.").c_str(),
        WS_CHILD | SS_LEFT,
        padH + S(24), y, clientWidth - padH * 2 - S(24), cbH,
        hwnd, (HMENU)(UINT_PTR)IDC_SCR_ENABLE_HINT, hInst, NULL);
    if (hGuiFont) SendMessageW(hHint, WM_SETFONT, (WPARAM)hGuiFont, TRUE);
    ShowWindow(hHint, s_scrEnabled ? SW_HIDE : SW_SHOW);
    y += cbH + gap;

    // ── Separator ─────────────────────────────────────────────────────────────
    CreateWindowExW(0, L"STATIC", L"",
        WS_CHILD | WS_VISIBLE | SS_ETCHEDHORZ,
        padH, y, clientWidth - padH * 2, S(2),
        hwnd, NULL, hInst, NULL);
    y += S(2) + gap;

    // ── Toolbar row ───────────────────────────────────────────────────────────
    std::wstring addTxt  = loc(L"scr_toolbar_add",    L"Add Script");
    std::wstring loadTxt = loc(L"scr_toolbar_load",   L"Load from file\u2026");
    std::wstring editTxt = loc(L"scr_toolbar_edit",   L"Edit");
    std::wstring delTxt  = loc(L"scr_toolbar_delete", L"Delete");

    int wAdd  = MeasureButtonWidth(addTxt,  true);
    int wLoad = MeasureButtonWidth(loadTxt, true);
    int wEdit = MeasureButtonWidth(editTxt, true);
    int wDel  = MeasureButtonWidth(delTxt,  true);

    int bx = padH;
    HWND hAdd = CreateCustomButtonWithIcon(
        hwnd, IDC_SCR_TOOLBAR_ADD, addTxt.c_str(), ButtonColor::Blue,
        L"shell32.dll", 149,   // "new document" / plus-style icon
        bx, y, wAdd, btnH, hInst);
    SetButtonTooltip(hAdd, loc(L"scr_toolbar_add_tip", L"Create a new script entry").c_str());
    EnableWindow(hAdd, s_scrEnabled ? TRUE : FALSE);
    bx += wAdd + gap;

    HWND hLoad = CreateCustomButtonWithIcon(
        hwnd, IDC_SCR_TOOLBAR_LOAD, loadTxt.c_str(), ButtonColor::Blue,
        L"shell32.dll", 3,     // folder/open icon
        bx, y, wLoad, btnH, hInst);
    SetButtonTooltip(hLoad,
        loc(L"scr_toolbar_load_tip",
            L"Load a .bat or .ps1 file from disk and add it as a new script").c_str());
    EnableWindow(hLoad, s_scrEnabled ? TRUE : FALSE);
    bx += wLoad + S(20);   // visual gap before action buttons

    HWND hEdit = CreateCustomButtonWithIcon(
        hwnd, IDC_SCR_TOOLBAR_EDIT, editTxt.c_str(), ButtonColor::Blue,
        L"shell32.dll", 269,   // pencil/edit icon
        bx, y, wEdit, btnH, hInst);
    SetButtonTooltip(hEdit,
        loc(L"scr_toolbar_edit_tip", L"Open the selected script in the editor").c_str());
    EnableWindow(hEdit, FALSE);
    bx += wEdit + gap;

    HWND hDel = CreateCustomButtonWithIcon(
        hwnd, IDC_SCR_TOOLBAR_DELETE, delTxt.c_str(), ButtonColor::Red,
        L"shell32.dll", 131,   // delete/trash icon
        bx, y, wDel, btnH, hInst);
    SetButtonTooltip(hDel,
        loc(L"scr_toolbar_delete_tip",
            L"Remove the selected script from this project").c_str());
    EnableWindow(hDel, FALSE);
    y += btnH + gap;

    // ── Build the 64×64 ImageList ─────────────────────────────────────────────
    if (s_hImgList) { ImageList_Destroy(s_hImgList); s_hImgList = NULL; }
    int iconPx = S(64);
    s_hImgList = ImageList_Create(iconPx, iconPx,
                                  ILC_COLOR32 | ILC_MASK, 1, 0);
    if (s_hImgList) {
        HICON hIco = NULL;
        UINT ex = PrivateExtractIconsW(L"shell32.dll", 1,
                                       iconPx, iconPx, &hIco, NULL, 1, 0);
        if (ex == 0 || !hIco)
            ExtractIconExW(L"shell32.dll", 1, &hIco, NULL, 1);
        if (hIco) {
            ImageList_AddIcon(s_hImgList, hIco);
            DestroyIcon(hIco);
        }
    }

    // ── ListView (LargeIcon) ──────────────────────────────────────────────────
    RECT rcClient; GetClientRect(hwnd, &rcClient);
    int listH = rcClient.bottom - y - S(10);
    if (listH < S(120)) listH = S(120);

    s_hScrList = CreateWindowExW(
        WS_EX_CLIENTEDGE, WC_LISTVIEW, L"",
        WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS |
        LVS_ICON | LVS_SINGLESEL | LVS_AUTOARRANGE | LVS_SHOWSELALWAYS,
        padH, y, clientWidth - padH * 2, listH,
        hwnd, (HMENU)(UINT_PTR)IDC_SCR_LIST, hInst, NULL);

    if (s_hScrList) {
        ListView_SetExtendedListViewStyle(s_hScrList,
            LVS_EX_DOUBLEBUFFER | LVS_EX_UNDERLINEHOT);
        if (s_hImgList)
            ListView_SetImageList(s_hScrList, s_hImgList, LVSIL_NORMAL);
    }
    y += listH + gap;

    // ── "No scripts yet" label (shown when list is empty) ─────────────────────
    HWND hEmptyHint = CreateWindowExW(0, L"STATIC",
        loc(L"scr_no_scripts",
            L"No scripts added yet. Click \u201cAdd Script\u201d to create one.").c_str(),
        WS_CHILD | SS_CENTER | SS_CENTERIMAGE,
        padH, y - listH - gap + S(10),    // centred inside the list area
        clientWidth - padH * 2, S(24),
        hwnd, (HMENU)(UINT_PTR)IDC_SCR_EMPTY_HINT, hInst, NULL);
    if (hGuiFont) SendMessageW(hEmptyHint, WM_SETFONT, (WPARAM)hGuiFont, TRUE);
    ShowWindow(hEmptyHint, s_scripts.empty() ? SW_SHOW : SW_HIDE);

    // Populate from in-memory state (either loaded by SCR_LoadFromDb or empty)
    RefreshList(hwnd);
    UpdateToolbar(hwnd);
}

// ── SCR_TearDown ──────────────────────────────────────────────────────────────
void SCR_TearDown(HWND /*hwnd*/)
{
    // Controls are destroyed by the generic SwitchPage enumeration loop.
    // Free the ImageList here; it's not a child window.
    if (s_hImgList) { ImageList_Destroy(s_hImgList); s_hImgList = NULL; }
    s_hScrList  = NULL;
    s_hGuiFont  = NULL;
    s_hInst     = NULL;
    s_pLocale   = NULL;
}

// ── OpenEditorForItem — shared by toolbar Edit + double-click ─────────────────
static void OpenEditorForItem(HWND hwnd, int idx)
{
    if (idx < 0 || idx >= (int)s_scripts.size()) return;
    DB::ScriptRow copy = s_scripts[idx];
    if (SCR_EditDialog(hwnd, s_hInst, *s_pLocale, s_scripts, copy)) {
        copy.sort_order = idx;
        s_scripts[idx]  = copy;
        RefreshList(hwnd);
        // Restore selection
        if (s_hScrList)
            ListView_SetItemState(s_hScrList, idx,
                LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
        MainWindow::MarkAsModified();
    }
}

// ── SCR_OnCommand ─────────────────────────────────────────────────────────────
bool SCR_OnCommand(HWND hwnd, int wmId, int wmEvent, HWND /*hCtrl*/)
{
    // Master enable checkbox
    if (wmId == IDC_SCR_MASTER_ENABLE && wmEvent == BN_CLICKED) {
        HWND hCb = GetDlgItem(hwnd, IDC_SCR_MASTER_ENABLE);
        s_scrEnabled = (hCb && SendMessageW(hCb, BM_GETCHECK, 0, 0) == BST_CHECKED);
        HWND hHint = GetDlgItem(hwnd, IDC_SCR_ENABLE_HINT);
        if (hHint) ShowWindow(hHint, s_scrEnabled ? SW_HIDE : SW_SHOW);
        UpdateToolbar(hwnd);
        MainWindow::MarkAsModified();
        return true;
    }

    // Add Script
    if (wmId == IDC_SCR_TOOLBAR_ADD && wmEvent == BN_CLICKED) {
        DB::ScriptRow newScr;
        newScr.id           = s_nextScrId;
        newScr.when_to_run  = (int)SWR_AFTER_FILES;
        newScr.type         = SCR_TYPE_PS1;
        newScr.wait_for_completion = 1;
        if (SCR_EditDialog(hwnd, s_hInst, *s_pLocale, s_scripts, newScr)) {
            newScr.sort_order = (int)s_scripts.size();
            s_scripts.push_back(newScr);
            s_nextScrId++;
            RefreshList(hwnd);
            // Select the new item
            if (s_hScrList) {
                int ni = (int)s_scripts.size() - 1;
                ListView_SetItemState(s_hScrList, ni,
                    LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
            }
            UpdateToolbar(hwnd);
            MainWindow::MarkAsModified();
        }
        return true;
    }

    // Load from file — opens file dialog, pre-fills content, then edit dialog
    if (wmId == IDC_SCR_TOOLBAR_LOAD && wmEvent == BN_CLICKED) {
        OPENFILENAMEW ofn = {};
        wchar_t szFile[MAX_PATH] = {};
        ofn.lStructSize  = sizeof(OPENFILENAMEW);
        ofn.hwndOwner    = hwnd;
        ofn.lpstrFile    = szFile;
        ofn.nMaxFile     = MAX_PATH;
        ofn.lpstrFilter  = L"Script Files (*.bat;*.cmd;*.ps1)\0*.bat;*.cmd;*.ps1\0All Files (*.*)\0*.*\0";
        ofn.nFilterIndex = 1;
        ofn.Flags = OFN_EXPLORER | OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST | OFN_HIDEREADONLY;

        if (GetOpenFileNameW(&ofn)) {
            std::wstring content;
            if (ReadScriptFile(szFile, content)) {
                // Derive defaults from the file
                DB::ScriptRow newScr;
                newScr.id     = s_nextScrId;
                newScr.content = content;
                // Guess type from extension
                std::wstring path(szFile);
                auto ext = path.rfind(L'.');
                if (ext != std::wstring::npos) {
                    std::wstring e = path.substr(ext);
                    for (auto& c : e) c = (wchar_t)towlower(c);
                    newScr.type = (e == L".bat" || e == L".cmd") ? SCR_TYPE_BAT : SCR_TYPE_PS1;
                }
                // Use the filename (without extension) as default name
                auto sep = path.find_last_of(L"\\/");
                std::wstring fname = (sep != std::wstring::npos) ? path.substr(sep + 1) : path;
                auto dot = fname.rfind(L'.');
                if (dot != std::wstring::npos) fname = fname.substr(0, dot);
                newScr.name          = fname;
                newScr.when_to_run   = (int)SWR_AFTER_FILES;
                newScr.wait_for_completion = 1;

                if (SCR_EditDialog(hwnd, s_hInst, *s_pLocale, s_scripts, newScr)) {
                    newScr.sort_order = (int)s_scripts.size();
                    s_scripts.push_back(newScr);
                    s_nextScrId++;
                    RefreshList(hwnd);
                    if (s_hScrList) {
                        int ni = (int)s_scripts.size() - 1;
                        ListView_SetItemState(s_hScrList, ni,
                            LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
                    }
                    UpdateToolbar(hwnd);
                    MainWindow::MarkAsModified();
                }
            }
        }
        return true;
    }

    // Edit selected script
    if (wmId == IDC_SCR_TOOLBAR_EDIT && wmEvent == BN_CLICKED) {
        if (!s_hScrList) return true;
        int sel = ListView_GetNextItem(s_hScrList, -1, LVNI_SELECTED);
        OpenEditorForItem(hwnd, sel);
        UpdateToolbar(hwnd);
        return true;
    }

    // Delete selected script
    if (wmId == IDC_SCR_TOOLBAR_DELETE && wmEvent == BN_CLICKED) {
        if (!s_hScrList) return true;
        int sel = ListView_GetNextItem(s_hScrList, -1, LVNI_SELECTED);
        if (sel < 0 || sel >= (int)s_scripts.size()) return true;

        // Confirmation
        wchar_t fmt[256];
        std::wstring msg;
        {
            std::wstring tmpl = loc(L"scr_delete_confirm",
                L"Remove the script \u201c%s\u201d?\nThis cannot be undone.");
            msg.resize(tmpl.size() + s_scripts[sel].name.size() + 4);
            _snwprintf_s(&msg[0], msg.size(), _TRUNCATE,
                         tmpl.c_str(), s_scripts[sel].name.c_str());
        }
        int r = MessageBoxW(hwnd, msg.c_str(),
                            loc(L"scr_delete_title", L"Remove Script").c_str(),
                            MB_YESNO | MB_ICONQUESTION);
        if (r == IDYES) {
            s_scripts.erase(s_scripts.begin() + sel);
            // Recalculate sort_order
            for (int i = 0; i < (int)s_scripts.size(); i++)
                s_scripts[i].sort_order = i;
            RefreshList(hwnd);
            UpdateToolbar(hwnd);
            MainWindow::MarkAsModified();
        }
        return true;
    }

    return false;
}

// ── SCR_OnNotify ──────────────────────────────────────────────────────────────
LRESULT SCR_OnNotify(HWND hwnd, LPNMHDR nmhdr, bool* handled)
{
    *handled = false;
    if (!nmhdr || nmhdr->idFrom != IDC_SCR_LIST) return 0;

    // Selection changed — update toolbar button states
    if (nmhdr->code == LVN_ITEMCHANGED) {
        LPNMLISTVIEW pnmv = (LPNMLISTVIEW)nmhdr;
        if (pnmv->uChanged & LVIF_STATE) {
            UpdateToolbar(hwnd);
            *handled = true;
            return 0;
        }
    }

    // Double-click — open editor
    if (nmhdr->code == NM_DBLCLK) {
        if (s_hScrList) {
            int sel = ListView_GetNextItem(s_hScrList, -1, LVNI_SELECTED);
            OpenEditorForItem(hwnd, sel);
            UpdateToolbar(hwnd);
        }
        *handled = true;
        return 0;
    }

    // Right-click — context menu: Edit / Delete
    if (nmhdr->code == NM_RCLICK) {
        if (!s_hScrList) return 0;
        int sel = ListView_GetNextItem(s_hScrList, -1, LVNI_SELECTED);
        if (sel < 0) return 0;

        HMENU hMenu = CreatePopupMenu();
        AppendMenuW(hMenu, MF_STRING, 1,
                    loc(L"scr_ctx_edit",   L"Edit\u2026").c_str());
        AppendMenuW(hMenu, MF_STRING, 2,
                    loc(L"scr_ctx_delete", L"Remove").c_str());
        POINT pt; GetCursorPos(&pt);
        int cmd = TrackPopupMenu(hMenu,
            TPM_RETURNCMD | TPM_RIGHTBUTTON, pt.x, pt.y, 0, hwnd, NULL);
        DestroyMenu(hMenu);

        if (cmd == 1) {
            OpenEditorForItem(hwnd, sel);
            UpdateToolbar(hwnd);
        } else if (cmd == 2) {
            // Delegate to the Delete button handler
            SendMessageW(hwnd, WM_COMMAND,
                MAKEWPARAM(IDC_SCR_TOOLBAR_DELETE, BN_CLICKED), 0);
        }
        *handled = true;
        return 0;
    }

    return 0;
}

// ── SCR_SaveToDb ──────────────────────────────────────────────────────────────
void SCR_SaveToDb(int projectId)
{
    DB::DeleteScriptsForProject(projectId);
    for (DB::ScriptRow& s : s_scripts) {
        s.project_id = projectId;
        int newId = DB::InsertScript(projectId, s);
        if (newId > 0) s.id = newId;
    }
}

// ── SCR_LoadFromDb ────────────────────────────────────────────────────────────
void SCR_LoadFromDb(int projectId)
{
    s_scripts = DB::GetScriptsForProject(projectId);
    s_nextScrId = 1;
    for (const auto& s : s_scripts)
        if (s.id >= s_nextScrId) s_nextScrId = s.id + 1;
    // If the page is open, refresh the ListView now
    if (s_hScrList) {
        HWND hwnd = GetParent(s_hScrList);
        RefreshList(hwnd);
        UpdateToolbar(hwnd);
    }
}

