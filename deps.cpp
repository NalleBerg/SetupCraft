/*
 * deps.cpp — Dependencies page implementation for SetupCraft (page index 3).
 *
 * All Dependencies-page state lives here as file-scope statics.
 * mainwindow.cpp routes WM_NOTIFY and WM_COMMAND here via the public
 * functions declared in deps.h.
 *
 * Layout rules:  all pixel values through S(),  all strings through locale.
 * Persistence:   in-memory only; written to DB on IDM_FILE_SAVE.
 */

#include "deps.h"
#include "dep_edit_dialog.h"
#include "my_scrollbar_vscroll.h"
#include "my_scrollbar_hscroll.h"
#include "mainwindow.h"   // MainWindow::MarkAsModified(), GetLocale()
#include "dpi.h"          // S() DPI-scale macro
#include "button.h"       // CreateCustomButtonWithIcon, CreateCustomButtonWithCompositeIcon
#include "tooltip.h"      // ShowMultilingualTooltip, HideTooltip, IsTooltipVisible
#include "db.h"
#include <windowsx.h>     // GET_X_LPARAM / GET_Y_LPARAM
#include <algorithm>

// PrivateExtractIconsW — undocumented but reliable (used by button.cpp etc.).
extern "C" __declspec(dllimport) UINT WINAPI PrivateExtractIconsW(
    LPCWSTR szFileName, int nIconIndex, int cxIcon, int cyIcon,
    HICON* phicon, UINT* piconid, UINT nIcons, UINT flags);

// ── Module-private state ──────────────────────────────────────────────────────

static std::vector<ExternalDep> s_deps;
static int                       s_nextDepId = 1;   // in-memory counter; reset by DEP_Reset

// Custom scrollbar handles for the ListView.
static HMSB s_hMsbDepListV = NULL;
static HMSB s_hMsbDepListH = NULL;

// Live handles to the page ListView and buttons.
// Set by DEP_BuildPage; the SwitchPage teardown destroys the page container
// which in turn destroys all child controls, so we just NULLify here.
static HWND s_hDepList   = NULL;
static HWND s_hDepAdd    = NULL;
static HWND s_hDepEdit   = NULL;
static HWND s_hDepRemove = NULL;

// Saved HINSTANCE and locale pointer for use in the subclass proc.
static HINSTANCE                              s_hInst  = NULL;
static const std::map<std::wstring,std::wstring>* s_pLocale = NULL;

// ── Helpers ───────────────────────────────────────────────────────────────────

// Safe locale lookup.
static std::wstring L10n(const wchar_t* key, const wchar_t* fallback)
{
    if (!s_pLocale) return fallback;
    auto it = s_pLocale->find(key);
    return (it != s_pLocale->end()) ? it->second : fallback;
}

// Delivery-type display string.
static std::wstring DeliveryStr(DepDelivery d)
{
    switch (d) {
    case DD_BUNDLED:           return L10n(L"dep_delivery_bundled",      L"Bundled");
    case DD_AUTO_DOWNLOAD:     return L10n(L"dep_delivery_download",     L"Auto-download");
    case DD_REDIRECT_URL:      return L10n(L"dep_delivery_redirect",     L"Redirect URL");
    case DD_INSTRUCTIONS_ONLY: return L10n(L"dep_delivery_instructions", L"Instructions only");
    }
    return L"";
}

// ── ListView population ────────────────────────────────────────────────────────

// Repopulate the ListView from s_deps.
static void RefreshList()
{
    if (!s_hDepList || !IsWindow(s_hDepList)) return;
    ListView_DeleteAllItems(s_hDepList);
    int row = 0;
    for (const ExternalDep& dep : s_deps) {
        LVITEMW lvi = {};
        lvi.mask    = LVIF_TEXT | LVIF_PARAM;
        lvi.iItem   = row;
        lvi.iSubItem = 0;
        lvi.pszText = const_cast<LPWSTR>(dep.display_name.c_str());
        lvi.lParam  = (LPARAM)dep.id;
        ListView_InsertItem(s_hDepList, &lvi);

        std::wstring delivStr = DeliveryStr(dep.delivery);
        ListView_SetItemText(s_hDepList, row, 1, const_cast<LPWSTR>(delivStr.c_str()));

        std::wstring reqStr = dep.is_required
            ? L10n(L"dep_required",  L"Required")
            : L10n(L"dep_optional",  L"Optional");
        ListView_SetItemText(s_hDepList, row, 2, const_cast<LPWSTR>(reqStr.c_str()));

        // Detection column: "Reg key" > "File path" > "—"
        std::wstring detStr;
        if (!dep.detect_reg_key.empty())
            detStr = L10n(L"dep_detect_reg",  L"Registry key");
        else if (!dep.detect_file_path.empty())
            detStr = L10n(L"dep_detect_file", L"File path");
        else
            detStr = L"—";
        ListView_SetItemText(s_hDepList, row, 3, const_cast<LPWSTR>(detStr.c_str()));

        ++row;
    }
}

