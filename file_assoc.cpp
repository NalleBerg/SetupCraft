/*
 * file_assoc.cpp — File Associations page implementation for SetupCraft (page index 10).
 *
 * All page state lives here as file-scope statics.
 * mainwindow.cpp routes WM_NOTIFY and WM_COMMAND here via the public functions
 * declared in file_assoc.h.
 *
 * Layout rules: all pixel values through S(), all strings through locale.
 * Persistence:  in-memory only; written to DB on IDM_FILE_SAVE.
 *
 * The ListView uses LVS_EX_CHECKBOXES so each row has an Enabled check.
 * The check state is kept in sync with FileAssocRow::enabled.
 */

#include "file_assoc.h"
#include "file_assoc_dialog.h"
#include "my_scrollbar_vscroll.h"
#include "my_scrollbar_hscroll.h"
#include "mainwindow.h"   // MainWindow::MarkAsModified()
#include "dpi.h"          // S()
#include "button.h"       // CreateCustomButtonWithIcon, CreateCustomButtonWithCompositeIcon
#include "tooltip.h"      // ShowMultilingualTooltip, HideTooltip
#include "db.h"
#include <windowsx.h>     // GET_X_LPARAM / GET_Y_LPARAM
#include <algorithm>

// ── Module-private state ──────────────────────────────────────────────────────

static std::vector<FileAssocRow> s_assocs;
static int                        s_nextId = 1;

static HMSB s_hMsbFaListV = NULL;
static HMSB s_hMsbFaListH = NULL;

static HWND s_hFaList   = NULL;
static HWND s_hFaAdd    = NULL;
static HWND s_hFaEdit   = NULL;
static HWND s_hFaRemove = NULL;

static HINSTANCE s_hInst   = NULL;
static const std::map<std::wstring, std::wstring>* s_pLocale = NULL;
static bool s_refreshing  = false;  // suppress LVN_ITEMCHANGED during RefreshList()

// ── Helpers ───────────────────────────────────────────────────────────────────

static std::wstring L10n(const wchar_t* key, const wchar_t* fallback)
{
    if (!s_pLocale) return fallback;
    auto it = s_pLocale->find(key);
    return (it != s_pLocale->end()) ? it->second : fallback;
}

// ── ListView population ───────────────────────────────────────────────────────

static void RefreshList()
{
    if (!s_hFaList || !IsWindow(s_hFaList)) return;
    s_refreshing = true;
    ListView_DeleteAllItems(s_hFaList);
    for (int i = 0; i < (int)s_assocs.size(); i++) {
        const FileAssocRow& r = s_assocs[i];

        LVITEMW lvi = {};
        lvi.mask     = LVIF_TEXT | LVIF_PARAM;
        lvi.iItem    = i;
        lvi.iSubItem = 0;
        lvi.pszText  = const_cast<LPWSTR>(r.extension.c_str());
        lvi.lParam   = (LPARAM)r.id;
        ListView_InsertItem(s_hFaList, &lvi);

        std::wstring desc = r.description.empty() ? L"—" : r.description;
        ListView_SetItemText(s_hFaList, i, 1, const_cast<LPWSTR>(desc.c_str()));

        // ProgID column: show auto-derived label if empty.
        std::wstring pid = r.prog_id.empty()
            ? L10n(L"fa_progid_auto", L"(auto)")
            : r.prog_id;
        ListView_SetItemText(s_hFaList, i, 2, const_cast<LPWSTR>(pid.c_str()));

        std::wstring icon = r.icon_path.empty() ? L"—" : r.icon_path;
        ListView_SetItemText(s_hFaList, i, 3, const_cast<LPWSTR>(icon.c_str()));

        std::wstring open = r.open_cmd.empty() ? L"—" : r.open_cmd;
        ListView_SetItemText(s_hFaList, i, 4, const_cast<LPWSTR>(open.c_str()));

        // Sync check state with FileAssocRow::enabled.
        ListView_SetCheckState(s_hFaList, i, r.enabled != 0);
    }
    s_refreshing = false;
}

// Return pointer to the assoc with the given in-memory id.
static FileAssocRow* FindById(int id)
{
    for (FileAssocRow& r : s_assocs) if (r.id == id) return &r;
    return nullptr;
}

