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
#include "my_scrollbar.h"
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
    // Re-suppress native bars re-enabled by ListView_InsertItem.
    if (s_hMsbDepListV) { ShowScrollBar(s_hDepList, SB_VERT, FALSE); msb_sync(s_hMsbDepListV); }
    if (s_hMsbDepListH) { ShowScrollBar(s_hDepList, SB_HORZ, FALSE); msb_sync(s_hMsbDepListH); }

    // ── TEST DATA: overflow V+H ── remove before release ─────────────────────
    // Appended once per session so the list overflows both V and H regardless
    // of the project's real content.  Covers all delivery types, both
    // required/optional, and all three detection strategies so every column
    // path is exercised.  IDs start at 9000 to avoid DB collisions.
    // Delete this block (and s_testDataAdded) before shipping.
    {
        static bool s_testDataAdded = false;
        if (!s_testDataAdded) {
            s_testDataAdded = true;
            auto add = [&](int id, const wchar_t* name, DepDelivery del,
                           bool req, const wchar_t* regKey,
                           const wchar_t* filePath) {
                ExternalDep d;
                d.id               = id;
                d.display_name     = name;
                d.delivery         = del;
                d.is_required      = req;
                d.detect_reg_key   = regKey  ? regKey  : L"";
                d.detect_file_path = filePath ? filePath : L"";
                s_deps.push_back(d);
            };
            // Rows: mix all four delivery types, both required/optional,
            // all three detection kinds (reg, file, none).
            add(9000, L"Microsoft Visual C++ 2022 Redistributable (x64) — Required Runtime",
                DD_AUTO_DOWNLOAD,    true,
                L"HKLM\\SOFTWARE\\Microsoft\\VisualStudio\\14.0\\VC\\Runtimes\\X64", nullptr);
            add(9001, L".NET 8.0 Desktop Runtime — Windows Presentation Foundation",
                DD_AUTO_DOWNLOAD,    true,
                L"HKLM\\SOFTWARE\\dotnet\\Setup\\InstalledVersions\\x64\\sharedhost", nullptr);
            add(9002, L"DirectX End-User Runtime Web Installer (June 2010)",
                DD_REDIRECT_URL,     false,
                nullptr, L"%SystemRoot%\\System32\\d3dx9_43.dll");
            add(9003, L"WebView2 Runtime — Evergreen Bootstrapper from Microsoft CDN",
                DD_AUTO_DOWNLOAD,    true,
                L"HKLM\\SOFTWARE\\WOW6432Node\\Microsoft\\EdgeUpdate\\Clients\\{F3017226-FE2A-4295-8BDF-00C3A9A7E4C5}", nullptr);
            add(9004, L"OpenSSL 3.x Light — TLS 1.3 Support Library (bundled, no install)",
                DD_BUNDLED,          true,
                nullptr, L"%ProgramFiles%\\MyApp\\bin\\libssl-3-x64.dll");
            add(9005, L"SQLite 3.45 Amalgamation (bundled as static lib, no detection needed)",
                DD_BUNDLED,          true,
                nullptr, nullptr);
            add(9006, L"WinPcap / Npcap Network Capture Driver — Optional Packet Analyser Feature",
                DD_REDIRECT_URL,     false,
                L"HKLM\\SYSTEM\\CurrentControlSet\\Services\\npcap", nullptr);
            add(9007, L"7-Zip Command-Line Tool — Required for Archive Extraction Module",
                DD_INSTRUCTIONS_ONLY, false,
                nullptr, L"%ProgramFiles%\\7-Zip\\7z.exe");
            add(9008, L"Python 3.12 Embeddable Package (x64) — Scripting Engine",
                DD_AUTO_DOWNLOAD,    false,
                L"HKLM\\SOFTWARE\\Python\\PythonCore\\3.12\\InstallPath", nullptr);
            add(9009, L"Git for Windows — Required by the Automatic Update Sub-System",
                DD_REDIRECT_URL,     true,
                nullptr, L"%ProgramFiles%\\Git\\bin\\git.exe");
            add(9010, L"MSVC Runtime 14.38 — Extremely Long Name to Force Horizontal Column Overflow Without Any Doubt Whatsoever",
                DD_AUTO_DOWNLOAD,    true,
                L"HKLM\\SOFTWARE\\Microsoft\\VisualStudio\\14.0\\VC\\Runtimes\\ARM64", nullptr);
            add(9011, L"Node.js 20 LTS — Server-Side Scripting for Dashboard Extension (Optional)",
                DD_INSTRUCTIONS_ONLY, false,
                nullptr, L"%ProgramFiles%\\nodejs\\node.exe");
            add(9012, L"libcurl 8.6 with WinSSL Backend (bundled DLL, silent, no user prompt)",
                DD_BUNDLED,          true,
                nullptr, nullptr);
            add(9013, L"Ghostscript 10.03 PostScript Interpreter — PDF Export Feature Only",
                DD_REDIRECT_URL,     false,
                nullptr, L"%ProgramFiles%\\gs\\gs10.03.0\\bin\\gswin64c.exe");
            add(9014, L"Universal CRT (KB2999226) — Windows 8.1 and Earlier Only",
                DD_AUTO_DOWNLOAD,    true,
                L"HKLM\\SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Component Based Servicing\\Packages\\Package_for_KB2999226", nullptr);
            add(9015, L"OpenJDK 21 LTS — Required by the Report Generator Module",
                DD_AUTO_DOWNLOAD,    false,
                L"HKLM\\SOFTWARE\\Eclipse Adoptium\\JDK\\21.0\\hotspot\\MSI", nullptr);
            add(9016, L"Bonjour for Windows — mDNS Service Discovery (Optional LAN Feature)",
                DD_BUNDLED,          false,
                nullptr, L"%ProgramFiles%\\Bonjour\\mDNSResponder.exe");
            add(9017, L"Microsoft Edge WebView2 Runtime Evergreen — Fallback for Older Systems",
                DD_REDIRECT_URL,     false,
                nullptr, nullptr);
            add(9018, L"Redis 7.2 for Windows — Optional Caching Layer for Enterprise Installations",
                DD_INSTRUCTIONS_ONLY, false,
                nullptr, L"%ProgramFiles%\\Redis\\redis-server.exe");
            add(9019, L"Inno Setup 6.3 — Required Only if the Developer Wants to Repackage Manually",
                DD_REDIRECT_URL,     false,
                nullptr, L"%ProgramFiles(x86)%\\Inno Setup 6\\ISCC.exe");
            add(9020, L"WinSparkle 0.8 — Automatic Update Framework (bundled, MIT-licensed)",
                DD_BUNDLED,          true,
                nullptr, nullptr);
            RefreshList();
            // Re-suppress native bars re-enabled by ListView_InsertItem.
            if (s_hMsbDepListV) { ShowScrollBar(s_hDepList, SB_VERT, FALSE); msb_sync(s_hMsbDepListV); }
            if (s_hMsbDepListH) { ShowScrollBar(s_hDepList, SB_HORZ, FALSE); msb_sync(s_hMsbDepListH); }
        }
    }
    // ── END TEST DATA ─────────────────────────────────────────────────────────
}