// Find dep by in-memory id.
static ExternalDep* FindDepById(int id)
{
    for (ExternalDep& d : s_deps) if (d.id == id) return &d;
    return nullptr;
}

// Return the dep referenced by the currently selected ListView row, or nullptr.
static ExternalDep* SelectedDep()
{
    if (!s_hDepList) return nullptr;
    int sel = ListView_GetNextItem(s_hDepList, -1, LVNI_SELECTED);
    if (sel < 0) return nullptr;
    LVITEMW lvi = {}; lvi.mask = LVIF_PARAM; lvi.iItem = sel;
    ListView_GetItem(s_hDepList, &lvi);
    return FindDepById((int)lvi.lParam);
}

// ── ListView subclass proc ────────────────────────────────────────────────────
// Handles right-click context menu and mouse-leave tooltip hiding.

static WNDPROC s_origListProc = NULL;

static LRESULT CALLBACK DepListSubclassProc(
    HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg) {
    case WM_RBUTTONUP: {
        int x = GET_X_LPARAM(lParam), y = GET_Y_LPARAM(lParam);
        LVHITTESTINFO ht = {}; ht.pt = { x, y };
        ListView_HitTest(hwnd, &ht);
        if (ht.iItem >= 0) {
            // Make sure the row is selected.
            ListView_SetItemState(hwnd, ht.iItem,
                LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);

            POINT pt = { x, y };
            ClientToScreen(hwnd, &pt);

            HMENU hMenu = CreatePopupMenu();
            AppendMenuW(hMenu, MF_STRING, IDM_EXT_DEP_CTX_EDIT,
                L10n(L"dep_ctx_edit",   L"Edit…").c_str());
            AppendMenuW(hMenu, MF_STRING, IDM_EXT_DEP_CTX_REMOVE,
                L10n(L"dep_ctx_remove", L"Remove").c_str());

            HWND hParent = GetParent(GetParent(hwnd)); // list → page container → main
            int cmd = TrackPopupMenu(hMenu, TPM_RETURNCMD | TPM_RIGHTBUTTON,
                                     pt.x, pt.y, 0, hParent, NULL);
            DestroyMenu(hMenu);

            if (cmd == IDM_EXT_DEP_CTX_EDIT)
                SendMessageW(hParent, WM_COMMAND, MAKEWPARAM(IDC_DEP_EDIT, BN_CLICKED), 0);
            else if (cmd == IDM_EXT_DEP_CTX_REMOVE)
                SendMessageW(hParent, WM_COMMAND, MAKEWPARAM(IDC_DEP_REMOVE, BN_CLICKED), 0);
        }
        return 0;
    }
    case WM_MOUSELEAVE:
        HideTooltip();
        break;
    }
    return CallWindowProcW(s_origListProc, hwnd, msg, wParam, lParam);
}

// ── DEP_Reset ─────────────────────────────────────────────────────────────────

void DEP_Reset()
{
    s_deps.clear();
    s_nextDepId    = 1;
    s_hMsbDepListV = NULL;  // stale-handle guard (WM_DESTROY already freed ctx)
    s_hMsbDepListH = NULL;
}

bool DEP_HasAny()
{
    return !s_deps.empty();
}

int DEP_GetDeliveryModeMask()
{
    int mask = 0;
    for (const auto& d : s_deps)
        mask |= (1 << (int)d.delivery);
    return mask;
}

// ── DEP_TearDown ──────────────────────────────────────────────────────────────