// Return pointer for the currently selected row, or nullptr.
static FileAssocRow* SelectedAssoc()
{
    if (!s_hFaList) return nullptr;
    int sel = ListView_GetNextItem(s_hFaList, -1, LVNI_SELECTED);
    if (sel < 0) return nullptr;
    LVITEMW lvi = {}; lvi.mask = LVIF_PARAM; lvi.iItem = sel;
    ListView_GetItem(s_hFaList, &lvi);
    return FindById((int)lvi.lParam);
}

// ── ListView subclass proc ────────────────────────────────────────────────────
// Handles right-click context menu, mouse-leave tooltip hiding, and
// check-state changes from LVS_EX_CHECKBOXES.

static WNDPROC s_origListProc = NULL;

static LRESULT CALLBACK FaListSubclassProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg) {
    case WM_RBUTTONUP: {
        int x = GET_X_LPARAM(lParam), y = GET_Y_LPARAM(lParam);
        LVHITTESTINFO ht = {}; ht.pt = { x, y };
        ListView_HitTest(hwnd, &ht);
        if (ht.iItem >= 0) {
            ListView_SetItemState(hwnd, ht.iItem,
                LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);

            POINT pt = { x, y };
            ClientToScreen(hwnd, &pt);

            HMENU hMenu = CreatePopupMenu();
            AppendMenuW(hMenu, MF_STRING, IDM_FA_CTX_EDIT,
                L10n(L"fa_ctx_edit",   L"Edit…").c_str());
            AppendMenuW(hMenu, MF_STRING, IDM_FA_CTX_REMOVE,
                L10n(L"fa_ctx_remove", L"Remove").c_str());

            HWND hParent = GetParent(GetParent(hwnd));
            int cmd = TrackPopupMenu(hMenu, TPM_RETURNCMD | TPM_RIGHTBUTTON,
                                     pt.x, pt.y, 0, hParent, NULL);
            DestroyMenu(hMenu);

            if (cmd == IDM_FA_CTX_EDIT)
                SendMessageW(hParent, WM_COMMAND, MAKEWPARAM(IDC_FA_EDIT, BN_CLICKED), 0);
            else if (cmd == IDM_FA_CTX_REMOVE)
                SendMessageW(hParent, WM_COMMAND, MAKEWPARAM(IDC_FA_REMOVE, BN_CLICKED), 0);
        }
        return 0;
    }
    case WM_MOUSELEAVE:
        HideTooltip();
        break;
    }
    return CallWindowProcW(s_origListProc, hwnd, msg, wParam, lParam);
}

// ── FA_Reset ──────────────────────────────────────────────────────────────────

void FA_Reset()
{
    s_assocs.clear();
    s_nextId       = 1;
    s_hMsbFaListV  = NULL;
    s_hMsbFaListH  = NULL;
}

bool FA_HasAnyEnabled()
{
    for (const FileAssocRow& r : s_assocs)
        if (r.enabled) return true;
    return false;
}

const std::vector<FileAssocRow>& FA_GetAssociations()
{
    return s_assocs;
}

// ── FA_TearDown ───────────────────────────────────────────────────────────────

void FA_TearDown(HWND /*hwnd*/)
{
    // Detach scrollbars BEFORE any DestroyWindow — same double-free guard as DEP.
    if (s_hMsbFaListV) { msb_detach(s_hMsbFaListV); s_hMsbFaListV = NULL; }
    if (s_hMsbFaListH) { msb_detach(s_hMsbFaListH); s_hMsbFaListH = NULL; }

    // Restore ListView subclass proc before the window is destroyed.
    if (s_hFaList && IsWindow(s_hFaList) && s_origListProc) {
        SetWindowLongPtrW(s_hFaList, GWLP_WNDPROC, (LONG_PTR)s_origListProc);
    }
    s_origListProc = NULL;
    s_hFaList      = NULL;
    s_hFaAdd       = NULL;
    s_hFaEdit      = NULL;
    s_hFaRemove    = NULL;
}

// ── FA_BuildPage ──────────────────────────────────────────────────────────────