// ── DEP_OnNotify ──────────────────────────────────────────────────────────────

LRESULT DEP_OnNotify(HWND /*hwnd*/, LPNMHDR nmhdr, bool* handled)
{
    if (!s_hDepList) { *handled = false; return 0; }

    // HDN_ENDTRACK fires (from the header child) when the user finishes resizing
    // a column.  The ListView may have scrolled the content to keep columns
    // visible; sync the H-bar's lvHPos so the next scroll delta is correct.
    if (nmhdr->hwndFrom == ListView_GetHeader(s_hDepList) &&
        (nmhdr->code == HDN_ENDTRACKW || nmhdr->code == HDN_ENDTRACKA)) {
        if (s_hMsbDepListH) msb_reposition(s_hMsbDepListH);
        *handled = false;   // let default processing continue
        return 0;
    }

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
            if (s_hMsbDepListV) { ShowScrollBar(s_hDepList, SB_VERT, FALSE); msb_sync(s_hMsbDepListV); }
            if (s_hMsbDepListH) { ShowScrollBar(s_hDepList, SB_HORZ, FALSE); msb_sync(s_hMsbDepListH); }
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
            if (s_hMsbDepListV) { ShowScrollBar(s_hDepList, SB_VERT, FALSE); msb_sync(s_hMsbDepListV); }
            if (s_hMsbDepListH) { ShowScrollBar(s_hDepList, SB_HORZ, FALSE); msb_sync(s_hMsbDepListH); }
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
        if (s_hMsbDepListV) { ShowScrollBar(s_hDepList, SB_VERT, FALSE); msb_sync(s_hMsbDepListV); }
        if (s_hMsbDepListH) { ShowScrollBar(s_hDepList, SB_HORZ, FALSE); msb_sync(s_hMsbDepListH); }
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
    if (s_hMsbDepListV) { ShowScrollBar(s_hDepList, SB_VERT, FALSE); msb_sync(s_hMsbDepListV); }
    if (s_hMsbDepListH) {
        SCROLLINFO siH = {sizeof(siH), SIF_ALL};
        GetScrollInfo(s_hDepList, SB_HORZ, &siH);
        ShowScrollBar(s_hDepList, SB_HORZ, FALSE);
        if (siH.nMax > siH.nMin) SetScrollInfo(s_hDepList, SB_HORZ, &siH, FALSE);
        msb_sync(s_hMsbDepListH);
    }
}

// ── DEP_RepositionScrollbars ──────────────────────────────────────────────────

void DEP_RepositionScrollbars()
{
    if (s_hMsbDepListV) msb_reposition(s_hMsbDepListV);
    if (s_hMsbDepListH) msb_reposition(s_hMsbDepListH);
}