void DEP_TearDown(HWND /*hwnd*/)
{
    // Detach custom scrollbars BEFORE any DestroyWindow calls.
    if (s_hMsbDepListV) { msb_detach(s_hMsbDepListV); s_hMsbDepListV = NULL; }
    if (s_hMsbDepListH) { msb_detach(s_hMsbDepListH); s_hMsbDepListH = NULL; }

    // Restore ListView subclass proc before the window is destroyed.
    if (s_hDepList && IsWindow(s_hDepList) && s_origListProc) {
        SetWindowLongPtrW(s_hDepList, GWLP_WNDPROC, (LONG_PTR)s_origListProc);
    }
    s_origListProc = NULL;
    s_hDepList     = NULL;
    s_hDepAdd      = NULL;
    s_hDepEdit     = NULL;
    s_hDepRemove   = NULL;
}

// ── DEP_BuildPage ─────────────────────────────────────────────────────────────

void DEP_BuildPage(HWND hwnd, HINSTANCE hInst,
                   int pageY, int clientWidth,
                   HFONT hPageTitleFont, HFONT hGuiFont,
                   const std::map<std::wstring, std::wstring>& locale)
{
    s_hInst   = hInst;
    s_pLocale = &locale;

    // ── Layout constants ──────────────────────────────────────────────────────
    const int padH   = S(20);
    const int padT   = S(20);
    const int gap    = S(10);
    const int btnH   = S(34);
    const int titleH = S(38);

    // ── Page title ────────────────────────────────────────────────────────────
    int y = pageY + padT;
    HWND hTitle = CreateWindowExW(0, L"STATIC",
        L10n(L"dep_page_title", L"Dependencies").c_str(),
        WS_CHILD | WS_VISIBLE | SS_LEFT,
        padH, y, clientWidth - padH * 2, titleH,
        hwnd, (HMENU)(UINT_PTR)IDC_DEP_PAGE_TITLE, hInst, NULL);
    if (hPageTitleFont) SendMessageW(hTitle, WM_SETFONT, (WPARAM)hPageTitleFont, TRUE);
    y += titleH + gap;

    // ── Action buttons ────────────────────────────────────────────────────────
    std::wstring addTxt = L10n(L"dep_btn_add",    L"Add Dependency");
    std::wstring edtTxt = L10n(L"dep_btn_edit",   L"Edit");
    std::wstring rmvTxt = L10n(L"dep_btn_remove", L"Remove");
    int wAdd = MeasureButtonWidth(addTxt, true);
    int wEdt = MeasureButtonWidth(edtTxt, true);
    int wRmv = MeasureButtonWidth(rmvTxt, true);

    // Add (Green, composite: package+arrow = shell32 257+29)
    s_hDepAdd = CreateCustomButtonWithCompositeIcon(
        hwnd, IDC_DEP_ADD,
        addTxt.c_str(),
        ButtonColor::Green,
        L"shell32.dll", 257, L"shell32.dll", 29,
        padH, y, wAdd, btnH, hInst);
    SetButtonTooltip(s_hDepAdd,
        L10n(L"dep_btn_add_tip", L"Add a new external dependency").c_str());

    // Edit (Blue, magnifier = shell32 87)
    s_hDepEdit = CreateCustomButtonWithIcon(
        hwnd, IDC_DEP_EDIT,
        edtTxt.c_str(),
        ButtonColor::Blue,
        L"shell32.dll", 87,
        padH + wAdd + gap, y, wEdt, btnH, hInst);
    SetButtonTooltip(s_hDepEdit,
        L10n(L"dep_btn_edit_tip", L"Edit the selected dependency").c_str());
    EnableWindow(s_hDepEdit, FALSE);

    // Remove (Red, red X = shell32 131)
    s_hDepRemove = CreateCustomButtonWithIcon(
        hwnd, IDC_DEP_REMOVE,
        rmvTxt.c_str(),
        ButtonColor::Red,
        L"shell32.dll", 131,
        padH + wAdd + gap + wEdt + gap, y, wRmv, btnH, hInst);
    SetButtonTooltip(s_hDepRemove,
        L10n(L"dep_btn_remove_tip", L"Remove the selected dependency").c_str());
    EnableWindow(s_hDepRemove, FALSE);

    y += btnH + gap;

    // ── ListView ──────────────────────────────────────────────────────────────
    int listH = clientWidth; // will be clipped by WS_CHILD; use a generous height
    (void)listH;
    RECT rcClient; GetClientRect(hwnd, &rcClient);
    int listBottom = rcClient.bottom - S(25) - S(5); // above status bar
    int listHeight = listBottom - y;
    if (listHeight < S(60)) listHeight = S(60);

    s_hDepList = CreateWindowExW(
        WS_EX_CLIENTEDGE, WC_LISTVIEW, L"",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP |
        LVS_REPORT | LVS_SHOWSELALWAYS | LVS_SINGLESEL,
        padH, y, clientWidth - padH * 2, listHeight,
        hwnd, (HMENU)(UINT_PTR)IDC_DEP_LIST, hInst, NULL);

    ListView_SetExtendedListViewStyle(s_hDepList, LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES);
    if (hGuiFont) SendMessageW(s_hDepList, WM_SETFONT, (WPARAM)hGuiFont, TRUE);

    // Columns: Name  |  Delivery  |  Required  |  Detection
    int colNameW    = (clientWidth - padH * 2) * 35 / 100;
    int colDelivW   = (clientWidth - padH * 2) * 22 / 100;
    int colReqW     = (clientWidth - padH * 2) * 18 / 100;
    int colDetW     = (clientWidth - padH * 2) - colNameW - colDelivW - colReqW;

    LVCOLUMNW lvc = {};
    lvc.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_FMT;
    lvc.fmt  = LVCFMT_LEFT;

    lvc.cx = colNameW;
    std::wstring colName = L10n(L"dep_col_name", L"Name");
    lvc.pszText = const_cast<LPWSTR>(colName.c_str());
    ListView_InsertColumn(s_hDepList, 0, &lvc);

    lvc.cx = colDelivW;
    std::wstring colDeliv = L10n(L"dep_col_delivery", L"Delivery");
    lvc.pszText = const_cast<LPWSTR>(colDeliv.c_str());
    ListView_InsertColumn(s_hDepList, 1, &lvc);

    lvc.cx = colReqW;
    std::wstring colReq = L10n(L"dep_col_required", L"Required");
    lvc.pszText = const_cast<LPWSTR>(colReq.c_str());
    ListView_InsertColumn(s_hDepList, 2, &lvc);

    lvc.cx = colDetW;
    std::wstring colDet = L10n(L"dep_col_detection", L"Detection");
    lvc.pszText = const_cast<LPWSTR>(colDet.c_str());
    ListView_InsertColumn(s_hDepList, 3, &lvc);

    // Subclass the ListView for right-click context menu.
    s_origListProc = (WNDPROC)SetWindowLongPtrW(
        s_hDepList, GWLP_WNDPROC, (LONG_PTR)DepListSubclassProc);

    // Attach custom hidden scrollbars BEFORE population so the WM_NCPAINT
    // intercept is in place during row insertion — same pattern as the
    // Components page (mainwindow.cpp), where thumb drag is confirmed working.
    s_hMsbDepListV = msb_attach(s_hDepList, MSB_VERTICAL);
    s_hMsbDepListH = msb_attach(s_hDepList, MSB_HORIZONTAL);
    if (s_hMsbDepListH)
        msb_set_edge_gap(s_hMsbDepListH,
            GetSystemMetrics(SM_CYEDGE) + GetSystemMetrics(SM_CYBORDER) + 6);

    // Populate from in-memory state.
    RefreshList();

}