void FA_BuildPage(HWND hwnd, HINSTANCE hInst,
                  int pageY, int clientWidth,
                  HFONT hPageTitleFont, HFONT hGuiFont,
                  const std::map<std::wstring, std::wstring>& locale)
{
    s_hInst   = hInst;
    s_pLocale = &locale;

    const int padH   = S(20);
    const int padT   = S(20);
    const int gap    = S(10);
    const int btnH   = S(34);
    const int titleH = S(38);

    int y = pageY + padT;

    // ── Page title ────────────────────────────────────────────────────────────
    HWND hTitle = CreateWindowExW(0, L"STATIC",
        L10n(L"fa_page_title", L"File Associations").c_str(),
        WS_CHILD | WS_VISIBLE | SS_LEFT,
        padH, y, clientWidth - padH * 2, titleH,
        hwnd, (HMENU)(UINT_PTR)IDC_FA_PAGE_TITLE, hInst, NULL);
    if (hPageTitleFont) SendMessageW(hTitle, WM_SETFONT, (WPARAM)hPageTitleFont, TRUE);
    y += titleH + gap;

    // ── Action buttons ────────────────────────────────────────────────────────
    std::wstring addTxt = L10n(L"fa_btn_add",    L"Add Association");
    std::wstring edtTxt = L10n(L"fa_btn_edit",   L"Edit");
    std::wstring rmvTxt = L10n(L"fa_btn_remove", L"Remove");
    int wAdd = MeasureButtonWidth(addTxt, true);
    int wEdt = MeasureButtonWidth(edtTxt, true);
    int wRmv = MeasureButtonWidth(rmvTxt, true);

    // Add (Green, composite: document+arrow)
    s_hFaAdd = CreateCustomButtonWithCompositeIcon(
        hwnd, IDC_FA_ADD,
        addTxt.c_str(), ButtonColor::Green,
        L"shell32.dll", 1, L"shell32.dll", 29,
        padH, y, wAdd, btnH, hInst);
    SetButtonTooltip(s_hFaAdd,
        L10n(L"fa_btn_add_tip", L"Add a new file association").c_str());

    // Edit (Blue)
    s_hFaEdit = CreateCustomButtonWithIcon(
        hwnd, IDC_FA_EDIT,
        edtTxt.c_str(), ButtonColor::Blue,
        L"shell32.dll", 87,
        padH + wAdd + gap, y, wEdt, btnH, hInst);
    SetButtonTooltip(s_hFaEdit,
        L10n(L"fa_btn_edit_tip", L"Edit the selected file association").c_str());
    EnableWindow(s_hFaEdit, FALSE);

    // Remove (Red)
    s_hFaRemove = CreateCustomButtonWithIcon(
        hwnd, IDC_FA_REMOVE,
        rmvTxt.c_str(), ButtonColor::Red,
        L"shell32.dll", 131,
        padH + wAdd + gap + wEdt + gap, y, wRmv, btnH, hInst);
    SetButtonTooltip(s_hFaRemove,
        L10n(L"fa_btn_remove_tip", L"Remove the selected file association").c_str());
    EnableWindow(s_hFaRemove, FALSE);

    y += btnH + gap;

    // ── ListView ──────────────────────────────────────────────────────────────
    RECT rcClient; GetClientRect(hwnd, &rcClient);
    int listBottom = rcClient.bottom - S(25) - S(5);
    int listHeight = listBottom - y;
    if (listHeight < S(60)) listHeight = S(60);

    s_hFaList = CreateWindowExW(
        WS_EX_CLIENTEDGE, WC_LISTVIEW, L"",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP |
        LVS_REPORT | LVS_SHOWSELALWAYS | LVS_SINGLESEL,
        padH, y, clientWidth - padH * 2, listHeight,
        hwnd, (HMENU)(UINT_PTR)IDC_FA_LIST, hInst, NULL);

    ListView_SetExtendedListViewStyle(s_hFaList,
        LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES | LVS_EX_CHECKBOXES);
    if (hGuiFont) SendMessageW(s_hFaList, WM_SETFONT, (WPARAM)hGuiFont, TRUE);

    // Columns: Extension | Description | ProgID | Icon | Open Command
    int w = clientWidth - padH * 2;
    int wExt  = w * 10 / 100;
    int wDesc = w * 22 / 100;
    int wPid  = w * 15 / 100;
    int wIcon = w * 18 / 100;
    int wOpen = w - wExt - wDesc - wPid - wIcon;

    LVCOLUMNW lvc = {};
    lvc.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_FMT;
    lvc.fmt  = LVCFMT_LEFT;

    std::wstring c0 = L10n(L"fa_col_ext",  L"Extension");
    lvc.cx = wExt; lvc.pszText = const_cast<LPWSTR>(c0.c_str());
    ListView_InsertColumn(s_hFaList, 0, &lvc);

    std::wstring c1 = L10n(L"fa_col_desc", L"Description");
    lvc.cx = wDesc; lvc.pszText = const_cast<LPWSTR>(c1.c_str());
    ListView_InsertColumn(s_hFaList, 1, &lvc);

    std::wstring c2 = L10n(L"fa_col_progid", L"ProgID");
    lvc.cx = wPid; lvc.pszText = const_cast<LPWSTR>(c2.c_str());
    ListView_InsertColumn(s_hFaList, 2, &lvc);

    std::wstring c3 = L10n(L"fa_col_icon", L"Icon");
    lvc.cx = wIcon; lvc.pszText = const_cast<LPWSTR>(c3.c_str());
    ListView_InsertColumn(s_hFaList, 3, &lvc);

    std::wstring c4 = L10n(L"fa_col_open", L"Open Command");
    lvc.cx = wOpen; lvc.pszText = const_cast<LPWSTR>(c4.c_str());
    ListView_InsertColumn(s_hFaList, 4, &lvc);

    // Subclass for right-click context menu.
    s_origListProc = (WNDPROC)SetWindowLongPtrW(
        s_hFaList, GWLP_WNDPROC, (LONG_PTR)FaListSubclassProc);

    // Attach custom scrollbars.
    s_hMsbFaListV = msb_attach(s_hFaList, MSB_VERTICAL);
    s_hMsbFaListH = msb_attach(s_hFaList, MSB_HORIZONTAL);
    if (s_hMsbFaListH)
        msb_set_edge_gap(s_hMsbFaListH,
            GetSystemMetrics(SM_CYEDGE) + GetSystemMetrics(SM_CYBORDER) + 6);

    RefreshList();
}

// ── FA_OnNotify ───────────────────────────────────────────────────────────────

bool FA_OnNotify(HWND hwnd, NMHDR* hdr)
{
    if (!s_hFaList) return false;
    if (hdr->hwndFrom != s_hFaList) return false;

    if (hdr->code == LVN_ITEMCHANGED) {
        if (s_refreshing) return true;   // ignore synthetic changes during RefreshList()
        NMLISTVIEW* nmlv = (NMLISTVIEW*)hdr;

        // ── Sync check-state change → FileAssocRow::enabled ──────────────────
        // LVS_EX_CHECKBOXES fires LVN_ITEMCHANGED with LVIF_STATE; the check
        // state lives in bits 12–15 (LVIS_STATEIMAGEMASK).
        if ((nmlv->uChanged & LVIF_STATE) &&
            (nmlv->uNewState & LVIS_STATEIMAGEMASK) !=
            (nmlv->uOldState & LVIS_STATEIMAGEMASK))
        {
            // Determine the new check state (1-based image index: 1=unchecked 2=checked).
            int imgIdx = (nmlv->uNewState & LVIS_STATEIMAGEMASK) >> 12;
            bool checked = (imgIdx == 2);

            LVITEMW lvi = {}; lvi.mask = LVIF_PARAM; lvi.iItem = nmlv->iItem;
            ListView_GetItem(s_hFaList, &lvi);
            FileAssocRow* r = FindById((int)lvi.lParam);
            if (r) {
                r->enabled = checked ? 1 : 0;
                MainWindow::MarkAsModified();
            }
        }

        // ── Selection change → enable/disable Edit and Remove buttons ─────────
        int sel = ListView_GetSelectedCount(s_hFaList);
        EnableWindow(s_hFaEdit,   sel > 0 ? TRUE : FALSE);
        EnableWindow(s_hFaRemove, sel > 0 ? TRUE : FALSE);
        return true;
    }

    if (hdr->code == NM_DBLCLK) {
        LPNMITEMACTIVATE nia = (LPNMITEMACTIVATE)hdr;
        if (nia->iItem >= 0) {
            HWND hMain = GetParent(s_hFaList);
            SendMessageW(hMain, WM_COMMAND, MAKEWPARAM(IDC_FA_EDIT, BN_CLICKED), 0);
        }
        return true;
    }

    return false;
}