// ── DEP_OnNotify ──────────────────────────────────────────────────────────────

LRESULT DEP_OnNotify(HWND /*hwnd*/, LPNMHDR nmhdr, bool* handled)
{
    if (!s_hDepList) { *handled = false; return 0; }

    if (nmhdr->hwndFrom != s_hDepList) { *handled = false; return 0; }

    if (nmhdr->code == LVN_ITEMCHANGED) {
        int sel = ListView_GetSelectedCount(s_hDepList);
        EnableWindow(s_hDepEdit,   sel > 0 ? TRUE : FALSE);
        EnableWindow(s_hDepRemove, sel > 0 ? TRUE : FALSE);
        *handled = true;
        return 0;
    }

    if (nmhdr->code == NM_DBLCLK) {
        // Double-click: open edit dialog for the selected item.
        LPNMITEMACTIVATE nia = (LPNMITEMACTIVATE)nmhdr;
        if (nia->iItem >= 0) {
            // Delegate to the Edit button handler in the main window.
            HWND hMainWnd = GetParent(s_hDepList);
            SendMessageW(hMainWnd, WM_COMMAND, MAKEWPARAM(IDC_DEP_EDIT, BN_CLICKED), 0);
        }
        *handled = true;
        return 0;
    }

    *handled = false;
    return 0;
}