// ── FA_OnCommand ──────────────────────────────────────────────────────────────

bool FA_OnCommand(HWND hwnd, int wmId, int wmEvent)
{
    if (!s_hFaList) return false;

    if (wmId == IDC_FA_ADD && wmEvent == BN_CLICKED) {
        FileAssocRow blank;
        if (s_pLocale && FA_ShowEditDialog(hwnd, s_hInst, *s_pLocale, blank, true)) {
            blank.id = s_nextId++;
            s_assocs.push_back(blank);
            RefreshList();
            int last = (int)s_assocs.size() - 1;
            ListView_SetItemState(s_hFaList, last,
                LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
            ListView_EnsureVisible(s_hFaList, last, FALSE);
            MainWindow::MarkAsModified();
        }
        return true;
    }

    if (wmId == IDC_FA_EDIT && wmEvent == BN_CLICKED) {
        FileAssocRow* r = SelectedAssoc();
        if (!r) return true;
        FileAssocRow copy = *r;
        if (s_pLocale && FA_ShowEditDialog(hwnd, s_hInst, *s_pLocale, copy, false)) {
            *r = copy;
            RefreshList();
            MainWindow::MarkAsModified();
        }
        return true;
    }

    if (wmId == IDC_FA_REMOVE && wmEvent == BN_CLICKED) {
        int sel = ListView_GetNextItem(s_hFaList, -1, LVNI_SELECTED);
        if (sel < 0) return true;
        LVITEMW lvi = {}; lvi.mask = LVIF_PARAM; lvi.iItem = sel;
        ListView_GetItem(s_hFaList, &lvi);
        int removeId = (int)lvi.lParam;

        std::wstring extName;
        if (FileAssocRow* r = FindById(removeId)) extName = r->extension;

        std::wstring msg;
        if (!extName.empty())
            msg = L10n(L"fa_confirm_remove_named", L"") ;
        if (msg.empty())
            msg = L10n(L"fa_confirm_remove", L"Remove this file association?");
        if (!msg.empty() && !extName.empty()) {
            size_t pos = msg.find(L"{0}");
            if (pos != std::wstring::npos) msg.replace(pos, 3, extName);
        }

        std::wstring title = L10n(L"confirm_remove_title", L"Confirm Remove");
        if (MessageBoxW(hwnd, msg.c_str(), title.c_str(), MB_YESNO | MB_ICONWARNING) != IDYES)
            return true;

        s_assocs.erase(std::remove_if(s_assocs.begin(), s_assocs.end(),
            [removeId](const FileAssocRow& r){ return r.id == removeId; }), s_assocs.end());
        RefreshList();
        EnableWindow(s_hFaEdit,   FALSE);
        EnableWindow(s_hFaRemove, FALSE);
        MainWindow::MarkAsModified();
        return true;
    }

    return false;
}

// ── FA_SaveToDb ───────────────────────────────────────────────────────────────

void FA_SaveToDb(int projectId)
{
    DB::DeleteFileAssocsForProject(projectId);
    for (FileAssocRow& r : s_assocs) {
        int newId = DB::InsertFileAssoc(projectId, r);
        r.id = newId;
    }
}

// ── FA_LoadFromDb ─────────────────────────────────────────────────────────────

void FA_LoadFromDb(int projectId)
{
    s_assocs = DB::GetFileAssocsForProject(projectId);
    s_nextId = 1;
    for (const FileAssocRow& r : s_assocs)
        if (r.id >= s_nextId) s_nextId = r.id + 1;
    RefreshList();
}