// ── DEP_OnCommand ─────────────────────────────────────────────────────────────

bool DEP_OnCommand(HWND hwnd, int id, int event, HWND /*hCtrl*/)
{
    if (!s_hDepList) return false;

    if (id == IDC_DEP_ADD && event == BN_CLICKED) {
        ExternalDep blank;
        if (s_pLocale && DEP_EditDialog(hwnd, s_hInst, *s_pLocale, blank)) {
            blank.id = s_nextDepId++;
            s_deps.push_back(blank);
            RefreshList();
            // Select the newly added item.
            int last = (int)s_deps.size() - 1;
            ListView_SetItemState(s_hDepList, last,
                LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
            ListView_EnsureVisible(s_hDepList, last, FALSE);
            MainWindow::MarkAsModified();
        }
        return true;
    }

    if (id == IDC_DEP_EDIT && event == BN_CLICKED) {
        ExternalDep* dep = SelectedDep();
        if (!dep) return true;
        ExternalDep copy = *dep;
        if (s_pLocale && DEP_EditDialog(hwnd, s_hInst, *s_pLocale, copy)) {
            *dep = copy;
            RefreshList();
            MainWindow::MarkAsModified();
        }
        return true;
    }

    if (id == IDC_DEP_REMOVE && event == BN_CLICKED) {
        int sel = ListView_GetNextItem(s_hDepList, -1, LVNI_SELECTED);
        if (sel < 0) return true;
        LVITEMW lvi = {}; lvi.mask = LVIF_PARAM; lvi.iItem = sel;
        ListView_GetItem(s_hDepList, &lvi);
        int removeId = (int)lvi.lParam;

        // Confirm removal.
        std::wstring depName;
        if (ExternalDep* d = FindDepById(removeId)) depName = d->display_name;
        std::wstring msg = L10n(L"dep_confirm_remove", L"Remove this dependency?");
        if (!depName.empty()) {
            std::wstring fmt = L10n(L"dep_confirm_remove_named", L"");
            if (!fmt.empty()) {
                // Replace {0} with the name.
                size_t pos = fmt.find(L"{0}");
                if (pos != std::wstring::npos) {
                    fmt.replace(pos, 3, depName);
                    msg = fmt;
                } else {
                    msg = L"Remove '" + depName + L"'?";
                }
            } else {
                msg = L"Remove '" + depName + L"'?";
            }
        }

        std::wstring title = L10n(L"confirm_remove_title", L"Confirm Remove");
        if (MessageBoxW(hwnd, msg.c_str(), title.c_str(), MB_YESNO | MB_ICONWARNING) != IDYES)
            return true;

        s_deps.erase(std::remove_if(s_deps.begin(), s_deps.end(),
            [removeId](const ExternalDep& d){ return d.id == removeId; }), s_deps.end());
        RefreshList();
        // Update button states.
        EnableWindow(s_hDepEdit,   FALSE);
        EnableWindow(s_hDepRemove, FALSE);
        MainWindow::MarkAsModified();
        return true;
    }

    return false;
}

// ── DEP_SaveToDb ──────────────────────────────────────────────────────────────

void DEP_SaveToDb(int projectId)
{
    DB::DeleteExternalDepsForProject(projectId);
    for (ExternalDep& dep : s_deps) {
        int newId = DB::InsertExternalDep(projectId, dep);
        dep.id = newId;
    }
}

// ── DEP_LoadFromDb ────────────────────────────────────────────────────────────

void DEP_LoadFromDb(int projectId)
{
    s_deps = DB::GetExternalDepsForProject(projectId);
    s_nextDepId = 1;
    for (const ExternalDep& d : s_deps)
        if (d.id >= s_nextDepId) s_nextDepId = d.id + 1;
    RefreshList();
}

// ── DEP_RepositionScrollbars ──────────────────────────────────────────────────

void DEP_RepositionScrollbars()
{
    if (s_hMsbDepListV) msb_reposition(s_hMsbDepListV);
    if (s_hMsbDepListH) msb_reposition(s_hMsbDepListH);
}
