#include "mainwindow.h"
#include "dpi.h"
#include <commctrl.h>
#include <shlwapi.h>
#include <shlobj.h>
#include <shobjidl.h>
#include <shellapi.h>
#include <windowsx.h>
#include <functional>
#include <vector>
#include <set>
#include <unordered_set>
#include <algorithm>
#include "ctrlw.h"
#include "button.h"
#include "spinner_dialog.h"
#include "about.h"
#include "tooltip.h"
#include "about_icon.h"
#include "dragdrop.h"
#include "checkbox.h"
#include "notes_editor.h"
#include "shortcuts.h"
#include "deps.h"
#include "dialogs.h"
#include <richedit.h>
#include <commdlg.h>
#include <fstream>

// (no extra declarations needed — ExtractIconExW is in shellapi.h)

// Static member initialization
ProjectRow MainWindow::s_currentProject = {};
std::map<std::wstring, std::wstring> MainWindow::s_locale;
HWND MainWindow::s_hTab = NULL;
HWND MainWindow::s_hStatus = NULL;
HWND MainWindow::s_hCurrentTabContent = NULL;
HWND MainWindow::s_hCurrentPage = NULL;
HWND MainWindow::s_hTooltip = NULL;
HWND MainWindow::s_hPageButton1 = NULL;
HWND MainWindow::s_hPageButton2 = NULL;
HWND MainWindow::s_hTreeView = NULL;
HWND MainWindow::s_hListView = NULL;
HTREEITEM MainWindow::s_hProgramFilesRoot = NULL;
HTREEITEM MainWindow::s_hProgramDataRoot = NULL;
HTREEITEM MainWindow::s_hAppDataRoot = NULL;
HTREEITEM MainWindow::s_hAskAtInstallRoot = NULL;
bool MainWindow::s_askAtInstallEnabled = false;
int MainWindow::s_toolbarHeight = 50;
int MainWindow::s_currentPageIndex = 0;
static int  s_scPageContentH   = 0;  // absolute Y of first pixel below Shortcuts page content
static int  s_idlgPageContentH = 0;  // absolute Y of first pixel below Dialogs page content
static bool s_hasUnsavedChanges = false;
static bool s_isNewUnsavedProject = false;
static HTREEITEM s_rightClickedItem = NULL; // Track which TreeView item was right-clicked
static int s_rightClickedRegIndex = -1; // Track which ListView item was right-clicked
static bool s_projectNameManuallySet = false; // Track if user manually edited project name
static bool s_updatingProjectNameProgrammatically = false; // Prevent EN_CHANGE during programmatic updates
static bool s_installPathUserEdited = false; // Once user manually picks a path, stop auto-updating it
static std::wstring s_currentInstallPath;    // Persists the install path across page switches
static HWND s_hAboutButton = NULL; // Track About button for tooltip
static bool s_aboutMouseTracking = false; // Track mouse for About button tooltip
static HFONT s_hGuiFont = NULL; // Scaled GUI font for labels/edits/checkboxes
// Mirror entry-screen tooltip tracking state
static HWND s_currentTooltipIcon = NULL; // Which icon currently has the tooltip shown
static bool s_mouseTracking = false; // General mouse tracking flag for tooltip
static HTREEITEM s_lastHoveredTreeItem = NULL;
static WNDPROC s_prevTreeProc = NULL;
static WNDPROC s_prevWarnIconProc = NULL;
static WNDPROC s_prevCompInfoIconProc = NULL;
static bool    s_compInfoTooltipTracking = false;
static HFONT s_scaledFont = NULL;     // Scaled default GUI font for labels/edits/checkboxes
static HFONT s_hPageTitleFont = NULL; // Larger bold system font used for all page headlines (i18n-safe, NONCLIENTMETRICS-based)
// Forward declarations for functions defined later
static LRESULT CALLBACK TreeView_SubclassProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
static std::vector<std::wstring> GetAvailableLocales();
static LRESULT CALLBACK WarningIcon_SubclassProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
// Timer callback that polls cursor position every 60ms to hide the tooltip on
// the disabled Components button the moment the cursor leaves its rect.
static void CALLBACK CompTT_TimerCallback(HWND hwnd, UINT, UINT_PTR id, DWORD);
// Same pattern for both disabled pin-icon buttons in the Shortcuts page.
static void CALLBACK ScPinStartTT_TimerCallback(HWND hwnd, UINT, UINT_PTR id, DWORD);
static void CALLBACK ScPinTbTT_TimerCallback(HWND hwnd, UINT, UINT_PTR id, DWORD);

// Returns the full path to Program Files as reported by Windows (localized, e.g. "C:\Programfiler").
// This handles all Windows languages automatically.
static std::wstring GetProgramFilesPath() {
    wchar_t path[MAX_PATH] = {};
    if (SHGetSpecialFolderPathW(NULL, path, CSIDL_PROGRAM_FILES, FALSE))
        return std::wstring(path);
    return L"C:\\Program Files"; // safe fallback
}

// Returns just the folder name component (e.g. "Programfiler" on Norwegian Windows).
static std::wstring GetProgramFilesFolderName() {
    std::wstring full = GetProgramFilesPath();
    size_t pos = full.find_last_of(L"\\/");
    return (pos != std::wstring::npos) ? full.substr(pos + 1) : full;
}

// Per-node virtual-file map (keyed by live HTREEITEM; rebuilt from snapshot on restore)
static std::map<HTREEITEM, std::vector<VirtualFolderFile>> s_virtualFolderFiles;

// Drag-and-drop is handled by the dragdrop module (dragdrop.h/.cpp).

// Recursive snapshot of the Files-page tree (persists across page switches)
static std::vector<TreeNodeSnapshot> s_treeSnapshot_ProgramFiles;
static std::vector<TreeNodeSnapshot> s_treeSnapshot_ProgramData;
static std::vector<TreeNodeSnapshot> s_treeSnapshot_AppData;
static std::vector<TreeNodeSnapshot> s_treeSnapshot_AskAtInstall;

// Registry entry structure
struct RegistryEntry {
    std::wstring hive;     // HKLM, HKCU, etc.
    std::wstring path;     // Registry path
    std::wstring name;     // Value name
    std::wstring type;     // REG_SZ, REG_DWORD, etc.
    std::wstring data;     // Value data
};
static std::map<HTREEITEM, std::vector<RegistryEntry>> s_registryValues;

// Registry page state
static bool s_registerInWindows = true;
static std::wstring s_appIconPath;
static std::wstring s_appPublisher;
static HWND s_hRegTreeView = NULL;
static HWND s_hRegListView = NULL;
static HWND s_hRegKeyDialog = NULL;
static bool s_navigateToRegKey = false;
static bool s_warningTooltipTracking = false;
static bool s_compDisabledTooltipTracking = false;
#define IDT_COMP_TT          1001  // timer id for comp-disabled tooltip hover check
static bool s_scPinStartTtTracking = false;
static bool s_scPinTbTtTracking   = false;
#define IDT_SC_PIN_START_TT  1002  // timer id for pin-to-start disabled tooltip
#define IDT_SC_PIN_TB_TT     1003  // timer id for pin-to-taskbar disabled tooltip

// Timer callback: polls every 60ms while tooltip is up and hides it the moment
// the cursor is no longer over the disabled Components button.
// Using the callback form of SetTimer (not WM_TIMER dispatch) so it fires
// promptly regardless of message-queue load.
static void CALLBACK CompTT_TimerCallback(HWND hwnd, UINT, UINT_PTR id, DWORD) {
    HWND hBtn = GetDlgItem(hwnd, 5081); // IDC_TB_COMPONENTS — defined below the includes
    bool over = false;
    if (hBtn && !IsWindowEnabled(hBtn)) {
        POINT pt; GetCursorPos(&pt);
        RECT rc;  GetWindowRect(hBtn, &rc);
        over = (PtInRect(&rc, pt) != FALSE);
    }
    if (!over) {
        HideTooltip();
        KillTimer(hwnd, id);
        s_compDisabledTooltipTracking = false;
    }
}
static void CALLBACK ScPinStartTT_TimerCallback(HWND hwnd, UINT, UINT_PTR id, DWORD) {
    HWND hBtn = GetDlgItem(hwnd, IDC_SC_PINSTART_BTN);
    bool over = false;
    if (hBtn && !IsWindowEnabled(hBtn)) {
        POINT pt; GetCursorPos(&pt);
        RECT rc;  GetWindowRect(hBtn, &rc);
        over = (PtInRect(&rc, pt) != FALSE);
    }
    if (!over) {
        HideTooltip();
        KillTimer(hwnd, id);
        s_scPinStartTtTracking = false;
    }
}
static void CALLBACK ScPinTbTT_TimerCallback(HWND hwnd, UINT, UINT_PTR id, DWORD) {
    HWND hBtn = GetDlgItem(hwnd, IDC_SC_PINTASKBAR_BTN);
    bool over = false;
    if (hBtn && !IsWindowEnabled(hBtn)) {
        POINT pt; GetCursorPos(&pt);
        RECT rc;  GetWindowRect(hBtn, &rc);
        over = (PtInRect(&rc, pt) != FALSE);
    }
    if (!over) {
        HideTooltip();
        KillTimer(hwnd, id);
        s_scPinTbTtTracking = false;
    }
}
// Components page state
static HWND s_hCompListView = NULL;
static HWND s_hCompTreeView = NULL;
static std::vector<ComponentRow> s_components;
static bool s_filesPageHasContent = false; // tracks whether Files page has any files/folders

// Command and control IDs
#define IDM_EDIT_REDO       4012
#define IDM_BUILD_COMPILE   4021
#define IDM_BUILD_TEST      4022
#define IDM_HELP_ABOUT      4031

// File/menu command IDs
#define IDM_FILE_NEW        4001
#define IDM_FILE_OPEN       4002
#define IDM_FILE_SAVE       4003
#define IDM_FILE_SAVEAS     4004
#define IDM_FILE_CLOSE      4005
#define IDM_FILE_EXIT       4006

// Edit commands
#define IDM_EDIT_UNDO       4011

#define IDC_TAB_CONTROL     5001
#define IDC_STATUS_BAR      5002

// Toolbar button IDs
#define IDC_TB_FILES        5010
#define IDC_TB_ADD_REGISTRY 5011
#define IDC_TB_ADD_SHORTCUT 5012
#define IDC_TB_ADD_DEPEND   5013
#define IDC_TB_SETTINGS     5014
#define IDC_TB_BUILD        5015
#define IDC_TB_TEST         5016
#define IDC_TB_SCRIPTS      5017
#define IDC_TB_SAVE         5018
#define IDC_TB_ABOUT        5019
#define IDC_TB_DIALOGS      5080
#define IDC_TB_COMPONENTS   5081
#define IDC_TB_EXIT         5082
#define IDC_TB_CLOSE_PROJECT 5083

// Components page control IDs
#define IDC_COMP_ENABLE       5060
#define IDC_COMP_LISTVIEW     5061
#define IDC_COMP_ADD          5062
#define IDC_COMP_TREEVIEW     5063
#define IDC_COMP_EDIT         5064
#define IDC_COMP_REMOVE       5065
#define IDC_COMP_INFO_ICON    5066

// Components edit dialog control IDs (scoped to CompEditDlg window)
#define IDC_COMPDLG_NAME     301
#define IDC_COMPDLG_DESC     302
#define IDC_COMPDLG_REQUIRED 303
#define IDC_COMPDLG_SRC      304
#define IDC_COMPDLG_BROWSE   305
#define IDC_COMPDLG_DST      306
#define IDC_COMPDLG_OK       307
#define IDC_COMPDLG_CANCEL   308
#define IDC_COMPDLG_DEPS     309   // multi-select listbox for dependencies

// VFS Picker dialog control IDs (scoped to VFSPickerDlg window)
#define IDC_VFSPICKER_TREE   6100
#define IDC_VFSPICKER_LIST   6101
#define IDC_VFSPICKER_OK     6102
#define IDC_VFSPICKER_CANCEL 6103

// Components context menu
#define IDM_COMP_CTX_EDIT       6200
#define IDM_COMP_CTX_REMOVE     6201
#define IDM_COMP_TREE_CTX_EDIT  6202
#define IDC_FOLDER_DLG_REQUIRED    320
#define IDC_FOLDER_DLG_DEPS_LIST   321   // read-only listbox showing selected dep names
#define IDC_FOLDER_DLG_CHOOSE_DEPS 322   // "Choose..." button
// Folder dependency picker dialog (CompFolderDepPicker)
#define IDC_FDDP_TREE              323
#define IDC_FDDP_OK                324
#define IDC_FDDP_CANCEL            325
#define IDC_FOLDER_DLG_PRESELECTED 326  // Pre-selected checkbox (locked when Required is checked)
#define IDC_FOLDER_DLG_REMOVE_DEPS  327  // "Remove" button in fold-edit dep list
#define IDM_DEPS_CTX_REMOVE         6210 // dep-list context menu: Remove
#define IDM_DEPS_CTX_SHOWFILES      6211 // dep-list context menu: Show files…

// Notes button inside folder edit and component edit dialogs
#define IDC_FOLDER_DLG_NOTES       340
#define IDC_COMPDLG_NOTES          341

// Files dialog button IDs
#define IDC_FILES_ADD_DIR   5020
#define IDC_FILES_ADD_FILES 5021
#define IDC_FILES_DLG       5022
#define IDC_BROWSE_INSTALL_DIR 5023
#define IDC_FILES_REMOVE    5024
#define IDC_PROJECT_NAME    5025
#define IDC_INSTALL_FOLDER  5026
// Ask-at-install checkbox
#define IDC_ASK_AT_INSTALL  5027

// Context menu IDs
#define IDM_TREEVIEW_ADD_FOLDER 5030
#define IDM_TREEVIEW_REMOVE_FOLDER 5031
#define IDM_TREEVIEW_RENAME 5059

// Registry context menu IDs
#define IDM_REG_ADD_KEY         5032
#define IDM_REG_ADD_VALUE       5033
#define IDM_REG_EDIT_VALUE      5034
#define IDM_REG_DELETE_VALUE    5035
#define IDM_REG_DELETE_KEY      5036
#define IDM_REG_EDIT_KEY        5037

// Registry page control IDs
#define IDC_REG_CHECKBOX    5040
#define IDC_REG_ICON_PREVIEW 5041
#define IDC_REG_ADD_ICON    5042
#define IDC_REG_DISPLAY_NAME 5043
#define IDC_REG_VERSION     5044
#define IDC_REG_PUBLISHER   5045
#define IDC_REG_TREEVIEW    5046
#define IDC_REG_LISTVIEW    5047
#define IDC_REG_ADD_KEY     5048
#define IDC_REG_ADD_VALUE   5049
#define IDC_REG_REMOVE      5050
#define IDC_REG_SHOW_REGKEY 5051
#define IDC_REG_EDIT        5056
#define IDC_REG_WARNING_ICON 5057
#define IDC_REG_BACKUP      5058
#define IDC_REGKEY_DLG_CLOSE 5052
#define IDC_REGKEY_DLG_NAVIGATE 5053
#define IDC_REGKEY_DLG_COPY 5054
#define IDC_REGKEY_DLG_EDIT 5055

// Add Value dialog IDs
#define IDC_ADDVAL_NAME     5060
#define IDC_ADDVAL_TYPE     5061
#define IDC_ADDVAL_DATA     5062
#define IDC_ADDVAL_OK       5063
#define IDC_ADDVAL_CANCEL   5064

// Add Key dialog IDs
#define IDC_ADDKEY_NAME     5070
#define IDC_ADDKEY_OK       5071
#define IDC_ADDKEY_CANCEL   5072

// Subclass proc for the Components-page info icon (shell32 #258).
// Shows a simple single-line tooltip explaining that files/folders only appear
// after the project is saved.
static LRESULT CALLBACK CompInfoIcon_SubclassProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_ERASEBKGND: {
        RECT rc; GetClientRect(hwnd, &rc);
        FillRect((HDC)wParam, &rc, GetSysColorBrush(COLOR_BTNFACE));
        return TRUE;
    }
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);
        RECT rc; GetClientRect(hwnd, &rc);
        FillRect(hdc, &rc, GetSysColorBrush(COLOR_BTNFACE));
        int w = rc.right, h = rc.bottom;
        // Draw the floppy-disk icon (icon handle stored in GWLP_USERDATA)
        HICON hIco = (HICON)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
        if (hIco) DrawIconEx(hdc, 0, 0, hIco, w, h, 0, NULL, DI_NORMAL);
        // Overlay "FYI!" on the white label strip (upper body of the diskette)
        // Label area: ~9-88% wide, 18-54% tall (proportional to icon size)
        RECT labelRc = { (LONG)(w*9/100), (LONG)(h*19/100),
                         (LONG)(w*89/100), (LONG)(h*55/100) };
        int fh = -(labelRc.bottom - labelRc.top - 1); // fill label height
        HFONT hFont = CreateFontW(fh, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_SWISS, L"Arial");
        HFONT hOld = (HFONT)SelectObject(hdc, hFont);
        SetBkMode(hdc, TRANSPARENT);
        SetTextColor(hdc, RGB(20, 20, 120)); // dark navy, readable on light label
        // Label text from locale — key "comp_info_icon_label", fallback "FYI!"
        auto itLbl = MainWindow::GetLocale().find(L"comp_info_icon_label");
        std::wstring lblText = (itLbl != MainWindow::GetLocale().end()) ? itLbl->second : L"FYI!";
        DrawTextW(hdc, lblText.c_str(), -1, &labelRc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        SelectObject(hdc, hOld);
        DeleteObject(hFont);
        EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_MOUSEMOVE: {
        if (!IsTooltipVisible()) {
            auto it = MainWindow::GetLocale().find(L"comp_info_tooltip");
            std::wstring text = (it != MainWindow::GetLocale().end()) ? it->second
                : L"Files and folders will not appear in the dependency picker\nuntil the project has been saved at least once.";
            // Convert escaped newlines into real CR/LF
            size_t p = 0;
            while ((p = text.find(L"\\r\\n", p)) != std::wstring::npos) { text.replace(p, 4, L"\r\n"); p += 2; }
            p = 0;
            while ((p = text.find(L"\\n",   p)) != std::wstring::npos) { text.replace(p, 2, L"\r\n"); p += 2; }
            std::vector<std::pair<std::wstring,std::wstring>> entry = {{L"", text}};
            RECT rc; GetWindowRect(hwnd, &rc);
            ShowMultilingualTooltip(entry, rc.left, rc.bottom + 5, GetParent(hwnd));
            s_currentTooltipIcon = hwnd;
        }
        if (!s_compInfoTooltipTracking) {
            TRACKMOUSEEVENT tme = {};
            tme.cbSize = sizeof(tme); tme.dwFlags = TME_LEAVE; tme.hwndTrack = hwnd;
            TrackMouseEvent(&tme);
            s_compInfoTooltipTracking = true;
        }
        break;
    }
    case WM_MOUSELEAVE: {
        HideTooltip();
        s_currentTooltipIcon = NULL;
        s_compInfoTooltipTracking = false;
        break;
    }
    case WM_NCDESTROY:
        SetWindowLongPtrW(hwnd, GWLP_WNDPROC, (LONG_PTR)s_prevCompInfoIconProc);
        break;
    }
    return CallWindowProcW(s_prevCompInfoIconProc, hwnd, msg, wParam, lParam);
}

// Subclass proc for the registry warning icon
static LRESULT CALLBACK WarningIcon_SubclassProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_MOUSEMOVE: {
        if (!IsTooltipVisible()) {
            auto it = MainWindow::GetLocale().find(L"reg_warning_tooltip");
            std::wstring tooltipText = (it != MainWindow::GetLocale().end()) ? it->second :
                L"Editing the registry can change the machine's behaviour and maybe harm it, so edit at your own risk.";

            // Convert escaped newlines ("\\r\\n" and "\\n") into real CRLF for multiline support
            size_t pos = 0;
            while ((pos = tooltipText.find(L"\\r\\n", pos)) != std::wstring::npos) {
                tooltipText.replace(pos, 4, L"\r\n");
                pos += 2;
            }
            pos = 0;
            while ((pos = tooltipText.find(L"\\n", pos)) != std::wstring::npos) {
                tooltipText.replace(pos, 2, L"\r\n");
                pos += 2;
            }

            std::vector<std::pair<std::wstring,std::wstring>> simpleEntry;
            simpleEntry.push_back({L"", tooltipText});

            RECT rcIcon;
            GetWindowRect(hwnd, &rcIcon);
            POINT ptIcon = { rcIcon.left, rcIcon.bottom + 5 };
            ShowMultilingualTooltip(simpleEntry, ptIcon.x, ptIcon.y, GetParent(hwnd));
            s_currentTooltipIcon = hwnd;
        }

        if (!s_warningTooltipTracking) {
            TRACKMOUSEEVENT tme = {0};
            tme.cbSize = sizeof(tme);
            tme.dwFlags = TME_LEAVE;
            tme.hwndTrack = hwnd;
            TrackMouseEvent(&tme);
            s_warningTooltipTracking = true;
        }
        break;
    }
    case WM_MOUSELEAVE: {
        if (IsTooltipVisible()) {
            HideTooltip();
            s_currentTooltipIcon = NULL;
        }
        s_warningTooltipTracking = false;
        break;
    }
    case WM_NCDESTROY: {
        SetWindowLongPtrW(hwnd, GWLP_WNDPROC, (LONG_PTR)s_prevWarnIconProc);
        break;
    }
    }
    return CallWindowProcW(s_prevWarnIconProc, hwnd, msg, wParam, lParam);
}

HWND MainWindow::Create(HINSTANCE hInstance, const ProjectRow &project, const std::map<std::wstring, std::wstring> &locale) {
    // Reset state so opening any project (new or existing) starts clean
    s_hasUnsavedChanges = false;
    s_isNewUnsavedProject = false;
    s_askAtInstallEnabled = false;
    s_treeSnapshot_ProgramFiles.clear();
    s_treeSnapshot_ProgramData.clear();
    s_treeSnapshot_AppData.clear();
    s_treeSnapshot_AskAtInstall.clear();
    s_virtualFolderFiles.clear();
    s_filesPageHasContent = false;
    SC_Reset();
    DEP_Reset();
    IDLG_Reset();
    s_components.clear();   // load once here, never on page switch
    s_currentProject = project;
    s_locale = locale;

    // Seed the installer-title section with the project name as default.
    // IDLG_SetInstallerInfo must be called AFTER IDLG_Reset() and after
    // s_currentProject is assigned so the name is available.
    IDLG_SetInstallerInfo(project.name, L"");

    // Load components for existing projects into memory now.
    // They stay in memory for the lifetime of the project window;
    // DB is only written when the user explicitly saves.
    if (project.id > 0) {
        s_components = DB::GetComponentsForProject(project.id);
        for (auto& comp : s_components)
            if (comp.id > 0)
                comp.dependencies = DB::GetDependenciesForComponent(comp.id);
        SC_LoadFromDb(project.id);   // load shortcuts + menu nodes + opt-out flags
        DEP_LoadFromDb(project.id);   // load external dependencies
        IDLG_LoadFromDb(project.id);  // load installer dialog RTF content
    }

    // Restore ask-at-install preference for this project
    if (project.id > 0) {
        std::wstring val;
        std::wstring key = L"ask_at_install_" + std::to_wstring(project.id);
        if (DB::GetSetting(key, val))
            s_askAtInstallEnabled = (val == L"1");
    }
    
    // Load application icon from resource
    HICON hIcon = LoadIconW(hInstance, MAKEINTRESOURCEW(1));
    HICON hIconSm = (HICON)LoadImageW(hInstance, MAKEINTRESOURCEW(1), IMAGE_ICON, 16, 16, 0);
    
    // Register window class
    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(WNDCLASSEXW);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.lpszClassName = L"SetupCraftMainWindow";
    wc.hIcon = hIcon ? hIcon : LoadIcon(NULL, IDI_APPLICATION);
    wc.hIconSm = hIconSm ? hIconSm : LoadIcon(NULL, IDI_APPLICATION);
    
    if (!RegisterClassExW(&wc)) {
        // Already registered, ignore
    }
    
    // Create window with project name in title (10% smaller vertically: 768 * 0.9 = 691)
    std::wstring title = L"SetupCraft - " + project.name;
    
    // Calculate centered position on screen
    RECT rcWork;
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &rcWork, 0);
    int width = S(1024);
    int height = S(691);
    int x = rcWork.left + (rcWork.right - rcWork.left - width) / 2;
    int y = rcWork.top + (rcWork.bottom - rcWork.top - height) / 2;
    
    // Ensure window is visible
    if (x < rcWork.left) x = rcWork.left;
    if (y < rcWork.top) y = rcWork.top;
    if (x + width > rcWork.right) x = rcWork.right - width;
    if (y + height > rcWork.bottom) y = rcWork.bottom - height;
    
    HWND hwnd = CreateWindowExW(
        0,
        L"SetupCraftMainWindow",
        title.c_str(),
        WS_OVERLAPPEDWINDOW,
        x, y, width, height,
        NULL, NULL, hInstance, NULL
    );
    
    if (hwnd) {
        // Set window icon explicitly for title bar and taskbar
        if (hIcon) SendMessageW(hwnd, WM_SETICON, ICON_BIG, (LPARAM)hIcon);
        if (hIconSm) SendMessageW(hwnd, WM_SETICON, ICON_SMALL, (LPARAM)hIconSm);
        
        ShowWindow(hwnd, SW_SHOW);
        UpdateWindow(hwnd);
    }
    
    return hwnd;
}

HWND MainWindow::CreateNew(HINSTANCE hInstance, const std::map<std::wstring, std::wstring> &locale) {
    // Create a temporary project structure
    ProjectRow tempProject = {};
    tempProject.id = 0;  // 0 = not saved to database
    tempProject.name = L"New Project";
    tempProject.directory = L"";
    tempProject.version = L"1.0.0";
    tempProject.lang = L"en_GB";
    tempProject.description = L"";
    tempProject.created = 0;
    tempProject.last_updated = 0;
    
    s_isNewUnsavedProject = true;
    s_hasUnsavedChanges = false;
    s_projectNameManuallySet = false; // Reset for new project
    s_installPathUserEdited = false;  // Reset for new project
    s_currentInstallPath.clear();     // Reset for new project
    
    return Create(hInstance, tempProject, locale);
}

void MainWindow::MarkAsModified() {
    s_hasUnsavedChanges = true;
    if (s_hStatus && IsWindow(s_hStatus)) InvalidateRect(s_hStatus, NULL, TRUE);
}

// Recomputes whether the Files page has content and enables/disables the Components toolbar button.
// When the Files tree is active we check it directly; when destroyed we rely on the last known state.
void MainWindow::UpdateComponentsButtonState(HWND hwnd) {
    HWND hTree = s_hTreeView;
    if (hTree && IsWindow(hTree)) {
        // Files page is currently shown — recompute from actual tree state
        s_filesPageHasContent = !s_virtualFolderFiles.empty();
        if (!s_filesPageHasContent) {
            HTREEITEM roots[] = { s_hProgramFilesRoot, s_hProgramDataRoot,
                                  s_hAppDataRoot,      s_hAskAtInstallRoot };
            for (HTREEITEM hRoot : roots) {
                if (hRoot && TreeView_GetChild(hTree, hRoot)) {
                    s_filesPageHasContent = true;
                    break;
                }
            }
        }
    }
    // When the tree is not active s_filesPageHasContent retains the value set before
    // the page was switched away, which is correct.
    HWND hBtn = GetDlgItem(hwnd, IDC_TB_COMPONENTS);
    if (hBtn) EnableWindow(hBtn, s_filesPageHasContent ? TRUE : FALSE);
}

void MainWindow::MarkAsSaved() {
    s_hasUnsavedChanges = false;
    s_isNewUnsavedProject = false;
}

bool MainWindow::HasUnsavedChanges() {
    return s_hasUnsavedChanges;
}

bool MainWindow::IsNewUnsavedProject() {
    return s_isNewUnsavedProject;
}

void MainWindow::CreateMenuBar(HWND hwnd) {
    HMENU hMenuBar = CreateMenu();
    
    // File menu
    HMENU hFileMenu = CreatePopupMenu();
    AppendMenuW(hFileMenu, MF_STRING, IDM_FILE_NEW, L"&New Project\tCtrl+N");
    AppendMenuW(hFileMenu, MF_STRING, IDM_FILE_SAVE, L"&Save\tCtrl+S");
    AppendMenuW(hFileMenu, MF_SEPARATOR, 0, NULL);
    AppendMenuW(hFileMenu, MF_STRING, IDM_FILE_CLOSE, L"&Close Project\tCtrl+W");
    AppendMenuW(hFileMenu, MF_STRING, IDM_FILE_EXIT, L"E&xit\tAlt+F4");
    AppendMenuW(hMenuBar, MF_POPUP, (UINT_PTR)hFileMenu, L"&File");
    
    // Edit menu
    HMENU hEditMenu = CreatePopupMenu();
    AppendMenuW(hEditMenu, MF_STRING, IDM_EDIT_UNDO, L"&Undo\tCtrl+Z");
    AppendMenuW(hEditMenu, MF_STRING, IDM_EDIT_REDO, L"&Redo\tCtrl+Y");
    AppendMenuW(hMenuBar, MF_POPUP, (UINT_PTR)hEditMenu, L"&Edit");
    
    // Build menu
    HMENU hBuildMenu = CreatePopupMenu();
    AppendMenuW(hBuildMenu, MF_STRING, IDM_BUILD_COMPILE, L"&Compile Installer\tF7");
    AppendMenuW(hBuildMenu, MF_STRING, IDM_BUILD_TEST, L"&Test Installer\tF5");
    AppendMenuW(hMenuBar, MF_POPUP, (UINT_PTR)hBuildMenu, L"&Build");
    
    // Help menu
    HMENU hHelpMenu = CreatePopupMenu();
    AppendMenuW(hHelpMenu, MF_STRING, IDM_HELP_ABOUT, L"&About SetupCraft...");
    AppendMenuW(hMenuBar, MF_POPUP, (UINT_PTR)hHelpMenu, L"&Help");
    
    SetMenu(hwnd, hMenuBar);
}

void MainWindow::CreateToolbar(HWND hwnd, HINSTANCE hInst) {
    // Two-row layout: row1 = navigation pages, row2 = action pages
    const int btnH   = S(31);  // button height for each row
    const int gap    = S(4);   // gap between buttons
    const int row1Y  = S(5);               // top of row 1
    const int row2Y  = S(5) + btnH + S(4); // top of row 2  (= S(40))

    // Auto-measure toolbar button width from the label text.
    // Mirrors button.cpp: S(10) left margin + S(20) icon + S(15) icon-to-text gap + text + S(8) right margin.
    // Uses a bold variant of the NONCLIENTMETRICS font (matching DrawCustomButton) measured on a screen DC.
    auto MeasureTBWidth = [&](const std::wstring& label) -> int {
        HDC mdc = GetDC(NULL);
        NONCLIENTMETRICSW mncm = {};
        mncm.cbSize = sizeof(mncm);
        SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(mncm), &mncm, 0);
        mncm.lfMessageFont.lfHeight = MulDiv(mncm.lfMessageFont.lfHeight, 120, 100);
        mncm.lfMessageFont.lfWeight = FW_BOLD;
        mncm.lfMessageFont.lfQuality = CLEARTYPE_QUALITY;
        HFONT mf = CreateFontIndirectW(&mncm.lfMessageFont);
        HGDIOBJ mold = SelectObject(mdc, mf);
        SIZE msz = {};
        GetTextExtentPoint32W(mdc, label.c_str(), (int)label.size(), &msz);
        SelectObject(mdc, mold);
        DeleteObject(mf);
        ReleaseDC(NULL, mdc);
        int w = S(10) + S(20) + S(15) + msz.cx + S(8);
        return (w < S(55)) ? S(55) : w;
    };

    // --- ROW 1: navigation pages ---
    int x = S(10);

    auto itFiles = s_locale.find(L"tb_files");
    std::wstring filesText = (itFiles != s_locale.end()) ? itFiles->second : L"Files";
    int wFiles = MeasureTBWidth(filesText);
    CreateCustomButtonWithIcon(hwnd, IDC_TB_FILES, filesText, ButtonColor::Blue,
        L"shell32.dll", 4, x, row1Y, wFiles, btnH, hInst);
    x += wFiles + gap;

    auto itComp = s_locale.find(L"tb_components");
    std::wstring compText = (itComp != s_locale.end()) ? itComp->second : L"Components";
    int wComp = MeasureTBWidth(compText);
    CreateCustomButtonWithIcon(hwnd, IDC_TB_COMPONENTS, compText, ButtonColor::Blue,
        L"shell32.dll", 278, x, row1Y, wComp, btnH, hInst);
    // Disabled until the Files page has at least one file or folder
    EnableWindow(GetDlgItem(hwnd, IDC_TB_COMPONENTS), s_filesPageHasContent ? TRUE : FALSE);
    x += wComp + gap;

    auto itAddReg = s_locale.find(L"tb_add_registry");
    std::wstring addRegText = (itAddReg != s_locale.end()) ? itAddReg->second : L"Registry";
    int wReg = MeasureTBWidth(addRegText);
    CreateCustomButtonWithIcon(hwnd, IDC_TB_ADD_REGISTRY, addRegText, ButtonColor::Blue,
        L"shell32.dll", 166, x, row1Y, wReg, btnH, hInst);
    x += wReg + gap;

    auto itAddShortcut = s_locale.find(L"tb_add_shortcut");
    std::wstring addShortcutText = (itAddShortcut != s_locale.end()) ? itAddShortcut->second : L"Shortcuts";
    int wShortcut = MeasureTBWidth(addShortcutText);
    CreateCustomButtonWithCompositeIcon(hwnd, IDC_TB_ADD_SHORTCUT, addShortcutText, ButtonColor::Blue,
        L"shell32.dll", 257, L"shell32.dll", 29, x, row1Y, wShortcut, btnH, hInst);
    x += wShortcut + gap;

    auto itAddDep = s_locale.find(L"tb_add_dependency");
    std::wstring addDepText = (itAddDep != s_locale.end()) ? itAddDep->second : L"Dependencies";
    int wDep = MeasureTBWidth(addDepText);
    CreateCustomButtonWithIcon(hwnd, IDC_TB_ADD_DEPEND, addDepText, ButtonColor::Blue,
        L"shell32.dll", 278, x, row1Y, wDep, btnH, hInst);
    x += wDep + gap;

    auto itDialogs = s_locale.find(L"tb_dialogs");
    std::wstring dialogsText = (itDialogs != s_locale.end()) ? itDialogs->second : L"Dialogs";
    int wDialogs = MeasureTBWidth(dialogsText);
    CreateCustomButtonWithIcon(hwnd, IDC_TB_DIALOGS, dialogsText, ButtonColor::Blue,
        L"shell32.dll", 23, x, row1Y, wDialogs, btnH, hInst);
    int row1EndX = x + wDialogs;

    // --- ROW 2: action pages ---
    x = S(10);

    auto itSettings = s_locale.find(L"tb_settings");
    std::wstring settingsText = (itSettings != s_locale.end()) ? itSettings->second : L"Settings";
    int wSettings = MeasureTBWidth(settingsText);
    CreateCustomButtonWithIcon(hwnd, IDC_TB_SETTINGS, settingsText, ButtonColor::Blue,
        L"shell32.dll", 314, x, row2Y, wSettings, btnH, hInst);
    x += wSettings + gap;

    auto itScripts = s_locale.find(L"tb_scripts");
    std::wstring scriptsText = (itScripts != s_locale.end()) ? itScripts->second : L"Scripts";
    int wScripts = MeasureTBWidth(scriptsText);
    CreateCustomButtonWithIcon(hwnd, IDC_TB_SCRIPTS, scriptsText, ButtonColor::Blue,
        L"shell32.dll", 310, x, row2Y, wScripts, btnH, hInst);
    x += wScripts + gap;

    auto itTest = s_locale.find(L"tb_test");
    std::wstring testText = (itTest != s_locale.end()) ? itTest->second : L"Test (F5)";
    int wTest = MeasureTBWidth(testText);
    CreateCustomButtonWithIcon(hwnd, IDC_TB_TEST, testText, ButtonColor::Blue,
        L"shell32.dll", 138, x, row2Y, wTest, btnH, hInst);
    x += wTest + gap;

    auto itBuild = s_locale.find(L"tb_build");
    std::wstring buildText = (itBuild != s_locale.end()) ? itBuild->second : L"Build (F7)";
    int wBuild = MeasureTBWidth(buildText);
    CreateCustomButtonWithIcon(hwnd, IDC_TB_BUILD, buildText, ButtonColor::Green,
        L"shell32.dll", 80, x, row2Y, wBuild, btnH, hInst);
    x += wBuild + gap;

    auto itSave = s_locale.find(L"tb_save");
    std::wstring saveText = (itSave != s_locale.end()) ? itSave->second : L"Save";
    int wSave = MeasureTBWidth(saveText);
    CreateCustomButtonWithIcon(hwnd, IDC_TB_SAVE, saveText, ButtonColor::Green,
        L"shell32.dll", 258, x, row2Y, wSave, btnH, hInst);
    x += wSave + gap;

    auto itCloseProj = s_locale.find(L"close_project");
    std::wstring closeProjText = (itCloseProj != s_locale.end()) ? itCloseProj->second : L"Close Project";
    int wCloseProj = MeasureTBWidth(closeProjText);
    HWND hCloseProjBtn = CreateCustomButtonWithIcon(hwnd, IDC_TB_CLOSE_PROJECT, closeProjText, ButtonColor::Red,
        L"shell32.dll", 131, x, row2Y, wCloseProj, btnH, hInst);
    SetButtonTooltip(hCloseProjBtn, L"Close this project and return to the start screen");
    x += wCloseProj + gap;

    auto itExitTb = s_locale.find(L"exit");
    std::wstring exitTbText = (itExitTb != s_locale.end()) ? itExitTb->second : L"Exit";
    int wExit = MeasureTBWidth(exitTbText);
    CreateCustomButtonWithIcon(hwnd, IDC_TB_EXIT, exitTbText, ButtonColor::Red,
        L"shell32.dll", 27, x, row2Y, wExit, btnH, hInst);
    int row2EndX = x + wExit;

    // About icon — flush right, centered vertically across both rows
    const int aboutIconSize = S(36);
    RECT rcToolbar;
    GetClientRect(hwnd, &rcToolbar);
    const int aboutIconX = rcToolbar.right - aboutIconSize - S(10);
    const int aboutIconY = (s_toolbarHeight - aboutIconSize) / 2;
    s_hAboutButton = CreateAboutIconControl(hwnd, hInst, aboutIconX, aboutIconY, aboutIconSize, IDC_TB_ABOUT, s_locale);
}

// Helper function to clean up registry TreeView items (free lParam memory)
static void CleanupRegistryTreeView(HWND hTreeView, HTREEITEM hItem) {
    if (!hItem) return;
    
    // Get item info
    TVITEMW tvItem = {};
    tvItem.mask = TVIF_PARAM | TVIF_HANDLE;
    tvItem.hItem = hItem;
    
    if (TreeView_GetItem(hTreeView, &tvItem)) {
        // Free the lParam if it's a string pointer (not a predefined HKEY)
        if (tvItem.lParam && 
            tvItem.lParam != (LPARAM)HKEY_LOCAL_MACHINE && 
            tvItem.lParam != (LPARAM)HKEY_CURRENT_USER) {
            delete (std::wstring*)tvItem.lParam;
        }
    }
    
    // Recursively clean up children
    HTREEITEM hChild = TreeView_GetChild(hTreeView, hItem);
    while (hChild) {
        HTREEITEM hNext = TreeView_GetNextSibling(hTreeView, hChild);
        CleanupRegistryTreeView(hTreeView, hChild);
        hChild = hNext;
    }
}

// Helper function to populate registry TreeView with actual registry keys
// Create template registry structure showing installer-added entries only
static void CreateTemplateRegistryTree(HWND hTreeView, const std::wstring& projectName, const std::wstring& publisher, 
                                       const std::wstring& version, const std::wstring& directory, const std::wstring& iconPath) {
    // Helper to insert tree items (expanded by default)
    auto InsertTreeItem = [&](HTREEITEM hParent, const std::wstring& text, const std::wstring* path = nullptr, bool expanded = true) -> HTREEITEM {
        TVINSERTSTRUCTW tvis = {};
        tvis.hParent = hParent;
        tvis.hInsertAfter = TVI_LAST;
        tvis.item.mask = TVIF_TEXT | TVIF_PARAM | TVIF_STATE;
        tvis.item.stateMask = TVIS_EXPANDED;
        tvis.item.state = expanded ? TVIS_EXPANDED : 0;
        tvis.item.pszText = (LPWSTR)text.c_str();
        
        if (path) {
            std::wstring* storedPath = new std::wstring(*path);
            tvis.item.lParam = (LPARAM)storedPath;
        } else {
            tvis.item.lParam = 0;
        }
        
        return TreeView_InsertItem(hTreeView, &tvis);
    };
    
    // Use publisher or placeholder
    std::wstring pub = publisher.empty() ? L"[Publisher]" : publisher;
    std::wstring app = projectName.empty() ? L"[AppName]" : projectName;
    
    // Common template for hive roots
    TVINSERTSTRUCTW tvis = {};
    tvis.hParent = TVI_ROOT;
    tvis.hInsertAfter = TVI_LAST;
    tvis.item.mask = TVIF_TEXT | TVIF_STATE;
    tvis.item.stateMask = TVIS_EXPANDED;
    tvis.item.state = TVIS_EXPANDED;
    
    // ========== HKEY_CLASSES_ROOT ==========
    tvis.item.pszText = (LPWSTR)L"HKEY_CLASSES_ROOT";
    HTREEITEM hHKCR = TreeView_InsertItem(hTreeView, &tvis);
    
    // HKCR\.ext (file extensions)
    std::wstring extPath = L".ext";
    InsertTreeItem(hHKCR, L".ext", &extPath);
    
    // HKCR\CLSID (COM classes)
    std::wstring clsidPath = L"CLSID";
    HTREEITEM hCLSID = InsertTreeItem(hHKCR, L"CLSID", &clsidPath);
    std::wstring clsidGuidPath = L"CLSID\\{GUID}";
    InsertTreeItem(hCLSID, L"{GUID}", &clsidGuidPath);
    
    // ========== HKEY_CURRENT_USER ==========
    tvis.item.pszText = (LPWSTR)L"HKEY_CURRENT_USER";
    HTREEITEM hHKCU = TreeView_InsertItem(hTreeView, &tvis);
    
    // HKCU\SOFTWARE
    std::wstring hkcuSoftwarePath = L"SOFTWARE";
    HTREEITEM hHKCUSoftware = InsertTreeItem(hHKCU, L"SOFTWARE", &hkcuSoftwarePath);
    
    // HKCU\SOFTWARE\[Publisher]
    std::wstring hkcuPublisherPath = L"SOFTWARE\\" + pub;
    InsertTreeItem(hHKCUSoftware, pub, &hkcuPublisherPath);
    
    // HKCU\Environment (user environment variables)
    std::wstring envPath = L"Environment";
    InsertTreeItem(hHKCU, L"Environment", &envPath);
    
    // ========== HKEY_LOCAL_MACHINE ==========
    tvis.item.pszText = (LPWSTR)L"HKEY_LOCAL_MACHINE";
    HTREEITEM hHKLM = TreeView_InsertItem(hTreeView, &tvis);
    
    // HKLM\SOFTWARE
    std::wstring hklmSoftwarePath = L"SOFTWARE";
    HTREEITEM hHKLMSoftware = InsertTreeItem(hHKLM, L"SOFTWARE", &hklmSoftwarePath);
    
    // HKLM\SOFTWARE\[Publisher]
    std::wstring hklmPublisherPath = L"SOFTWARE\\" + pub;
    InsertTreeItem(hHKLMSoftware, pub, &hklmPublisherPath);
    
    // HKLM\SOFTWARE\Microsoft\Windows\CurrentVersion
    std::wstring microsoftPath = L"SOFTWARE\\Microsoft";
    HTREEITEM hMicrosoft = InsertTreeItem(hHKLMSoftware, L"Microsoft", &microsoftPath);
    
    std::wstring windowsPath = L"SOFTWARE\\Microsoft\\Windows";
    HTREEITEM hWindows = InsertTreeItem(hMicrosoft, L"Windows", &windowsPath);
    
    std::wstring currentVersionPath = L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion";
    HTREEITEM hCurrentVersion = InsertTreeItem(hWindows, L"CurrentVersion", &currentVersionPath);
    
    // HKLM\SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\[AppName]
    std::wstring uninstallPath = L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Uninstall";
    HTREEITEM hUninstall = InsertTreeItem(hCurrentVersion, L"Uninstall", &uninstallPath);
    
    std::wstring uninstallAppPath = L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\" + app;
    HTREEITEM hUninstallApp = InsertTreeItem(hUninstall, app, &uninstallAppPath);
    
    // Populate registry values for the uninstall key
    if (hUninstallApp) {
        std::vector<RegistryEntry> values;
        
        // DisplayName
        RegistryEntry displayName;
        displayName.hive = L"HKLM";
        displayName.path = uninstallAppPath;
        displayName.name = L"DisplayName";
        displayName.type = L"REG_SZ";
        displayName.data = projectName;
        values.push_back(displayName);
        
        // DisplayVersion
        RegistryEntry displayVersion;
        displayVersion.hive = L"HKLM";
        displayVersion.path = uninstallAppPath;
        displayVersion.name = L"DisplayVersion";
        displayVersion.type = L"REG_SZ";
        displayVersion.data = version;
        values.push_back(displayVersion);
        
        // Publisher
        if (!publisher.empty()) {
            RegistryEntry publisherEntry;
            publisherEntry.hive = L"HKLM";
            publisherEntry.path = uninstallAppPath;
            publisherEntry.name = L"Publisher";
            publisherEntry.type = L"REG_SZ";
            publisherEntry.data = publisher;
            values.push_back(publisherEntry);
        }
        
        // InstallLocation
        RegistryEntry installLocation;
        installLocation.hive = L"HKLM";
        installLocation.path = uninstallAppPath;
        installLocation.name = L"InstallLocation";
        installLocation.type = L"REG_SZ";
        installLocation.data = directory;
        values.push_back(installLocation);
        
        // DisplayIcon
        if (!iconPath.empty()) {
            RegistryEntry displayIcon;
            displayIcon.hive = L"HKLM";
            displayIcon.path = uninstallAppPath;
            displayIcon.name = L"DisplayIcon";
            displayIcon.type = L"REG_SZ";
            displayIcon.data = iconPath;
            values.push_back(displayIcon);
        }
        
        // UninstallString
        RegistryEntry uninstallString;
        uninstallString.hive = L"HKLM";
        uninstallString.path = uninstallAppPath;
        uninstallString.name = L"UninstallString";
        uninstallString.type = L"REG_SZ";
        uninstallString.data = directory + L"\\uninstall.exe";
        values.push_back(uninstallString);
        
        // Store in map
        s_registryValues[hUninstallApp] = values;
    }
    
    // HKLM\SOFTWARE\Microsoft\Windows\CurrentVersion\Run (startup programs)
    std::wstring runPath = L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Run";
    InsertTreeItem(hCurrentVersion, L"Run", &runPath);
    
    // HKLM\SYSTEM (system configuration)
    std::wstring systemPath = L"SYSTEM";
    HTREEITEM hSystem = InsertTreeItem(hHKLM, L"SYSTEM", &systemPath);
    
    std::wstring currentControlSetPath = L"SYSTEM\\CurrentControlSet";
    HTREEITEM hCCS = InsertTreeItem(hSystem, L"CurrentControlSet", &currentControlSetPath);
    
    std::wstring servicesPath = L"SYSTEM\\CurrentControlSet\\Services";
    InsertTreeItem(hCCS, L"Services", &servicesPath);
    
    // ========== HKEY_USERS ==========
    tvis.item.pszText = (LPWSTR)L"HKEY_USERS";
    HTREEITEM hHKU = TreeView_InsertItem(hTreeView, &tvis);
    
    // HKU\.DEFAULT (default user profile)
    std::wstring defaultUserPath = L".DEFAULT";
    HTREEITEM hDefaultUser = InsertTreeItem(hHKU, L".DEFAULT", &defaultUserPath);
    
    std::wstring defaultSoftwarePath = L".DEFAULT\\SOFTWARE";
    InsertTreeItem(hDefaultUser, L"SOFTWARE", &defaultSoftwarePath);
    
    // ========== HKEY_CURRENT_CONFIG ==========
    tvis.item.pszText = (LPWSTR)L"HKEY_CURRENT_CONFIG";
    HTREEITEM hHKCC = TreeView_InsertItem(hTreeView, &tvis);
    
    // HKCC\SOFTWARE (current hardware profile software)
    std::wstring hkccSoftwarePath = L"SOFTWARE";
    InsertTreeItem(hHKCC, L"SOFTWARE", &hkccSoftwarePath);
    
    // HKCC\SYSTEM (current hardware profile system)
    std::wstring hkccSystemPath = L"SYSTEM";
    InsertTreeItem(hHKCC, L"SYSTEM", &hkccSystemPath);
}


// ---------------------------------------------------------------------------
// Helpers: recursively snapshot / restore the Files-page TreeView so that
// the full folder hierarchy (including virtual folders) survives page switches.
// ---------------------------------------------------------------------------
void MainWindow::SaveTreeSnapshot(HWND hTree, HTREEITEM hParent,
                                  std::vector<TreeNodeSnapshot> &out)
{
    HTREEITEM hChild = TreeView_GetChild(hTree, hParent);
    while (hChild) {
        TreeNodeSnapshot snap;
        wchar_t buf[1024] = {};
        TVITEMW tvi = {};
        tvi.mask      = TVIF_TEXT | TVIF_PARAM;
        tvi.hItem     = hChild;
        tvi.pszText   = buf;
        tvi.cchTextMax = 1023;
        TreeView_GetItem(hTree, &tvi);
        snap.text     = buf;
        snap.fullPath = tvi.lParam ? reinterpret_cast<wchar_t *>(tvi.lParam) : L"";
        snap.expanded = (TreeView_GetItemState(hTree, hChild, TVIS_EXPANDED) & TVIS_EXPANDED) != 0;
        auto it = s_virtualFolderFiles.find(hChild);
        if (it != s_virtualFolderFiles.end())
            snap.virtualFiles = it->second;
        SaveTreeSnapshot(hTree, hChild, snap.children);
        out.push_back(std::move(snap));
        hChild = TreeView_GetNextSibling(hTree, hChild);
    }
}

void MainWindow::RestoreTreeSnapshot(HWND hTree, HTREEITEM hParent,
                                     const std::vector<TreeNodeSnapshot> &nodes)
{
    for (const auto &snap : nodes) {
        HTREEITEM hNew = AddTreeNode(hTree, hParent, snap.text, snap.fullPath);
        if (!snap.virtualFiles.empty())
            s_virtualFolderFiles[hNew] = snap.virtualFiles;
        if (!snap.children.empty()) {
            RestoreTreeSnapshot(hTree, hNew, snap.children);
            if (snap.expanded)
                TreeView_Expand(hTree, hNew, TVE_EXPAND);
        }
    }
}

// Expand every node nested under hItem (used for first-visit full-expand).
static void ExpandAllSubnodes(HWND hTree, HTREEITEM hItem) {
    HTREEITEM hChild = TreeView_GetChild(hTree, hItem);
    while (hChild) {
        TreeView_Expand(hTree, hChild, TVE_EXPAND);
        ExpandAllSubnodes(hTree, hChild);
        hChild = TreeView_GetNextSibling(hTree, hChild);
    }
}

// Traverse the Comp-page tree and persist each node's expanded state back into
// the TreeNodeSnapshot it points to (lParam).  Called when leaving page 9.
static void SaveCompTreeExpansion(HWND hTree, HTREEITEM hParent) {
    HTREEITEM hChild = TreeView_GetChild(hTree, hParent);
    while (hChild) {
        TVITEMW tvi = {};
        tvi.hItem     = hChild;
        tvi.mask      = TVIF_PARAM | TVIF_STATE;
        tvi.stateMask = TVIS_EXPANDED;
        TreeView_GetItem(hTree, &tvi);
        if (tvi.lParam) {
            const TreeNodeSnapshot* p = reinterpret_cast<const TreeNodeSnapshot*>(tvi.lParam);
            p->compExpanded = (tvi.state & TVIS_EXPANDED) != 0;  // mutable field, independent from Files page
        }
        SaveCompTreeExpansion(hTree, hChild);
        hChild = TreeView_GetNextSibling(hTree, hChild);
    }
}

// ---------------------------------------------------------------------------

// Recursively clone hSrc subtree (text, fullPath, virtualFiles) under hNewParent.
// virtualFiles are *moved* (not copied) so they need no second erase.
// Enumerate files at a real disk path and append them as VirtualFolderFile
// entries into s_virtualFolderFiles[hTarget].  Only direct children (no subdirs).
static void IngestRealPathFiles(HTREEITEM hTarget, const std::wstring& folderPath) {
    std::wstring searchPath = folderPath + L"\\*";
    WIN32_FIND_DATAW fd = {};
    HANDLE hFind = FindFirstFileW(searchPath.c_str(), &fd);
    if (hFind == INVALID_HANDLE_VALUE) return;
    do {
        if (wcscmp(fd.cFileName, L".") == 0 || wcscmp(fd.cFileName, L"..") == 0) continue;
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
        VirtualFolderFile vf;
        vf.sourcePath    = folderPath + L"\\" + fd.cFileName;
        vf.destination   = L"\\" + std::wstring(fd.cFileName);
        vf.install_scope = L"";
        s_virtualFolderFiles[hTarget].push_back(vf);
    } while (FindNextFileW(hFind, &fd));
    FindClose(hFind);
}

// Recursively collect source paths from the VFS subtree rooted at hItem.
// Uses s_virtualFolderFiles for virtual nodes; falls back to a direct disk scan
// for real-path nodes (lParam points to the folder path).
// Must be called BEFORE s_virtualFolderFiles entries are erased for hItem.
static void CollectSubtreePaths(HWND hTree, HTREEITEM hItem,
                                 std::unordered_set<std::wstring>& out)
{
    auto it = s_virtualFolderFiles.find(hItem);
    if (it != s_virtualFolderFiles.end()) {
        for (const auto& vf : it->second)
            if (!vf.sourcePath.empty()) out.insert(vf.sourcePath);
    } else {
        // Real-path node whose virtualFiles were never populated — scan disk.
        TVITEMW tvi = {}; tvi.mask = TVIF_PARAM; tvi.hItem = hItem;
        TreeView_GetItem(hTree, &tvi);
        if (tvi.lParam) {
            const wchar_t* fp = reinterpret_cast<const wchar_t*>(tvi.lParam);
            if (fp && *fp) {
                std::wstring sp = std::wstring(fp) + L"\\*";
                WIN32_FIND_DATAW fd = {};
                HANDLE hFind = FindFirstFileW(sp.c_str(), &fd);
                if (hFind != INVALID_HANDLE_VALUE) {
                    do {
                        if (!wcscmp(fd.cFileName, L".") || !wcscmp(fd.cFileName, L"..")) continue;
                        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
                        out.insert(std::wstring(fp) + L"\\" + fd.cFileName);
                    } while (FindNextFileW(hFind, &fd));
                    FindClose(hFind);
                }
            }
        }
    }
    HTREEITEM hChild = TreeView_GetChild(hTree, hItem);
    while (hChild) {
        CollectSubtreePaths(hTree, hChild, out);
        hChild = TreeView_GetNextSibling(hTree, hChild);
    }
}

// Delete all file-type component rows whose source_path is in pathSet from the DB.
static void PurgeComponentRowsByPaths(const std::unordered_set<std::wstring>& pathSet)
{
    // Remove component rows whose source path is in the deleted set.
    // Works purely on the in-memory s_components vector — no DB access.
    // The DB is only written when the developer explicitly saves (IDM_FILE_SAVE),
    // so the Required flags on surviving rows are preserved intact.
    if (pathSet.empty()) return;
    s_components.erase(
        std::remove_if(s_components.begin(), s_components.end(),
            [&](const ComponentRow& c) {
                return c.source_type == L"file" && pathSet.count(c.source_path);
            }),
        s_components.end());
}

// Look up a locale key and substitute {0} with val (leave as-is if no placeholder).
static std::wstring LocFmt(const std::map<std::wstring, std::wstring>& loc,
                            const std::wstring& key,
                            const std::wstring& val = L"")
{
    auto it = loc.find(key);
    std::wstring s = (it != loc.end()) ? it->second : key;
    auto p = s.find(L"{0}");
    if (p != std::wstring::npos) s.replace(p, 3, val);
    return s;
}

static HTREEITEM CloneTreeSubtree(HWND hTree, HTREEITEM hSrc, HTREEITEM hNewParent) {
    wchar_t text[256] = {};
    TVITEMW tvi = {};
    tvi.mask       = TVIF_TEXT | TVIF_PARAM;
    tvi.hItem      = hSrc;
    tvi.pszText    = text;
    tvi.cchTextMax = 256;
    TreeView_GetItem(hTree, &tvi);
    std::wstring fullPath;
    if (tvi.lParam) fullPath = (const wchar_t*)tvi.lParam;

    // Always create the clone as a virtual node (empty lParam) so that
    // TVN_SELCHANGED uses s_virtualFolderFiles instead of triggering a
    // live disk scan, which blocks the UI thread and caused the erratic freeze.
    HTREEITEM hNew = MainWindow::AddTreeNode(hTree, hNewParent, text, L"");

    // If the source was a real-path node, read its direct files from disk
    // and store them as virtual entries.  This is the only place those files
    // exist — they were never in s_virtualFolderFiles.
    if (!fullPath.empty()) {
        IngestRealPathFiles(hNew, fullPath);
    }

    // Move virtualFiles from old item to new item (handles already-virtual nodes).
    auto it = s_virtualFolderFiles.find(hSrc);
    if (it != s_virtualFolderFiles.end()) {
        auto& dest = s_virtualFolderFiles[hNew];
        dest.insert(dest.end(),
            std::make_move_iterator(it->second.begin()),
            std::make_move_iterator(it->second.end()));
        s_virtualFolderFiles.erase(it);
    }

    // Recurse children before the item is deleted.
    HTREEITEM hChild = TreeView_GetChild(hTree, hSrc);
    while (hChild) {
        HTREEITEM hNext = TreeView_GetNextSibling(hTree, hChild);
        CloneTreeSubtree(hTree, hChild, hNew);
        hChild = hNext;
    }
    return hNew;
}

// Force-refresh the ListView to show the files belonging to hItem.
// Only reads from s_virtualFolderFiles — skips the disk-scan path because
// after a merge/move the result data is always in s_virtualFolderFiles,
// and TVN_SELCHANGED handles the physical-path case for normal navigation.
static void ForceRefreshListView(HWND hwndListView, HTREEITEM hItem) {
    if (!hwndListView || !IsWindow(hwndListView) || !hItem) return;
    ListView_DeleteAllItems(hwndListView);
    auto it = s_virtualFolderFiles.find(hItem);
    if (it != s_virtualFolderFiles.end()) {
        for (const auto& fileInfo : it->second) {
            SHFILEINFOW sfi = {};
            SHGetFileInfoW(fileInfo.sourcePath.c_str(), FILE_ATTRIBUTE_NORMAL, &sfi, sizeof(sfi),
                           SHGFI_SYSICONINDEX | SHGFI_SMALLICON | SHGFI_USEFILEATTRIBUTES);
            LVITEMW lvi = {};
            lvi.mask     = LVIF_TEXT | LVIF_PARAM | LVIF_IMAGE;
            lvi.iItem    = ListView_GetItemCount(hwndListView);
            lvi.pszText  = (LPWSTR)fileInfo.sourcePath.c_str();
            lvi.iImage   = sfi.iIcon;
            wchar_t* copy = (wchar_t*)malloc((fileInfo.sourcePath.size() + 1) * sizeof(wchar_t));
            if (copy) { wcscpy(copy, fileInfo.sourcePath.c_str()); lvi.lParam = (LPARAM)copy; }
            int idx = ListView_InsertItem(hwndListView, &lvi);
            ListView_SetItemText(hwndListView, idx, 1, (LPWSTR)fileInfo.destination.c_str());
        }
    }
    InvalidateRect(hwndListView, NULL, TRUE);
    UpdateWindow(hwndListView);
}

// Callback for SHBrowseForFolderW — navigates to the initial folder.
// bi.lParam must be a LPARAM pointing to a null-terminated wide path.
static int CALLBACK PickerFolderCallback(HWND hwnd, UINT uMsg, LPARAM /*lParam*/, LPARAM lpData) {
    if (uMsg == BFFM_INITIALIZED && lpData)
        SendMessageW(hwnd, BFFM_SETSELECTION, TRUE, lpData);
    return 0;
}

// Recursively save all tree nodes (folder nodes + their virtual files) to the
// DB files table.  rootPrefix is one of the four fixed root labels used as the
// leading path component so we know which root to restore nodes under on load.
static void SaveTreeToDb(int projectId,
                         const std::vector<TreeNodeSnapshot> &nodes,
                         const std::wstring &parentVirtualPath)
{
    for (const auto &snap : nodes) {
        std::wstring myPath = parentVirtualPath + L"\\" + snap.text;
        // Save the folder node itself (install_scope == L"__folder__")
        DB::InsertFile(projectId, snap.fullPath, myPath, L"__folder__");
        // Save any virtual files attached to this folder
        for (const auto &f : snap.virtualFiles) {
            // f.destination is like L"\filename.exe" (leading backslash)
            DB::InsertFile(projectId, f.sourcePath, myPath + f.destination,
                           f.install_scope.empty() ? L"" : f.install_scope);
        }
        // Recurse into children
        SaveTreeToDb(projectId, snap.children, myPath);
    }
}

// Forward declaration — full definition is in the VFS Picker section below.
static void VFSPicker_AddSubtree(HWND hTree, HTREEITEM hParent,
                                 const std::vector<TreeNodeSnapshot>& nodes);
static void CollectSnapshotPaths(const TreeNodeSnapshot& snap, std::vector<std::wstring>& out);
static void PopulateSnapshotFilesFromDisk(std::vector<TreeNodeSnapshot>& nodes);
// Same as CollectSnapshotPaths but does NOT recurse into children.
// Used by UpdateCompTreeRequiredIcons so that each tree node is judged only by
// its OWN direct files; intermediate container folders (like WinProgramSuite)
// will have anyFound=false and will not steal the required icon from a single
// required child subtree (WinProgramManager) and propagate it to unrelated
// sibling subtrees (WinUpdate, assets).
static void CollectSnapshotPathsLocal(const TreeNodeSnapshot& snap, std::vector<std::wstring>& out);
static void CollectAllFiles(const std::vector<TreeNodeSnapshot>& nodes,
                            int projectId, std::vector<ComponentRow>& out,
                            const std::wstring& sectionRoot);
static void UpdateCompTreeRequiredIcons(HWND hTree, HTREEITEM hItem,
                                        const std::wstring& sectionHint,
                                        bool parentIsRequired);

// -- Multi-select tracking set for Files-page TreeView ----------------------
static WNDPROC g_origFilesTreeProc = NULL;
static std::set<HTREEITEM> s_filesTreeMultiSel;

static LRESULT CALLBACK FilesTree_CtrlClickProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    if (msg == WM_LBUTTONDOWN) {
        TVHITTESTINFO ht = {};
        ht.pt.x = GET_X_LPARAM(lParam);
        ht.pt.y = GET_Y_LPARAM(lParam);
        HTREEITEM hHit = TreeView_HitTest(hwnd, &ht);
        if (hHit && (ht.flags & TVHT_ONITEMSTATEICON)) {
            // Let the original proc toggle the native TVS_CHECKBOXES state,
            // then sync our tracking set from the resulting check state.
            LRESULT r = CallWindowProcW(g_origFilesTreeProc, hwnd, msg, wParam, lParam);
            UINT state = TreeView_GetItemState(hwnd, hHit, TVIS_STATEIMAGEMASK);
            bool nowChecked = ((state >> 12) == 2); // state image index 2 = checked

            // Helper: recursively apply the same check state to all descendants.
            std::function<void(HTREEITEM, bool)> SetChildrenChecked =
                [&](HTREEITEM hItem, bool checked) {
                HTREEITEM hChild = TreeView_GetChild(hwnd, hItem);
                while (hChild) {
                    UINT idx = checked ? 2 : 1;
                    TreeView_SetItemState(hwnd, hChild,
                        INDEXTOSTATEIMAGEMASK(idx), TVIS_STATEIMAGEMASK);
                    if (checked)
                        s_filesTreeMultiSel.insert(hChild);
                    else
                        s_filesTreeMultiSel.erase(hChild);
                    SetChildrenChecked(hChild, checked);
                    hChild = TreeView_GetNextSibling(hwnd, hChild);
                }
            };

            if (nowChecked)
                s_filesTreeMultiSel.insert(hHit);
            else
                s_filesTreeMultiSel.erase(hHit);
            SetChildrenChecked(hHit, nowChecked);
            InvalidateRect(hwnd, NULL, FALSE);
            return r;
        }
    }
    if (msg == WM_NCDESTROY) {
        if (g_origFilesTreeProc)
            SetWindowLongPtrW(hwnd, GWLP_WNDPROC, (LONG_PTR)g_origFilesTreeProc);
        g_origFilesTreeProc = NULL;
    }
    return g_origFilesTreeProc
        ? CallWindowProcW(g_origFilesTreeProc, hwnd, msg, wParam, lParam)
        : DefWindowProcW(hwnd, msg, wParam, lParam);
}

// ── Comp-tree required-icon tooltip subclass ─────────────────────────────────
static WNDPROC g_origCompTreeProc    = NULL;
static bool    s_compTreeTTTracking  = false;

static LRESULT CALLBACK CompTree_TooltipSubclassProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg) {
    case WM_MOUSEMOVE: {
        TVHITTESTINFO ht = {};
        ht.pt.x = GET_X_LPARAM(lParam);
        ht.pt.y = GET_Y_LPARAM(lParam);
        HTREEITEM hHit = TreeView_HitTest(hwnd, &ht);
        bool overRequired = false;
        if (hHit && (ht.flags & TVHT_ONITEM)) {
            TVITEMW tvi = {}; tvi.hItem = hHit; tvi.mask = TVIF_IMAGE;
            TreeView_GetItem(hwnd, &tvi);
            overRequired = (tvi.iImage == 3);
        }
        if (overRequired && !IsTooltipVisible()) {
            POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
            ClientToScreen(hwnd, &pt);
            std::vector<TooltipEntry> entries = {{ L"", L"Required" }};
            ShowMultilingualTooltip(entries, pt.x + 12, pt.y + 18, GetParent(hwnd));
        } else if (!overRequired && IsTooltipVisible()) {
            HideTooltip();
        }
        if (!s_compTreeTTTracking) {
            TRACKMOUSEEVENT tme = {}; tme.cbSize = sizeof(tme);
            tme.dwFlags = TME_LEAVE; tme.hwndTrack = hwnd;
            TrackMouseEvent(&tme);
            s_compTreeTTTracking = true;
        }
        break;
    }
    case WM_MOUSELEAVE:
        HideTooltip();
        s_compTreeTTTracking = false;
        break;
    case WM_NCDESTROY:
        if (g_origCompTreeProc) {
            SetWindowLongPtrW(hwnd, GWLP_WNDPROC, (LONG_PTR)g_origCompTreeProc);
            g_origCompTreeProc = NULL;
        }
        break;
    }
    return g_origCompTreeProc ? CallWindowProcW(g_origCompTreeProc, hwnd, msg, wParam, lParam)
                              : DefWindowProcW(hwnd, msg, wParam, lParam);
}

// Walk up the comp tree to find the section root node (lParam == 0) and return its label.
// Returns e.g. L"Program Files", L"AskAtInstall", etc.
static std::wstring GetCompTreeItemSection(HWND hTree, HTREEITEM hItem)
{
    HTREEITEM cur = hItem;
    while (cur) {
        wchar_t buf[256] = {};
        TVITEMW t = {}; t.hItem = cur; t.mask = TVIF_PARAM | TVIF_TEXT;
        t.pszText = buf; t.cchTextMax = 256;
        TreeView_GetItem(hTree, &t);
        if (t.lParam == 0)   // section root has no snapshot
            return std::wstring(buf);
        HTREEITEM hP = TreeView_GetParent(hTree, cur);
        if (!hP) break;
        cur = hP;
    }
    return L"";
}

// Walk the comp tree and set icon index 3 (required) or 0 (optional) on each
// node based on whether all its files are marked is_required == 1 in the
// correct section (so AskAtInstall components are never confused with
// Program Files components that share the same source path).
// parentIsRequired: when true, nodes with NO registered components inherit the
// parent's required state instead of defaulting to optional.  This makes
// empty or component-less subfolders (img/, locale/ etc.) correctly reflect the
// cascaded required state of their parent.
static void UpdateCompTreeRequiredIcons(HWND hTree, HTREEITEM hItem,
                                        const std::wstring& sectionHint = L"",
                                        bool parentIsRequired = false)
{
    while (hItem) {
        wchar_t textBuf[256] = {};
        TVITEMW tvi = {}; tvi.hItem = hItem;
        tvi.mask = TVIF_PARAM | TVIF_TEXT;
        tvi.pszText = textBuf; tvi.cchTextMax = 256;
        TreeView_GetItem(hTree, &tvi);
        const TreeNodeSnapshot* snap = (const TreeNodeSnapshot*)tvi.lParam;

        // Section root nodes have lParam == 0; their label IS the section name.
        std::wstring mySection = sectionHint;
        if (!snap && textBuf[0])
            mySection = textBuf;

        int useIcon = 0;
        if (snap) {
            std::vector<std::wstring> paths;
            CollectSnapshotPaths(*snap, paths);
            bool anyFound       = false;
            bool allRequired    = true;
            bool folderTypeUsed = false;

            // Phase 1: folder-type component — stored as a single row whose
            // source_path is the folder itself (not individual files within it).
            // Check this first so that a folder-type required component always
            // wins, regardless of whether its individual files are registered.
            if (!snap->fullPath.empty()) {
                for (const auto& c : s_components) {
                    if (c.source_path != snap->fullPath) continue;
                    if (!c.dest_path.empty() && c.dest_path != mySection) continue;
                    folderTypeUsed = true;
                    anyFound       = true;
                    if (!c.is_required) allRequired = false;
                    break;
                }
            }

            // Phase 2: file-type components — only when there is no folder-type
            // row covering this exact folder.  An unregistered file (not found in
            // s_components) means the folder cannot be considered fully required,
            // which prevents a container folder (e.g. WinProgramSuite) from
            // incorrectly inheriting all-required when only one of its child
            // subtrees has required components and the rest are unregistered.
            if (!folderTypeUsed) {
                for (const auto& p : paths) {
                    bool found = false;
                    for (const auto& c : s_components) {
                        if (c.source_path != p) continue;
                        if (!c.dest_path.empty() && c.dest_path != mySection) continue;
                        found    = true;
                        anyFound = true;
                        if (!c.is_required) allRequired = false;
                        break;
                    }
                    if (!found) allRequired = false;
                    if (!allRequired) break;
                }
            }
            if (anyFound)
                useIcon = allRequired ? 3 : 0;
            else
                useIcon = parentIsRequired ? 3 : 0;  // inherit parent state
            tvi.mask = TVIF_IMAGE | TVIF_SELECTEDIMAGE;
            tvi.iImage         = useIcon;
            tvi.iSelectedImage = (useIcon == 3) ? 3 : 1;
            TreeView_SetItem(hTree, &tvi);
        }
        // Propagate this node's resolved required state to its children so that
        // component-less sub-folders (img/, locale/ etc.) pick up the cascade.
        UpdateCompTreeRequiredIcons(hTree, TreeView_GetChild(hTree, hItem), mySection, useIcon == 3);
        hItem = TreeView_GetNextSibling(hTree, hItem);
    }
}
// ─────────────────────────────────────────────────────────────────────────────

void MainWindow::SwitchPage(HWND hwnd, int pageIndex) {
    // Snapshot Files page tree before tearing it down so we can restore it
    // (including virtual folders and full hierarchy) when we return.
    if (s_currentPageIndex == 0 && s_hTreeView && IsWindow(s_hTreeView)) {
        // Only clear+resave a snapshot when the corresponding root exists.
        // Roots absent in the current mode (e.g. ProgramData while AskAtInstall
        // is enabled) keep their previous snapshot so switching modes round-trips
        // without losing data.
        if (s_hProgramFilesRoot) { s_treeSnapshot_ProgramFiles.clear();  SaveTreeSnapshot(s_hTreeView, s_hProgramFilesRoot, s_treeSnapshot_ProgramFiles); }
        if (s_hProgramDataRoot)  { s_treeSnapshot_ProgramData.clear();   SaveTreeSnapshot(s_hTreeView, s_hProgramDataRoot,  s_treeSnapshot_ProgramData);  }
        if (s_hAppDataRoot)      { s_treeSnapshot_AppData.clear();        SaveTreeSnapshot(s_hTreeView, s_hAppDataRoot,       s_treeSnapshot_AppData);      }
        if (s_hAskAtInstallRoot) { s_treeSnapshot_AskAtInstall.clear();   SaveTreeSnapshot(s_hTreeView, s_hAskAtInstallRoot,  s_treeSnapshot_AskAtInstall);  }
        UpdateComponentsButtonState(hwnd);
    }
    // Save Comp page tree expansion state before teardown
    if (s_currentPageIndex == 9 && s_hCompTreeView && IsWindow(s_hCompTreeView)) {
        HTREEITEM hR = TreeView_GetRoot(s_hCompTreeView);
        while (hR) {
            SaveCompTreeExpansion(s_hCompTreeView, hR);
            hR = TreeView_GetNextSibling(s_hCompTreeView, hR);
        }
    }
    // Destroy previous page content
    if (s_hCurrentPage) {
        DestroyWindow(s_hCurrentPage);
        s_hCurrentPage = NULL;
    }
    
    // Destroy previous page buttons
    if (s_hPageButton1) {
        DestroyWindow(s_hPageButton1);
        s_hPageButton1 = NULL;
    }
    if (s_hPageButton2) {
        DestroyWindow(s_hPageButton2);
        s_hPageButton2 = NULL;
    }
    
    // Destroy all known control IDs from previous pages
    int controlIds[] = {
        IDC_BROWSE_INSTALL_DIR, IDC_FILES_REMOVE, IDC_PROJECT_NAME, IDC_INSTALL_FOLDER,
        IDC_REG_CHECKBOX, IDC_REG_ICON_PREVIEW, IDC_REG_ADD_ICON, IDC_REG_DISPLAY_NAME,
        IDC_REG_VERSION, IDC_REG_PUBLISHER, IDC_REG_ADD_KEY, IDC_REG_ADD_VALUE, IDC_REG_EDIT, IDC_REG_REMOVE, IDC_REG_BACKUP,
        IDC_REG_SHOW_REGKEY, IDC_REG_WARNING_ICON, IDC_REG_TREEVIEW, IDC_REG_LISTVIEW,
        IDC_COMP_ENABLE, IDC_COMP_LISTVIEW, IDC_COMP_TREEVIEW, IDC_COMP_ADD, IDC_COMP_EDIT, IDC_COMP_REMOVE,
        IDC_SC_DESKTOP_BTN, IDC_SC_DESKTOP_OPT, IDC_SC_PINSTART_BTN, IDC_SC_PINTASKBAR_BTN,
        IDC_SC_SM_TREE, IDC_SC_SM_ADD, IDC_SC_SM_REMOVE, IDC_SC_SM_ADDSC,
        IDC_SC_SM_PIN_LABEL, IDC_SC_TB_PIN_LABEL,
        IDC_SC_SM_PIN_OPT, IDC_SC_TB_PIN_OPT,
        5100, 5101, 5102, 5103, 5104, 5105, 5106, 5107, 5108, 5109, 5110, 5300, 5301, 5302, 5303, 5304 // Labels and other static controls
    };
    
    for (int id : controlIds) {
        HWND hCtrl = GetDlgItem(hwnd, id);
        if (hCtrl) {
            DestroyWindow(hCtrl);
        }
    }
    
    // Enumerate and destroy ALL child windows in the page area (below toolbar, above status bar)
    RECT rcClient;
    GetClientRect(hwnd, &rcClient);
    HWND hChild = GetWindow(hwnd, GW_CHILD);
    while (hChild) {
        HWND hNext = GetWindow(hChild, GW_HWNDNEXT);
        
        // Get child position
        RECT rcChild;
        GetWindowRect(hChild, &rcChild);
        MapWindowPoints(HWND_DESKTOP, hwnd, (POINT*)&rcChild, 2);
        
        // If child is in page area (not toolbar or status bar), destroy it
        if (rcChild.top > s_toolbarHeight && rcChild.bottom < rcClient.bottom - 25) {
            // Skip toolbar buttons and known handles
            int childId = GetDlgCtrlID(hChild);
            bool isToolbarBtn = (childId >= IDC_TB_FILES && childId <= IDC_TB_ABOUT) || childId == IDC_TB_DIALOGS || childId == IDC_TB_COMPONENTS || childId == IDC_TB_EXIT || childId == IDC_TB_CLOSE_PROJECT;
            if (!isToolbarBtn) {
                if (hChild != s_hTreeView && hChild != s_hListView && 
                    hChild != s_hRegTreeView && hChild != s_hRegListView &&
                    hChild != s_hCompListView &&
                    hChild != s_hPageButton1 && hChild != s_hPageButton2) {
                    DestroyWindow(hChild);
                }
            }
        }
        
        hChild = hNext;
    }
    
    // Destroy TreeView and ListView if they exist (they're children of main window now)
    if (s_hTreeView && IsWindow(s_hTreeView)) {
        DestroyWindow(s_hTreeView);
    }
    if (s_hListView && IsWindow(s_hListView)) {
        DestroyWindow(s_hListView);
    }
    
    // Destroy Registry page TreeView and ListView
    if (s_hRegTreeView && IsWindow(s_hRegTreeView)) {
        // Clean up lParam memory from registry TreeView items
        HTREEITEM hRoot = TreeView_GetRoot(s_hRegTreeView);
        while (hRoot) {
            HTREEITEM hNext = TreeView_GetNextSibling(s_hRegTreeView, hRoot);
            CleanupRegistryTreeView(s_hRegTreeView, hRoot);
            hRoot = hNext;
        }
        DestroyWindow(s_hRegTreeView);
    }
    if (s_hRegListView && IsWindow(s_hRegListView)) {
        DestroyWindow(s_hRegListView);
    }
    
    // Clear tree/list handles
    s_filesTreeMultiSel.clear();
    s_hTreeView = NULL;
    s_hListView = NULL;
    s_hProgramFilesRoot = NULL;
    s_hProgramDataRoot  = NULL;
    s_hAppDataRoot      = NULL;
    s_hAskAtInstallRoot = NULL;
    s_hRegTreeView = NULL;
    s_hRegListView = NULL;
    
    // Destroy Components page ListView
    if (s_hCompListView && IsWindow(s_hCompListView)) {
        DestroyWindow(s_hCompListView);
    }
    s_hCompListView = NULL;
    if (s_hCompTreeView && IsWindow(s_hCompTreeView)) {
        DestroyWindow(s_hCompTreeView);
    }
    s_hCompTreeView = NULL;
    // NOTE: s_components is intentionally NOT cleared here.
    // It lives in memory for the full project session and is only written
    // to DB on explicit Save (IDM_FILE_SAVE).  SwitchPage(9) reads it directly.

    IDLG_TearDown(hwnd);
    SC_TearDown(hwnd);
    DEP_TearDown(hwnd);

    // Clear registry values map
    s_registryValues.clear();
    
    // Hide About tooltip when switching pages
    HideTooltip();
    s_aboutMouseTracking = false;
    
    // Force complete window redraw
    RedrawWindow(hwnd, NULL, NULL, RDW_ERASE | RDW_INVALIDATE | RDW_ALLCHILDREN);
    
    s_currentPageIndex = pageIndex;

    // Update active-page highlight on toolbar buttons
    static const struct { int pageIdx; int btnId; } kPageBtns[] = {
        {0, IDC_TB_FILES},       {1, IDC_TB_ADD_REGISTRY}, {2, IDC_TB_ADD_SHORTCUT},
        {3, IDC_TB_ADD_DEPEND},  {4, IDC_TB_DIALOGS},      {5, IDC_TB_SETTINGS},
        {6, IDC_TB_BUILD},       {7, IDC_TB_TEST},          {8, IDC_TB_SCRIPTS},
        {9, IDC_TB_COMPONENTS},
    };
    for (auto& entry : kPageBtns) {
        HWND hBtn = GetDlgItem(hwnd, entry.btnId);
        if (!hBtn) continue;
        if (entry.pageIdx == pageIndex)
            SetPropW(hBtn, L"IsActivePage", (HANDLE)(INT_PTR)1);
        else
            RemovePropW(hBtn, L"IsActivePage");
        InvalidateRect(hBtn, NULL, TRUE);
    }

    RECT rc;
    GetClientRect(hwnd, &rc);
    HINSTANCE hInst = (HINSTANCE)GetWindowLongPtr(hwnd, GWLP_HINSTANCE);
    
    // Calculate page area (below toolbar, above status bar)
    int pageY = s_toolbarHeight;
    int pageHeight = rc.bottom - s_toolbarHeight - 25;
    
    // Create page-specific content
    switch (pageIndex) {
    case 0: // Files page - no page container, all controls are direct children of main window
    {
        // H3 headline (direct child of main window)
        auto itFilesMgmt = s_locale.find(L"files_management");
        std::wstring filesMgmtText = (itFilesMgmt != s_locale.end()) ? itFilesMgmt->second : L"Files Management";
        HWND hTitle = CreateWindowExW(0, L"STATIC", filesMgmtText.c_str(),
            WS_CHILD | WS_VISIBLE | SS_LEFT,
            S(20), pageY + S(15), rc.right - S(40), S(38),
            hwnd, (HMENU)5100, hInst, NULL); // Give it an ID for WM_CTLCOLORSTATIC
        if (s_hPageTitleFont) SendMessageW(hTitle, WM_SETFONT, (WPARAM)s_hPageTitleFont, TRUE);
        
        // Measure all three button texts first, then reconcile widths so neither row clips.
        // If Remove is wider than the two-button row, distribute the extra onto wFDir so
        // Add Folder grows and the visual block stays left-aligned and consistent.
        auto itAddFolder = s_locale.find(L"files_add_folder");
        std::wstring addFolderText = (itAddFolder != s_locale.end()) ? itAddFolder->second : L"Add Folder";
        auto itAddFiles = s_locale.find(L"files_add_files");
        std::wstring addFilesText = (itAddFiles != s_locale.end()) ? itAddFiles->second : L"Add Files";
        auto itRemove = s_locale.find(L"files_remove");
        std::wstring removeText = (itRemove != s_locale.end()) ? itRemove->second : L"Remove";

        int wFDir   = MeasureButtonWidth(addFolderText, true);
        int wFFiles = MeasureButtonWidth(addFilesText,  true);
        int wRemove = MeasureButtonWidth(removeText,    true);
        const int fBtnGap = S(10);
        int topRowW = wFDir + fBtnGap + wFFiles;
        if (wRemove > topRowW) {
            wFDir  += wRemove - topRowW;  // grow Add Folder to fill the gap
            topRowW = wRemove;
        }

        s_hPageButton1 = CreateCustomButtonWithIcon(hwnd, IDC_FILES_ADD_DIR, addFolderText, ButtonColor::Blue,
            L"shell32.dll", 296, S(20), s_toolbarHeight + S(55), wFDir, S(35), hInst);
        s_hPageButton2 = CreateCustomButtonWithIcon(hwnd, IDC_FILES_ADD_FILES, addFilesText, ButtonColor::Blue,
            L"shell32.dll", 71, S(20) + wFDir + fBtnGap, s_toolbarHeight + S(55), wFFiles, S(35), hInst);
        HWND hRemoveBtn = CreateCustomButtonWithIcon(hwnd, IDC_FILES_REMOVE, removeText, ButtonColor::Red,
            L"shell32.dll", 234, S(20), s_toolbarHeight + S(100), topRowW, S(35), hInst);
        
        // Project name label and field
        auto itProjName = s_locale.find(L"files_project_name");
        std::wstring projNameText = (itProjName != s_locale.end()) ? itProjName->second : L"Project name:";
        HWND hProjectLabel = CreateWindowExW(0, L"STATIC", projNameText.c_str(),
            WS_CHILD | WS_VISIBLE | SS_LEFT | SS_CENTERIMAGE,
            S(290), pageY + S(55), S(100), S(22),
            hwnd, NULL, hInst, NULL);
        if (s_scaledFont) SendMessageW(hProjectLabel, WM_SETFONT, (WPARAM)s_scaledFont, TRUE);
        
        // Set programmatic flag to prevent EN_CHANGE from marking as manually edited
        s_updatingProjectNameProgrammatically = true;
        HWND hProjectEdit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", s_currentProject.name.c_str(),
            WS_CHILD | WS_VISIBLE | WS_BORDER | ES_LEFT | ES_AUTOHSCROLL,
            S(395), pageY + S(55), rc.right - S(460), S(22),
            hwnd, (HMENU)IDC_PROJECT_NAME, hInst, NULL);
        if (s_scaledFont) SendMessageW(hProjectEdit, WM_SETFONT, (WPARAM)s_scaledFont, TRUE);
        s_updatingProjectNameProgrammatically = false;
        
        // Install folder label and field (aligned with project name field)
        auto itInstFld = s_locale.find(L"files_install_folder");
        std::wstring instFldText = (itInstFld != s_locale.end()) ? itInstFld->second : L"Install folder:";
        HWND hInstallLabel = CreateWindowExW(0, L"STATIC", instFldText.c_str(),
            WS_CHILD | WS_VISIBLE | SS_LEFT | SS_CENTERIMAGE,
            S(290), pageY + S(82), S(100), S(22),
            hwnd, NULL, hInst, NULL);
        if (s_scaledFont) SendMessageW(hInstallLabel, WM_SETFONT, (WPARAM)s_scaledFont, TRUE);
        
        std::wstring defaultPath = s_currentInstallPath.empty()
            ? GetProgramFilesPath() + L"\\" + s_currentProject.name
            : s_currentInstallPath;
        HWND hInstallEdit = CreateWindowExW(0, L"STATIC", defaultPath.c_str(),
            WS_CHILD | WS_VISIBLE | SS_LEFT | SS_CENTERIMAGE,
            S(395), pageY + S(82), rc.right - S(460), S(22),
            hwnd, (HMENU)IDC_INSTALL_FOLDER, hInst, NULL);
        if (s_scaledFont) SendMessageW(hInstallEdit, WM_SETFONT, (WPARAM)s_scaledFont, TRUE);
        
        // Browse button for install folder (aligned with install folder field)
        CreateCustomButtonWithIcon(hwnd, IDC_BROWSE_INSTALL_DIR, L"...", ButtonColor::Blue,
            L"shell32.dll", 4, rc.right - S(55), s_toolbarHeight + S(82), S(35), S(22), hInst);

        // Ask-at-install custom checkbox below install folder
        auto itAskUser = s_locale.find(L"files_ask_user_install");
        std::wstring askUserText = (itAskUser != s_locale.end()) ? itAskUser->second : L"Ask end user at install (Per-user / All-users)";
        HWND hAskChk = CreateCustomCheckbox(hwnd, IDC_ASK_AT_INSTALL, askUserText,
            s_askAtInstallEnabled,
            S(395), pageY + S(110), S(360), S(22), hInst);
        if (s_scaledFont) SendMessageW(hAskChk, WM_SETFONT, (WPARAM)s_scaledFont, TRUE);
        
        // Hint label: explains how checkbox multi-select works
        {
            auto itHint = s_locale.find(L"files_remove_hint");
            std::wstring hintText = (itHint != s_locale.end()) ? itHint->second
                : L"Tick items to select for removal. Multiple items can be ticked at once.";
            HWND hHint = CreateWindowExW(0, L"STATIC", hintText.c_str(),
                WS_CHILD | WS_VISIBLE | SS_LEFT | SS_CENTERIMAGE,
                S(20), s_toolbarHeight + S(138), rc.right - S(40), S(18),
                hwnd, NULL, hInst, NULL);
            if (s_scaledFont) SendMessageW(hHint, WM_SETFONT, (WPARAM)s_scaledFont, TRUE);
        }

        // Calculate split pane dimensions (TreeView 30%, ListView 70%)
        int viewTop = S(165);  // Moved down to make room for Remove button and hint label
        int viewHeight = pageHeight - S(175);
        int treeWidth = (int)((rc.right - S(50)) * 0.3);
        int listWidth = (rc.right - S(50)) - treeWidth - S(5); // 5px gap
        
        // TreeView on the left (folder hierarchy) - child of main window to receive notifications
        s_hTreeView = CreateWindowExW(WS_EX_CLIENTEDGE, WC_TREEVIEW, NULL,
            WS_CHILD | WS_VISIBLE | WS_BORDER | TVS_HASLINES | TVS_HASBUTTONS | TVS_LINESATROOT | TVS_SHOWSELALWAYS | TVS_EDITLABELS | TVS_CHECKBOXES,
            S(20), s_toolbarHeight + viewTop, treeWidth, viewHeight,
            hwnd, (HMENU)102, hInst, NULL);
        
        // Set indent width to make hierarchy more visible
        TreeView_SetIndent(s_hTreeView, S(19));
        if (s_scaledFont) SendMessageW(s_hTreeView, WM_SETFONT, (WPARAM)s_scaledFont, TRUE);

        // Checkbox-click subclass: syncs s_filesTreeMultiSel with TVS_CHECKBOXES state
        if (!g_origFilesTreeProc)
            g_origFilesTreeProc = (WNDPROC)SetWindowLongPtrW(
                s_hTreeView, GWLP_WNDPROC, (LONG_PTR)FilesTree_CtrlClickProc);
        // Image list is 36 px wide (32 px icon + 4 px transparent left padding) so
        // there is a visible gap between the TVS_CHECKBOXES checkbox and the folder icon.
        HIMAGELIST hImageList = ImageList_Create(36, 32, ILC_COLOR32 | ILC_MASK, 3, 1);
        if (hImageList) {
            // Helper: draw hIco (32×32) into a 36×32 transparent DIB at x=4,
            // then add the DIB to the image list — this is what creates the gap.
            auto AddIconPadded = [&](HICON hIco) {
                if (!hIco) return;
                HDC hScreen = GetDC(NULL);
                BITMAPINFO bmi = {};
                bmi.bmiHeader.biSize        = sizeof(BITMAPINFOHEADER);
                bmi.bmiHeader.biWidth       = 36;
                bmi.bmiHeader.biHeight      = -32;  // top-down
                bmi.bmiHeader.biPlanes      = 1;
                bmi.bmiHeader.biBitCount    = 32;
                bmi.bmiHeader.biCompression = BI_RGB;
                void* pBits = nullptr;
                HBITMAP hPad = CreateDIBSection(hScreen, &bmi, DIB_RGB_COLORS, &pBits, NULL, 0);
                if (hPad && pBits) {
                    memset(pBits, 0, 36 * 32 * 4);  // fully transparent
                    HDC hMem = CreateCompatibleDC(hScreen);
                    HBITMAP hOld = (HBITMAP)SelectObject(hMem, hPad);
                    DrawIconEx(hMem, 4, 0, hIco, 32, 32, 0, NULL, DI_NORMAL);
                    SelectObject(hMem, hOld);
                    DeleteDC(hMem);
                    ImageList_Add(hImageList, hPad, NULL);
                    DeleteObject(hPad);
                }
                ReleaseDC(NULL, hScreen);
            };

            // Load folder icons from shell32.dll
            HMODULE hShell32 = LoadLibraryW(L"shell32.dll");
            if (hShell32) {
                HICON hFolderClosed = (HICON)LoadImageW(hShell32, MAKEINTRESOURCEW(4), IMAGE_ICON, 32, 32, 0);
                HICON hFolderOpen   = (HICON)LoadImageW(hShell32, MAKEINTRESOURCEW(5), IMAGE_ICON, 32, 32, 0);
                AddIconPadded(hFolderClosed);
                AddIconPadded(hFolderOpen);
                // Create the AskAtInstall badge icon (solid blue circle) programmatically
                HICON hBadge = NULL;
                {
                    HDC hdc = GetDC(NULL);
                    HBITMAP hBmp = CreateCompatibleBitmap(hdc, 32, 32);
                    HDC hMem = CreateCompatibleDC(hdc);
                    HBITMAP hOld = (HBITMAP)SelectObject(hMem, hBmp);
                    RECT rcBmp = {0,0,32,32};
                    FillRect(hMem, &rcBmp, (HBRUSH)GetStockObject(NULL_BRUSH));
                    HBRUSH hCircle = CreateSolidBrush(RGB(65,105,225));
                    HBRUSH hOldBrush = (HBRUSH)SelectObject(hMem, hCircle);
                    Ellipse(hMem, 6, 6, 26, 26);
                    SelectObject(hMem, hOldBrush);
                    DeleteObject(hCircle);
                    ICONINFO ii = {};
                    ii.fIcon    = TRUE;
                    ii.hbmMask  = hBmp;
                    ii.hbmColor = hBmp;
                    hBadge = CreateIconIndirect(&ii);
                    SelectObject(hMem, hOld);
                    DeleteDC(hMem);
                    ReleaseDC(NULL, hdc);
                }
                AddIconPadded(hBadge);
                if (hFolderClosed) DestroyIcon(hFolderClosed);
                if (hFolderOpen)   DestroyIcon(hFolderOpen);
                if (hBadge)        DestroyIcon(hBadge);
                FreeLibrary(hShell32);
            }
            TreeView_SetImageList(s_hTreeView, hImageList, TVSIL_NORMAL);
            // Ensure rows are tall enough for 32x32 icons
            TreeView_SetItemHeight(s_hTreeView, 34);
        }
        // Replace the native TVS_CHECKBOXES images with our custom themed ones.
        UpdateTreeViewCheckboxImages(s_hTreeView, S(16));
        
        // ListView on the right (current folder contents - files only) - child of main window
        s_hListView = CreateWindowExW(WS_EX_CLIENTEDGE, WC_LISTVIEW, NULL,
            WS_CHILD | WS_VISIBLE | LVS_REPORT | LVS_SHAREIMAGELISTS | WS_BORDER,
            S(20) + treeWidth + S(5), s_toolbarHeight + viewTop, listWidth, viewHeight,
            hwnd, (HMENU)100, hInst, NULL);
        if (s_scaledFont) SendMessageW(s_hListView, WM_SETFONT, (WPARAM)s_scaledFont, TRUE);
        { HWND hLvHdr = ListView_GetHeader(s_hListView); if (hLvHdr && s_scaledFont) SendMessageW(hLvHdr, WM_SETFONT, (WPARAM)s_scaledFont, TRUE); }
        
        // Get system image list for file icons
        SHFILEINFOW sfi = {};
        HIMAGELIST hSysImageList = (HIMAGELIST)SHGetFileInfoW(L"C:\\", 0, &sfi, sizeof(sfi), 
            SHGFI_SYSICONINDEX | SHGFI_SMALLICON);
        if (hSysImageList) {
            ListView_SetImageList(s_hListView, hSysImageList, LVSIL_SMALL);
        }
        
        ListView_SetExtendedListViewStyle(s_hListView, LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES | LVS_EX_INFOTIP);
        
        LVCOLUMNW col = {};
        col.mask = LVCF_TEXT | LVCF_WIDTH;
        col.cx = (int)(listWidth * 0.55);
        col.pszText = (LPWSTR)L"Source Path";
        ListView_InsertColumn(s_hListView, 0, &col);
        col.cx = (int)(listWidth * 0.45);
        col.pszText = (LPWSTR)L"Destination";
        ListView_InsertColumn(s_hListView, 1, &col);
        
        // Always create "Program Files" root node (use Windows-localized name)
        std::wstring defaultInstallPath = GetProgramFilesPath() + L"\\" + s_currentProject.name;
        std::wstring installPathCopy = defaultInstallPath;
        size_t lastSlash = installPathCopy.find_last_of(L"\\/");
        std::wstring parentPath = GetProgramFilesFolderName();
        if (lastSlash != std::wstring::npos) {
            std::wstring temp = installPathCopy.substr(0, lastSlash);
            size_t secondLastSlash = temp.find_last_of(L"\\/");
            if (secondLastSlash != std::wstring::npos) {
                parentPath = temp.substr(secondLastSlash + 1);
            }
        }
        s_hProgramFilesRoot = AddTreeNode(s_hTreeView, TVI_ROOT, parentPath, L"");
        // If AskAtInstall mode enabled, create the special virtual root; otherwise create ProgramData and AppData
        if (s_askAtInstallEnabled) {
            s_hAskAtInstallRoot = AddTreeNode(s_hTreeView, TVI_ROOT, L"AskAtInstall", L"");
            if (s_hAskAtInstallRoot) {
                // Assign badge icon (last image index)
                TVITEMW itImg = {};
                itImg.hItem = s_hAskAtInstallRoot;
                itImg.mask = TVIF_IMAGE | TVIF_SELECTEDIMAGE;
                itImg.iImage = 2; // third image added (badge)
                itImg.iSelectedImage = 2;
                TreeView_SetItem(s_hTreeView, &itImg);
            }
        } else {
            // Add ProgramData and AppData (Roaming) roots so developer can add files there as well
            wchar_t szPath[MAX_PATH] = {};
            if (SUCCEEDED(SHGetFolderPathW(NULL, CSIDL_COMMON_APPDATA, NULL, 0, szPath))) {
                std::wstring pdPath = szPath;
                    s_hProgramDataRoot = AddTreeNode(s_hTreeView, TVI_ROOT, L"ProgramData", pdPath);
            } else {
                s_hProgramDataRoot = AddTreeNode(s_hTreeView, TVI_ROOT, L"ProgramData", L"C:\\ProgramData");
            }
            if (SUCCEEDED(SHGetFolderPathW(NULL, CSIDL_APPDATA, NULL, 0, szPath))) {
                std::wstring appdataPath = szPath;
                s_hAppDataRoot = AddTreeNode(s_hTreeView, TVI_ROOT, L"AppData (Roaming)", appdataPath);
            } else {
                s_hAppDataRoot = AddTreeNode(s_hTreeView, TVI_ROOT, L"AppData (Roaming)", L"%APPDATA%");
            }
        }
        TreeView_Expand(s_hTreeView, s_hProgramFilesRoot, TVE_EXPAND);
        
        // Populate TreeView: restore snapshot if one exists (preserves full hierarchy
        // + virtual folders), otherwise populate fresh from disk.
        bool hasSnapshot = !s_treeSnapshot_ProgramFiles.empty() ||
                           !s_treeSnapshot_ProgramData.empty()  ||
                           !s_treeSnapshot_AppData.empty()       ||
                           !s_treeSnapshot_AskAtInstall.empty();
        if (hasSnapshot) {
            // Drop stale HTREEITEM keys; restore will re-populate with fresh keys.
            s_virtualFolderFiles.clear();
            if (s_hProgramFilesRoot) { RestoreTreeSnapshot(s_hTreeView, s_hProgramFilesRoot, s_treeSnapshot_ProgramFiles);  TreeView_Expand(s_hTreeView, s_hProgramFilesRoot, TVE_EXPAND); }
            if (s_hProgramDataRoot)  { RestoreTreeSnapshot(s_hTreeView, s_hProgramDataRoot,  s_treeSnapshot_ProgramData);   TreeView_Expand(s_hTreeView, s_hProgramDataRoot,  TVE_EXPAND); }
            if (s_hAppDataRoot)      { RestoreTreeSnapshot(s_hTreeView, s_hAppDataRoot,       s_treeSnapshot_AppData);       TreeView_Expand(s_hTreeView, s_hAppDataRoot,      TVE_EXPAND); }
            if (s_hAskAtInstallRoot) { RestoreTreeSnapshot(s_hTreeView, s_hAskAtInstallRoot,  s_treeSnapshot_AskAtInstall);  TreeView_Expand(s_hTreeView, s_hAskAtInstallRoot, TVE_EXPAND); }
        } else {
            // No session snapshot — first time this project is shown in this session.
            // Prefer rebuilding from the persisted DB rows (works even when directory=""
            // and survives restarts). Fall back to a disk scan if no DB rows exist.
            bool rebuiltFromDb = false;
            if (s_currentProject.id > 0) {
                auto fileRows = DB::GetFilesForProject(s_currentProject.id);
                if (!fileRows.empty()) {
                    rebuiltFromDb = true;
                    // Sort shallowest first so every parent node is inserted before its children.
                    std::sort(fileRows.begin(), fileRows.end(),
                        [](const FileRow &a, const FileRow &b) {
                            return std::count(a.destination_path.begin(), a.destination_path.end(), L'\\')
                                 < std::count(b.destination_path.begin(), b.destination_path.end(), L'\\');
                        });
                    // Map virtual path → live HTREEITEM for parent lookup.
                    std::map<std::wstring, HTREEITEM> pathToNode;
                    pathToNode[L"Program Files"]     = s_hProgramFilesRoot;
                    pathToNode[L"ProgramData"]       = s_hProgramDataRoot;
                    pathToNode[L"AppData (Roaming)"] = s_hAppDataRoot;
                    pathToNode[L"AskAtInstall"]      = s_hAskAtInstallRoot;

                    for (const auto &row : fileRows) {
                        if (row.install_scope == L"__folder__") {
                            size_t sep = row.destination_path.rfind(L'\\');
                            std::wstring parentPath = (sep != std::wstring::npos)
                                ? row.destination_path.substr(0, sep) : L"";
                            std::wstring nodeName = (sep != std::wstring::npos)
                                ? row.destination_path.substr(sep + 1) : row.destination_path;
                            auto it = pathToNode.find(parentPath);
                            if (it != pathToNode.end() && it->second) {
                                HTREEITEM hNode = AddTreeNode(s_hTreeView, it->second,
                                                              nodeName, row.source_path);
                                pathToNode[row.destination_path] = hNode;
                            }
                        } else {
                            // File row — attach to parent folder node.
                            size_t sep = row.destination_path.rfind(L'\\');
                            std::wstring parentPath = (sep != std::wstring::npos)
                                ? row.destination_path.substr(0, sep) : L"";
                            std::wstring fileName = (sep != std::wstring::npos)
                                ? row.destination_path.substr(sep)
                                : (L"\\" + row.destination_path);
                            auto it = pathToNode.find(parentPath);
                            if (it != pathToNode.end() && it->second) {
                                VirtualFolderFile vf;
                                vf.sourcePath    = row.source_path;
                                vf.destination   = fileName;
                                vf.install_scope = row.install_scope;
                                s_virtualFolderFiles[it->second].push_back(vf);
                            }
                        }
                    }
                    TreeView_Expand(s_hTreeView, s_hProgramFilesRoot, TVE_EXPAND);
                    ExpandAllSubnodes(s_hTreeView, s_hProgramFilesRoot);
                    if (s_hProgramDataRoot)  { TreeView_Expand(s_hTreeView, s_hProgramDataRoot,  TVE_EXPAND); ExpandAllSubnodes(s_hTreeView, s_hProgramDataRoot);  }
                    if (s_hAppDataRoot)      { TreeView_Expand(s_hTreeView, s_hAppDataRoot,       TVE_EXPAND); ExpandAllSubnodes(s_hTreeView, s_hAppDataRoot);       }
                    if (s_hAskAtInstallRoot) { TreeView_Expand(s_hTreeView, s_hAskAtInstallRoot,  TVE_EXPAND); ExpandAllSubnodes(s_hTreeView, s_hAskAtInstallRoot);  }
                }
            }
            if (!rebuiltFromDb && !s_currentProject.directory.empty()) {
                PopulateTreeView(s_hTreeView, s_currentProject.directory, defaultPath);
                ExpandAllSubnodes(s_hTreeView, s_hProgramFilesRoot);
            }
        }
        // Subclass TreeView so we can show tooltip on hover
        if (s_hTreeView) {
            s_prevTreeProc = (WNDPROC)SetWindowLongPtrW(s_hTreeView, GWLP_WNDPROC, (LONG_PTR)TreeView_SubclassProc);
        }

        // Register drag-and-drop module for TreeView and ListView.
        {
            DragDropConfig ddcfg = {};
            ddcfg.hwndParent   = hwnd;
            ddcfg.hwndTreeView = s_hTreeView;
            ddcfg.hwndListView = s_hListView;

            ddcfg.canStartTreeDrag = [](HTREEITEM h) -> bool {
                return h != s_hProgramFilesRoot && h != s_hProgramDataRoot
                    && h != s_hAppDataRoot      && h != s_hAskAtInstallRoot;
            };

            ddcfg.canStartListDrag = [](int iItem) -> bool {
                HTREEITEM hSel = TreeView_GetSelection(s_hTreeView);
                if (!hSel) return false;
                auto it = s_virtualFolderFiles.find(hSel);
                return (it != s_virtualFolderFiles.end() &&
                        iItem >= 0 && iItem < (int)it->second.size());
            };

            ddcfg.isValidDrop = [](HTREEITEM hDrop) -> bool {
                if (!hDrop) return false;
                // Ask-at-install root is never a valid drop target (it is a flag node,
                // not a real install-path folder).
                if (hDrop == s_hAskAtInstallRoot) return false;
                // The four section roots accept folder drags but not file (ListView) drags.
                bool isSectionRoot = (hDrop == s_hProgramFilesRoot
                                   || hDrop == s_hProgramDataRoot
                                   || hDrop == s_hAppDataRoot);
                if (isSectionRoot && DragDrop_GetSource() == DragSourceKind::ListView) return false;
                if (DragDrop_GetSource() == DragSourceKind::TreeView) {
                    HTREEITEM hSrc = DragDrop_GetTreeItem();
                    if (!hSrc)                                              return false;
                    if (hDrop == hSrc)                                      return false;
                    if (DragDrop_IsDescendant(s_hTreeView, hSrc, hDrop))    return false;
                    if (hDrop == TreeView_GetParent(s_hTreeView, hSrc))     return false;
                }
                if (DragDrop_GetSource() == DragSourceKind::ListView) {
                    if (hDrop == TreeView_GetSelection(s_hTreeView))        return false;
                }
                return true;
            };

            ddcfg.onDrop = [hwnd](HTREEITEM hDrop) {
                if (DragDrop_GetSource() == DragSourceKind::TreeView) {
                    HTREEITEM hOld       = DragDrop_GetTreeItem();
                    HTREEITEM hOldParent = TreeView_GetParent(s_hTreeView, hOld);
                    bool wasUnderPF = (hOldParent == s_hProgramFilesRoot);

                    // ── Get the dragged folder's name ─────────────────────────
                    wchar_t draggedName[260] = {};
                    { TVITEMW tvi = {}; tvi.hItem = hOld; tvi.mask = TVIF_TEXT;
                      tvi.pszText = draggedName; tvi.cchTextMax = 260;
                      TreeView_GetItem(s_hTreeView, &tvi); }

                    // ── Look for a same-named child at the drop target ────────
                    HTREEITEM hExisting = NULL;
                    { HTREEITEM hCh = TreeView_GetChild(s_hTreeView, hDrop);
                      while (hCh) {
                          wchar_t chName[260] = {};
                          TVITEMW tvi2 = {}; tvi2.hItem = hCh; tvi2.mask = TVIF_TEXT;
                          tvi2.pszText = chName; tvi2.cchTextMax = 260;
                          TreeView_GetItem(s_hTreeView, &tvi2);
                          if (hCh != hOld && lstrcmpiW(chName, draggedName) == 0)
                              { hExisting = hCh; break; }
                          hCh = TreeView_GetNextSibling(s_hTreeView, hCh);
                      } }

                    HTREEITEM hResultItem = NULL;

                    if (hExisting) {
                        // ── Duplicate name: ask Merge / Overwrite / Cancel ────
                        auto LS = [&](const wchar_t* k, const wchar_t* fb) -> std::wstring {
                            auto it = s_locale.find(k);
                            return (it != s_locale.end()) ? it->second : fb;
                        };
                        std::wstring title   = LS(L"merge_title",   L"Folder Already Exists");
                        std::wstring instr   = LS(L"merge_instr",   L"A folder with this name already exists");
                        std::wstring content = LS(L"merge_content", L"A folder named \"%s\" already exists at the target location.");
                        { auto p = content.find(L"%s"); if (p != std::wstring::npos) content.replace(p, 2, draggedName); }
                        std::wstring btnMerge = LS(L"merge_btn_merge",     L"Merge");
                        std::wstring btnOvwr  = LS(L"merge_btn_overwrite", L"Overwrite");
                        std::wstring btnCnl   = LS(L"cancel",              L"Cancel");

                        TASKDIALOG_BUTTON btns[] = {
                            { 101, btnMerge.c_str() },
                            { 102, btnOvwr.c_str()  },
                            { 103, btnCnl.c_str()   },
                        };
                        TASKDIALOGCONFIG tdc = {};
                        tdc.cbSize             = sizeof(tdc);
                        tdc.hwndParent         = hwnd;
                        tdc.dwFlags            = TDF_POSITION_RELATIVE_TO_WINDOW;
                        tdc.pszWindowTitle     = title.c_str();
                        tdc.pszMainInstruction = instr.c_str();
                        tdc.pszContent         = content.c_str();
                        tdc.pButtons           = btns;
                        tdc.cButtons           = 3;
                        tdc.nDefaultButton     = 101;
                        int clickedBtn = 103;
                        TaskDialogIndirect(&tdc, &clickedBtn, NULL, NULL);

                        if (clickedBtn == 101) {
                            // Merge: first transfer files directly owned by hOld into
                            // hExisting (CloneTreeSubtree only processes tree children,
                            // not the s_virtualFolderFiles of the folders themselves).

                            // If hOld is a real-path node its files are on disk,
                            // not in s_virtualFolderFiles — ingest them now.
                            {
                                wchar_t oldPathBuf[MAX_PATH] = {};
                                TVITEMW tviOld = {};
                                tviOld.hItem      = hOld;
                                tviOld.mask       = TVIF_PARAM;
                                tviOld.pszText    = oldPathBuf;
                                tviOld.cchTextMax = MAX_PATH;
                                TreeView_GetItem(s_hTreeView, &tviOld);
                                if (tviOld.lParam) {
                                    const wchar_t* rp = (const wchar_t*)tviOld.lParam;
                                    if (rp && wcslen(rp) > 0)
                                        IngestRealPathFiles(hExisting, rp);
                                }
                            }

                            auto itOldFiles = s_virtualFolderFiles.find(hOld);
                            if (itOldFiles != s_virtualFolderFiles.end()) {
                                auto& dest = s_virtualFolderFiles[hExisting];
                                dest.insert(dest.end(),
                                    itOldFiles->second.begin(),
                                    itOldFiles->second.end());
                                s_virtualFolderFiles.erase(itOldFiles);
                            }
                            // Then move every subfolder of hOld into hExisting.
                            HTREEITEM hCh = TreeView_GetChild(s_hTreeView, hOld);
                            while (hCh) {
                                HTREEITEM hNext = TreeView_GetNextSibling(s_hTreeView, hCh);
                                CloneTreeSubtree(s_hTreeView, hCh, hExisting);
                                hCh = hNext;
                            }
                            TreeView_DeleteItem(s_hTreeView, hOld);
                            hResultItem = hExisting;
                        } else if (clickedBtn == 102) {
                            // Overwrite: delete existing, then normal clone
                            TreeView_DeleteItem(s_hTreeView, hExisting);
                            hResultItem = CloneTreeSubtree(s_hTreeView, hOld, hDrop);
                            TreeView_DeleteItem(s_hTreeView, hOld);
                        } else {
                            return;  // Cancel — no drop
                        }
                    } else {
                        // Normal move — no name conflict
                        hResultItem = CloneTreeSubtree(s_hTreeView, hOld, hDrop);
                        TreeView_DeleteItem(s_hTreeView, hOld);
                    }

                    TreeView_Expand(s_hTreeView, hDrop, TVE_EXPAND);
                    if (hResultItem) {
                        TreeView_SelectItem(s_hTreeView, hResultItem);
                        ForceRefreshListView(s_hListView, hResultItem);
                    }
                    if (wasUnderPF) UpdateInstallPathFromTree(hwnd);
                    MarkAsModified();
                    UpdateComponentsButtonState(hwnd);
                } else if (DragDrop_GetSource() == DragSourceKind::ListView) {
                    HTREEITEM hSrc = TreeView_GetSelection(s_hTreeView);
                    auto it = s_virtualFolderFiles.find(hSrc);
                    int idx = DragDrop_GetListIndex();
                    if (it != s_virtualFolderFiles.end() &&
                        idx >= 0 && idx < (int)it->second.size()) {
                        VirtualFolderFile vf = it->second[idx];
                        it->second.erase(it->second.begin() + idx);
                        s_virtualFolderFiles[hDrop].push_back(vf);
                        TreeView_Expand(s_hTreeView, hDrop, TVE_EXPAND);
                        TreeView_SelectItem(s_hTreeView, hDrop);
                        MarkAsModified();
                    }
                }
            };

            DragDrop_Register(ddcfg);
        }
        
        // Sync Components button: only upgrade (never downgrade) so navigating back
        // to a temporarily-empty tree doesn't disable an already-enabled button.
        {
            bool treeHasContent = false;
            if (!treeHasContent) {
                HTREEITEM roots[] = { s_hProgramFilesRoot, s_hProgramDataRoot,
                                      s_hAppDataRoot, s_hAskAtInstallRoot };
                for (HTREEITEM hR : roots) {
                    if (hR && TreeView_GetChild(s_hTreeView, hR)) { treeHasContent = true; break; }
                }
            }
            if (treeHasContent) s_filesPageHasContent = true;
            HWND hBtn = GetDlgItem(hwnd, IDC_TB_COMPONENTS);
            if (hBtn) EnableWindow(hBtn, s_filesPageHasContent ? TRUE : FALSE);
        }
        break;
    }
    case 1: // Registry page
    {
        // Get localized strings
        auto itRegTitle = s_locale.find(L"reg_page_title");
        std::wstring regTitle = (itRegTitle != s_locale.end()) ? itRegTitle->second : L"Registry Management";
        
        auto itRegRegister = s_locale.find(L"reg_register_app");
        std::wstring regRegister = (itRegRegister != s_locale.end()) ? itRegRegister->second : L"Register in Windows Installed Programs";
        
        auto itAddIcon = s_locale.find(L"reg_add_icon");
        std::wstring addIcon = (itAddIcon != s_locale.end()) ? itAddIcon->second : L"Add Icon";
        
        auto itAddIconTT = s_locale.find(L"reg_add_icon_tooltip");
        std::wstring addIconTT = (itAddIconTT != s_locale.end()) ? itAddIconTT->second : L"Add icon to show in Installed apps in Windows settings (.ico)";
        
        auto itDisplayName = s_locale.find(L"reg_display_name");
        std::wstring displayName = (itDisplayName != s_locale.end()) ? itDisplayName->second : L"Display Name:";
        
        auto itVersion = s_locale.find(L"reg_version");
        std::wstring versionText = (itVersion != s_locale.end()) ? itVersion->second : L"Version:";
        
        auto itPublisher = s_locale.find(L"reg_publisher");
        std::wstring publisherText = (itPublisher != s_locale.end()) ? itPublisher->second : L"Publisher:";
        
        auto itAddKey = s_locale.find(L"reg_add_key");
        std::wstring addKeyText = (itAddKey != s_locale.end()) ? itAddKey->second : L"Add Key";
        
        auto itAddValue = s_locale.find(L"reg_add_value");
        std::wstring addValueText = (itAddValue != s_locale.end()) ? itAddValue->second : L"Add Value";
        
        auto itEdit = s_locale.find(L"reg_edit");
        std::wstring editText = (itEdit != s_locale.end()) ? itEdit->second : L"Edit";
        
        auto itRemove = s_locale.find(L"reg_remove");
        std::wstring removeText = (itRemove != s_locale.end()) ? itRemove->second : L"Delete";
        
        // H3 headline
        HWND hTitle = CreateWindowExW(0, L"STATIC", regTitle.c_str(),
            WS_CHILD | WS_VISIBLE | SS_LEFT,
            S(20), pageY + S(15), rc.right - S(40), S(38),
            hwnd, (HMENU)5100, hInst, NULL);
        if (s_hPageTitleFont) SendMessageW(hTitle, WM_SETFONT, (WPARAM)s_hPageTitleFont, TRUE);
        
        int currentY = pageY + S(55);
        
        // Checkbox for "Register in Windows Installed Programs"
        HWND hCheckbox = CreateWindowExW(0, L"BUTTON", regRegister.c_str(),
            WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
            S(20), currentY, S(400), S(25),
            hwnd, (HMENU)IDC_REG_CHECKBOX, hInst, NULL);
        if (s_scaledFont) SendMessageW(hCheckbox, WM_SETFONT, (WPARAM)s_scaledFont, TRUE);
        SendMessageW(hCheckbox, BM_SETCHECK, s_registerInWindows ? BST_CHECKED : BST_UNCHECKED, 0);
        currentY += S(35);
        
        // Layout: Icon + Buttons on left, Fields on right
        // Icon preview area (48x48 static control with border)
        HWND hIconPreview = CreateWindowExW(WS_EX_CLIENTEDGE, L"STATIC", L"",
            WS_CHILD | WS_VISIBLE | SS_ICON | SS_CENTERIMAGE,
            S(20), currentY, S(48), S(48),
            hwnd, (HMENU)IDC_REG_ICON_PREVIEW, hInst, NULL);
        
        // Load default generic icon from shell32.dll (icon #2 - generic file icon)
        if (hIconPreview) {
            HICON hDefaultIcon = ExtractIconW(hInst, L"shell32.dll", 2);
            if (hDefaultIcon) {
                SendMessageW(hIconPreview, STM_SETICON, (WPARAM)hDefaultIcon, 0);
            }
        }
        
        // Add Icon button (with shell32.dll icon #127)
        int wRegAddIcon = MeasureButtonWidth(addIcon, true);
        s_hPageButton1 = CreateCustomButtonWithIcon(hwnd, IDC_REG_ADD_ICON, addIcon.c_str(), ButtonColor::Blue,
            L"shell32.dll", 127, S(80), currentY, wRegAddIcon, S(30), hInst);
        
        // Add tooltip for Add Icon button
        if (s_hTooltip) {
            TOOLINFOW ti = {};
            ti.cbSize = sizeof(TOOLINFOW);
            ti.uFlags = TTF_IDISHWND | TTF_SUBCLASS;
            ti.hwnd = hwnd;
            ti.uId = (UINT_PTR)s_hPageButton1;
            ti.lpszText = (LPWSTR)addIconTT.c_str();
            SendMessageW(s_hTooltip, TTM_ADDTOOL, 0, (LPARAM)&ti);
        }
        
        // Show Regkey button (with shell32.dll icon #268) below Add Icon
        auto itShowRegkey = s_locale.find(L"reg_show_regkey");
        std::wstring showRegkeyText = (itShowRegkey != s_locale.end()) ? itShowRegkey->second : L"Show Regkey";
        
        int wRegShowKey = MeasureButtonWidth(showRegkeyText, true);
        CreateCustomButtonWithIcon(hwnd, IDC_REG_SHOW_REGKEY, showRegkeyText.c_str(), ButtonColor::Blue,
            L"shell32.dll", 268, S(80), currentY + S(35), wRegShowKey, S(30), hInst);
        
        // Display Name field (right side, aligned with icon)
        { HWND hLbl5101 = CreateWindowExW(0, L"STATIC", displayName.c_str(),
            WS_CHILD | WS_VISIBLE | SS_LEFT | SS_CENTERIMAGE,
            S(220), currentY, S(100), S(22),
            hwnd, (HMENU)5101, hInst, NULL);
          if (s_scaledFont && hLbl5101) SendMessageW(hLbl5101, WM_SETFONT, (WPARAM)s_scaledFont, TRUE); }
        
        HWND hDisplayNameEdit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", s_currentProject.name.c_str(),
            WS_CHILD | WS_VISIBLE | WS_BORDER | ES_LEFT | ES_AUTOHSCROLL,
            S(325), currentY, S(300), S(22),
            hwnd, (HMENU)IDC_REG_DISPLAY_NAME, hInst, NULL);
        if (s_scaledFont && hDisplayNameEdit) SendMessageW(hDisplayNameEdit, WM_SETFONT, (WPARAM)s_scaledFont, TRUE);
        currentY += S(27);
        
        // Version field
        { HWND hLbl5102 = CreateWindowExW(0, L"STATIC", versionText.c_str(),
            WS_CHILD | WS_VISIBLE | SS_LEFT | SS_CENTERIMAGE,
            S(220), currentY, S(100), S(22),
            hwnd, (HMENU)5102, hInst, NULL);
          if (s_scaledFont && hLbl5102) SendMessageW(hLbl5102, WM_SETFONT, (WPARAM)s_scaledFont, TRUE); }
        
        HWND hVersionEdit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", s_currentProject.version.c_str(),
            WS_CHILD | WS_VISIBLE | WS_BORDER | ES_LEFT | ES_AUTOHSCROLL,
            S(325), currentY, S(300), S(22),
            hwnd, (HMENU)IDC_REG_VERSION, hInst, NULL);
        if (s_scaledFont && hVersionEdit) SendMessageW(hVersionEdit, WM_SETFONT, (WPARAM)s_scaledFont, TRUE);
        currentY += S(27);
        
        // Publisher field
        { HWND hLbl5103 = CreateWindowExW(0, L"STATIC", publisherText.c_str(),
            WS_CHILD | WS_VISIBLE | SS_LEFT | SS_CENTERIMAGE,
            S(220), currentY, S(100), S(22),
            hwnd, (HMENU)5103, hInst, NULL);
          if (s_scaledFont && hLbl5103) SendMessageW(hLbl5103, WM_SETFONT, (WPARAM)s_scaledFont, TRUE); }
        
        HWND hPublisherEdit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", s_appPublisher.c_str(),
            WS_CHILD | WS_VISIBLE | WS_BORDER | ES_LEFT | ES_AUTOHSCROLL,
            S(325), currentY, S(300), S(22),
            hwnd, (HMENU)IDC_REG_PUBLISHER, hInst, NULL);
        if (s_scaledFont && hPublisherEdit) SendMessageW(hPublisherEdit, WM_SETFONT, (WPARAM)s_scaledFont, TRUE);
        currentY += S(40);
        
        // Divider line
        CreateWindowExW(0, L"STATIC", L"",
            WS_CHILD | WS_VISIBLE | SS_ETCHEDHORZ,
            S(20), currentY, rc.right - S(40), 2,
            hwnd, (HMENU)5104, hInst, NULL);
        currentY += S(20);
        
        // Custom Registry Entries section with warning icon and backup button
        auto itCustReg = s_locale.find(L"custom_registry_entries");
        std::wstring custRegText = (itCustReg != s_locale.end()) ? itCustReg->second : L"Custom Registry Entries";
        { HWND hLbl5105 = CreateWindowExW(0, L"STATIC", custRegText.c_str(),
            WS_CHILD | WS_VISIBLE | SS_LEFT,
            S(20), currentY, S(300), S(20),
            hwnd, (HMENU)5105, hInst, NULL);
          if (s_scaledFont && hLbl5105) SendMessageW(hLbl5105, WM_SETFONT, (WPARAM)s_scaledFont, TRUE); }
        
        // Backup Registry button (icon 238 from shell32.dll) - Create Restore Point
        auto itBackup = s_locale.find(L"reg_backup");
        std::wstring backupText = (itBackup != s_locale.end()) ? itBackup->second : L"Create Restore Point";
        
        CreateCustomButtonWithIcon(hwnd, IDC_REG_BACKUP, backupText.c_str(), ButtonColor::Green,
            L"shell32.dll", 238, rc.right - S(190), currentY, S(170), S(40), hInst);
        
        // Warning icon (imageres.dll icon 244) - positioned just to left of backup button
        HWND hWarningIcon = CreateWindowExW(0, L"STATIC", L"",
            WS_CHILD | WS_VISIBLE | SS_ICON | SS_CENTERIMAGE | SS_NOTIFY,
            rc.right - S(230), currentY + S(4), S(32), S(32),
            hwnd, (HMENU)IDC_REG_WARNING_ICON, hInst, NULL);
        
        // Load and set warning icon from imageres.dll
        HICON hWarnIcon = NULL;
        HMODULE hImageres = LoadLibraryW(L"imageres.dll");
        if (hImageres) {
            // Try loading with LoadImageW for better size control
            hWarnIcon = (HICON)LoadImageW(hImageres, MAKEINTRESOURCEW(244), IMAGE_ICON, S(24), S(24), LR_DEFAULTCOLOR);
            if (!hWarnIcon) {
                // Fallback to LoadIconW
                hWarnIcon = LoadIconW(hImageres, MAKEINTRESOURCEW(244));
            }
            FreeLibrary(hImageres);
        }
        // Fallback to system warning icon if loading from imageres failed
        if (!hWarnIcon) {
            hWarnIcon = LoadIconW(NULL, IDI_WARNING);
        }
        if (hWarnIcon) {
            SendMessageW(hWarningIcon, STM_SETICON, (WPARAM)hWarnIcon, 0);
        }

        // Subclass the warning icon so we can get per-control WM_MOUSELEAVE
        s_prevWarnIconProc = (WNDPROC)SetWindowLongPtrW(hWarningIcon, GWLP_WNDPROC, (LONG_PTR)WarningIcon_SubclassProc);
        
        currentY += S(48);
        
        // Split pane: TreeView (left 40%) + ListView (right 60%)
        int treeWidth = (int)((rc.right - S(40)) * 0.4);
        int listWidth = (rc.right - S(40)) - treeWidth - S(10);
        int paneHeight = rc.bottom - currentY - S(90); // Space for bottom buttons
        
        // TreeView for registry hive structure with horizontal scrolling
        s_hRegTreeView = CreateWindowExW(WS_EX_CLIENTEDGE, WC_TREEVIEW, L"",
            WS_CHILD | WS_VISIBLE | WS_BORDER | TVS_HASLINES | TVS_HASBUTTONS | TVS_LINESATROOT | TVS_SHOWSELALWAYS | WS_VSCROLL | WS_HSCROLL,
            S(20), currentY, treeWidth, paneHeight,
            hwnd, (HMENU)IDC_REG_TREEVIEW, hInst, NULL);
        if (s_scaledFont) SendMessageW(s_hRegTreeView, WM_SETFONT, (WPARAM)s_scaledFont, TRUE);
        
        // Populate TreeView with template registry structure
        CreateTemplateRegistryTree(s_hRegTreeView, s_currentProject.name, s_appPublisher, 
                                   s_currentProject.version, s_currentProject.directory, s_appIconPath);
        
        // ListView for registry entries with horizontal scrolling
        s_hRegListView = CreateWindowExW(WS_EX_CLIENTEDGE, WC_LISTVIEW, L"",
            WS_CHILD | WS_VISIBLE | WS_BORDER | LVS_REPORT | LVS_SINGLESEL | WS_VSCROLL | WS_HSCROLL,
            S(30) + treeWidth, currentY, listWidth, paneHeight,
            hwnd, (HMENU)IDC_REG_LISTVIEW, hInst, NULL);
        if (s_scaledFont) SendMessageW(s_hRegListView, WM_SETFONT, (WPARAM)s_scaledFont, TRUE);
        { HWND hRegLvHdr = ListView_GetHeader(s_hRegListView); if (hRegLvHdr && s_scaledFont) SendMessageW(hRegLvHdr, WM_SETFONT, (WPARAM)s_scaledFont, TRUE); }
        
        // Set extended ListView styles
        ListView_SetExtendedListViewStyle(s_hRegListView, LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES);
        
        // Add columns
        auto itColName = s_locale.find(L"reg_col_name");
        std::wstring colName = (itColName != s_locale.end()) ? itColName->second : L"Name";
        
        auto itColType = s_locale.find(L"reg_col_type");
        std::wstring colType = (itColType != s_locale.end()) ? itColType->second : L"Type";
        
        auto itColData = s_locale.find(L"reg_col_data");
        std::wstring colData = (itColData != s_locale.end()) ? itColData->second : L"Data";
        
        LVCOLUMNW lvc = {};
        lvc.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_FMT;
        lvc.fmt = LVCFMT_LEFT;
        
        lvc.pszText = (LPWSTR)colName.c_str();
        lvc.cx = (int)(listWidth * 0.35);
        ListView_InsertColumn(s_hRegListView, 0, &lvc);
        
        lvc.pszText = (LPWSTR)colType.c_str();
        lvc.cx = (int)(listWidth * 0.25);
        ListView_InsertColumn(s_hRegListView, 1, &lvc);
        
        lvc.pszText = (LPWSTR)colData.c_str();
        lvc.cx = (int)(listWidth * 0.40);
        ListView_InsertColumn(s_hRegListView, 2, &lvc);
        
        currentY += paneHeight + S(10);
        
        // Buttons: Add Key, Add Value, Edit, Delete
        {
            const int rGap = S(10);
            int rw0 = MeasureButtonWidth(addKeyText,   true);
            int rw1 = MeasureButtonWidth(addValueText, true);
            int rw2 = MeasureButtonWidth(editText,     true);
            int rw3 = MeasureButtonWidth(removeText,   true);
            int rx1 = S(20) + rw0 + rGap;
            int rx2 = rx1   + rw1 + rGap;
            int rx3 = rx2   + rw2 + rGap;
            s_hPageButton2 = CreateCustomButtonWithIcon(hwnd, IDC_REG_ADD_KEY, addKeyText.c_str(), ButtonColor::Blue,
                L"shell32.dll", 4, S(20), currentY, rw0, S(35), hInst);
            CreateCustomButtonWithIcon(hwnd, IDC_REG_ADD_VALUE, addValueText.c_str(), ButtonColor::Blue,
                L"shell32.dll", 70, rx1, currentY, rw1, S(35), hInst);
            CreateCustomButtonWithIcon(hwnd, IDC_REG_EDIT, editText.c_str(), ButtonColor::Blue,
                L"shell32.dll", 269, rx2, currentY, rw2, S(35), hInst);
            CreateCustomButtonWithIcon(hwnd, IDC_REG_REMOVE, removeText.c_str(), ButtonColor::Red,
                L"shell32.dll", 234, rx3, currentY, rw3, S(35), hInst);
        }
        
        // Create tooltip for warning icon - removed (using custom tooltip instead)
        
        break;
    }
    case 2: // Shortcuts page — delegated to the shortcuts module
    {
        s_scPageContentH = SC_BuildPage(hwnd, hInst, pageY, rc.right, s_hPageTitleFont, s_hGuiFont, s_locale);
        // Add a vertical scrollbar when the content is taller than the view.
        {
            // Use the actual status-bar height so the scroll rect never overlaps it.
            int statusH = 25;
            if (s_hStatus && IsWindow(s_hStatus)) {
                RECT rcSB; GetWindowRect(s_hStatus, &rcSB);
                statusH = rcSB.bottom - rcSB.top;
            }
            int viewH    = rc.bottom - pageY - statusH;
            int contentH = s_scPageContentH - pageY + S(15);  // +15 px gap at bottom
            if (contentH > viewH) {
                LONG ws = GetWindowLongW(hwnd, GWL_STYLE);
                if (!(ws & WS_VSCROLL)) {
                    SetWindowLongW(hwnd, GWL_STYLE, ws | WS_VSCROLL);
                    SetWindowPos(hwnd, NULL, 0, 0, 0, 0,
                        SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_FRAMECHANGED);
                }
                SCROLLINFO si = {};
                si.cbSize = sizeof(si);
                si.fMask  = SIF_RANGE | SIF_PAGE | SIF_POS | SIF_DISABLENOSCROLL;
                si.nMin   = 0;
                si.nMax   = contentH - 1;
                si.nPage  = (UINT)viewH;
                si.nPos   = 0;
                SetScrollInfo(hwnd, SB_VERT, &si, TRUE);
            }
        }
        // Give every page control WS_CLIPSIBLINGS so that when any of them
        // scrolls into the status-bar zone it does NOT paint over the status
        // bar.  WS_CLIPSIBLINGS makes a window skip painting in any area
        // covered by a sibling window, so the status bar (always kept at
        // HWND_TOP) will always appear on top regardless of Z-order timing.
        {
            HWND hC = GetWindow(hwnd, GW_CHILD);
            while (hC) {
                HWND hN = GetWindow(hC, GW_HWNDNEXT);
                if (hC != s_hStatus && hC != s_hAboutButton) {
                    int cid = GetDlgCtrlID(hC);
                    bool isTB = (cid >= IDC_TB_FILES && cid <= IDC_TB_ABOUT) ||
                                 cid == IDC_TB_DIALOGS || cid == IDC_TB_COMPONENTS ||
                                 cid == IDC_TB_EXIT    || cid == IDC_TB_CLOSE_PROJECT;
                    if (!isTB) {
                        LONG_PTR st = GetWindowLongPtrW(hC, GWL_STYLE);
                        if (!(st & WS_CLIPSIBLINGS))
                            SetWindowLongPtrW(hC, GWL_STYLE, st | WS_CLIPSIBLINGS);
                    }
                }
                hC = hN;
            }
        }
        break;
    }
    case 3: // Dependencies page
    {
        DEP_BuildPage(hwnd, hInst, pageY, rc.right,
                      s_hPageTitleFont, s_hGuiFont, s_locale);
        break;
    }
    case 4: // Dialogs page
    {
        s_idlgPageContentH = IDLG_BuildPage(hwnd, hInst, pageY, rc.right,
                               s_hPageTitleFont, s_hGuiFont, s_locale);
        // Add a vertical scrollbar when the content is taller than the view.
        {
            int statusH = 25;
            if (s_hStatus && IsWindow(s_hStatus)) {
                RECT rcSB; GetWindowRect(s_hStatus, &rcSB);
                statusH = rcSB.bottom - rcSB.top;
            }
            int viewH    = rc.bottom - pageY - statusH;
            int contentH = s_idlgPageContentH - pageY + S(15);  // +15 px gap at bottom
            if (contentH > viewH) {
                LONG ws = GetWindowLongW(hwnd, GWL_STYLE);
                if (!(ws & WS_VSCROLL)) {
                    SetWindowLongW(hwnd, GWL_STYLE, ws | WS_VSCROLL);
                    SetWindowPos(hwnd, NULL, 0, 0, 0, 0,
                        SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_FRAMECHANGED);
                }
                SCROLLINFO si = {};
                si.cbSize = sizeof(si);
                si.fMask  = SIF_RANGE | SIF_PAGE | SIF_POS | SIF_DISABLENOSCROLL;
                si.nMin   = 0;
                si.nMax   = contentH - 1;
                si.nPage  = (UINT)viewH;
                si.nPos   = 0;
                SetScrollInfo(hwnd, SB_VERT, &si, TRUE);
            }
        }
        // WS_CLIPSIBLINGS on every page control so scrolled controls can't
        // overdraw the status bar (kept at HWND_TOP).
        {
            HWND hC = GetWindow(hwnd, GW_CHILD);
            while (hC) {
                HWND hN = GetWindow(hC, GW_HWNDNEXT);
                if (hC != s_hStatus && hC != s_hAboutButton) {
                    int cid = GetDlgCtrlID(hC);
                    bool isTB = (cid >= IDC_TB_FILES && cid <= IDC_TB_ABOUT) ||
                                 cid == IDC_TB_DIALOGS || cid == IDC_TB_COMPONENTS ||
                                 cid == IDC_TB_EXIT    || cid == IDC_TB_CLOSE_PROJECT;
                    if (!isTB) {
                        LONG_PTR st = GetWindowLongPtrW(hC, GWL_STYLE);
                        if (!(st & WS_CLIPSIBLINGS))
                            SetWindowLongPtrW(hC, GWL_STYLE, st | WS_CLIPSIBLINGS);
                    }
                }
                hC = hN;
            }
        }
        break;
    }
    case 5: // Settings page
    {
        // Create container for page content
        s_hCurrentPage = CreateWindowExW(
            0, L"STATIC", L"",
            WS_CHILD | WS_VISIBLE,
            0, pageY, rc.right, pageHeight,
            hwnd, NULL, hInst, NULL);
        
        HWND hTitle = CreateWindowExW(0, L"STATIC", L"Installer Settings",
            WS_CHILD | WS_VISIBLE | SS_LEFT,
            S(20), S(20), rc.right - S(40), S(38),
            s_hCurrentPage, NULL, hInst, NULL);
        if (s_hPageTitleFont) SendMessageW(hTitle, WM_SETFONT, (WPARAM)s_hPageTitleFont, TRUE);
        
        CreateWindowExW(0, L"STATIC", L"Settings (License, OS requirements, etc.) to be implemented",
            WS_CHILD | WS_VISIBLE | SS_LEFT,
            S(20), S(60), rc.right - S(40), S(20),
            s_hCurrentPage, NULL, hInst, NULL);
        break;
    }
    case 6: // Build page
    {
        // Create container for page content
        s_hCurrentPage = CreateWindowExW(
            0, L"STATIC", L"",
            WS_CHILD | WS_VISIBLE,
            0, pageY, rc.right, pageHeight,
            hwnd, NULL, hInst, NULL);
        
        HWND hTitle = CreateWindowExW(0, L"STATIC", L"Build Installer",
            WS_CHILD | WS_VISIBLE | SS_LEFT,
            S(20), S(20), rc.right - S(40), S(38),
            s_hCurrentPage, NULL, hInst, NULL);
        if (s_hPageTitleFont) SendMessageW(hTitle, WM_SETFONT, (WPARAM)s_hPageTitleFont, TRUE);
        
        CreateWindowExW(0, L"STATIC", L"Build/compile functionality to be implemented",
            WS_CHILD | WS_VISIBLE | SS_LEFT,
            S(20), S(60), rc.right - S(40), S(20),
            s_hCurrentPage, NULL, hInst, NULL);
        break;
    }
    case 7: // Test page
    {
        // Create container for page content
        s_hCurrentPage = CreateWindowExW(
            0, L"STATIC", L"",
            WS_CHILD | WS_VISIBLE,
            0, pageY, rc.right, pageHeight,
            hwnd, NULL, hInst, NULL);
        
        HWND hTitle = CreateWindowExW(0, L"STATIC", L"Test Installer",
            WS_CHILD | WS_VISIBLE | SS_LEFT,
            S(20), S(20), rc.right - S(40), S(38),
            s_hCurrentPage, NULL, hInst, NULL);
        if (s_hPageTitleFont) SendMessageW(hTitle, WM_SETFONT, (WPARAM)s_hPageTitleFont, TRUE);
        
        CreateWindowExW(0, L"STATIC", L"Test functionality to be implemented",
            WS_CHILD | WS_VISIBLE | SS_LEFT,
            S(20), S(60), rc.right - S(40), S(20),
            s_hCurrentPage, NULL, hInst, NULL);
        break;
    }
    case 8: // Scripts page
    {
        // Create container for page content
        s_hCurrentPage = CreateWindowExW(
            0, L"STATIC", L"",
            WS_CHILD | WS_VISIBLE,
            0, pageY, rc.right, pageHeight,
            hwnd, NULL, hInst, NULL);
        
        HWND hTitle = CreateWindowExW(0, L"STATIC", L"Run Scripts",
            WS_CHILD | WS_VISIBLE | SS_LEFT,
            S(20), S(20), rc.right - S(40), S(38),
            s_hCurrentPage, NULL, hInst, NULL);
        if (s_hPageTitleFont) SendMessageW(hTitle, WM_SETFONT, (WPARAM)s_hPageTitleFont, TRUE);
        
        CreateWindowExW(0, L"STATIC", L"Configure scripts and executables to run before/after installation",
            WS_CHILD | WS_VISIBLE | SS_LEFT,
            S(20), S(60), rc.right - S(40), S(20),
            s_hCurrentPage, NULL, hInst, NULL);
        break;
    }
    case 9: // Components page - direct children of hwnd (no container)
    {
        // Title
        auto itCompTitle = s_locale.find(L"comp_page_title");
        std::wstring compTitle = (itCompTitle != s_locale.end()) ? itCompTitle->second : L"Components";
        HWND hCompTitle = CreateWindowExW(0, L"STATIC", compTitle.c_str(),
            WS_CHILD | WS_VISIBLE | SS_LEFT,
            S(20), pageY + S(15), rc.right - S(40), S(38),
            hwnd, (HMENU)5100, hInst, NULL);
        if (s_hPageTitleFont) SendMessageW(hCompTitle, WM_SETFONT, (WPARAM)s_hPageTitleFont, TRUE);

        // Enable-components checkbox — use custom themed checkbox (same as all others)
        auto itEnable = s_locale.find(L"comp_enable");
        std::wstring enableText = (itEnable != s_locale.end()) ? itEnable->second : L"Use component-based installation";
        HWND hCompEnable = CreateCustomCheckbox(hwnd, IDC_COMP_ENABLE, enableText,
            s_currentProject.use_components != 0,
            S(20), pageY + S(60), rc.right - S(40), S(28), hInst);

        // Hint label
        auto itHint = s_locale.find(L"comp_enable_hint");
        std::wstring hintText = (itHint != s_locale.end()) ? itHint->second : L"When unchecked, all files are installed as one complete package.";
        HWND hHintLabel = CreateWindowExW(0, L"STATIC", hintText.c_str(),
            WS_CHILD | WS_VISIBLE | SS_LEFT,
            S(20), pageY + S(94), rc.right - S(74), S(22),
            hwnd, NULL, hInst, NULL);

        // Separator
        CreateWindowExW(0, L"STATIC", L"",
            WS_CHILD | WS_VISIBLE | SS_ETCHEDHORZ,
            S(20), pageY + S(124), rc.right - S(40), S(2),
            hwnd, NULL, hInst, NULL);

        // Info icon to the right, vertically centred in the space above the separator.
        // shell32.dll icon #258 = floppy disk ("save" hint).
        // Do NOT use SS_ICON — it auto-resizes the control on STM_SETICON.
        // Instead use a plain SS_NOTIFY static, extract icon via PrivateExtractIconsW,
        // store in GWLP_USERDATA, and draw manually in WM_PAINT.
        {
            int iconSz = S(26);
            int iconX  = rc.right - S(20) - iconSz;
            int iconY  = pageY + S(93); // below checkbox (ends at pageY+S(88)), aligned with hint label row; hint label narrowed to not reach icon X
            HWND hInfo = CreateWindowExW(0, L"STATIC", NULL,
                WS_CHILD | WS_VISIBLE | SS_NOTIFY,
                iconX, iconY, iconSz, iconSz,
                hwnd, (HMENU)IDC_COMP_INFO_ICON, hInst, NULL);
            if (hInfo) {
                wchar_t shell32Path[MAX_PATH];
                GetSystemDirectoryW(shell32Path, MAX_PATH);
                wcscat_s(shell32Path, L"\\shell32.dll");
                HICON hIco = NULL;
                PrivateExtractIconsW(shell32Path, 258, iconSz, iconSz, &hIco, NULL, 1, 0);
                if (!hIco) ExtractIconExW(shell32Path, 258, NULL, &hIco, 1);
                // Store icon in GWLP_USERDATA so WM_PAINT can draw it at our exact size
                SetWindowLongPtrW(hInfo, GWLP_USERDATA, (LONG_PTR)hIco);
                s_prevCompInfoIconProc = (WNDPROC)SetWindowLongPtrW(
                    hInfo, GWLP_WNDPROC, (LONG_PTR)CompInfoIcon_SubclassProc);
            }
        }

        // Button row: Edit only
        auto itEditBtn = s_locale.find(L"comp_edit");
        std::wstring editTxt = (itEditBtn != s_locale.end()) ? itEditBtn->second : L"Edit";

        int wCompEdit = MeasureButtonWidth(editTxt, true);
        CreateCustomButtonWithIcon(hwnd, IDC_COMP_EDIT, editTxt.c_str(), ButtonColor::Blue,
            L"imageres.dll", 109, S(20), pageY + S(134), wCompEdit, S(34), hInst);

        // Split-pane: TreeView (left) + ListView (right), mirroring the Files page layout
        int splitY  = pageY + S(178);
        int splitH  = pageHeight - S(185);
        int totalW  = rc.right - S(40);
        int treeW   = S(270);
        int listX   = S(20) + treeW + S(8);
        int listW   = totalW - treeW - S(8);

        // Left pane: folder tree
        s_hCompTreeView = CreateWindowExW(WS_EX_CLIENTEDGE, WC_TREEVIEWW, L"",
            WS_CHILD | WS_VISIBLE | TVS_HASLINES | TVS_LINESATROOT |
            TVS_HASBUTTONS | TVS_SHOWSELALWAYS,
            S(20), splitY, treeW, splitH,
            hwnd, (HMENU)IDC_COMP_TREEVIEW, hInst, NULL);
        // Attach folder icons (16×16 from shell32.dll — indices 0=closed, 1=open)
        {
            HIMAGELIST hCompIL = ImageList_Create(32, 32, ILC_COLOR32 | ILC_MASK, 2, 2);
            if (hCompIL) {
                HMODULE hShell32 = LoadLibraryW(L"shell32.dll");
                if (hShell32) {
                    HICON hClose = (HICON)LoadImageW(hShell32, MAKEINTRESOURCEW(4), IMAGE_ICON, 32, 32, 0);
                    HICON hOpen  = (HICON)LoadImageW(hShell32, MAKEINTRESOURCEW(5), IMAGE_ICON, 32, 32, 0);
                    if (hClose) { ImageList_AddIcon(hCompIL, hClose); DestroyIcon(hClose); }
                    if (hOpen)  { ImageList_AddIcon(hCompIL, hOpen);  DestroyIcon(hOpen);  }
                    // Badge icon (blue circle) for AskAtInstall root — index 2
                    HICON hBadge = NULL;
                    {
                        HDC hdc = GetDC(NULL);
                        HBITMAP hBmp = CreateCompatibleBitmap(hdc, 32, 32);
                        HDC hMem = CreateCompatibleDC(hdc);
                        HBITMAP hOld = (HBITMAP)SelectObject(hMem, hBmp);
                        HBRUSH hCircle = CreateSolidBrush(RGB(65,105,225));
                        HBRUSH hOldBrush = (HBRUSH)SelectObject(hMem, hCircle);
                        Ellipse(hMem, 6, 6, 26, 26);
                        SelectObject(hMem, hOldBrush);
                        DeleteObject(hCircle);
                        ICONINFO ii = {};
                        ii.fIcon = TRUE;
                        ii.hbmMask = hBmp;
                        ii.hbmColor = hBmp;
                        hBadge = CreateIconIndirect(&ii);
                        SelectObject(hMem, hOld);
                        DeleteDC(hMem);
                        ReleaseDC(NULL, hdc);
                    }
                    if (hBadge) { ImageList_AddIcon(hCompIL, hBadge); DestroyIcon(hBadge); }
                    // Required-folder icon: shell32.dll sequential index 110
                    // (yellow folder with blue checkmark badge — verified in IconViewer).
                    {
                        wchar_t shell32Path[MAX_PATH];
                        GetSystemDirectoryW(shell32Path, MAX_PATH);
                        wcscat_s(shell32Path, L"\\shell32.dll");
                        HICON hReq = NULL;
                        ExtractIconExW(shell32Path, 110, &hReq, NULL, 1);
                        if (hReq) { ImageList_AddIcon(hCompIL, hReq); DestroyIcon(hReq); }
                    }
                    FreeLibrary(hShell32);
                }
                TreeView_SetImageList(s_hCompTreeView, hCompIL, TVSIL_NORMAL);
                TreeView_SetItemHeight(s_hCompTreeView, 34);
                // Store for cleanup
                HIMAGELIST hOldIL = (HIMAGELIST)GetPropW(hwnd, L"hCompTreeIL");
                if (hOldIL) ImageList_Destroy(hOldIL);
                SetPropW(hwnd, L"hCompTreeIL", (HANDLE)hCompIL);
            }
            // Install tooltip subclass (only once)
            if (!g_origCompTreeProc) {
                g_origCompTreeProc = (WNDPROC)SetWindowLongPtrW(
                    s_hCompTreeView, GWLP_WNDPROC,
                    (LONG_PTR)CompTree_TooltipSubclassProc);
            }
        }

        // Right pane: component list
        s_hCompListView = CreateWindowExW(WS_EX_CLIENTEDGE, WC_LISTVIEW, L"",
            WS_CHILD | WS_VISIBLE | LVS_REPORT | LVS_SHOWSELALWAYS | WS_BORDER,
            listX, splitY, listW, splitH,
            hwnd, (HMENU)IDC_COMP_LISTVIEW, hInst, NULL);
        ListView_SetExtendedListViewStyle(s_hCompListView,
            LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES | LVS_EX_INFOTIP);

        // Columns
        auto compLocStr = [&](const wchar_t *key, const wchar_t *fallback) -> std::wstring {
            auto itC = s_locale.find(key);
            return (itC != s_locale.end()) ? itC->second : fallback;
        };
        {
            LVCOLUMNW col = {};
            col.mask = LVCF_TEXT | LVCF_WIDTH;
            int colIdx = 0;
            std::wstring cName = compLocStr(L"comp_col_name",     L"Name");
            std::wstring cDesc = compLocStr(L"comp_col_desc",     L"Description");
            std::wstring cReq  = compLocStr(L"comp_col_required", L"Required");
            std::wstring cType = compLocStr(L"comp_col_type",     L"Type");
            std::wstring cPath = compLocStr(L"comp_col_path",     L"Source Path");
            col.pszText = (LPWSTR)cName.c_str(); col.cx = S(170); ListView_InsertColumn(s_hCompListView, colIdx++, &col);
            col.pszText = (LPWSTR)cDesc.c_str(); col.cx = S(180); ListView_InsertColumn(s_hCompListView, colIdx++, &col);
            col.pszText = (LPWSTR)cReq.c_str();  col.cx = S(75);  ListView_InsertColumn(s_hCompListView, colIdx++, &col);
            col.pszText = (LPWSTR)cType.c_str(); col.cx = S(65);  ListView_InsertColumn(s_hCompListView, colIdx++, &col);
            col.pszText = (LPWSTR)cPath.c_str(); col.cx = listW - S(510 + 20); ListView_InsertColumn(s_hCompListView, colIdx++, &col);
        }

        // s_components is loaded once when the project opens (MainWindow::Create).
        // On subsequent visits to this page it is used as-is from memory.
        // Only load from DB on the very first visit (empty guard) so that a plain
        // page switch never overwrites unsaved in-memory changes.
        if (s_components.empty() && s_currentProject.id > 0) {
            s_components = DB::GetComponentsForProject(s_currentProject.id);
            for (auto& comp : s_components)
                if (comp.id > 0)
                    comp.dependencies = DB::GetDependenciesForComponent(comp.id);
        }
        if (s_currentProject.id > 0) {
            // Repair legacy rows that have dest_path=="" by inferring their section
            // from the VFS snapshots.  The VFS snapshots are always built in the same
            // section order as the initial auto-populate (PF → PD → AD → AAI), so
            // positional matching recovers the correct section even when the same
            // source file is registered in more than one section.
            {
                bool anyLegacy = false;
                for (const auto& c : s_components)
                    if (c.dest_path.empty()) { anyLegacy = true; break; }
                if (anyLegacy) {
                    // Build per-path ordered section lists from each snapshot.
                    std::unordered_map<std::wstring, std::vector<std::wstring>> sectionSeq;
                    auto gather = [&](const std::vector<TreeNodeSnapshot>& nodes,
                                      const std::wstring& sec) {
                        std::vector<ComponentRow> tmp;
                        CollectAllFiles(nodes, 0, tmp, L"");
                        for (const auto& row : tmp)
                            if (!row.source_path.empty())
                                sectionSeq[row.source_path].push_back(sec);
                    };
                    gather(s_treeSnapshot_ProgramFiles,  L"Program Files");
                    gather(s_treeSnapshot_ProgramData,   L"ProgramData");
                    gather(s_treeSnapshot_AppData,       L"AppData (Roaming)");
                    gather(s_treeSnapshot_AskAtInstall,  L"AskAtInstall");

                    std::unordered_map<std::wstring, int> pathUsed;
                    for (auto& comp : s_components) {
                        if (!comp.dest_path.empty()) continue;
                        int idx = pathUsed[comp.source_path]++;
                        auto it = sectionSeq.find(comp.source_path);
                        if (it != sectionSeq.end() && idx < (int)it->second.size())
                            comp.dest_path = it->second[idx];
                        // No DB write here — dest_path repair is in-memory only until Save.
                    }
                }
            }
        }

        // Build folder tree from the 4 VFS snapshots
        {
            auto addRoot = [&](const wchar_t* label, int imgIdx,
                               const std::vector<TreeNodeSnapshot>& snaps)
            {
                if (snaps.empty()) return;
                TVINSERTSTRUCTW tvis = {};
                tvis.hParent             = TVI_ROOT;
                tvis.hInsertAfter        = TVI_LAST;
                tvis.item.mask           = TVIF_TEXT | TVIF_PARAM | TVIF_IMAGE | TVIF_SELECTEDIMAGE;
                tvis.item.pszText        = (LPWSTR)label;
                tvis.item.lParam         = 0;  // section root — no files
                tvis.item.iImage         = imgIdx;
                tvis.item.iSelectedImage = (imgIdx == 0) ? 1 : imgIdx;
                HTREEITEM hR = (HTREEITEM)SendMessageW(s_hCompTreeView, TVM_INSERTITEMW,
                                                       0, (LPARAM)&tvis);
                if (hR) {
                    VFSPicker_AddSubtree(s_hCompTreeView, hR, snaps);
                    TreeView_Expand(s_hCompTreeView, hR, TVE_EXPAND);
                }
            };
            addRoot(L"Program Files",    0, s_treeSnapshot_ProgramFiles);
            addRoot(L"ProgramData",       0, s_treeSnapshot_ProgramData);
            addRoot(L"AppData (Roaming)", 0, s_treeSnapshot_AppData);
            addRoot(L"AskAtInstall",      2, s_treeSnapshot_AskAtInstall);

            // Mark required folders with special icon
            UpdateCompTreeRequiredIcons(s_hCompTreeView, TreeView_GetRoot(s_hCompTreeView));
        }

        // Apply system message font to non-button controls
        {
            NONCLIENTMETRICSW ncm = {};
            ncm.cbSize = sizeof(ncm);
            SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(ncm), &ncm, 0);
            if (ncm.lfMessageFont.lfHeight < 0)
                ncm.lfMessageFont.lfHeight = (LONG)(ncm.lfMessageFont.lfHeight * 1.2f);
            ncm.lfMessageFont.lfQuality = CLEARTYPE_QUALITY;
            HFONT hCtrlFont = CreateFontIndirectW(&ncm.lfMessageFont);
            if (hCtrlFont) {
                SendMessageW(hCompEnable,    WM_SETFONT, (WPARAM)hCtrlFont, TRUE);
                SendMessageW(hHintLabel,     WM_SETFONT, (WPARAM)hCtrlFont, TRUE);
                SendMessageW(s_hCompListView,WM_SETFONT, (WPARAM)hCtrlFont, TRUE);
                SendMessageW(s_hCompTreeView,WM_SETFONT, (WPARAM)hCtrlFont, TRUE);
                HFONT hOld = (HFONT)GetPropW(hwnd, L"hCompCtrlFont");
                if (hOld) DeleteObject(hOld);
                SetPropW(hwnd, L"hCompCtrlFont", (HANDLE)hCtrlFont);
            }
        }

        // Grey out action controls if components are not enabled
        bool compEnabled = (s_currentProject.use_components != 0);
        EnableWindow(GetDlgItem(hwnd, IDC_COMP_EDIT),   compEnabled ? TRUE : FALSE);
        EnableWindow(s_hCompListView,                   compEnabled ? TRUE : FALSE);
        EnableWindow(s_hCompTreeView,                   compEnabled ? TRUE : FALSE);

        // Auto-select the first real folder in the tree (so the list isn't blank)
        if (compEnabled && s_hCompTreeView) {
            HTREEITEM hFirst = TreeView_GetRoot(s_hCompTreeView);
            if (hFirst) {
                HTREEITEM hChild = TreeView_GetChild(s_hCompTreeView, hFirst);
                if (hChild)
                    TreeView_SelectItem(s_hCompTreeView, hChild);
                else
                    TreeView_SelectItem(s_hCompTreeView, hFirst);
            }
        }

        break;
    }
    }
}

HTREEITEM MainWindow::AddTreeNode(HWND hTree, HTREEITEM hParent, const std::wstring &text, const std::wstring &fullPath) {
    // Allocate and store path
    wchar_t* pathCopy = (wchar_t*)malloc((fullPath.length() + 1) * sizeof(wchar_t));
    if (pathCopy) {
        wcscpy(pathCopy, fullPath.c_str());
    }
    
    TVINSERTSTRUCTW tvis = {};
    tvis.hParent = hParent;
    tvis.hInsertAfter = TVI_LAST;
    tvis.item.mask = TVIF_TEXT | TVIF_PARAM | TVIF_IMAGE | TVIF_SELECTEDIMAGE;
    tvis.item.pszText = (LPWSTR)text.c_str();
    tvis.item.lParam = (LPARAM)pathCopy;
    tvis.item.iImage = 0;  // Closed folder icon
    tvis.item.iSelectedImage = 1;  // Open folder icon
    
    HTREEITEM hNew = TreeView_InsertItem(hTree, &tvis);
    return hNew;
}

HTREEITEM MainWindow::GetAskAtInstallRoot() {
    return s_hAskAtInstallRoot;
}

const std::map<std::wstring, std::wstring>& MainWindow::GetLocale() {
    return s_locale;
}

void MainWindow::AddTreeNodeRecursive(HWND hTree, HTREEITEM hParent, const std::wstring &folderPath) {
    std::wstring searchPath = folderPath + L"\\*";
    WIN32_FIND_DATAW findData;
    HANDLE hFind = FindFirstFileW(searchPath.c_str(), &findData);
    
    if (hFind != INVALID_HANDLE_VALUE) {
        do {
            if (findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
                // Skip . and ..
                if (wcscmp(findData.cFileName, L".") != 0 && wcscmp(findData.cFileName, L"..") != 0) {
                    std::wstring subPath = folderPath + L"\\" + findData.cFileName;
                    HTREEITEM hItem = AddTreeNode(hTree, hParent, findData.cFileName, subPath);
                    // Recursively add subdirectories
                    AddTreeNodeRecursive(hTree, hItem, subPath);
                }
            }
        } while (FindNextFileW(hFind, &findData));
        FindClose(hFind);
    }
}

void MainWindow::PopulateTreeView(HWND hTree, const std::wstring &rootPath, const std::wstring &installPath) {
    // Extract folder components from install path
    // e.g., "C:\\Program Files\\SetupCraft" -> show "Program Files" as root -> "SetupCraft" as child
    
    // Find last two folder components
    std::wstring installPathCopy = installPath;
    size_t lastSlash = installPathCopy.find_last_of(L"\\/");
    std::wstring appName = (lastSlash != std::wstring::npos) ? installPathCopy.substr(lastSlash + 1) : installPathCopy;
    
    // Use existing Program Files root if available, otherwise create it
    HTREEITEM hParent = s_hProgramFilesRoot;
    if (!hParent) {
        std::wstring parentPath;
        if (lastSlash != std::wstring::npos) {
            parentPath = installPathCopy.substr(0, lastSlash);
            size_t secondLastSlash = parentPath.find_last_of(L"\\/");
            if (secondLastSlash != std::wstring::npos) {
                parentPath = parentPath.substr(secondLastSlash + 1);
            }
        } else {
            parentPath = GetProgramFilesFolderName();
        }
        hParent = AddTreeNode(hTree, TVI_ROOT, parentPath, L"");
        s_hProgramFilesRoot = hParent;
    }
    
    // Add app node as child (e.g., "SetupCraft")
    HTREEITEM hRoot = AddTreeNode(hTree, hParent, appName, rootPath);
    
    // Recursively add all subdirectories under the app node
    AddTreeNodeRecursive(hTree, hRoot, rootPath);
    
    // Expand parent and root nodes
    TreeView_Expand(hTree, hParent, TVE_EXPAND);
    UpdateWindow(hTree);
    TreeView_Expand(hTree, hRoot, TVE_EXPAND);
    UpdateWindow(hTree);
    
    // Select app root and populate list with root contents
    TreeView_SelectItem(hTree, hRoot);
    PopulateListView(s_hListView, rootPath);
    
    // Force complete redraw
    InvalidateRect(hTree, NULL, TRUE);
    UpdateWindow(hTree);
}

// Helper to enumerate available locale codes from locale directory
static std::vector<std::wstring> GetAvailableLocales() {
    std::vector<std::wstring> out;
    WIN32_FIND_DATAW fd;
    wchar_t exePath[MAX_PATH];
    if (!GetModuleFileNameW(NULL, exePath, _countof(exePath))) return out;
    wchar_t *p = wcsrchr(exePath, L'\\'); if (!p) return out; *p = 0;
    std::wstring search = std::wstring(exePath) + L"\\locale\\*.txt";
    HANDLE h = FindFirstFileW(search.c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return out;
    do {
        if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
            std::wstring name = fd.cFileName;
            size_t pos = name.rfind(L".txt");
            if (pos != std::wstring::npos) out.push_back(name.substr(0, pos));
        }
    } while (FindNextFileW(h, &fd));
    FindClose(h);
    std::sort(out.begin(), out.end());
    out.erase(std::unique(out.begin(), out.end()), out.end());
    return out;
}

// TreeView subclass to show AskAtInstall tooltip on hover
static LRESULT CALLBACK TreeView_SubclassProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_KEYDOWN:
        if (wParam == VK_F2) {
            HTREEITEM hSel = TreeView_GetSelection(hwnd);
            bool isSysRoot = (hSel == MainWindow::GetProgramFilesRoot() ||
                              hSel == MainWindow::GetProgramDataRoot()  ||
                              hSel == MainWindow::GetAppDataRoot()      ||
                              hSel == MainWindow::GetAskAtInstallRoot());
            if (hSel && !isSysRoot)
                TreeView_EditLabel(hwnd, hSel);
            return 0;
        }
        break;

    case WM_MOUSEMOVE: {
        // Hit test tree item under cursor
        POINT pt; GetCursorPos(&pt);
        ScreenToClient(hwnd, &pt);
        TVHITTESTINFO ht = {};
        ht.pt = pt;
        HTREEITEM hItem = TreeView_HitTest(hwnd, &ht);
        if (hItem != s_lastHoveredTreeItem) {
            // If moved onto AskAtInstall root, show tooltip
                    if (hItem == MainWindow::GetAskAtInstallRoot()) {
                RECT rcItem;
                    if (TreeView_GetItemRect(hwnd, hItem, &rcItem, TRUE)) {
                        // Use selected language only (from locale) and show below mouse pointer
                        auto it = MainWindow::GetLocale().find(L"ask_at_install_tooltip");
                        std::wstring text = (it != MainWindow::GetLocale().end()) ? it->second : L"Installer will ask the end user whether to install for current user or all users; files will go to AppData or ProgramData accordingly.";
                        std::vector<TooltipEntry> entries;
                        entries.push_back({L"", text});
                        POINT pt; GetCursorPos(&pt);
                        ShowMultilingualTooltip(entries, pt.x, pt.y + 20, GetParent(hwnd));
                    }
            } else {
                HideTooltip();
            }
            s_lastHoveredTreeItem = hItem;
        }
        // start mouse leave tracking
        TRACKMOUSEEVENT tme = { sizeof(tme), TME_LEAVE, hwnd, 0 };
        TrackMouseEvent(&tme);
        break;
    }
    case WM_MOUSELEAVE: {
        HideTooltip();
        s_lastHoveredTreeItem = NULL;
        break;
    }
    }
    return CallWindowProcW(s_prevTreeProc, hwnd, msg, wParam, lParam);
}

void MainWindow::UpdateInstallPathFromTree(HWND hwnd) {
    // Get the first child of Program Files root
    if (!s_hTreeView || !s_hProgramFilesRoot) {
        return;
    }
    
    HTREEITEM hFirstChild = TreeView_GetChild(s_hTreeView, s_hProgramFilesRoot);
    
    if (hFirstChild) {
        // Get folder name
        wchar_t folderName[256];
        TVITEMW item = {};
        item.mask = TVIF_TEXT;
        item.hItem = hFirstChild;
        item.pszText = folderName;
        item.cchTextMax = 256;
        TreeView_GetItem(s_hTreeView, &item);
        
        // Update install folder field
        std::wstring newInstallPath = GetProgramFilesPath() + L"\\" + std::wstring(folderName);
        SetDlgItemTextW(hwnd, IDC_INSTALL_FOLDER, newInstallPath.c_str());
        
        // Also update project name if this is a new project and user hasn't manually edited it
        if (!s_projectNameManuallySet) {
            s_currentProject.name = folderName;
            std::wstring newTitle = L"SetupCraft - " + std::wstring(folderName);
            SetWindowTextW(hwnd, newTitle.c_str());
            
            // Update project name field programmatically
            s_updatingProjectNameProgrammatically = true;
            SetDlgItemTextW(hwnd, IDC_PROJECT_NAME, folderName);
            s_updatingProjectNameProgrammatically = false;
        }
    } else {
        // No folders under Program Files, set to default
        std::wstring defaultPath = GetProgramFilesPath() + L"\\New Project";
        SetDlgItemTextW(hwnd, IDC_INSTALL_FOLDER, defaultPath.c_str());
    }
}

void MainWindow::PopulateListView(HWND hList, const std::wstring &folderPath) {
    // Clear existing items
    ListView_DeleteAllItems(hList);
    
    // Scan folder - show only files
    std::wstring searchPath = folderPath + L"\\*";
    WIN32_FIND_DATAW findData;
    HANDLE hFind = FindFirstFileW(searchPath.c_str(), &findData);
    
    if (hFind != INVALID_HANDLE_VALUE) {
        int itemIndex = 0;
        do {
            // Skip . and .. and directories - show only files
            if (wcscmp(findData.cFileName, L".") != 0 && 
                wcscmp(findData.cFileName, L"..") != 0 &&
                !(findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
                
                std::wstring fullPath = folderPath + L"\\" + findData.cFileName;
                
                // Get file icon index
                SHFILEINFOW sfi = {};
                SHGetFileInfoW(fullPath.c_str(), 0, &sfi, sizeof(sfi), SHGFI_SYSICONINDEX | SHGFI_SMALLICON);
                
                LVITEMW lvi = {};
                lvi.mask = LVIF_TEXT | LVIF_PARAM | LVIF_IMAGE;
                lvi.iItem = itemIndex;
                lvi.iSubItem = 0;
                lvi.pszText = (LPWSTR)fullPath.c_str();
                lvi.iImage = sfi.iIcon;
                
                // Store full path in LPARAM for tooltip
                wchar_t* pathCopy = (wchar_t*)malloc((fullPath.length() + 1) * sizeof(wchar_t));
                if (pathCopy) {
                    wcscpy(pathCopy, fullPath.c_str());
                    lvi.lParam = (LPARAM)pathCopy;
                }
                
                int idx = ListView_InsertItem(hList, &lvi);
                
                // Destination (relative to install dir)
                std::wstring dest = L"\\" + std::wstring(findData.cFileName);
                ListView_SetItemText(hList, idx, 1, (LPWSTR)dest.c_str());
                
                itemIndex++;
            }
        } while (FindNextFileW(hFind, &findData));
        FindClose(hFind);
    }
}

void MainWindow::CreateTabControl(HWND hwnd, HINSTANCE hInst) {
    // Get client area size
    RECT rc;
    GetClientRect(hwnd, &rc);
    
    // Create tab control (leave space for toolbar at top and status bar at bottom)
    s_hTab = CreateWindowExW(
        0,
        WC_TABCONTROL,
        L"",
        WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS,
        0, s_toolbarHeight,
        rc.right, rc.bottom - s_toolbarHeight - 25,  // Leave space for toolbar and status bar
        hwnd,
        (HMENU)IDC_TAB_CONTROL,
        hInst,
        NULL
    );
    
    // Add tabs
    TCITEMW tie = {};
    tie.mask = TCIF_TEXT;
    
    tie.pszText = (LPWSTR)L"Files";
    TabCtrl_InsertItem(s_hTab, 0, &tie);
    
    tie.pszText = (LPWSTR)L"Setup Information";
    TabCtrl_InsertItem(s_hTab, 1, &tie);
    
    tie.pszText = (LPWSTR)L"Install Locations";
    TabCtrl_InsertItem(s_hTab, 2, &tie);
    
    tie.pszText = (LPWSTR)L"Icons/Shortcuts";
    TabCtrl_InsertItem(s_hTab, 3, &tie);
    
    tie.pszText = (LPWSTR)L"Build Settings";
    TabCtrl_InsertItem(s_hTab, 4, &tie);
    
    // Show first tab content
    SwitchTab(hwnd, 0);
}

void MainWindow::CreateStatusBar(HWND hwnd, HINSTANCE hInst) {
    s_hStatus = CreateWindowExW(
        0,
        STATUSCLASSNAME,
        NULL,
        WS_CHILD | WS_VISIBLE | SBARS_SIZEGRIP,
        0, 0, 0, 0,
        hwnd,
        (HMENU)IDC_STATUS_BAR,
        hInst,
        NULL
    );

    // Split into two parts: project info (left) + save indicator (right)
    RECT rcClient;
    GetClientRect(hwnd, &rcClient);
    int indW = S(120);
    int parts[2] = { rcClient.right - indW, -1 };
    SendMessageW(s_hStatus, SB_SETPARTS, 2, (LPARAM)parts);

    // Part 0: project info text
    std::wstring statusText = L"Project: " + s_currentProject.name +
                              L"  |  Version: " + s_currentProject.version +
                              L"  |  Directory: " + s_currentProject.directory;
    SendMessageW(s_hStatus, SB_SETTEXT, 0, (LPARAM)statusText.c_str());

    // Part 1: owner-draw save indicator (starts as Saved since project was just loaded)
    SendMessageW(s_hStatus, SB_SETTEXT, (WPARAM)(1 | SBT_OWNERDRAW), (LPARAM)0);

    if (s_scaledFont) SendMessageW(s_hStatus, WM_SETFONT, (WPARAM)s_scaledFont, TRUE);
}

void MainWindow::SwitchTab(HWND hwnd, int tabIndex) {
    // Destroy previous tab content
    if (s_hCurrentTabContent) {
        DestroyWindow(s_hCurrentTabContent);
        s_hCurrentTabContent = NULL;
    }
    
    // Get tab display area
    RECT rcTab;
    GetWindowRect(s_hTab, &rcTab);
    ScreenToClient(hwnd, (POINT*)&rcTab.left);
    ScreenToClient(hwnd, (POINT*)&rcTab.right);
    TabCtrl_AdjustRect(s_hTab, FALSE, &rcTab);
    
    HINSTANCE hInst = (HINSTANCE)GetWindowLongPtr(hwnd, GWLP_HINSTANCE);
    
    // Create content for selected tab
    switch (tabIndex) {
    case 0: // Files tab
        s_hCurrentTabContent = CreateWindowExW(
            0, L"STATIC",
            L"Files Tab - List of files to include in the installer\n(To be implemented)",
            WS_CHILD | WS_VISIBLE | SS_LEFT,
            rcTab.left + 10, rcTab.top + 10,
            rcTab.right - rcTab.left - 20, rcTab.bottom - rcTab.top - 20,
            hwnd, NULL, hInst, NULL
        );
        break;
        
    case 1: // Setup Information tab
        s_hCurrentTabContent = CreateWindowExW(
            0, L"STATIC",
            L"Setup Information Tab - Application name, version, publisher, etc.\n(To be implemented)",
            WS_CHILD | WS_VISIBLE | SS_LEFT,
            rcTab.left + 10, rcTab.top + 10,
            rcTab.right - rcTab.left - 20, rcTab.bottom - rcTab.top - 20,
            hwnd, NULL, hInst, NULL
        );
        break;
        
    case 2: // Install Locations tab
        s_hCurrentTabContent = CreateWindowExW(
            0, L"STATIC",
            L"Install Locations Tab - Default directories for installation\n(To be implemented)",
            WS_CHILD | WS_VISIBLE | SS_LEFT,
            rcTab.left + 10, rcTab.top + 10,
            rcTab.right - rcTab.left - 20, rcTab.bottom - rcTab.top - 20,
            hwnd, NULL, hInst, NULL
        );
        break;
        
    case 3: // Icons/Shortcuts tab
        s_hCurrentTabContent = CreateWindowExW(
            0, L"STATIC",
            L"Icons/Shortcuts Tab - Start menu and desktop shortcuts\n(To be implemented)",
            WS_CHILD | WS_VISIBLE | SS_LEFT,
            rcTab.left + 10, rcTab.top + 10,
            rcTab.right - rcTab.left - 20, rcTab.bottom - rcTab.top - 20,
            hwnd, NULL, hInst, NULL
        );
        break;
        
    case 4: // Build Settings tab
        s_hCurrentTabContent = CreateWindowExW(
            0, L"STATIC",
            L"Build Settings Tab - Output file, compression, signing\n(To be implemented)",
            WS_CHILD | WS_VISIBLE | SS_LEFT,
            rcTab.left + 10, rcTab.top + 10,
            rcTab.right - rcTab.left - 20, rcTab.bottom - rcTab.top - 20,
            hwnd, NULL, hInst, NULL
        );
        break;
    }
}

// Registry key dialog data
struct RegKeyDialogData {
    std::wstring regPath;
    std::wstring labelText;
    std::wstring closeText;
    std::wstring navigateText;
    std::wstring copyText;
};

// Layout constants for the Registry Key dialog (design-time px at 96 DPI)
static const int RK_PAD_H  = 20;  // left/right padding
static const int RK_PAD_T  = 20;  // top padding
static const int RK_LBL_H  = 22;  // label height
static const int RK_GAP_LE = 10;  // gap label → edit
static const int RK_EDIT_H = 60;  // registry path edit height (multiline)
static const int RK_GAP_EB = 15;  // gap edit → buttons
static const int RK_BTN_H  = 38;  // button height
static const int RK_PAD_B  = 20;  // bottom padding
static const int RK_CONT_W = 560; // content (label/edit) width
static const int RK_BTN_W0 = 200; // "Take me there" button width
static const int RK_BTN_W1 = 120; // "Copy" button width
static const int RK_BTN_W2 = 120; // "Close" button width
static const int RK_BTN_GAP = 15; // gap between buttons

// Dialog procedure for Show Registry Key dialog
LRESULT CALLBACK RegKeyDialogProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_CREATE: {
        CREATESTRUCTW* cs = (CREATESTRUCTW*)lParam;
        HINSTANCE hInst = cs->hInstance;

        RegKeyDialogData* pData = (RegKeyDialogData*)cs->lpCreateParams;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)pData);

        RECT rcC; GetClientRect(hwnd, &rcC);
        int cW = rcC.right, cH = rcC.bottom;
        int contW = cW - 2*S(RK_PAD_H); // label and edit width

        // Label
        CreateWindowExW(0, L"STATIC", pData->labelText.c_str(),
            WS_CHILD | WS_VISIBLE | SS_LEFT,
            S(RK_PAD_H), S(RK_PAD_T), contW, S(RK_LBL_H),
            hwnd, NULL, hInst, NULL);

        // Registry path edit (read-only, multiline so long paths word-wrap)
        int editY = S(RK_PAD_T) + S(RK_LBL_H) + S(RK_GAP_LE);
        HWND hEdit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", pData->regPath.c_str(),
            WS_CHILD | WS_VISIBLE | ES_LEFT | ES_READONLY | ES_MULTILINE | ES_AUTOHSCROLL | WS_HSCROLL,
            S(RK_PAD_H), editY, contW, S(RK_EDIT_H),
            hwnd, (HMENU)IDC_REGKEY_DLG_EDIT, hInst, NULL);

        // 3 buttons centred, pinned to bottom
        int wNav   = MeasureButtonWidth(pData->navigateText, true);
        int wCopy  = MeasureButtonWidth(pData->copyText, true);
        int wClose = MeasureButtonWidth(pData->closeText, true);
        int totalBtnW = wNav + wCopy + wClose + 2*S(RK_BTN_GAP);
        int startX    = (cW - totalBtnW) / 2;
        int btnY      = cH - S(RK_PAD_B) - S(RK_BTN_H);

        CreateCustomButtonWithIcon(hwnd, IDC_REGKEY_DLG_NAVIGATE, pData->navigateText.c_str(), ButtonColor::Blue,
            L"shell32.dll", 267, startX, btnY, wNav, S(RK_BTN_H), hInst);
        CreateCustomButtonWithIcon(hwnd, IDC_REGKEY_DLG_COPY, pData->copyText.c_str(), ButtonColor::Blue,
            L"shell32.dll", 64,
            startX+wNav+S(RK_BTN_GAP), btnY, wCopy, S(RK_BTN_H), hInst);
        CreateCustomButtonWithIcon(hwnd, IDC_REGKEY_DLG_CLOSE, pData->closeText.c_str(), ButtonColor::Blue,
            L"shell32.dll", 300,
            startX+wNav+S(RK_BTN_GAP)+wCopy+S(RK_BTN_GAP), btnY,
            wClose, S(RK_BTN_H), hInst);

        // Apply system message font to all controls
        {
            NONCLIENTMETRICSW ncm = {};
            ncm.cbSize = sizeof(ncm);
            SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(ncm), &ncm, 0);
            if (ncm.lfMessageFont.lfHeight < 0)
                ncm.lfMessageFont.lfHeight = (LONG)(ncm.lfMessageFont.lfHeight * 1.2f);
            ncm.lfMessageFont.lfQuality = CLEARTYPE_QUALITY;
            HFONT hCtrlFont = CreateFontIndirectW(&ncm.lfMessageFont);
            if (hCtrlFont) {
                EnumChildWindows(hwnd, [](HWND hChild, LPARAM lp) -> BOOL {
                    SendMessageW(hChild, WM_SETFONT, (WPARAM)(HFONT)lp, TRUE);
                    return TRUE;
                }, (LPARAM)hCtrlFont);
                SetPropW(hwnd, L"hCtrlFont", (HANDLE)hCtrlFont);
            }
        }
        
        return 0;
    }
    
    case WM_CONTEXTMENU: {
        // Show context menu on right-click in edit control
        HWND hEdit = GetDlgItem(hwnd, IDC_REGKEY_DLG_EDIT);
        if ((HWND)wParam == hEdit) {
            HMENU hMenu = CreatePopupMenu();
            RegKeyDialogData* pData = (RegKeyDialogData*)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
            if (hMenu && pData) {
                AppendMenuW(hMenu, MF_STRING, IDC_REGKEY_DLG_COPY, pData->copyText.c_str());
                
                POINT pt;
                pt.x = GET_X_LPARAM(lParam);
                pt.y = GET_Y_LPARAM(lParam);
                
                TrackPopupMenu(hMenu, TPM_RIGHTBUTTON, pt.x, pt.y, 0, hwnd, NULL);
                DestroyMenu(hMenu);
            }
        }
        return 0;
    }
    
    case WM_COMMAND:
        if (LOWORD(wParam) == IDC_REGKEY_DLG_CLOSE) {
            DestroyWindow(hwnd);
            s_hRegKeyDialog = NULL;
            return 0;
        }
        else if (LOWORD(wParam) == IDC_REGKEY_DLG_COPY) {
            // Copy registry path to clipboard
            HWND hEdit = GetDlgItem(hwnd, IDC_REGKEY_DLG_EDIT);
            if (hEdit) {
                int len = GetWindowTextLengthW(hEdit);
                if (len > 0) {
                    std::wstring text(len + 1, L'\0');
                    GetWindowTextW(hEdit, &text[0], len + 1);
                    text.resize(len);
                    
                    if (OpenClipboard(hwnd)) {
                        EmptyClipboard();
                        HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE, (text.length() + 1) * sizeof(wchar_t));
                        if (hMem) {
                            wchar_t* pMem = (wchar_t*)GlobalLock(hMem);
                            if (pMem) {
                                wcscpy(pMem, text.c_str());
                                GlobalUnlock(hMem);
                                SetClipboardData(CF_UNICODETEXT, hMem);
                            }
                        }
                        CloseClipboard();
                    }
                }
            }
            return 0;
        }
        else if (LOWORD(wParam) == IDC_REGKEY_DLG_NAVIGATE) {
            s_navigateToRegKey = true;
            // Don't close the dialog - let user see both dialog and navigation
            // Trigger navigation in parent window
            HWND hwndParent = GetParent(hwnd);
            if (hwndParent) {
                PostMessageW(hwndParent, WM_USER + 100, 0, 0);
            }
            return 0;
        }
        break;
    
    case WM_DRAWITEM: {
        LPDRAWITEMSTRUCT dis = (LPDRAWITEMSTRUCT)lParam;
        if (dis->CtlID == IDC_REGKEY_DLG_CLOSE || dis->CtlID == IDC_REGKEY_DLG_NAVIGATE || dis->CtlID == IDC_REGKEY_DLG_COPY) {
            ButtonColor color = (ButtonColor)GetWindowLongPtr(dis->hwndItem, GWLP_USERDATA);
            NONCLIENTMETRICSW ncm = {};
            ncm.cbSize = sizeof(ncm);
            SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(ncm), &ncm, 0);
            if (ncm.lfMessageFont.lfHeight < 0)
                ncm.lfMessageFont.lfHeight = (LONG)(ncm.lfMessageFont.lfHeight * 1.2f);
            ncm.lfMessageFont.lfWeight = FW_BOLD;
            ncm.lfMessageFont.lfQuality = CLEARTYPE_QUALITY;
            HFONT hFont = CreateFontIndirectW(&ncm.lfMessageFont);
            LRESULT result = DrawCustomButton(dis, color, hFont);
            if (hFont) DeleteObject(hFont);
            return result;
        }
        break;
    }
    
    case WM_KEYDOWN: {
        // Handle Ctrl+A and Ctrl+C in edit control
        if (GetKeyState(VK_CONTROL) & 0x8000) {
            HWND hEdit = GetDlgItem(hwnd, IDC_REGKEY_DLG_EDIT);
            if (wParam == 'A') {
                // Select all
                if (hEdit) {
                    SendMessageW(hEdit, EM_SETSEL, 0, -1);
                }
                return 0;
            }
            else if (wParam == 'C') {
                // Copy
                if (hEdit && GetFocus() == hEdit) {
                    SendMessageW(hwnd, WM_COMMAND, IDC_REGKEY_DLG_COPY, 0);
                }
                return 0;
            }
        }
        break;
    }
    
    case WM_CTLCOLORSTATIC: {
        // Make the read-only label and read-only edit background match the white dialog background
        HDC hdc = (HDC)wParam;
        SetBkColor(hdc, GetSysColor(COLOR_WINDOW));
        SetTextColor(hdc, GetSysColor(COLOR_WINDOWTEXT));
        return (LRESULT)GetSysColorBrush(COLOR_WINDOW);
    }

    case WM_CLOSE:
        DestroyWindow(hwnd);
        s_hRegKeyDialog = NULL;
        return 0;
    
    case WM_DESTROY: {
        HFONT hCtrlFont = (HFONT)GetPropW(hwnd, L"hCtrlFont");
        if (hCtrlFont) { DeleteObject(hCtrlFont); RemovePropW(hwnd, L"hCtrlFont"); }
        s_hRegKeyDialog = NULL;
        return 0;
    }
    }
    
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

// Validate registry value data based on type
bool ValidateRegistryData(const std::wstring& type, const std::wstring& data, std::wstring& errorMsg) {
    if (type == L"REG_DWORD") {
        // Must be a valid 32-bit number (hex or decimal)
        if (data.empty()) {
            errorMsg = L"DWORD value cannot be empty. Enter a number (0-4294967295) or hex (0x...)";
            return false;
        }
        
        try {
            if (data.length() >= 2 && data.substr(0, 2) == L"0x") {
                // Hex format
                unsigned long long val = std::stoull(data.substr(2), nullptr, 16);
                if (val > 0xFFFFFFFF) {
                    errorMsg = L"DWORD value too large. Maximum: 0xFFFFFFFF (4294967295)";
                    return false;
                }
            } else {
                // Decimal format
                unsigned long long val = std::stoull(data);
                if (val > 0xFFFFFFFF) {
                    errorMsg = L"DWORD value too large. Maximum: 4294967295";
                    return false;
                }
            }
        } catch (...) {
            errorMsg = L"Invalid DWORD value. Enter a number (0-4294967295) or hex (0x00000000)";
            return false;
        }
    }
    else if (type == L"REG_QWORD") {
        // Must be a valid 64-bit number (hex or decimal)
        if (data.empty()) {
            errorMsg = L"QWORD value cannot be empty. Enter a number or hex (0x...)";
            return false;
        }
        
        try {
            if (data.length() >= 2 && data.substr(0, 2) == L"0x") {
                std::stoull(data.substr(2), nullptr, 16);
            } else {
                std::stoull(data);
            }
        } catch (...) {
            errorMsg = L"Invalid QWORD value. Enter a number or hex (0x0000000000000000)";
            return false;
        }
    }
    else if (type == L"REG_BINARY") {
        // Should be hex bytes (space or comma separated optional)
        if (!data.empty()) {
            for (wchar_t c : data) {
                if (!iswxdigit(c) && c != L' ' && c != L',' && c != L'-') {
                    errorMsg = L"Binary data must contain only hex digits (0-9, A-F) and optional spaces/commas";
                    return false;
                }
            }
        }
    }
    // REG_SZ, REG_EXPAND_SZ, REG_MULTI_SZ accept any text
    
    return true;
}

// Add Value dialog data
struct AddValueDialogData {
    std::wstring nameText;
    std::wstring typeText;
    std::wstring dataText;
    std::wstring okText;
    std::wstring cancelText;
    std::wstring valueName;
    std::wstring valueType;
    std::wstring valueData;
    bool okClicked;
};

// Dialog procedure for Add Registry Value dialog
// ── AddValue dialog layout constants (design-px at 96 DPI) ────────────────────
// Layout: label column | gap | edit/combo column, 3 data rows then buttons.
static const int AV_PAD_H   = 20; // left/right padding
static const int AV_PAD_T   = 20; // top padding
static const int AV_PAD_B   = 20; // bottom padding
static const int AV_LBL_W   = 145; // label column width
static const int AV_FLD_GAP = 10;  // gap between label and field
static const int AV_EDIT_W  = 460; // edit/combo field width
static const int AV_ROW_H   = 28;  // row height (label/edit/combo)
static const int AV_GAP_R1  = 16;  // vertical gap between rows 1 and 2
static const int AV_GAP_R2  = 18;  // vertical gap between rows 2 and 3
static const int AV_GAP_RB  = 34;  // vertical gap between last row and buttons
static const int AV_BTN_H   = 38;
static const int AV_BTN_W0  = 155; // OK
static const int AV_BTN_W1  = 155; // Cancel
static const int AV_BTN_GAP = 10;

LRESULT CALLBACK AddValueDialogProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_CREATE: {
        CREATESTRUCTW* cs = (CREATESTRUCTW*)lParam;
        HINSTANCE hInst = cs->hInstance;

        AddValueDialogData* pData = (AddValueDialogData*)cs->lpCreateParams;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)pData);

        RECT rcC; GetClientRect(hwnd, &rcC);
        int cW = rcC.right;
        int editX = S(AV_PAD_H) + S(AV_LBL_W) + S(AV_FLD_GAP);
        int editW = cW - editX - S(AV_PAD_H);

        int y = S(AV_PAD_T);

        // Row 1 — Value Name
        CreateWindowExW(0, L"STATIC", pData->nameText.c_str(),
            WS_CHILD | WS_VISIBLE | SS_LEFT,
            S(AV_PAD_H), y, S(AV_LBL_W), S(AV_ROW_H), hwnd, NULL, hInst, NULL);
        HWND hNameEdit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", pData->valueName.c_str(),
            WS_CHILD | WS_VISIBLE | WS_BORDER | ES_LEFT | ES_AUTOHSCROLL,
            editX, y, editW, S(AV_ROW_H), hwnd, (HMENU)IDC_ADDVAL_NAME, hInst, NULL);
        SetDlgItemTextW(hwnd, IDC_ADDVAL_NAME, pData->valueName.c_str());
        y += S(AV_ROW_H) + S(AV_GAP_R1);

        // Row 2 — Type
        CreateWindowExW(0, L"STATIC", pData->typeText.c_str(),
            WS_CHILD | WS_VISIBLE | SS_LEFT,
            S(AV_PAD_H), y, S(AV_LBL_W), S(AV_ROW_H), hwnd, NULL, hInst, NULL);
        HWND hCombo = CreateWindowExW(WS_EX_CLIENTEDGE, L"COMBOBOX", L"",
            WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | WS_VSCROLL,
            editX, y, editW, S(200), hwnd, (HMENU)IDC_ADDVAL_TYPE, hInst, NULL);

        SendMessageW(hCombo, CB_ADDSTRING, 0, (LPARAM)L"REG_SZ - Text string value");
        SendMessageW(hCombo, CB_ADDSTRING, 0, (LPARAM)L"REG_BINARY - Binary data (any length)");
        SendMessageW(hCombo, CB_ADDSTRING, 0, (LPARAM)L"REG_DWORD - 32-bit number");
        SendMessageW(hCombo, CB_ADDSTRING, 0, (LPARAM)L"REG_QWORD - 64-bit number");
        SendMessageW(hCombo, CB_ADDSTRING, 0, (LPARAM)L"REG_MULTI_SZ - Multiple text strings");
        SendMessageW(hCombo, CB_ADDSTRING, 0, (LPARAM)L"REG_EXPAND_SZ - Expandable string (with %variables%)");

        int typeIndex = 0;
        if (!pData->valueType.empty()) {
            if (pData->valueType == L"REG_SZ") typeIndex = 0;
            else if (pData->valueType == L"REG_BINARY") typeIndex = 1;
            else if (pData->valueType == L"REG_DWORD") typeIndex = 2;
            else if (pData->valueType == L"REG_QWORD") typeIndex = 3;
            else if (pData->valueType == L"REG_MULTI_SZ") typeIndex = 4;
            else if (pData->valueType == L"REG_EXPAND_SZ") typeIndex = 5;
        }
        SendMessageW(hCombo, CB_SETCURSEL, typeIndex, 0);
        y += S(AV_ROW_H) + S(AV_GAP_R2);

        // Row 3 — Data
        CreateWindowExW(0, L"STATIC", pData->dataText.c_str(),
            WS_CHILD | WS_VISIBLE | SS_LEFT,
            S(AV_PAD_H), y, S(AV_LBL_W), S(AV_ROW_H), hwnd, NULL, hInst, NULL);
        CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", pData->valueData.c_str(),
            WS_CHILD | WS_VISIBLE | WS_BORDER | ES_LEFT | ES_AUTOHSCROLL,
            editX, y, editW, S(AV_ROW_H), hwnd, (HMENU)IDC_ADDVAL_DATA, hInst, NULL);
        SetDlgItemTextW(hwnd, IDC_ADDVAL_DATA, pData->valueData.c_str());
        y += S(AV_ROW_H) + S(AV_GAP_RB);

        // Buttons — centred
        int wOK_AV  = MeasureButtonWidth(pData->okText, true);
        int wCnl_AV = MeasureButtonWidth(pData->cancelText, true);
        int totalBtnW = wOK_AV + S(AV_BTN_GAP) + wCnl_AV;
        int startX    = (cW - totalBtnW) / 2;
        CreateCustomButtonWithIcon(hwnd, IDC_ADDVAL_OK, pData->okText.c_str(), ButtonColor::Green,
            L"imageres.dll", 89, startX, y, wOK_AV, S(AV_BTN_H), hInst);
        CreateCustomButtonWithIcon(hwnd, IDC_ADDVAL_CANCEL, pData->cancelText.c_str(), ButtonColor::Red,
            L"shell32.dll", 131, startX + wOK_AV + S(AV_BTN_GAP), y,
            wCnl_AV, S(AV_BTN_H), hInst);

        // Apply system message font to all controls (labels, edits, combobox)
        {
            NONCLIENTMETRICSW ncm = {};
            ncm.cbSize = sizeof(ncm);
            SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(ncm), &ncm, 0);
            if (ncm.lfMessageFont.lfHeight < 0)
                ncm.lfMessageFont.lfHeight = (LONG)(ncm.lfMessageFont.lfHeight * 1.2f);
            ncm.lfMessageFont.lfQuality = CLEARTYPE_QUALITY;
            HFONT hCtrlFont = CreateFontIndirectW(&ncm.lfMessageFont);
            if (hCtrlFont) {
                EnumChildWindows(hwnd, [](HWND hChild, LPARAM lp) -> BOOL {
                    SendMessageW(hChild, WM_SETFONT, (WPARAM)(HFONT)lp, TRUE);
                    return TRUE;
                }, (LPARAM)hCtrlFont);
                SetPropW(hwnd, L"hCtrlFont", (HANDLE)hCtrlFont);
            }
        }

        // Set dropped width AFTER font is applied so item pixel widths are correct
        {
            HWND hCb = GetDlgItem(hwnd, IDC_ADDVAL_TYPE);
            if (hCb) SendMessageW(hCb, CB_SETDROPPEDWIDTH, 760, 0);
        }

        return 0;
    }
    
    case WM_COMMAND: {
        AddValueDialogData* pData = (AddValueDialogData*)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
        
        switch (LOWORD(wParam)) {
        case IDC_ADDVAL_OK: {
            if (pData) {
                // Get value name
                wchar_t nameBuf[256];
                GetDlgItemTextW(hwnd, IDC_ADDVAL_NAME, nameBuf, 256);
                pData->valueName = nameBuf;
                
                if (pData->valueName.empty()) {
                    MessageBoxW(hwnd, L"Value name cannot be empty.", L"Validation Error", MB_OK | MB_ICONWARNING);
                    return 0;
                }
                
                // Get value type
                HWND hCombo = GetDlgItem(hwnd, IDC_ADDVAL_TYPE);
                int sel = (int)SendMessageW(hCombo, CB_GETCURSEL, 0, 0);
                if (sel == CB_ERR) sel = 0;
                wchar_t typeBuf[256];
                SendMessageW(hCombo, CB_GETLBTEXT, sel, (LPARAM)typeBuf);
                
                // Extract just the type name (before the " - " separator)
                std::wstring fullText = typeBuf;
                size_t sepPos = fullText.find(L" - ");
                if (sepPos != std::wstring::npos) {
                    pData->valueType = fullText.substr(0, sepPos);
                } else {
                    pData->valueType = fullText;
                }
                
                // Get value data
                wchar_t dataBuf[1024];
                GetDlgItemTextW(hwnd, IDC_ADDVAL_DATA, dataBuf, 1024);
                pData->valueData = dataBuf;
                
                // Validate data based on type
                std::wstring errorMsg;
                if (!ValidateRegistryData(pData->valueType, pData->valueData, errorMsg)) {
                    MessageBoxW(hwnd, errorMsg.c_str(), L"Validation Error", MB_OK | MB_ICONWARNING);
                    return 0;
                }
                
                pData->okClicked = true;
            }
            DestroyWindow(hwnd);
            return 0;
        }
        
        case IDC_ADDVAL_CANCEL:
            DestroyWindow(hwnd);
            return 0;
        }
        break;
    }
    
    case WM_DRAWITEM: {
        LPDRAWITEMSTRUCT dis = (LPDRAWITEMSTRUCT)lParam;
        if (dis->CtlID == IDC_ADDVAL_OK || dis->CtlID == IDC_ADDVAL_CANCEL) {
            ButtonColor color = (ButtonColor)GetWindowLongPtr(dis->hwndItem, GWLP_USERDATA);
            NONCLIENTMETRICSW ncm = {};
            ncm.cbSize = sizeof(ncm);
            SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(ncm), &ncm, 0);
            if (ncm.lfMessageFont.lfHeight < 0)
                ncm.lfMessageFont.lfHeight = (LONG)(ncm.lfMessageFont.lfHeight * 1.2f);
            ncm.lfMessageFont.lfWeight = FW_BOLD;
            ncm.lfMessageFont.lfQuality = CLEARTYPE_QUALITY;
            HFONT hFont = CreateFontIndirectW(&ncm.lfMessageFont);
            LRESULT result = DrawCustomButton(dis, color, hFont);
            if (hFont) DeleteObject(hFont);
            return result;
        }
        break;
    }

    case WM_DESTROY: {
        HFONT hCtrlFont = (HFONT)GetPropW(hwnd, L"hCtrlFont");
        if (hCtrlFont) { DeleteObject(hCtrlFont); RemovePropW(hwnd, L"hCtrlFont"); }
        break;
    }

    case WM_CLOSE:
        DestroyWindow(hwnd);
        return 0;
    }
    
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

// Add Key dialog data
struct AddKeyDialogData {
    std::wstring nameText;
    std::wstring okText;
    std::wstring cancelText;
    std::wstring keyName;
    std::wstring defaultKeyName;
    bool okClicked;
};

// ── AddKey dialog layout constants (design-px at 96 DPI) ─────────────────────
// Layout: label | gap | edit on one row, then buttons below.
static const int AK_PAD_H   = 20; // left/right padding
static const int AK_PAD_T   = 22; // top padding
static const int AK_PAD_B   = 20; // bottom padding
static const int AK_LBL_W   = 145; // label column width
static const int AK_FLD_GAP = 10;  // gap between label and edit
static const int AK_EDIT_W  = 370; // edit field width
static const int AK_ROW_H   = 30;  // label/edit height
static const int AK_GAP_EB  = 36;  // gap from row bottom to button top
static const int AK_BTN_H   = 38;
static const int AK_BTN_W0  = 155; // OK
static const int AK_BTN_W1  = 155; // Cancel
static const int AK_BTN_GAP = 10;

// Dialog procedure for Add Registry Key dialog
LRESULT CALLBACK AddKeyDialogProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_CREATE: {
        CREATESTRUCTW* cs = (CREATESTRUCTW*)lParam;
        HINSTANCE hInst = cs->hInstance;

        AddKeyDialogData* pData = (AddKeyDialogData*)cs->lpCreateParams;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)pData);

        RECT rcC; GetClientRect(hwnd, &rcC);
        int cW = rcC.right;
        int editX = S(AK_PAD_H) + S(AK_LBL_W) + S(AK_FLD_GAP);
        int editW = cW - editX - S(AK_PAD_H);

        int y = S(AK_PAD_T);

        // Key Name — label and edit side by side
        CreateWindowExW(0, L"STATIC", pData->nameText.c_str(),
            WS_CHILD | WS_VISIBLE | SS_LEFT,
            S(AK_PAD_H), y, S(AK_LBL_W), S(AK_ROW_H), hwnd, NULL, hInst, NULL);
        CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", pData->defaultKeyName.c_str(),
            WS_CHILD | WS_VISIBLE | WS_BORDER | ES_LEFT | ES_AUTOHSCROLL,
            editX, y, editW, S(AK_ROW_H), hwnd, (HMENU)IDC_ADDKEY_NAME, hInst, NULL);
        SetDlgItemTextW(hwnd, IDC_ADDKEY_NAME, pData->defaultKeyName.c_str());
        y += S(AK_ROW_H) + S(AK_GAP_EB);

        // Buttons — centred
        int wOK_AK  = MeasureButtonWidth(pData->okText, true);
        int wCnl_AK = MeasureButtonWidth(pData->cancelText, true);
        int totalBtnW = wOK_AK + S(AK_BTN_GAP) + wCnl_AK;
        int startX    = (cW - totalBtnW) / 2;
        CreateCustomButtonWithIcon(hwnd, IDC_ADDKEY_OK, pData->okText.c_str(), ButtonColor::Green,
            L"imageres.dll", 89, startX, y, wOK_AK, S(AK_BTN_H), hInst);
        CreateCustomButtonWithIcon(hwnd, IDC_ADDKEY_CANCEL, pData->cancelText.c_str(), ButtonColor::Red,
            L"shell32.dll", 131, startX + wOK_AK + S(AK_BTN_GAP), y,
            wCnl_AK, S(AK_BTN_H), hInst);

        // Apply system message font to all controls (labels, edits)
        {
            NONCLIENTMETRICSW ncm = {};
            ncm.cbSize = sizeof(ncm);
            SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(ncm), &ncm, 0);
            if (ncm.lfMessageFont.lfHeight < 0)
                ncm.lfMessageFont.lfHeight = (LONG)(ncm.lfMessageFont.lfHeight * 1.2f);
            ncm.lfMessageFont.lfQuality = CLEARTYPE_QUALITY;
            HFONT hCtrlFont = CreateFontIndirectW(&ncm.lfMessageFont);
            if (hCtrlFont) {
                EnumChildWindows(hwnd, [](HWND hChild, LPARAM lp) -> BOOL {
                    SendMessageW(hChild, WM_SETFONT, (WPARAM)(HFONT)lp, TRUE);
                    return TRUE;
                }, (LPARAM)hCtrlFont);
                SetPropW(hwnd, L"hCtrlFont", (HANDLE)hCtrlFont);
            }
        }

        return 0;
    }
    
    case WM_COMMAND: {
        AddKeyDialogData* pData = (AddKeyDialogData*)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
        
        switch (LOWORD(wParam)) {
        case IDC_ADDKEY_OK: {
            if (pData) {
                // Get key name
                wchar_t nameBuf[256];
                GetDlgItemTextW(hwnd, IDC_ADDKEY_NAME, nameBuf, 256);
                std::wstring newName = nameBuf;
                // Trim whitespace
                while (!newName.empty() && iswspace(newName.back())) newName.pop_back();
                size_t i = 0; while (i < newName.size() && iswspace(newName[i])) i++; if (i>0) newName = newName.substr(i);

                if (newName.empty()) {
                    MessageBoxW(hwnd, L"Key name cannot be empty.", L"Validation Error", MB_OK | MB_ICONWARNING);
                    return 0;
                }

                // Disallow path separators in key name
                if (newName.find(L'\\') != std::wstring::npos || newName.find(L'/') != std::wstring::npos) {
                    MessageBoxW(hwnd, L"Key name cannot contain path separators (\\ or /).", L"Validation Error", MB_OK | MB_ICONWARNING);
                    return 0;
                }

                // Check for duplicates under the currently selected parent
                HTREEITEM hParent = NULL;
                if (s_hRegTreeView && IsWindow(s_hRegTreeView)) {
                    hParent = TreeView_GetSelection(s_hRegTreeView);
                }
                bool duplicate = false;
                if (hParent && s_hRegTreeView) {
                    HTREEITEM hChild = TreeView_GetChild(s_hRegTreeView, hParent);
                    while (hChild) {
                        wchar_t text[256];
                        TVITEMW item = {};
                        item.mask = TVIF_TEXT;
                        item.hItem = hChild;
                        item.pszText = text;
                        item.cchTextMax = 256;
                        TreeView_GetItem(s_hRegTreeView, &item);
                        std::wstring existing = text;
                        if (existing == newName) {
                            // If editing and name equals original default, allow
                            if (pData->defaultKeyName != newName) {
                                duplicate = true;
                                break;
                            }
                        }
                        hChild = TreeView_GetNextSibling(s_hRegTreeView, hChild);
                    }
                }
                if (duplicate) {
                    MessageBoxW(hwnd, L"A key with this name already exists under the selected parent.", L"Validation Error", MB_OK | MB_ICONWARNING);
                    return 0;
                }

                pData->keyName = newName;
                pData->okClicked = true;
            }
            DestroyWindow(hwnd);
            return 0;
        }
        
        case IDC_ADDKEY_CANCEL:
            DestroyWindow(hwnd);
            return 0;
        }
        break;
    }
    
    case WM_DRAWITEM: {
        LPDRAWITEMSTRUCT dis = (LPDRAWITEMSTRUCT)lParam;
        if (dis->CtlID == IDC_ADDKEY_OK || dis->CtlID == IDC_ADDKEY_CANCEL) {
            ButtonColor color = (ButtonColor)GetWindowLongPtr(dis->hwndItem, GWLP_USERDATA);
            NONCLIENTMETRICSW ncm = {};
            ncm.cbSize = sizeof(ncm);
            SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(ncm), &ncm, 0);
            if (ncm.lfMessageFont.lfHeight < 0)
                ncm.lfMessageFont.lfHeight = (LONG)(ncm.lfMessageFont.lfHeight * 1.2f);
            ncm.lfMessageFont.lfWeight = FW_BOLD;
            ncm.lfMessageFont.lfQuality = CLEARTYPE_QUALITY;
            HFONT hFont = CreateFontIndirectW(&ncm.lfMessageFont);
            LRESULT result = DrawCustomButton(dis, color, hFont);
            if (hFont) DeleteObject(hFont);
            return result;
        }
        break;
    }

    case WM_DESTROY: {
        HFONT hCtrlFont = (HFONT)GetPropW(hwnd, L"hCtrlFont");
        if (hCtrlFont) { DeleteObject(hCtrlFont); RemovePropW(hwnd, L"hCtrlFont"); }
        break;
    }

    case WM_CLOSE:
        DestroyWindow(hwnd);
        return 0;
    }
    
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

// ─── Notes / Description Rich-Text Editor Dialog ────────────────────────────
// Popup RichEdit editor for per-component tooltip notes shown during install.
// Supports: Bold, Italic, Underline, Subscript, Superscript, Font size, Color.
// Plain-text length is capped at 500 characters.  Saves to memory only; the
// main Save button persists to DB (via notes_rtf on ComponentRow).

struct CompNotesEditorData {
    std::wstring initRtf;    // existing RTF to show on open (empty = blank)
    std::wstring outRtf;     // RTF written back on OK
    bool         okClicked = false;
    // i18n strings
    std::wstring titleText;
    std::wstring okText;
    std::wstring cancelText;
    std::wstring charsLeftFmt; // e.g. L"%d characters left"
};

// RichEdit stream helpers ─────────────────────────────────────────────────────
struct RtfStreamBuf { const std::string *src; size_t pos; };

static DWORD CALLBACK NotesRtfReadCb(DWORD_PTR cookie, LPBYTE buf, LONG cb, LONG *pcb) {
    RtfStreamBuf *rb = (RtfStreamBuf*)cookie;
    size_t rem = rb->src->size() - rb->pos;
    LONG   n   = (LONG)(rem < (size_t)cb ? rem : (size_t)cb);
    if (n > 0) { memcpy(buf, rb->src->c_str() + rb->pos, n); rb->pos += n; }
    *pcb = n;
    return 0;
}

static DWORD CALLBACK NotesRtfWriteCb(DWORD_PTR cookie, LPBYTE buf, LONG cb, LONG *pcb) {
    std::string *s = (std::string*)cookie;
    s->append((char*)buf, cb);
    *pcb = cb;
    return 0;
}

// Load RTF into a RichEdit -  wstring (UTF-8 compatible RTF) → stream in
static void NotesEditor_StreamIn(HWND hEdit, const std::wstring& wrtf) {
    if (wrtf.empty()) { SetWindowTextW(hEdit, L""); return; }
    // RTF is ASCII-safe; convert wstring→string via WToUtf8
    std::string rtf;
    int n = WideCharToMultiByte(CP_UTF8, 0, wrtf.c_str(), -1, NULL, 0, NULL, NULL);
    if (n > 1) { rtf.resize(n - 1); WideCharToMultiByte(CP_UTF8, 0, wrtf.c_str(), -1, &rtf[0], n, NULL, NULL); }
    RtfStreamBuf rb = { &rtf, 0 };
    EDITSTREAM es   = {};
    es.dwCookie     = (DWORD_PTR)&rb;
    es.pfnCallback  = NotesRtfReadCb;
    SendMessageW(hEdit, EM_STREAMIN, SF_RTF, (LPARAM)&es);
}

// Dump RichEdit content to wstring RTF
static std::wstring NotesEditor_StreamOut(HWND hEdit) {
    std::string rtf;
    EDITSTREAM es  = {};
    es.dwCookie    = (DWORD_PTR)&rtf;
    es.pfnCallback = NotesRtfWriteCb;
    SendMessageW(hEdit, EM_STREAMOUT, SF_RTF, (LPARAM)&es);
    if (rtf.empty()) return L"";
    int n = MultiByteToWideChar(CP_UTF8, 0, rtf.c_str(), -1, NULL, 0);
    if (n <= 1) return L"";
    std::wstring out(n - 1, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, rtf.c_str(), -1, &out[0], n);
    return out;
}

// Get plain-text character count from RichEdit (for the 500-char status)
static int NotesEditor_PlainLen(HWND hEdit) {
    GETTEXTLENGTHEX gtl = {};
    gtl.flags    = GTL_NUMCHARS | GTL_PRECISE;
    gtl.codepage = 1200; // Unicode
    return (int)SendMessageW(hEdit, EM_GETTEXTLENGTHEX, (WPARAM)&gtl, 0);
}

// Toggle a CHARFORMAT2 effect bit on the current selection
static void NotesEditor_ToggleEffect(HWND hEdit, DWORD maskBit, DWORD effectBit) {
    CHARFORMAT2W cf = {};
    cf.cbSize = sizeof(cf);
    cf.dwMask = maskBit;
    SendMessageW(hEdit, EM_GETCHARFORMAT, SCF_SELECTION, (LPARAM)&cf);
    bool isOn = (cf.dwEffects & effectBit) != 0;
    cf.dwEffects = isOn ? 0 : effectBit;
    SendMessageW(hEdit, EM_SETCHARFORMAT, SCF_SELECTION, (LPARAM)&cf);
}

// Toggle sub/super (they are mutually exclusive and share CFM_SUBSCRIPT mask)
static void NotesEditor_ToggleScript(HWND hEdit, DWORD wantBit) {
    CHARFORMAT2W cf = {};
    cf.cbSize  = sizeof(cf);
    cf.dwMask  = CFM_SUBSCRIPT; // == CFM_SUPERSCRIPT == CFE_SUBSCRIPT|CFE_SUPERSCRIPT
    SendMessageW(hEdit, EM_GETCHARFORMAT, SCF_SELECTION, (LPARAM)&cf);
    bool isOn = (cf.dwEffects & wantBit) != 0;
    cf.dwEffects = isOn ? 0 : wantBit; // toggle: off clears both
    SendMessageW(hEdit, EM_SETCHARFORMAT, SCF_SELECTION, (LPARAM)&cf);
}

// Update the char-count label on the status region of the notes dialog
static void NotesEditor_UpdateStatus(HWND hwnd, HWND hEdit,
                                     const std::wstring& fmtStr) {
    int plainLen = NotesEditor_PlainLen(hEdit);
    int left     = 500 - plainLen;
    if (left < 0) left = 0;
    wchar_t buf[80];
    if (!fmtStr.empty())
        swprintf(buf, 80, fmtStr.c_str(), left);
    else
        swprintf(buf, 80, L"%d characters left", left);
    HWND hStatus = GetDlgItem(hwnd, IDC_STATUS_BAR); // re-used ID for inner label
    if (hStatus) SetWindowTextW(hStatus, buf);
}

// Font sizes offered in the combo (stored as pt values)
static const int s_notesFontSizes[] = { 8, 9, 10, 11, 12, 14, 16, 18, 20, 24, 28, 36 };
static const int s_notesFontSizeDefault = 10; // index of default (10 pt)

LRESULT CALLBACK CompNotesEditorDlgProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    static HMODULE s_hRichEdit = NULL;

    switch (msg) {
    case WM_CREATE: {
        CREATESTRUCTW*       cs    = (CREATESTRUCTW*)lParam;
        HINSTANCE            hInst = cs->hInstance;
        CompNotesEditorData* pData = (CompNotesEditorData*)cs->lpCreateParams;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)pData);

        // Load RichEdit DLL (try Msftedit first for RichEdit 4.1, fall back to 2.0)
        if (!s_hRichEdit) {
            s_hRichEdit = LoadLibraryW(L"Msftedit.dll");
            if (!s_hRichEdit) s_hRichEdit = LoadLibraryW(L"Riched20.dll");
        }
        const wchar_t* reClass = L"RichEdit20W";
        // Msftedit provides "RICHEDIT50W"; Riched20 provides "RichEdit20W"
        WNDCLASSEXW wce = {}; wce.cbSize = sizeof(wce);
        if (s_hRichEdit && GetClassInfoExW(s_hRichEdit, L"RICHEDIT50W", &wce))
            reClass = L"RICHEDIT50W";

        RECT rcC; GetClientRect(hwnd, &rcC);
        int cW = rcC.right, cH = rcC.bottom;
        int pad = S(10);

        // ── Row 1: toolbar buttons ────────────────────────────────────────────
        int tbY = pad;
        int btnSz  = S(28); // square toolbar button
        int btnGap = S(4);
        int x = pad;

        // Bold / Italic / Underline
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
        x += btnSz + S(10); // wider gap before script buttons

        // Subscript / Superscript
        HWND hSub = CreateWindowExW(0, L"BUTTON", L"X\u2082",   // X₂
            WS_CHILD|WS_VISIBLE|BS_PUSHBUTTON,
            x, tbY, btnSz+S(6), btnSz, hwnd, (HMENU)IDC_NOTES_SUBSCRIPT, hInst, NULL);
        x += btnSz+S(6) + btnGap;
        HWND hSup = CreateWindowExW(0, L"BUTTON", L"X\u00B2",   // X²
            WS_CHILD|WS_VISIBLE|BS_PUSHBUTTON,
            x, tbY, btnSz+S(6), btnSz, hwnd, (HMENU)IDC_NOTES_SUPERSCRIPT, hInst, NULL);
        x += btnSz+S(6) + S(10);

        // Font size combo
        int comboW = S(70);
        HWND hFontSize = CreateWindowExW(0, L"COMBOBOX", L"",
            WS_CHILD|WS_VISIBLE|CBS_DROPDOWNLIST|WS_VSCROLL,
            x, tbY, comboW, S(200), hwnd, (HMENU)IDC_NOTES_FONTSIZE, hInst, NULL);
        for (int i = 0; i < (int)(sizeof(s_notesFontSizes)/sizeof(s_notesFontSizes[0])); i++) {
            wchar_t sz[8]; swprintf(sz, 8, L"%d", s_notesFontSizes[i]);
            SendMessageW(hFontSize, CB_ADDSTRING, 0, (LPARAM)sz);
        }
        SendMessageW(hFontSize, CB_SETCURSEL, s_notesFontSizeDefault, 0);
        x += comboW + S(6);

        // Color button
        HWND hColor = CreateWindowExW(0, L"BUTTON", L"A\u25BC",
            WS_CHILD|WS_VISIBLE|BS_PUSHBUTTON,
            x, tbY, btnSz+S(10), btnSz, hwnd, (HMENU)IDC_NOTES_COLOR, hInst, NULL);
        (void)hColor;

        // ── Row 2: RichEdit ───────────────────────────────────────────────────
        int editY = tbY + btnSz + btnGap;
        int statusH = S(22);
        int btnRowH = S(38);
        int editH = cH - editY - S(6) - statusH - S(6) - btnRowH - pad;
        HWND hEdit = CreateWindowExW(WS_EX_CLIENTEDGE, reClass, L"",
            WS_CHILD|WS_VISIBLE|WS_VSCROLL|ES_MULTILINE|ES_WANTRETURN|ES_AUTOVSCROLL,
            pad, editY, cW - 2*pad, editH,
            hwnd, (HMENU)IDC_NOTES_EDIT, hInst, NULL);
        // Limit to 500 plain-text chars
        SendMessageW(hEdit, EM_EXLIMITTEXT, 0, 500);
        // Default font: Segoe UI 10pt
        CHARFORMAT2W cfDef = {};
        cfDef.cbSize      = sizeof(cfDef);
        cfDef.dwMask      = CFM_FACE | CFM_SIZE | CFM_CHARSET;
        cfDef.yHeight     = 10 * 20; // 10pt in twips (half-points*2)
        cfDef.bCharSet    = DEFAULT_CHARSET;
        wcscpy_s(cfDef.szFaceName, L"Segoe UI");
        SendMessageW(hEdit, EM_SETCHARFORMAT, SCF_ALL, (LPARAM)&cfDef);
        // Load existing RTF
        if (!pData->initRtf.empty())
            NotesEditor_StreamIn(hEdit, pData->initRtf);
        // Enable change notification
        SendMessageW(hEdit, EM_SETEVENTMASK, 0, ENM_CHANGE);

        // ── Row 3: status bar (char count) ───────────────────────────────────
        int statusY = editY + editH + S(6);
        CreateWindowExW(WS_EX_STATICEDGE, L"STATIC", L"",
            WS_CHILD|WS_VISIBLE|SS_LEFT|SS_CENTERIMAGE,
            pad, statusY, cW - 2*pad, statusH,
            hwnd, (HMENU)IDC_STATUS_BAR, hInst, NULL);
        NotesEditor_UpdateStatus(hwnd, hEdit, pData->charsLeftFmt);

        // ── Row 4: OK / Cancel ────────────────────────────────────────────────
        int btnY2 = statusY + statusH + S(6);
        int btnW  = S(130);
        int totalBtnW = 2*btnW + S(10);
        int startX    = (cW - totalBtnW) / 2;
        std::wstring okTxt  = pData->okText.empty()     ? L"Save"   : pData->okText;
        std::wstring canTxt = pData->cancelText.empty() ? L"Cancel" : pData->cancelText;
        CreateCustomButtonWithIcon(hwnd, IDC_NOTES_OK,     okTxt.c_str(),  ButtonColor::Green,
            L"imageres.dll", 89,  startX,             btnY2, btnW, S(38), hInst);
        CreateCustomButtonWithIcon(hwnd, IDC_NOTES_CANCEL, canTxt.c_str(), ButtonColor::Red,
            L"shell32.dll",  131, startX+btnW+S(10),  btnY2, btnW, S(38), hInst);

        // Tooltips for toolbar buttons
        SetButtonTooltip(hBold,   L"Bold");
        SetButtonTooltip(hItalic, L"Italic");
        SetButtonTooltip(hUnder,  L"Underline");
        SetButtonTooltip(hSub,    L"Subscript (e.g. H\u2082O)");
        SetButtonTooltip(hSup,    L"Superscript (e.g. m\u00B2)");
        SetButtonTooltip(hFontSize, L"Font size");
        SetButtonTooltip(GetDlgItem(hwnd, IDC_NOTES_COLOR), L"Text color");

        // Apply system UI font to toolbar controls (not the richedit)
        NONCLIENTMETRICSW ncm = {}; ncm.cbSize = sizeof(ncm);
        SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(ncm), &ncm, 0);
        ncm.lfMessageFont.lfQuality = CLEARTYPE_QUALITY;
        HFONT hF = CreateFontIndirectW(&ncm.lfMessageFont);
        if (hF) {
            // Apply to all children except the richedit (it manages its own font)
            auto applyFont = [](HWND hC, LPARAM lp) -> BOOL {
                wchar_t cls[64] = {};
                GetClassNameW(hC, cls, 64);
                if (_wcsicmp(cls, L"RichEdit20W") != 0 && _wcsicmp(cls, L"RICHEDIT50W") != 0)
                    SendMessageW(hC, WM_SETFONT, (WPARAM)(HFONT)lp, TRUE);
                return TRUE;
            };
            EnumChildWindows(hwnd, applyFont, (LPARAM)hF);
            // Bold font for Bold button
            NONCLIENTMETRICSW ncmB = ncm;
            ncmB.lfMessageFont.lfWeight = FW_BOLD;
            HFONT hFB = CreateFontIndirectW(&ncmB.lfMessageFont);
            if (hFB) { SendMessageW(hBold, WM_SETFONT, (WPARAM)hFB, TRUE); SetPropW(hwnd, L"hNotesBoldFont", hFB); }
            // Italic font for Italic button
            NONCLIENTMETRICSW ncmI = ncm;
            ncmI.lfMessageFont.lfItalic = TRUE;
            HFONT hFI = CreateFontIndirectW(&ncmI.lfMessageFont);
            if (hFI) { SendMessageW(hItalic, WM_SETFONT, (WPARAM)hFI, TRUE); SetPropW(hwnd, L"hNotesItalicFont", hFI); }
            SetPropW(hwnd, L"hNotesFont", hF);
        }
        SetFocus(hEdit);
        return 0;
    }

    case WM_COMMAND: {
        CompNotesEditorData* pData = (CompNotesEditorData*)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
        if (!pData) break;
        HWND hEdit = GetDlgItem(hwnd, IDC_NOTES_EDIT);
        int  wmId  = LOWORD(wParam);
        int  wmEv  = HIWORD(wParam);

        if (wmId == IDC_NOTES_CANCEL || wmId == IDCANCEL) {
            DestroyWindow(hwnd); return 0;
        }
        if (wmId == IDC_NOTES_OK) {
            pData->outRtf     = hEdit ? NotesEditor_StreamOut(hEdit) : L"";
            pData->okClicked  = true;
            DestroyWindow(hwnd); return 0;
        }
        if (wmId == IDC_NOTES_BOLD      && hEdit) { NotesEditor_ToggleEffect(hEdit, CFM_BOLD,      CFE_BOLD);      return 0; }
        if (wmId == IDC_NOTES_ITALIC    && hEdit) { NotesEditor_ToggleEffect(hEdit, CFM_ITALIC,    CFE_ITALIC);    return 0; }
        if (wmId == IDC_NOTES_UNDERLINE && hEdit) { NotesEditor_ToggleEffect(hEdit, CFM_UNDERLINE, CFE_UNDERLINE); return 0; }
        if (wmId == IDC_NOTES_SUBSCRIPT   && hEdit) { NotesEditor_ToggleScript(hEdit, CFE_SUBSCRIPT);   return 0; }
        if (wmId == IDC_NOTES_SUPERSCRIPT && hEdit) { NotesEditor_ToggleScript(hEdit, CFE_SUPERSCRIPT); return 0; }
        if (wmId == IDC_NOTES_COLOR && hEdit) {
            CHOOSECOLORW cc = {};
            static COLORREF s_custColors[16] = {};
            cc.lStructSize  = sizeof(cc);
            cc.hwndOwner    = hwnd;
            cc.lpCustColors = s_custColors;
            cc.Flags        = CC_FULLOPEN | CC_RGBINIT;
            // Seed with current selection colour
            CHARFORMAT2W cfC = {}; cfC.cbSize = sizeof(cfC); cfC.dwMask = CFM_COLOR;
            SendMessageW(hEdit, EM_GETCHARFORMAT, SCF_SELECTION, (LPARAM)&cfC);
            cc.rgbResult = cfC.crTextColor;
            if (ChooseColorW(&cc)) {
                CHARFORMAT2W cfSet = {}; cfSet.cbSize = sizeof(cfSet);
                cfSet.dwMask     = CFM_COLOR;
                cfSet.crTextColor = cc.rgbResult;
                cfSet.dwEffects  = 0; // clear CFE_AUTOCOLOR
                SendMessageW(hEdit, EM_SETCHARFORMAT, SCF_SELECTION, (LPARAM)&cfSet);
            }
            return 0;
        }
        if (wmId == IDC_NOTES_FONTSIZE && wmEv == CBN_SELCHANGE) {
            HWND hCombo = GetDlgItem(hwnd, IDC_NOTES_FONTSIZE);
            int  sel    = (int)SendMessageW(hCombo, CB_GETCURSEL, 0, 0);
            if (sel >= 0 && sel < (int)(sizeof(s_notesFontSizes)/sizeof(s_notesFontSizes[0]))) {
                CHARFORMAT2W cfSz = {}; cfSz.cbSize = sizeof(cfSz);
                cfSz.dwMask = CFM_SIZE;
                cfSz.yHeight = s_notesFontSizes[sel] * 20; // twips
                SendMessageW(hEdit, EM_SETCHARFORMAT, SCF_SELECTION, (LPARAM)&cfSz);
            }
            return 0;
        }
        // EN_CHANGE from RichEdit  → update status
        if (wmId == IDC_NOTES_EDIT && wmEv == EN_CHANGE && hEdit) {
            NotesEditor_UpdateStatus(hwnd, hEdit, pData->charsLeftFmt);
        }
        break;
    }

    case WM_DRAWITEM: {
        LPDRAWITEMSTRUCT dis = (LPDRAWITEMSTRUCT)lParam;
        if (dis->CtlID == IDC_NOTES_OK || dis->CtlID == IDC_NOTES_CANCEL) {
            ButtonColor color = (ButtonColor)GetWindowLongPtr(dis->hwndItem, GWLP_USERDATA);
            NONCLIENTMETRICSW ncm = {}; ncm.cbSize = sizeof(ncm);
            SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(ncm), &ncm, 0);
            if (ncm.lfMessageFont.lfHeight < 0)
                ncm.lfMessageFont.lfHeight = (LONG)(ncm.lfMessageFont.lfHeight * 1.2f);
            ncm.lfMessageFont.lfWeight  = FW_BOLD;
            ncm.lfMessageFont.lfQuality = CLEARTYPE_QUALITY;
            HFONT hFont = CreateFontIndirectW(&ncm.lfMessageFont);
            LRESULT res = DrawCustomButton(dis, color, hFont);
            if (hFont) DeleteObject(hFont);
            return res;
        }
        break;
    }

    case WM_CTLCOLORSTATIC: {
        HDC hdc = (HDC)wParam;
        SetBkColor(hdc, GetSysColor(COLOR_WINDOW));
        SetTextColor(hdc, GetSysColor(COLOR_WINDOWTEXT));
        return (LRESULT)GetSysColorBrush(COLOR_WINDOW);
    }

    case WM_DESTROY: {
        HFONT hF  = (HFONT)GetPropW(hwnd, L"hNotesFont");      if (hF)  { DeleteObject(hF);  RemovePropW(hwnd, L"hNotesFont"); }
        HFONT hFB = (HFONT)GetPropW(hwnd, L"hNotesBoldFont");  if (hFB) { DeleteObject(hFB); RemovePropW(hwnd, L"hNotesBoldFont"); }
        HFONT hFI = (HFONT)GetPropW(hwnd, L"hNotesItalicFont");if (hFI) { DeleteObject(hFI); RemovePropW(hwnd, L"hNotesItalicFont"); }
        break;
    }
    case WM_CLOSE:
        DestroyWindow(hwnd); return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

// Helper: open the notes editor as a modal popup.
// Returns true and writes RTF into outRtf if the user clicked Save.
static bool OpenNotesEditor(HWND hwndParent, const std::wstring& initRtf,
                             std::wstring& outRtf, const std::wstring& title)
{
    CompNotesEditorData nd;
    nd.initRtf       = initRtf;
    nd.titleText     = title.empty() ? L"Edit Notes" : title;
    nd.okText        = L"Save";
    nd.cancelText    = L"Cancel";
    nd.charsLeftFmt  = L"%d characters left";

    WNDCLASSEXW wc = {}; wc.cbSize = sizeof(wc);
    if (!GetClassInfoExW(GetModuleHandleW(NULL), L"CompNotesEditor", &wc)) {
        wc.lpfnWndProc   = CompNotesEditorDlgProc;
        wc.hInstance     = GetModuleHandleW(NULL);
        wc.lpszClassName = L"CompNotesEditor";
        wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
        wc.hCursor       = LoadCursor(NULL, IDC_ARROW);
        RegisterClassExW(&wc);
    }

    int dlgW = S(560), dlgH = S(440);
    RECT rcPar; GetWindowRect(hwndParent, &rcPar);
    int x = rcPar.left + (rcPar.right  - rcPar.left - dlgW) / 2;
    int y = rcPar.top  + (rcPar.bottom - rcPar.top  - dlgH) / 2;
    HWND hDlg = CreateWindowExW(WS_EX_TOPMOST | WS_EX_TOOLWINDOW,
        L"CompNotesEditor", nd.titleText.c_str(),
        WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_VISIBLE,
        x, y, dlgW, dlgH, hwndParent, NULL, GetModuleHandleW(NULL), &nd);

    MSG m;
    while (GetMessageW(&m, NULL, 0, 0) > 0) {
        if (!IsWindow(hDlg)) break;
        TranslateMessage(&m); DispatchMessageW(&m);
    }
    if (nd.okClicked) { outRtf = nd.outRtf; return true; }
    return false;
}

// ─── Folder Component Edit Dialog ───────────────────────────────────────────
// Lightweight dialog for editing the required-state of an entire folder subtree.

struct CompFolderDlgData {
    std::wstring folderName;     // shown as read-only label
    int          initRequired;   // 0 = No, 1 = Yes, -1 = Mixed
    std::wstring titleText;
    std::wstring requiredLabel;
    std::wstring cascadeHint;
    std::wstring depsLabel;
    std::wstring chooseDepsText;
    std::wstring okText;
    std::wstring cancelText;
    bool okClicked   = false;
    int  outRequired    = 0;
    int  outPreselected = 0;
    int  hintH          = 0; // pre-measured hint text height (px, already DPI-scaled)
    // Dependencies
    std::vector<ComponentRow> otherComponents;    // other comps in project for picker
    std::vector<int>          initDependencyIds;  // pre-selected dep IDs
    std::vector<int>          outDependencyIds;   // working state (updated by picker)
    // Notes (rich text)
    std::wstring initNotesRtf;  // loaded from component row
    std::wstring outNotesRtf;   // result after editor OK
    // Pre-selected state
    int          initPreselected = 0;  // 1 = pre-selected
    std::wstring preselectedLabel;
    // Identity of the folder being edited — forwarded to the dep picker for exclusion.
    const TreeNodeSnapshot* excludeNode = nullptr;
    std::wstring            sectionName; // VFS section owning this folder (for dep picker exclusion)
    int                     projectId = 0; // for dep picker auto-component creation
};

// ── CompFolderEdit dialog layout constants (design-px at 96 DPI) ──────────────
static const int CFE_PAD_H    = 20;
static const int CFE_PAD_T    = 20;
static const int CFE_PAD_B    = 20;
static const int CFE_CONT_W   = 520; // content width (label, checkbox, hint)
static const int CFE_NAME_H   = 28;  // folder name label height
static const int CFE_GAP_NR   = 10;  // gap: name → required checkbox
static const int CFE_CHECK_H  = 26;  // checkbox height
static const int CFE_GAP_RPS  = 6;   // gap: Required → Pre-selected
static const int CFE_GAP_RC   = 10;  // gap: Pre-selected → hint
static const int CFE_GAP_HB   = 14;  // gap: deps listbox → buttons
static const int CFE_BTN_H    = 38;
static const int CFE_BTN_W0   = 155; // OK
static const int CFE_BTN_W1   = 155; // Cancel
static const int CFE_BTN_GAP  = 10;
// Dependencies section (between cascade hint and buttons)
static const int CFE_GAP_CD       = 14;  // gap: cascade hint → deps label row
static const int CFE_DEPS_ROW_H   = 26;  // deps label / Choose button row height
static const int CFE_GAP_LD       = 6;   // gap: deps row → deps listbox
static const int CFE_DEPLIST_H      = 130;  // deps listview height (header + 5 rows)
static const int CFE_DEP_BTNS_ROW_H = 34;   // Choose + Remove buttons row height
static const int CFE_GAP_LB         = 6;    // gap: list → dep-buttons row
static const int CFE_GAP_DN       = 10;  // gap: deps listbox → notes button
static const int CFE_NOTES_BTN_H  = 32;  // Notes button height
static const int CFE_GAP_NB2      = 14;  // gap: notes button → OK/Cancel

// ─── Folder Dependency Picker Dialog ─────────────────────────────────────────
// Popup tree-view dialog that lets the user pick one or more OTHER components as
// dependencies of the current folder component.  Components are grouped by their
// destination section (dest_path) as root nodes; each component is a child node
// with a TVS_CHECKBOXES checkbox.  Section header nodes have their checkbox
// hidden (state-image set to 0) so they act as visual groups only.

struct CompFolderDepPickerData {
    const std::vector<ComponentRow>* components; // other components for file-level rows
    std::vector<int>                 initDeps;   // pre-checked component IDs
    const TreeNodeSnapshot*          excludeNode = nullptr; // VFS snapshot node to exclude (+ its subtree)
    bool                             okClicked = false;
    std::vector<int>                 outDeps;    // result IDs

    // VFS files that have no corresponding file-type ComponentRow yet.
    // On OK, a real component is auto-created for each selected entry.
    struct AutoFile { std::wstring srcPath; std::wstring dispName; std::wstring destSection; };
    std::vector<AutoFile> autoFiles;
    static constexpr int kAutoFileBase = 1000000; // synthetic lParam range start
    int projectId = 0;   // current project (for auto-creating file components)
};

// ─── GetVirtualPathForComp ───────────────────────────────────────────────────
// Searches the live VFS snapshots to build a full virtual path string for the
// given component  (e.g. "AskAtInstall\WinProgramManager\WinProgramManager.db").
// Falls back to dest_path\display_name when not found in any snapshot.
static std::wstring GetVirtualPathForComp(const ComponentRow& c)
{
    struct W {
        static bool Find(const std::vector<TreeNodeSnapshot>& nodes,
                         const std::wstring& srcPath, bool isFolder,
                         const std::wstring& prefix, std::wstring& out)
        {
            for (const auto& snap : nodes) {
                std::wstring p = prefix.empty() ? snap.text
                                                : prefix + L"\\" + snap.text;
                if (isFolder && !snap.fullPath.empty() &&
                    _wcsicmp(snap.fullPath.c_str(), srcPath.c_str()) == 0) {
                    out = p; return true;
                }
                if (!isFolder) {
                    for (const auto& vf : snap.virtualFiles) {
                        if (_wcsicmp(vf.sourcePath.c_str(), srcPath.c_str()) == 0) {
                            size_t sep = srcPath.rfind(L'\\');
                            std::wstring fname = (sep != std::wstring::npos)
                                ? srcPath.substr(sep + 1) : srcPath;
                            out = p + L"\\" + fname; return true;
                        }
                    }
                }
                if (Find(snap.children, srcPath, isFolder, p, out)) return true;
            }
            return false;
        }
    };
    const struct { const wchar_t* label; const std::vector<TreeNodeSnapshot>* snaps; } secs[] = {
        { L"Program Files",     &s_treeSnapshot_ProgramFiles },
        { L"ProgramData",       &s_treeSnapshot_ProgramData  },
        { L"AppData (Roaming)", &s_treeSnapshot_AppData      },
        { L"AskAtInstall",      &s_treeSnapshot_AskAtInstall },
    };
    bool isFolder = (c.source_type == L"folder");
    std::wstring out;
    for (const auto& s : secs) {
        if (W::Find(*s.snaps, c.source_path, isFolder, std::wstring(s.label), out))
            return out;
    }
    // Fallback: section\leaf-name
    return (c.dest_path.empty() ? L"" : c.dest_path + L"\\") + c.display_name;
}

// Helper: rebuild the deps display listview (IDC_FOLDER_DLG_DEPS_LIST) in the
// folder-edit dialog from pData->outDependencyIds + pData->otherComponents.
static void RefreshFolderDepsListbox(HWND hwndFolderDlg, CompFolderDlgData* pData)
{
    HWND hList = GetDlgItem(hwndFolderDlg, IDC_FOLDER_DLG_DEPS_LIST);
    if (!hList) return;
    ListView_DeleteAllItems(hList);
    const auto& loc = MainWindow::GetLocale();
    auto locStr = [&](const wchar_t* k, const wchar_t* fb) -> std::wstring {
        auto it = loc.find(k);
        return (it != loc.end()) ? it->second : fb;
    };
    std::wstring lblFolder = locStr(L"comp_type_folder", L"Folder");
    std::wstring lblFile   = locStr(L"comp_type_file",   L"File");

    // Build the set of folder source-paths already in the dep list.
    // File deps whose path sits inside one of these folders are hidden
    // from the summary list (they can be seen via the double-click popup).
    std::vector<std::wstring> depFolderPaths;
    for (int did : pData->outDependencyIds)
        for (const auto& oc : pData->otherComponents)
            if (oc.id == did && oc.source_type == L"folder" && !oc.source_path.empty())
                depFolderPaths.push_back(oc.source_path);

    auto isCoveredByFolder = [&](const ComponentRow& fc) -> bool {
        if (fc.source_type != L"file") return false;
        for (const auto& fp : depFolderPaths) {
            const std::wstring& sp = fc.source_path;
            if (sp.size() > fp.size() &&
                _wcsnicmp(sp.c_str(), fp.c_str(), fp.size()) == 0 &&
                (sp[fp.size()] == L'\\' || sp[fp.size()] == L'/'))
                return true;
        }
        return false;
    };

    int row = 0;
    for (int depId : pData->outDependencyIds) {
        for (const auto& oc : pData->otherComponents) {
            if (oc.id != depId) continue;
            if (isCoveredByFolder(oc)) break; // hidden under its folder row
            LVITEMW lvi = {}; lvi.mask = LVIF_TEXT | LVIF_PARAM;
            lvi.iItem   = row; lvi.iSubItem = 0;
            lvi.pszText = const_cast<LPWSTR>(oc.display_name.c_str());
            lvi.lParam  = (LPARAM)depId;
            ListView_InsertItem(hList, &lvi);
            std::wstring typeStr = (oc.source_type == L"folder") ? lblFolder : lblFile;
            ListView_SetItemText(hList, row, 1, const_cast<LPWSTR>(typeStr.c_str()));
            ++row;
            break;
        }
    }
    if (row == 0) {
        std::wstring noneStr = locStr(L"comp_deps_none", L"(none)");
        LVITEMW lvi = {}; lvi.mask = LVIF_TEXT | LVIF_PARAM;
        lvi.iItem = 0; lvi.iSubItem = 0;
        lvi.pszText = const_cast<LPWSTR>(noneStr.c_str());
        lvi.lParam  = 0;
        ListView_InsertItem(hList, &lvi);
    }
}

LRESULT CALLBACK CompFolderDepPickerDlgProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg) {
    case WM_CREATE: {
        CREATESTRUCTW*           cs    = (CREATESTRUCTW*)lParam;
        HINSTANCE                hInst = cs->hInstance;
        CompFolderDepPickerData* pData = (CompFolderDepPickerData*)cs->lpCreateParams;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)pData);

        RECT rcC; GetClientRect(hwnd, &rcC);
        int cW = rcC.right, cH = rcC.bottom;
        int pad = S(10), btnH = S(36), btnW = S(130);

        // Tree view fills client minus padding and button row
        int treeH = cH - 2*pad - btnH - pad;
        HWND hTree = CreateWindowExW(WS_EX_CLIENTEDGE, WC_TREEVIEW, L"",
            WS_CHILD | WS_VISIBLE | WS_VSCROLL |
            TVS_HASLINES | TVS_HASBUTTONS | TVS_LINESATROOT |
            TVS_SHOWSELALWAYS | TVS_CHECKBOXES,
            pad, pad, cW - 2*pad, treeH,
            hwnd, (HMENU)IDC_FDDP_TREE, hInst, NULL);

        // Replace native TVS_CHECKBOXES bitmaps with custom theme-aware GDI images.
        UpdateTreeViewCheckboxImages(hTree, S(16));

        // Build icon image list: 0 = folder, 1 = generic file.
        // Each entry is (iconSz + 4) px wide with the icon drawn at x=4, leaving a
        // 4 px transparent gap between the TVS_CHECKBOXES checkbox and the icon.
        {
            int iconSz  = GetSystemMetrics(SM_CXSMICON);
            int bmpW    = iconSz + 4;  // extra left padding = gap after checkbox
            HIMAGELIST hImgList = ImageList_Create(bmpW, iconSz, ILC_COLOR32 | ILC_MASK, 2, 0);
            if (hImgList) {
                // Helper: add an icon to the list with 4 px left padding.
                auto AddPadded = [&](HICON hIco) {
                    if (!hIco) return;
                    HDC hScreen = GetDC(NULL);
                    BITMAPINFO bmi = {};
                    bmi.bmiHeader.biSize        = sizeof(BITMAPINFOHEADER);
                    bmi.bmiHeader.biWidth       = bmpW;
                    bmi.bmiHeader.biHeight      = -iconSz;  // top-down
                    bmi.bmiHeader.biPlanes      = 1;
                    bmi.bmiHeader.biBitCount    = 32;
                    bmi.bmiHeader.biCompression = BI_RGB;
                    void* pBits = nullptr;
                    HBITMAP hPad = CreateDIBSection(hScreen, &bmi, DIB_RGB_COLORS, &pBits, NULL, 0);
                    if (hPad && pBits) {
                        memset(pBits, 0, bmpW * iconSz * 4);  // fully transparent
                        HDC hMem = CreateCompatibleDC(hScreen);
                        HBITMAP hOld = (HBITMAP)SelectObject(hMem, hPad);
                        DrawIconEx(hMem, 4, 0, hIco, iconSz, iconSz, 0, NULL, DI_NORMAL);
                        SelectObject(hMem, hOld);
                        DeleteDC(hMem);
                        ImageList_Add(hImgList, hPad, NULL);
                        DeleteObject(hPad);
                    }
                    ReleaseDC(NULL, hScreen);
                };

                SHFILEINFOW sfi = {};
                SHGetFileInfoW(L"x", FILE_ATTRIBUTE_DIRECTORY, &sfi, sizeof(sfi),
                    SHGFI_ICON | SHGFI_SMALLICON | SHGFI_USEFILEATTRIBUTES);
                AddPadded(sfi.hIcon);
                if (sfi.hIcon) DestroyIcon(sfi.hIcon);

                ZeroMemory(&sfi, sizeof(sfi));
                SHGetFileInfoW(L"x", FILE_ATTRIBUTE_NORMAL, &sfi, sizeof(sfi),
                    SHGFI_ICON | SHGFI_SMALLICON | SHGFI_USEFILEATTRIBUTES);
                AddPadded(sfi.hIcon);
                if (sfi.hIcon) DestroyIcon(sfi.hIcon);

                TreeView_SetImageList(hTree, hImgList, TVSIL_NORMAL);
                SetPropW(hwnd, L"hFDPImgList", (HANDLE)hImgList);
            }
        }

        // Populate from the live VFS snapshots so the full folder hierarchy is visible
        // even when folders have no ComponentRow yet.  The folder being edited
        // (excludeNode) is identified by pointer identity into s_treeSnapshot_*.
        // Its entire subtree is suppressed by skipping the recursive descent.
        //
        // If the user hasn't visited the Files page this session the snapshots may be
        // empty — rebuild them from DB now so AskAtInstall sub-folders and files
        // always appear.
        MainWindow::EnsureTreeSnapshotsFromDb();
        // Ensure every snapshot node has virtualFiles populated before walking:
        //   Virtual/AskAtInstall nodes → already filled from DB above.
        //   Real-path disk folders     → filled here via disk scan (fallback).
        // Running for all 4 sections means already-loaded snapshots (Files page
        // visited this session) are also covered, not just DB-rebuilt ones.
        PopulateSnapshotFilesFromDisk(s_treeSnapshot_ProgramFiles);
        PopulateSnapshotFilesFromDisk(s_treeSnapshot_ProgramData);
        PopulateSnapshotFilesFromDisk(s_treeSnapshot_AppData);
        PopulateSnapshotFilesFromDisk(s_treeSnapshot_AskAtInstall);
        {
            // The 4 canonical sections wired to the global VFS snapshots.
            struct SecDef { const wchar_t* label; const std::vector<TreeNodeSnapshot>* snaps; };
            const SecDef secDefs[] = {
                { L"Program Files",     &s_treeSnapshot_ProgramFiles },
                { L"ProgramData",       &s_treeSnapshot_ProgramData  },
                { L"AppData (Roaming)", &s_treeSnapshot_AppData      },
                { L"AskAtInstall",      &s_treeSnapshot_AskAtInstall },
            };

            // Helper: set checkbox state on a tree item.
            auto setStateImg = [&](HTREEITEM h, int idx) {
                TVITEMW t = {}; t.mask = TVIF_STATE; t.hItem = h;
                t.stateMask = TVIS_STATEIMAGEMASK; t.state = INDEXTOSTATEIMAGEMASK(idx);
                TreeView_SetItem(hTree, &t);
            };

            // Helper: insert one file component as a checkable child of hParent.
            auto insertFileNode = [&](HTREEITEM hParent, const ComponentRow& c) {
                TVINSERTSTRUCTW tvF = {};
                tvF.hParent = hParent; tvF.hInsertAfter = TVI_LAST;
                tvF.item.mask = TVIF_TEXT|TVIF_PARAM|TVIF_IMAGE|TVIF_SELECTEDIMAGE;
                tvF.item.pszText = const_cast<LPWSTR>(c.display_name.c_str());
                tvF.item.lParam  = (LPARAM)c.id;
                tvF.item.iImage  = 1; tvF.item.iSelectedImage = 1;
                HTREEITEM hFile = TreeView_InsertItem(hTree, &tvF);
                if (hFile) {
                    bool chk = false;
                    for (int d : pData->initDeps) if (d == c.id) { chk=true; break; }
                    setStateImg(hFile, chk ? 2 : 1);
                }
            };

            for (const auto& sd : secDefs) {
                if (!sd.snaps || sd.snaps->empty()) continue;

                // Create non-checkable section header (lParam == -1).
                TVINSERTSTRUCTW tvSec = {};
                tvSec.hParent = TVI_ROOT; tvSec.hInsertAfter = TVI_LAST;
                tvSec.item.mask = TVIF_TEXT|TVIF_PARAM|TVIF_IMAGE|TVIF_SELECTEDIMAGE;
                tvSec.item.pszText = const_cast<LPWSTR>(sd.label);
                tvSec.item.lParam  = (LPARAM)-1;
                tvSec.item.iImage  = 0; tvSec.item.iSelectedImage = 0;
                HTREEITEM hSec = TreeView_InsertItem(hTree, &tvSec);
                if (!hSec) continue;
                setStateImg(hSec, 0);

                // Walk the VFS snapshot and insert folder nodes with their file children
                // inline — no separate second pass.  This guarantees that each file
                // appears under exactly the folder it belongs to in the Files page tree.
                std::function<void(HTREEITEM, const std::vector<TreeNodeSnapshot>&, const std::wstring&)> addVFS;
                addVFS = [&](HTREEITEM hParent, const std::vector<TreeNodeSnapshot>& nodes, const std::wstring& secLabel) {
                    for (const auto& snap : nodes) {
                        // The folder being edited must not be selectable as its own
                        // dependency, but its children (subfolders/files) must still
                        // appear — skip just this node's insertion and recurse into
                        // its children under the current parent.
                        if (pData->excludeNode) {
                            const std::wstring& xp = pData->excludeNode->fullPath;
                            bool isExcluded = false;
                            if (!xp.empty()) {
                                if (!snap.fullPath.empty() &&
                                    _wcsicmp(snap.fullPath.c_str(), xp.c_str()) == 0)
                                    isExcluded = true;
                            } else if (&snap == pData->excludeNode) {
                                isExcluded = true;
                            }
                            if (isExcluded) {
                                // Still recurse so children are visible and selectable.
                                addVFS(hParent, snap.children, secLabel);
                                continue;
                            }
                        }

                        // ── Folder node ──────────────────────────────────────────────
                        // Find the folder-type ComponentRow so the node is checkable.
                        int compId = 0;
                        if (pData->components) {
                            for (const auto& c : *pData->components) {
                                if (c.source_type != L"folder") continue;
                                if (!snap.fullPath.empty()) {
                                    if (_wcsicmp(c.source_path.c_str(),
                                                 snap.fullPath.c_str()) == 0) {
                                        compId = c.id; break;
                                    }
                                } else if (!snap.text.empty()) {
                                    if (_wcsicmp(c.display_name.c_str(),
                                                 snap.text.c_str()) == 0) {
                                        compId = c.id; break;
                                    }
                                }
                            }
                        }

                        LPARAM lp = (compId > 0) ? (LPARAM)compId : (LPARAM)-2;
                        TVINSERTSTRUCTW tv = {};
                        tv.hParent = hParent; tv.hInsertAfter = TVI_LAST;
                        tv.item.mask = TVIF_TEXT|TVIF_PARAM|TVIF_IMAGE|TVIF_SELECTEDIMAGE;
                        std::wstring label = snap.text.empty() ? snap.fullPath : snap.text;
                        tv.item.pszText = const_cast<LPWSTR>(label.c_str());
                        tv.item.lParam  = lp;
                        tv.item.iImage  = 0; tv.item.iSelectedImage = 0;
                        HTREEITEM hNode = TreeView_InsertItem(hTree, &tv);
                        if (!hNode) continue;

                        if (lp > 0) {
                            bool chk = false;
                            for (int d : pData->initDeps) if (d == compId) { chk=true; break; }
                            setStateImg(hNode, chk ? 2 : 1);
                        } else {
                            setStateImg(hNode, 1); // structural folder — show unchecked
                        }

                        // ── Inline file children ─────────────────────────────────────
                        // snap.virtualFiles is always populated by this point:
                        //   Virtual/AskAtInstall nodes → from DB via EnsureTreeSnapshotsFromDb
                        //   Real-path disk folders     → from PopulateSnapshotFilesFromDisk
                        // For each entry match against a ComponentRow (use it) or
                        // insert an auto-file node (component auto-created on OK).
                        {
                            auto insertAutoFileNode = [&](HTREEITEM hP,
                                                          const std::wstring& srcPath,
                                                          const std::wstring& sec) {
                                std::wstring fname = srcPath;
                                size_t sl = fname.rfind(L'\\');
                                if (sl != std::wstring::npos) fname = fname.substr(sl + 1);
                                else { sl = fname.rfind(L'/'); if (sl != std::wstring::npos) fname = fname.substr(sl + 1); }
                                int autoIdx = (int)pData->autoFiles.size();
                                pData->autoFiles.push_back({srcPath, fname, sec});
                                TVINSERTSTRUCTW tvF = {};
                                tvF.hParent = hP; tvF.hInsertAfter = TVI_LAST;
                                tvF.item.mask = TVIF_TEXT|TVIF_PARAM|TVIF_IMAGE|TVIF_SELECTEDIMAGE;
                                tvF.item.pszText = const_cast<LPWSTR>(fname.c_str());
                                tvF.item.lParam  = (LPARAM)(CompFolderDepPickerData::kAutoFileBase + autoIdx);
                                tvF.item.iImage  = 1; tvF.item.iSelectedImage = 1;
                                HTREEITEM hF = TreeView_InsertItem(hTree, &tvF);
                                if (hF) setStateImg(hF, 1);
                            };

                            for (const auto& vf : snap.virtualFiles) {
                                if (vf.sourcePath.empty()) continue;
                                const ComponentRow* found = nullptr;
                                if (pData->components) {
                                    for (const auto& c : *pData->components) {
                                        if (c.source_type == L"file" &&
                                            _wcsicmp(c.source_path.c_str(),
                                                     vf.sourcePath.c_str()) == 0) {
                                            found = &c; break;
                                        }
                                    }
                                }
                                if (found)
                                    insertFileNode(hNode, *found);
                                else
                                    insertAutoFileNode(hNode, vf.sourcePath, secLabel);
                            }
                        }

                        // Recurse into child folders (collapsed by default).
                        addVFS(hNode, snap.children, secLabel);
                    }
                };
                addVFS(hSec, *sd.snaps, std::wstring(sd.label));

                TreeView_Expand(hTree, hSec, TVE_EXPAND);
            }
        }

        // OK / Cancel buttons
        int totalBtnW = 2*btnW + S(10);
        int startX = (cW - totalBtnW) / 2;
        int btnY = cH - pad - btnH;
        CreateCustomButtonWithIcon(hwnd, IDC_FDDP_OK,     L"OK",     ButtonColor::Green,
            L"imageres.dll", 89,  startX,           btnY, btnW, btnH, hInst);
        CreateCustomButtonWithIcon(hwnd, IDC_FDDP_CANCEL, L"Cancel", ButtonColor::Red,
            L"shell32.dll",  131, startX + btnW + S(10), btnY, btnW, btnH, hInst);

        // Font
        NONCLIENTMETRICSW ncm = {}; ncm.cbSize = sizeof(ncm);
        SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(ncm), &ncm, 0);
        if (ncm.lfMessageFont.lfHeight < 0)
            ncm.lfMessageFont.lfHeight = (LONG)(ncm.lfMessageFont.lfHeight * 1.2f);
        ncm.lfMessageFont.lfQuality = CLEARTYPE_QUALITY;
        HFONT hF = CreateFontIndirectW(&ncm.lfMessageFont);
        if (hF) {
            EnumChildWindows(hwnd, [](HWND hC, LPARAM lp) -> BOOL {
                SendMessageW(hC, WM_SETFONT, (WPARAM)(HFONT)lp, TRUE); return TRUE;
            }, (LPARAM)hF);
            SetPropW(hwnd, L"hFDPFont", (HANDLE)hF);
        }
        return 0;
    }

    case WM_COMMAND: {
        CompFolderDepPickerData* pData =
            (CompFolderDepPickerData*)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
        if (!pData) break;
        int wmId = LOWORD(wParam);

        if (wmId == IDC_FDDP_CANCEL || wmId == IDCANCEL) {
            pData->okClicked = false;
            DestroyWindow(hwnd); return 0;
        }
        if (wmId == IDC_FDDP_OK) {
            // Iterative DFS: collect every checked item with lParam > 0 at any depth.
            // Section headers have lParam == -1 so they are naturally skipped.
            HWND hTree = GetDlgItem(hwnd, IDC_FDDP_TREE);
            pData->outDeps.clear();
            if (hTree) {
                std::vector<HTREEITEM> stk;
                HTREEITEM hRoot = TreeView_GetRoot(hTree);
                while (hRoot) { stk.push_back(hRoot); hRoot = TreeView_GetNextSibling(hTree, hRoot); }
                while (!stk.empty()) {
                    HTREEITEM hItem = stk.back(); stk.pop_back();
                    TVITEMW tv = {}; tv.mask = TVIF_STATE|TVIF_PARAM;
                    tv.hItem = hItem; tv.stateMask = TVIS_STATEIMAGEMASK;
                    TreeView_GetItem(hTree, &tv);
                    if (((tv.state & TVIS_STATEIMAGEMASK) >> 12) == 2 && tv.lParam > 0) {
                        int lp = (int)tv.lParam;
                        if (lp >= CompFolderDepPickerData::kAutoFileBase) {
                            // VFS file without a pre-existing ComponentRow.
                            // Find or auto-create the file component now.
                            int idx = lp - CompFolderDepPickerData::kAutoFileBase;
                            if (idx < (int)pData->autoFiles.size()) {
                                const auto& af = pData->autoFiles[idx];
                                int realId = 0;
                                for (const auto& c : s_components)
                                    if (c.source_type == L"file" &&
                                        _wcsicmp(c.source_path.c_str(), af.srcPath.c_str()) == 0)
                                        { realId = c.id; break; }
                                if (realId == 0 && pData->projectId > 0) {
                                    ComponentRow nc;
                                    nc.project_id  = pData->projectId;
                                    nc.display_name = af.dispName;
                                    nc.source_type  = L"file";
                                    nc.source_path  = af.srcPath;
                                    nc.dest_path    = af.destSection;
                                    nc.is_required  = 0;
                                    realId = DB::InsertComponent(nc);
                                    if (realId > 0) {
                                        nc.id = realId;
                                        s_components.push_back(nc);
                                    }
                                }
                                if (realId > 0) pData->outDeps.push_back(realId);
                            }
                        } else {
                            pData->outDeps.push_back(lp);
                        }
                    }
                    HTREEITEM hChild = TreeView_GetChild(hTree, hItem);
                    while (hChild) { stk.push_back(hChild); hChild = TreeView_GetNextSibling(hTree, hChild); }
                }
            }
            pData->okClicked = true;
            DestroyWindow(hwnd); return 0;
        }
        break;
    }

    case WM_DRAWITEM: {
        LPDRAWITEMSTRUCT dis = (LPDRAWITEMSTRUCT)lParam;
        if (dis->CtlID == IDC_FDDP_OK || dis->CtlID == IDC_FDDP_CANCEL) {
            ButtonColor color = (ButtonColor)GetWindowLongPtr(dis->hwndItem, GWLP_USERDATA);
            NONCLIENTMETRICSW ncm = {}; ncm.cbSize = sizeof(ncm);
            SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(ncm), &ncm, 0);
            if (ncm.lfMessageFont.lfHeight < 0)
                ncm.lfMessageFont.lfHeight = (LONG)(ncm.lfMessageFont.lfHeight * 1.2f);
            ncm.lfMessageFont.lfWeight  = FW_BOLD;
            ncm.lfMessageFont.lfQuality = CLEARTYPE_QUALITY;
            HFONT hFont = CreateFontIndirectW(&ncm.lfMessageFont);
            LRESULT res = DrawCustomButton(dis, color, hFont);
            if (hFont) DeleteObject(hFont);
            return res;
        }
        break;
    }

    case WM_NOTIFY: {
        NMHDR* pnm = (NMHDR*)lParam;
        if (pnm->idFrom == IDC_FDDP_TREE && pnm->code == NM_CLICK) {
            HWND hTree2 = GetDlgItem(hwnd, IDC_FDDP_TREE);
            DWORD mp = GetMessagePos();
            TVHITTESTINFO ht = {};
            ht.pt.x = GET_X_LPARAM(mp); ht.pt.y = GET_Y_LPARAM(mp);
            ScreenToClient(hTree2, &ht.pt);
            HTREEITEM hHit = TreeView_HitTest(hTree2, &ht);
            if (hHit && (ht.flags & TVHT_ONITEMSTATEICON)) {
                TVITEMW tvi = {}; tvi.mask = TVIF_PARAM; tvi.hItem = hHit;
                TreeView_GetItem(hTree2, &tvi);
                if (tvi.lParam == -1) {
                    // Section root — not checkable; reset the state after the click settles.
                    SetTimer(hwnd, 1, 1, NULL);
                    SetPropW(hwnd, L"hFixItem", (HANDLE)hHit);
                } else {
                    // Any folder (structural or with component) or file: run full
                    // bidirectional cascade (down to children, up to parent folders).
                    SetTimer(hwnd, 2, 1, NULL);
                    SetPropW(hwnd, L"hAutoCheck", (HANDLE)hHit);
                }
            }
        }
        break;
    }
    case WM_TIMER: {
        if (wParam == 1) {
            KillTimer(hwnd, 1);
            HWND hTree2 = GetDlgItem(hwnd, IDC_FDDP_TREE);
            HTREEITEM hFix = (HTREEITEM)GetPropW(hwnd, L"hFixItem");
            if (hTree2 && hFix) {
                TVITEMW tvi = {}; tvi.mask = TVIF_STATE; tvi.hItem = hFix;
                tvi.stateMask = TVIS_STATEIMAGEMASK;
                tvi.state = INDEXTOSTATEIMAGEMASK(0);
                TreeView_SetItem(hTree2, &tvi);
            }
            RemovePropW(hwnd, L"hFixItem");
        }
        if (wParam == 2) {
            // Bidirectional cascade:
            //   DOWN — when a folder is checked or unchecked, propagate the same
            //          state to every descendant that has a visible checkbox
            //          (lParam > 0 or lParam == -2; skip section roots lParam == -1).
            //   UP   — when an item is checked, auto-check every ancestor folder
            //          that has a real ComponentRow (lParam > 0).
            KillTimer(hwnd, 2);
            HWND hTree2 = GetDlgItem(hwnd, IDC_FDDP_TREE);
            HTREEITEM hItem = (HTREEITEM)GetPropW(hwnd, L"hAutoCheck");
            RemovePropW(hwnd, L"hAutoCheck");
            if (hTree2 && hItem) {
                TVITEMW tvi = {}; tvi.mask = TVIF_STATE; tvi.hItem = hItem;
                tvi.stateMask = TVIS_STATEIMAGEMASK;
                TreeView_GetItem(hTree2, &tvi);
                bool checked = (((tvi.state & TVIS_STATEIMAGEMASK) >> 12) == 2);
                int  newState = checked ? 2 : 1;

                // Cascade DOWN: walk the entire subtree of the clicked node.
                {
                    std::vector<HTREEITEM> stk;
                    HTREEITEM hC = TreeView_GetChild(hTree2, hItem);
                    while (hC) { stk.push_back(hC); hC = TreeView_GetNextSibling(hTree2, hC); }
                    while (!stk.empty()) {
                        HTREEITEM h = stk.back(); stk.pop_back();
                        TVITEMW td = {}; td.mask = TVIF_PARAM; td.hItem = h;
                        TreeView_GetItem(hTree2, &td);
                        if (td.lParam != -1) { // skip section roots
                            td.mask = TVIF_STATE; td.stateMask = TVIS_STATEIMAGEMASK;
                            td.state = INDEXTOSTATEIMAGEMASK(newState);
                            TreeView_SetItem(hTree2, &td);
                        }
                        HTREEITEM hSub = TreeView_GetChild(hTree2, h);
                        while (hSub) { stk.push_back(hSub); hSub = TreeView_GetNextSibling(hTree2, hSub); }
                    }
                }

                // Cascade UP (check only): auto-check every ancestor folder up to
                // (but not including) the section root. Covers both real folder
                // components (lParam > 0) and structural VFS folders (lParam == -2).
                // Section roots have lParam == -1 and no visible checkbox — skip them.
                if (checked) {
                    HTREEITEM hP = TreeView_GetParent(hTree2, hItem);
                    while (hP) {
                        TVITEMW tp = {}; tp.mask = TVIF_PARAM; tp.hItem = hP;
                        TreeView_GetItem(hTree2, &tp);
                        if (tp.lParam != -1) { // -1 = section root, not checkable
                            tp.mask = TVIF_STATE; tp.stateMask = TVIS_STATEIMAGEMASK;
                            tp.state = INDEXTOSTATEIMAGEMASK(2);
                            TreeView_SetItem(hTree2, &tp);
                        }
                        hP = TreeView_GetParent(hTree2, hP);
                    }
                }
            }
        }
        break;
    }

    case WM_DESTROY: {
        KillTimer(hwnd, 1); KillTimer(hwnd, 2); // safety
        HIMAGELIST hIL = (HIMAGELIST)GetPropW(hwnd, L"hFDPImgList");
        if (hIL) { ImageList_Destroy(hIL); RemovePropW(hwnd, L"hFDPImgList"); }
        HFONT hF = (HFONT)GetPropW(hwnd, L"hFDPFont");
        if (hF) { DeleteObject(hF); RemovePropW(hwnd, L"hFDPFont"); }
        break;
    }
    case WM_SETTINGCHANGE: {
        // Rebuild custom checkbox state images when the user switches theme.
        HWND hTree2 = GetDlgItem(hwnd, IDC_FDDP_TREE);
        if (hTree2 && IsWindow(hTree2))
            UpdateTreeViewCheckboxImages(hTree2, S(16));
        break;
    }
    case WM_CLOSE:
        DestroyWindow(hwnd); return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

// ─── Dep-list popup helpers ─────────────────────────────────────────────────
static WNDPROC s_prevDepListProc = nullptr;

static const TreeNodeSnapshot* FindDepSnapNode(
    const std::vector<TreeNodeSnapshot>& nodes, const std::wstring& srcPath)
{
    for (const auto& snap : nodes) {
        if (!snap.fullPath.empty() &&
            _wcsicmp(snap.fullPath.c_str(), srcPath.c_str()) == 0)
            return &snap;
        const TreeNodeSnapshot* f = FindDepSnapNode(snap.children, srcPath);
        if (f) return f;
    }
    return nullptr;
}

static void PopulateDepsFileTree(
    HWND hTree, HTREEITEM hParent, const TreeNodeSnapshot& snap)
{
    for (const auto& vf : snap.virtualFiles) {
        if (vf.sourcePath.empty()) continue;
        size_t sl = vf.sourcePath.rfind(L'\\');
        std::wstring fname = (sl != std::wstring::npos)
            ? vf.sourcePath.substr(sl + 1) : vf.sourcePath;
        TVINSERTSTRUCTW tv = {};
        tv.hParent = hParent; tv.hInsertAfter = TVI_LAST;
        tv.item.mask = TVIF_TEXT | TVIF_IMAGE | TVIF_SELECTEDIMAGE;
        tv.item.pszText = const_cast<LPWSTR>(fname.c_str());
        tv.item.iImage = 1; tv.item.iSelectedImage = 1;
        TreeView_InsertItem(hTree, &tv);
    }
    for (const auto& child : snap.children) {
        TVINSERTSTRUCTW tv = {};
        tv.hParent = hParent; tv.hInsertAfter = TVI_LAST;
        tv.item.mask = TVIF_TEXT | TVIF_IMAGE | TVIF_SELECTEDIMAGE | TVIF_STATE;
        tv.item.pszText = const_cast<LPWSTR>(child.text.c_str());
        tv.item.iImage = 0; tv.item.iSelectedImage = 0;
        tv.item.stateMask = TVIS_EXPANDED; tv.item.state = TVIS_EXPANDED;
        HTREEITEM hC = TreeView_InsertItem(hTree, &tv);
        if (hC) PopulateDepsFileTree(hTree, hC, child);
    }
}

static LRESULT CALLBACK DepFilePopupProc(
    HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg) {
    case WM_COMMAND:
        if (LOWORD(wParam) == IDOK || LOWORD(wParam) == IDCANCEL) {
            bool* pDone = (bool*)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
            if (pDone) *pDone = true;
            DestroyWindow(hwnd); return 0;
        }
        break;
    case WM_CLOSE: {
        bool* pDone = (bool*)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
        if (pDone) *pDone = true;
        DestroyWindow(hwnd); return 0;
    }
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

static void ShowDepsFileListPopup(
    HWND hwndParent, const ComponentRow& folderComp)
{
    const std::vector<TreeNodeSnapshot>* secs[] = {
        &s_treeSnapshot_ProgramFiles, &s_treeSnapshot_ProgramData,
        &s_treeSnapshot_AppData,      &s_treeSnapshot_AskAtInstall
    };
    const TreeNodeSnapshot* snap = nullptr;
    for (auto* sv : secs) {
        snap = FindDepSnapNode(*sv, folderComp.source_path);
        if (snap) break;
    }

    HINSTANCE hInst = GetModuleHandleW(NULL);
    WNDCLASSEXW wc = {}; wc.cbSize = sizeof(wc);
    if (!GetClassInfoExW(hInst, L"DepFileListPopup", &wc)) {
        wc.lpfnWndProc   = DepFilePopupProc;
        wc.hInstance     = hInst;
        wc.lpszClassName = L"DepFileListPopup";
        wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
        wc.hCursor       = LoadCursor(NULL, IDC_ARROW);
        RegisterClassExW(&wc);
    }
    const auto& loc = MainWindow::GetLocale();
    auto locS = [&](const wchar_t* k, const wchar_t* fb) -> std::wstring {
        auto it = loc.find(k); return (it != loc.end()) ? it->second : fb;
    };
    std::wstring title  = locS(L"comp_deps_files_popup_title", L"Files in dependency");
    std::wstring closeT = locS(L"comp_deps_files_popup_close", L"Close");

    int popW = S(340), popH = S(360);
    RECT rcP; GetWindowRect(hwndParent, &rcP);
    int xP = rcP.left + (rcP.right  - rcP.left  - popW) / 2;
    int yP = rcP.top  + (rcP.bottom - rcP.top   - popH) / 2;
    bool done = false;
    HWND hPop = CreateWindowExW(WS_EX_TOPMOST | WS_EX_TOOLWINDOW,
        L"DepFileListPopup", title.c_str(),
        WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_VISIBLE,
        xP, yP, popW, popH, hwndParent, NULL, hInst, NULL);
    if (!hPop) return;
    SetWindowLongPtrW(hPop, GWLP_USERDATA, (LONG_PTR)&done);

    RECT rcC; GetClientRect(hPop, &rcC);
    int pad  = S(8);
    int btnH = S(28), btnW = S(90);
    int treeH = rcC.bottom - 2*pad - btnH - pad;

    HIMAGELIST hImgList = NULL;
    {
        int iconSz = GetSystemMetrics(SM_CXSMICON);
        hImgList = ImageList_Create(iconSz, iconSz, ILC_COLOR32 | ILC_MASK, 2, 0);
        if (hImgList) {
            wchar_t sysDir[MAX_PATH];
            GetSystemDirectoryW(sysDir, MAX_PATH);
            std::wstring sh32 = std::wstring(sysDir) + L"\\shell32.dll";
            HICON hIco = NULL;
            ExtractIconExW(sh32.c_str(), 3, NULL, &hIco, 1);
            if (hIco) { ImageList_AddIcon(hImgList, hIco); DestroyIcon(hIco); hIco = NULL; }
            ExtractIconExW(sh32.c_str(), 2, NULL, &hIco, 1);
            if (hIco) { ImageList_AddIcon(hImgList, hIco); DestroyIcon(hIco); }
        }
    }

    HWND hTree = CreateWindowExW(WS_EX_CLIENTEDGE, WC_TREEVIEW, L"",
        WS_CHILD | WS_VISIBLE | WS_VSCROLL |
        TVS_HASLINES | TVS_HASBUTTONS | TVS_LINESATROOT | TVS_SHOWSELALWAYS,
        pad, pad, rcC.right - 2*pad, treeH, hPop, NULL, hInst, NULL);
    if (hTree && hImgList)
        TreeView_SetImageList(hTree, hImgList, TVSIL_NORMAL);

    if (hTree) {
        std::wstring root = folderComp.display_name.empty()
            ? folderComp.source_path : folderComp.display_name;
        TVINSERTSTRUCTW tvR = {};
        tvR.hParent = TVI_ROOT; tvR.hInsertAfter = TVI_LAST;
        tvR.item.mask = TVIF_TEXT | TVIF_IMAGE | TVIF_SELECTEDIMAGE | TVIF_STATE;
        tvR.item.pszText = const_cast<LPWSTR>(root.c_str());
        tvR.item.iImage = 0; tvR.item.iSelectedImage = 0;
        tvR.item.stateMask = TVIS_EXPANDED; tvR.item.state = TVIS_EXPANDED;
        HTREEITEM hRoot = TreeView_InsertItem(hTree, &tvR);
        if (hRoot && snap) PopulateDepsFileTree(hTree, hRoot, *snap);
    }

    int btnX = (rcC.right - btnW) / 2;
    int btnY = rcC.bottom - pad - btnH;
    HWND hCloseBtn = CreateWindowExW(0, L"BUTTON", closeT.c_str(),
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        btnX, btnY, btnW, btnH, hPop, (HMENU)IDOK, hInst, NULL);

    NONCLIENTMETRICSW ncmP = {}; ncmP.cbSize = sizeof(ncmP);
    SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(ncmP), &ncmP, 0);
    if (ncmP.lfMessageFont.lfHeight < 0)
        ncmP.lfMessageFont.lfHeight = (LONG)(ncmP.lfMessageFont.lfHeight * 1.2f);
    ncmP.lfMessageFont.lfQuality = CLEARTYPE_QUALITY;
    HFONT hFP = CreateFontIndirectW(&ncmP.lfMessageFont);
    if (hFP) {
        if (hTree)     SendMessageW(hTree,     WM_SETFONT, (WPARAM)hFP, TRUE);
        if (hCloseBtn) SendMessageW(hCloseBtn, WM_SETFONT, (WPARAM)hFP, TRUE);
    }

    MSG msgP;
    while (!done && GetMessageW(&msgP, NULL, 0, 0) > 0) {
        if (!IsWindow(hPop)) break;
        TranslateMessage(&msgP); DispatchMessageW(&msgP);
    }
    if (hFP) DeleteObject(hFP);
    if (hImgList) ImageList_Destroy(hImgList);
}

// ListView subclass for CompFolderEditDlgProc's dep-summary list:
// custom hover tooltip, double-click → ShowDepsFileListPopup,
// right-click → context menu (Remove / Show files).
static LRESULT CALLBACK DepListSubclassProc(
    HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    CompFolderDlgData* pData =
        (CompFolderDlgData*)GetWindowLongPtrW(GetParent(hwnd), GWLP_USERDATA);
    switch (msg) {
    case WM_MOUSEMOVE: {
        if (!GetPropW(hwnd, L"DLTrack")) {
            TRACKMOUSEEVENT tme = { sizeof(tme), TME_LEAVE, hwnd, 0 };
            TrackMouseEvent(&tme);
            SetPropW(hwnd, L"DLTrack", (HANDLE)1);
        }
        LVHITTESTINFO ht = {};
        ht.pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
        int idx  = ListView_HitTest(hwnd, &ht);
        int last = (int)(INT_PTR)GetPropW(hwnd, L"DLLast") - 1;
        if (idx != last) {
            HideTooltip();
            SetPropW(hwnd, L"DLLast", (HANDLE)(INT_PTR)(idx + 1));
            if (idx >= 0 && (ht.flags & LVHT_ONITEM) && pData) {
                LVITEMW lvi = {}; lvi.mask = LVIF_PARAM; lvi.iItem = idx;
                ListView_GetItem(hwnd, &lvi);
                int depId = (int)lvi.lParam;
                if (depId > 0) {
                    const auto& loc = MainWindow::GetLocale();
                    std::wstring tip;
                    for (const auto& oc : pData->otherComponents) {
                        if (oc.id != depId) continue;
                        if (oc.source_type == L"folder") {
                            auto it = loc.find(L"comp_deps_folder_dblclick");
                            tip = (it != loc.end()) ? it->second
                                                    : L"Double-click to see files";
                        } else {
                            tip = GetVirtualPathForComp(oc);
                        }
                        break;
                    }
                    if (!tip.empty()) {
                        POINT pt = ht.pt; ClientToScreen(hwnd, &pt);
                        std::vector<TooltipEntry> te = {{L"", tip}};
                        ShowMultilingualTooltip(te, pt.x + S(12), pt.y + S(18),
                                                GetParent(hwnd));
                    }
                }
            }
        }
        break;
    }
    case WM_MOUSELEAVE:
        HideTooltip();
        RemovePropW(hwnd, L"DLTrack");
        SetPropW(hwnd, L"DLLast", NULL);
        break;
    case WM_LBUTTONDBLCLK: {
        LVHITTESTINFO ht2 = {};
        ht2.pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
        ListView_HitTest(hwnd, &ht2);
        if (ht2.iItem >= 0 && pData) {
            LVITEMW lv = {}; lv.mask = LVIF_PARAM; lv.iItem = ht2.iItem;
            ListView_GetItem(hwnd, &lv);
            for (const auto& oc : pData->otherComponents)
                if (oc.id == (int)lv.lParam && oc.source_type == L"folder") {
                    ShowDepsFileListPopup(GetParent(hwnd), oc); break;
                }
        }
        return 0;
    }
    case WM_RBUTTONUP: {
        LVHITTESTINFO ht3 = {};
        ht3.pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
        ListView_HitTest(hwnd, &ht3);
        int selCount = ListView_GetSelectedCount(hwnd);
        bool hasFolderSel = false;
        int  firstFolderDepId = 0;
        if (pData && selCount == 1) {
            int s = ListView_GetNextItem(hwnd, -1, LVNI_SELECTED);
            if (s >= 0) {
                LVITEMW lv = {}; lv.mask = LVIF_PARAM; lv.iItem = s;
                ListView_GetItem(hwnd, &lv);
                for (const auto& oc : pData->otherComponents)
                    if (oc.id == (int)lv.lParam && oc.source_type == L"folder")
                        { hasFolderSel = true; firstFolderDepId = oc.id; break; }
            }
        }
        const auto& loc  = MainWindow::GetLocale();
        auto locSC = [&](const wchar_t* k, const wchar_t* fb) -> std::wstring {
            auto it = loc.find(k); return (it != loc.end()) ? it->second : fb;
        };
        HMENU hMenu = CreatePopupMenu();
        AppendMenuW(hMenu,
            (selCount > 0) ? MF_STRING : (MF_STRING | MF_GRAYED),
            IDM_DEPS_CTX_REMOVE,
            locSC(L"comp_deps_ctx_remove", L"Remove").c_str());
        if (hasFolderSel)
            AppendMenuW(hMenu, MF_STRING, IDM_DEPS_CTX_SHOWFILES,
                locSC(L"comp_deps_ctx_showfiles", L"Show files\u2026").c_str());
        POINT ptS = ht3.pt; ClientToScreen(hwnd, &ptS);
        int cmd = (int)TrackPopupMenu(hMenu, TPM_RIGHTBUTTON | TPM_RETURNCMD,
            ptS.x, ptS.y, 0, GetParent(hwnd), NULL);
        DestroyMenu(hMenu);
        if (cmd == IDM_DEPS_CTX_REMOVE)
            SendMessageW(GetParent(hwnd), WM_COMMAND,
                         MAKEWPARAM(IDC_FOLDER_DLG_REMOVE_DEPS, BN_CLICKED), 0);
        else if (cmd == IDM_DEPS_CTX_SHOWFILES && hasFolderSel && pData)
            for (const auto& oc : pData->otherComponents)
                if (oc.id == firstFolderDepId && oc.source_type == L"folder")
                    { ShowDepsFileListPopup(GetParent(hwnd), oc); break; }
        return 0;
    }
    case WM_NCDESTROY: {
        HideTooltip();
        RemovePropW(hwnd, L"DLTrack");
        RemovePropW(hwnd, L"DLLast");
        WNDPROC prev = s_prevDepListProc;
        s_prevDepListProc = nullptr;
        SetWindowLongPtrW(hwnd, GWLP_WNDPROC, (LONG_PTR)prev);
        if (prev) return CallWindowProcW(prev, hwnd, msg, wParam, lParam);
        return 0;
    }
    }
    if (s_prevDepListProc)
        return CallWindowProcW(s_prevDepListProc, hwnd, msg, wParam, lParam);
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

LRESULT CALLBACK CompFolderEditDlgProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg) {
    case WM_CREATE: {
        CREATESTRUCTW*      cs    = (CREATESTRUCTW*)lParam;
        HINSTANCE           hInst = cs->hInstance;
        CompFolderDlgData*  pData = (CompFolderDlgData*)cs->lpCreateParams;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)pData);

        // Initialise working deps state from initDependencyIds
        pData->outDependencyIds = pData->initDependencyIds;
        // Initialise working notes from initNotesRtf
        pData->outNotesRtf = pData->initNotesRtf;

        RECT rcC; GetClientRect(hwnd, &rcC);
        int cW   = rcC.right;
        int cH   = rcC.bottom;
        int contW = cW - 2*S(CFE_PAD_H); // content width fills client minus padding

        int hintH = pData->hintH > 0 ? pData->hintH : S(42);
        int y = S(CFE_PAD_T);

        // Folder name (read-only label)
        CreateWindowExW(0, L"STATIC", pData->folderName.c_str(),
            WS_CHILD | WS_VISIBLE | SS_LEFT,
            S(CFE_PAD_H), y, contW, S(CFE_NAME_H), hwnd, NULL, hInst, NULL);
        y += S(CFE_NAME_H) + S(CFE_GAP_NR);

        // Required — custom checkbox (replaces native BS_AUTOCHECKBOX)
        CreateCustomCheckbox(hwnd, IDC_FOLDER_DLG_REQUIRED, pData->requiredLabel,
            pData->initRequired == 1,
            S(CFE_PAD_H), y, contW, S(CFE_CHECK_H), hInst);
        y += S(CFE_CHECK_H) + S(CFE_GAP_RPS);

        // Pre-selected — locked (checked + disabled) whenever Required is active.
        // The developer can uncheck it only while Required is off.
        bool preselInited = (pData->initRequired == 1) || (pData->initPreselected == 1);
        std::wstring psLabel = pData->preselectedLabel.empty()
            ? L"Pre-selected (ticked by default at install)" : pData->preselectedLabel;
        HWND hPresel = CreateCustomCheckbox(hwnd, IDC_FOLDER_DLG_PRESELECTED, psLabel,
            preselInited, S(CFE_PAD_H), y, contW, S(CFE_CHECK_H), hInst);
        if (pData->initRequired == 1 && hPresel)
            EnableWindow(hPresel, FALSE);  // can't un-tick while Required is on
        y += S(CFE_CHECK_H) + S(CFE_GAP_RC);

        // Cascade hint (may wrap over multiple lines)
        CreateWindowExW(0, L"STATIC", pData->cascadeHint.c_str(),
            WS_CHILD | WS_VISIBLE | SS_LEFT,
            S(CFE_PAD_H), y, contW, hintH, hwnd, NULL, hInst, NULL);
        y += hintH + S(CFE_GAP_CD);

        // Dependencies section: label only (Choose + Remove buttons are below the list)
        {
            std::wstring depsLbl = pData->depsLabel.empty() ? L"Dependencies:" : pData->depsLabel;
            CreateWindowExW(0, L"STATIC", depsLbl.c_str(),
                WS_CHILD | WS_VISIBLE | SS_LEFT | SS_CENTERIMAGE,
                S(CFE_PAD_H), y, contW, S(CFE_DEPS_ROW_H), hwnd, NULL, hInst, NULL);
        }
        y += S(CFE_DEPS_ROW_H) + S(CFE_GAP_LD);

        // Deps display list view — 2 columns: Name | Type
        // Multiselect; tooltip/double-click/context-menu via DepListSubclassProc.
        {
            HWND hDL = CreateWindowExW(WS_EX_CLIENTEDGE, WC_LISTVIEW, L"",
                WS_CHILD | WS_VISIBLE | WS_VSCROLL |
                LVS_REPORT | LVS_NOSORTHEADER | LVS_SHOWSELALWAYS,
                S(CFE_PAD_H), y, contW, S(CFE_DEPLIST_H),
                hwnd, (HMENU)IDC_FOLDER_DLG_DEPS_LIST, hInst, NULL);
            if (hDL) {
                ListView_SetExtendedListViewStyle(hDL, LVS_EX_FULLROWSELECT);
                const auto& locC = MainWindow::GetLocale();
                LVCOLUMNW col = {}; col.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_FMT;
                col.fmt = LVCFMT_LEFT;
                auto itN = locC.find(L"comp_deps_col_name");
                std::wstring cnN = (itN != locC.end()) ? itN->second : L"Name";
                col.cx      = (contW * 3) / 4;
                col.pszText = const_cast<LPWSTR>(cnN.c_str());
                ListView_InsertColumn(hDL, 0, &col);
                auto itT = locC.find(L"comp_deps_col_type");
                std::wstring cnT = (itT != locC.end()) ? itT->second : L"Type";
                col.cx      = contW - (contW * 3) / 4;
                col.pszText = const_cast<LPWSTR>(cnT.c_str());
                ListView_InsertColumn(hDL, 1, &col);
                // Subclass for tooltip, double-click, and context menu.
                s_prevDepListProc = (WNDPROC)SetWindowLongPtrW(
                    hDL, GWLP_WNDPROC, (LONG_PTR)DepListSubclassProc);
            }
        }
        y += S(CFE_DEPLIST_H) + S(CFE_GAP_LB);

        // Dep action buttons — right-aligned below list: [Remove] [Choose…]
        {
            const auto& locB = MainWindow::GetLocale();
            auto locSB = [&](const wchar_t* k, const wchar_t* fb) -> std::wstring {
                auto it = locB.find(k); return (it != locB.end()) ? it->second : fb;
            };
            int cW2 = S(120), rW = S(100), btnGap = S(6);
            int cX  = S(CFE_PAD_H) + contW - cW2;
            int rX  = cX - btnGap - rW;
            std::wstring chooseT = pData->chooseDepsText.empty()
                ? locSB(L"comp_choose_deps", L"Choose\u2026") : pData->chooseDepsText;
            std::wstring removeT = locSB(L"comp_deps_remove", L"Remove");
            CreateCustomButtonWithIcon(hwnd, IDC_FOLDER_DLG_CHOOSE_DEPS, chooseT,
                ButtonColor::Blue, L"shell32.dll", 87,
                cX, y, cW2, S(CFE_DEP_BTNS_ROW_H), hInst);
            HWND hRemBtn = CreateCustomButtonWithIcon(
                hwnd, IDC_FOLDER_DLG_REMOVE_DEPS, removeT,
                ButtonColor::Red, L"shell32.dll", 131,
                rX, y, rW, S(CFE_DEP_BTNS_ROW_H), hInst);
            EnableWindow(hRemBtn, FALSE); // enabled by LVN_ITEMCHANGED
        }
        y += S(CFE_DEP_BTNS_ROW_H) + S(CFE_GAP_DN);

        // Notes button — full width, opens rich-text notes editor
        HWND hNotesBtn = CreateWindowExW(0, L"BUTTON", L"Notes / Description...",
            WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            S(CFE_PAD_H), y, contW, S(CFE_NOTES_BTN_H),
            hwnd, (HMENU)IDC_FOLDER_DLG_NOTES, hInst, NULL);
        SetButtonTooltip(hNotesBtn, L"Edit rich-text notes shown as a tooltip during installation (max 500 characters)");
        y += S(CFE_NOTES_BTN_H) + S(CFE_GAP_NB2);

        // OK / Cancel — centred
        int wOK_CFE  = MeasureButtonWidth(pData->okText, true);
        int wCnl_CFE = MeasureButtonWidth(pData->cancelText, true);
        int totalBtnW = wOK_CFE + S(CFE_BTN_GAP) + wCnl_CFE;
        int startX    = (cW - totalBtnW) / 2;
        CreateCustomButtonWithIcon(hwnd, IDC_COMPDLG_OK,     pData->okText.c_str(),     ButtonColor::Green,
            L"imageres.dll", 89,  startX, y, wOK_CFE, S(CFE_BTN_H), hInst);
        CreateCustomButtonWithIcon(hwnd, IDC_COMPDLG_CANCEL, pData->cancelText.c_str(), ButtonColor::Red,
            L"shell32.dll",  131, startX + wOK_CFE + S(CFE_BTN_GAP), y,
            wCnl_CFE, S(CFE_BTN_H), hInst);

        // Font — set on all children (including the custom checkbox handle)
        NONCLIENTMETRICSW ncm = {}; ncm.cbSize = sizeof(ncm);
        SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(ncm), &ncm, 0);
        if (ncm.lfMessageFont.lfHeight < 0)
            ncm.lfMessageFont.lfHeight = (LONG)(ncm.lfMessageFont.lfHeight * 1.2f);
        ncm.lfMessageFont.lfQuality = CLEARTYPE_QUALITY;
        HFONT hF = CreateFontIndirectW(&ncm.lfMessageFont);
        if (hF) {
            EnumChildWindows(hwnd, [](HWND hC, LPARAM lp) -> BOOL {
                SendMessageW(hC, WM_SETFONT, (WPARAM)(HFONT)lp, TRUE); return TRUE;
            }, (LPARAM)hF);
            SetPropW(hwnd, L"hFolderDlgFont", (HANDLE)hF);
        }

        // Populate deps listbox with initial selection
        RefreshFolderDepsListbox(hwnd, pData);
        return 0;
    }
    case WM_COMMAND: {
        CompFolderDlgData* pData = (CompFolderDlgData*)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
        if (!pData) break;
        int wmId = LOWORD(wParam);

        if (wmId == IDC_FOLDER_DLG_REQUIRED) {
            // When Required is toggled on, Pre-selected must also be on and is locked.
            // When Required is toggled off, Pre-selected becomes free again.
            bool reqOn = (SendDlgItemMessageW(hwnd, IDC_FOLDER_DLG_REQUIRED,
                                              BM_GETCHECK, 0, 0) == BST_CHECKED);
            HWND hPs = GetDlgItem(hwnd, IDC_FOLDER_DLG_PRESELECTED);
            if (hPs) {
                if (reqOn) {
                    SendMessageW(hPs, BM_SETCHECK, BST_CHECKED, 0);
                    EnableWindow(hPs, FALSE);
                } else {
                    EnableWindow(hPs, TRUE);
                }
                InvalidateRect(hPs, NULL, TRUE);
            }
            return 0;
        }
        if (wmId == IDC_COMPDLG_CANCEL || wmId == IDCANCEL) {
            DestroyWindow(hwnd); return 0;
        }
        if (wmId == IDC_COMPDLG_OK) {
            pData->outRequired    = (SendDlgItemMessageW(hwnd, IDC_FOLDER_DLG_REQUIRED,
                                         BM_GETCHECK, 0, 0) == BST_CHECKED) ? 1 : 0;
            pData->outPreselected = (SendDlgItemMessageW(hwnd, IDC_FOLDER_DLG_PRESELECTED,
                                         BM_GETCHECK, 0, 0) == BST_CHECKED) ? 1 : 0;
            pData->okClicked   = true;
            DestroyWindow(hwnd); return 0;
        }
        if (wmId == IDC_FOLDER_DLG_NOTES) {
            NotesEditorData nd;
            nd.initRtf      = pData->outNotesRtf;
            nd.titleText    = L"Edit Notes";
            nd.okText       = L"Save";
            nd.cancelText   = L"Cancel";
            nd.charsLeftFmt = L"%d characters left";
            if (OpenNotesEditor(hwnd, nd))
                pData->outNotesRtf = nd.outRtf;
            return 0;
        }
        if (wmId == IDC_FOLDER_DLG_REMOVE_DEPS) {
            // Remove selected dep IDs; cascade-remove covered file deps too.
            HWND hLV = GetDlgItem(hwnd, IDC_FOLDER_DLG_DEPS_LIST);
            if (hLV && pData) {
                std::vector<int> removeIds;
                int sel = -1;
                while ((sel = ListView_GetNextItem(hLV, sel, LVNI_SELECTED)) >= 0) {
                    LVITEMW lv2 = {}; lv2.mask = LVIF_PARAM; lv2.iItem = sel;
                    ListView_GetItem(hLV, &lv2);
                    if ((int)lv2.lParam > 0) removeIds.push_back((int)lv2.lParam);
                }
                // For each folder being removed, also collect its covered file deps.
                for (int rid : removeIds) {
                    for (const auto& oc : pData->otherComponents) {
                        if (oc.id != rid || oc.source_type != L"folder") continue;
                        const std::wstring& fp = oc.source_path;
                        for (int d : pData->outDependencyIds) {
                            if (std::find(removeIds.begin(), removeIds.end(), d)
                                    != removeIds.end()) continue;
                            for (const auto& fc : pData->otherComponents) {
                                if (fc.id != d || fc.source_type != L"file") continue;
                                const std::wstring& sp = fc.source_path;
                                if (!fp.empty() && sp.size() > fp.size() &&
                                    _wcsnicmp(sp.c_str(), fp.c_str(), fp.size()) == 0 &&
                                    (sp[fp.size()] == L'\\' || sp[fp.size()] == L'/'))
                                    removeIds.push_back(d);
                                break;
                            }
                        }
                        break;
                    }
                }
                std::vector<int> newDeps;
                for (int d : pData->outDependencyIds)
                    if (std::find(removeIds.begin(), removeIds.end(), d) == removeIds.end())
                        newDeps.push_back(d);
                pData->outDependencyIds = std::move(newDeps);
                RefreshFolderDepsListbox(hwnd, pData);
            }
            return 0;
        }
        if (wmId == IDC_FOLDER_DLG_CHOOSE_DEPS) {
            // If any component has id==0 it has never been saved and has no stable DB id.
            // Dependencies stored against id=0 would be lost during the Save ID-remap.
            // Ask the user to save first; if they agree, save immediately then open the
            // picker in the same gesture (no need to press "Choose" a second time).
            {
                bool anyUnsaved = false;
                for (const auto& c : pData->otherComponents)
                    if (c.id == 0) { anyUnsaved = true; break; }
                if (anyUnsaved) {
                    const auto& loc = MainWindow::GetLocale();
                    auto L2 = [&](const wchar_t* k, const wchar_t* fb) -> std::wstring {
                        auto it = loc.find(k);
                        return (it != loc.end()) ? it->second : fb;
                    };
                    std::wstring dlgTitle = L2(L"comp_deps_unsaved_title", L"Save First");
                    std::wstring dlgMsg   = L2(L"comp_deps_unsaved_msg",
                        L"The project has not been saved yet. Components need a permanent "
                        L"ID before dependencies can be assigned.\n\nSave now and continue?");
                    bool confirmed = ShowConfirmDeleteDialog(hwnd, dlgTitle, dlgMsg, loc);
                    if (!confirmed) return 0;
                    // Save via the main window (parent of this dialog).
                    HWND hMain = GetParent(hwnd);
                    SendMessageW(hMain, WM_COMMAND, MAKEWPARAM(IDM_FILE_SAVE, 0), 0);
                    // Rebuild otherComponents from the freshly-saved s_components so that
                    // all IDs are now valid and the picker opens immediately below.
                    {
                        const std::wstring& exPath =
                            pData->excludeNode ? pData->excludeNode->fullPath : L"";
                        pData->otherComponents.clear();
                        for (const auto& oc : s_components) {
                            if (!exPath.empty()) {
                                if (oc.source_path == exPath) continue;
                                if (oc.source_path.size() > exPath.size() &&
                                    _wcsnicmp(oc.source_path.c_str(),
                                              exPath.c_str(), exPath.size()) == 0 &&
                                    (oc.source_path[exPath.size()] == L'\\' ||
                                     oc.source_path[exPath.size()] == L'/')) continue;
                            }
                            pData->otherComponents.push_back(oc);
                        }
                    }
                    // IDs were remapped during save — any prior outDependencyIds are stale.
                    // Clear them so the picker opens with a clean slate.
                    pData->outDependencyIds.clear();
                    RefreshFolderDepsListbox(hwnd, pData);
                    // Fall through to open the dep picker immediately.
                }
            }
            // Open the dep picker popup
            HINSTANCE hInst = (HINSTANCE)GetWindowLongPtrW(hwnd, GWLP_HINSTANCE);
            CompFolderDepPickerData pickerData;
            pickerData.components  = &pData->otherComponents;
            pickerData.initDeps    = pData->outDependencyIds;
            pickerData.excludeNode = pData->excludeNode;
            pickerData.projectId   = pData->projectId;

            WNDCLASSEXW wcPk = {};
            wcPk.cbSize = sizeof(wcPk);
            if (!GetClassInfoExW(GetModuleHandleW(NULL), L"CompFolderDepPicker", &wcPk)) {
                wcPk.lpfnWndProc   = CompFolderDepPickerDlgProc;
                wcPk.hInstance     = GetModuleHandleW(NULL);
                wcPk.lpszClassName = L"CompFolderDepPicker";
                wcPk.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
                wcPk.hCursor       = LoadCursor(NULL, IDC_ARROW);
                RegisterClassExW(&wcPk);
            }
            int pkW = S(420), pkH = S(380);
            RECT rcParent; GetWindowRect(hwnd, &rcParent);
            int xPk = rcParent.left + (rcParent.right - rcParent.left - pkW) / 2;
            int yPk = rcParent.top  + (rcParent.bottom - rcParent.top  - pkH) / 2;
            const auto& loc2 = MainWindow::GetLocale();
            auto it2 = loc2.find(L"comp_select_deps_title");
            std::wstring pickerTitle = (it2 != loc2.end())
                ? it2->second : L"Select Dependencies";
            HWND hPk = CreateWindowExW(WS_EX_TOPMOST | WS_EX_TOOLWINDOW,
                L"CompFolderDepPicker", pickerTitle.c_str(),
                WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_VISIBLE,
                xPk, yPk, pkW, pkH, hwnd, NULL, GetModuleHandleW(NULL), &pickerData);
            MSG msgPk;
            while (GetMessageW(&msgPk, NULL, 0, 0) > 0) {
                if (!IsWindow(hPk)) break;
                TranslateMessage(&msgPk); DispatchMessageW(&msgPk);
            }
            if (pickerData.okClicked) {
                pData->outDependencyIds = pickerData.outDeps;
                RefreshFolderDepsListbox(hwnd, pData);
            }
            return 0;
        }
        break;
    }
    case WM_NOTIFY: {
        NMHDR* pnm2 = (NMHDR*)lParam;
        // Enable/disable the Remove button as selection changes.
        if (pnm2->idFrom == IDC_FOLDER_DLG_DEPS_LIST &&
            pnm2->code   == LVN_ITEMCHANGED) {
            HWND hLV2 = GetDlgItem(hwnd, IDC_FOLDER_DLG_DEPS_LIST);
            HWND hRem = GetDlgItem(hwnd, IDC_FOLDER_DLG_REMOVE_DEPS);
            if (hLV2 && hRem)
                EnableWindow(hRem, (ListView_GetSelectedCount(hLV2) > 0) ? TRUE : FALSE);
        }
        break;
    }
    case WM_DRAWITEM: {
        LPDRAWITEMSTRUCT dis = (LPDRAWITEMSTRUCT)lParam;
        // Let custom checkbox handle its own drawing first
        if (DrawCustomCheckbox(dis)) return TRUE;
        if (dis->CtlID == IDC_COMPDLG_OK     || dis->CtlID == IDC_COMPDLG_CANCEL ||
            dis->CtlID == IDC_FOLDER_DLG_CHOOSE_DEPS ||
            dis->CtlID == IDC_FOLDER_DLG_REMOVE_DEPS) {
            ButtonColor color = (ButtonColor)GetWindowLongPtr(dis->hwndItem, GWLP_USERDATA);
            NONCLIENTMETRICSW ncm = {}; ncm.cbSize = sizeof(ncm);
            SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(ncm), &ncm, 0);
            if (ncm.lfMessageFont.lfHeight < 0)
                ncm.lfMessageFont.lfHeight = (LONG)(ncm.lfMessageFont.lfHeight * 1.2f);
            ncm.lfMessageFont.lfWeight  = FW_BOLD;
            ncm.lfMessageFont.lfQuality = CLEARTYPE_QUALITY;
            HFONT hFont = CreateFontIndirectW(&ncm.lfMessageFont);
            LRESULT res = DrawCustomButton(dis, color, hFont);
            if (hFont) DeleteObject(hFont);
            return res;
        }
        break;
    }
    case WM_CTLCOLORSTATIC: {
        HDC hdc = (HDC)wParam;
        SetBkColor(hdc, GetSysColor(COLOR_WINDOW));
        SetTextColor(hdc, GetSysColor(COLOR_WINDOWTEXT));
        return (LRESULT)GetSysColorBrush(COLOR_WINDOW);
    }
    case WM_SETTINGCHANGE:
        OnCheckboxSettingChange(hwnd);
        break;
    case WM_DESTROY: {
        HFONT hF = (HFONT)GetPropW(hwnd, L"hFolderDlgFont");
        if (hF) { DeleteObject(hF); RemovePropW(hwnd, L"hFolderDlgFont"); }
        break;
    }
    case WM_CLOSE:
        DestroyWindow(hwnd); return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

// Recursively collect all source paths from a single snapshot node and all its descendants.
// Falls back to disk scan when virtualFiles is empty but fullPath is a real folder.
static void CollectSnapshotPaths(const TreeNodeSnapshot& snap, std::vector<std::wstring>& out)
{
    if (!snap.virtualFiles.empty()) {
        for (const auto& vf : snap.virtualFiles)
            if (!vf.sourcePath.empty()) out.push_back(vf.sourcePath);
    } else if (!snap.fullPath.empty()) {
        // Real-path node whose virtualFiles were never populated (user never clicked it
        // on the Files page). Scan disk directly — files only, no subdirs.
        std::wstring sp = snap.fullPath + L"\\*";
        WIN32_FIND_DATAW fd = {};
        HANDLE hFind = FindFirstFileW(sp.c_str(), &fd);
        if (hFind != INVALID_HANDLE_VALUE) {
            do {
                if (wcscmp(fd.cFileName, L".") == 0 || wcscmp(fd.cFileName, L"..") == 0) continue;
                if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
                out.push_back(snap.fullPath + L"\\" + fd.cFileName);
            } while (FindNextFileW(hFind, &fd));
            FindClose(hFind);
        }
    }
    for (const auto& child : snap.children)
        CollectSnapshotPaths(child, out);
}

// Non-recursive variant of CollectSnapshotPaths: collects only the files that
// belong DIRECTLY to this node (virtualFiles or an immediate disk scan) without
// descending into snap.children.  Used by UpdateCompTreeRequiredIcons so that
// each folder is evaluated on its own files alone; child folders are walked
// separately by the TreeView traversal and may inherit via parentIsRequired.
static void CollectSnapshotPathsLocal(const TreeNodeSnapshot& snap, std::vector<std::wstring>& out)
{
    if (!snap.virtualFiles.empty()) {
        for (const auto& vf : snap.virtualFiles)
            if (!vf.sourcePath.empty()) out.push_back(vf.sourcePath);
    } else if (!snap.fullPath.empty()) {
        std::wstring sp = snap.fullPath + L"\\*";
        WIN32_FIND_DATAW fd = {};
        HANDLE hFind = FindFirstFileW(sp.c_str(), &fd);
        if (hFind != INVALID_HANDLE_VALUE) {
            do {
                if (wcscmp(fd.cFileName, L".") == 0 || wcscmp(fd.cFileName, L"..") == 0) continue;
                if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
                out.push_back(snap.fullPath + L"\\" + fd.cFileName);
            } while (FindNextFileW(hFind, &fd));
            FindClose(hFind);
        }
    }
    // Intentionally no recursion into snap.children.
}

// Recursively collect all VirtualFolderFiles from a snapshot tree into ComponentRow vector.
// Falls back to disk scan for real-path nodes whose virtualFiles were never populated.
static void CollectAllFiles(const std::vector<TreeNodeSnapshot>& nodes,
                            int projectId,
                            std::vector<ComponentRow>& out,
                            const std::wstring& sectionRoot = L"")
{
    for (const auto& snap : nodes) {
        if (!snap.virtualFiles.empty()) {
            for (const auto& vf : snap.virtualFiles) {
                if (vf.sourcePath.empty()) continue;
                std::wstring fname = vf.sourcePath;
                size_t sep = fname.find_last_of(L"\\/");
                if (sep != std::wstring::npos) fname = fname.substr(sep + 1);
                ComponentRow comp;
                comp.project_id   = projectId;
                comp.display_name = fname;
                comp.description  = L"";
                comp.is_required  = 0;
                comp.source_type  = L"file";
                comp.source_path  = vf.sourcePath;
                comp.dest_path    = sectionRoot;   // section tag for scoped cascade
                out.push_back(comp);
            }
        } else if (!snap.fullPath.empty()) {
            // Real-path node with no cached virtualFiles — enumerate from disk.
            std::wstring sp = snap.fullPath + L"\\*";
            WIN32_FIND_DATAW fd = {};
            HANDLE hFind = FindFirstFileW(sp.c_str(), &fd);
            if (hFind != INVALID_HANDLE_VALUE) {
                do {
                    if (wcscmp(fd.cFileName, L".") == 0 || wcscmp(fd.cFileName, L"..") == 0) continue;
                    if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
                    ComponentRow comp;
                    comp.project_id   = projectId;
                    comp.display_name = fd.cFileName;
                    comp.description  = L"";
                    comp.is_required  = 0;
                    comp.source_type  = L"file";
                    comp.source_path  = snap.fullPath + L"\\" + fd.cFileName;
                    comp.dest_path    = sectionRoot;   // section tag for scoped cascade
                    out.push_back(comp);
                } while (FindNextFileW(hFind, &fd));
                FindClose(hFind);
            }
        }
        CollectAllFiles(snap.children, projectId, out, sectionRoot);
    }
}

// ─── EnsureTreeSnapshotsFromDb ───────────────────────────────────────────────
// Rebuilds any empty s_treeSnapshot_* vectors from the persisted DB rows so
// the dep picker can show the full VFS hierarchy even when the user has not
// visited the Files page in this session.  Safe to call multiple times — it
// is a no-op once all four snapshots are non-empty.
void MainWindow::EnsureTreeSnapshotsFromDb()
{
    if (s_currentProject.id <= 0) return;

    // Record which sections need rebuilding at entry — do not re-check inside loops.
    bool needRebuild[4] = {
        s_treeSnapshot_ProgramFiles.empty(),
        s_treeSnapshot_ProgramData.empty(),
        s_treeSnapshot_AppData.empty(),
        s_treeSnapshot_AskAtInstall.empty() && s_askAtInstallEnabled
    };
    if (!needRebuild[0] && !needRebuild[1] && !needRebuild[2] && !needRebuild[3]) return;

    auto fileRows = DB::GetFilesForProject(s_currentProject.id);
    if (fileRows.empty()) return;

    const std::wstring secs[] = { L"Program Files", L"ProgramData",
                                   L"AppData (Roaming)", L"AskAtInstall" };
    std::vector<TreeNodeSnapshot>* snapVecs[] = {
        &s_treeSnapshot_ProgramFiles, &s_treeSnapshot_ProgramData,
        &s_treeSnapshot_AppData,      &s_treeSnapshot_AskAtInstall
    };
    std::map<std::wstring, int> secIdx;
    for (int i = 0; i < 4; ++i) secIdx[secs[i]] = i;

    // The section is always the first path component (e.g. "AskAtInstall").
    auto findSection = [&](const std::wstring& destPath) -> std::wstring {
        size_t sep = destPath.find(L'\\');
        return (sep != std::wstring::npos) ? destPath.substr(0, sep) : L"";
    };

    // Step 1 — Build all folder nodes in a std::map for stable-reference storage.
    std::map<std::wstring, TreeNodeSnapshot> nodeMap;
    for (const auto& row : fileRows) {
        if (row.install_scope != L"__folder__") continue;
        size_t sep = row.destination_path.rfind(L'\\');
        if (sep == std::wstring::npos) continue;
        std::wstring sec = findSection(row.destination_path);
        auto sit = secIdx.find(sec);
        if (sit == secIdx.end() || !needRebuild[sit->second]) continue;

        TreeNodeSnapshot& snap = nodeMap[row.destination_path];
        snap.text     = row.destination_path.substr(sep + 1);
        snap.fullPath = row.source_path;
        snap.expanded = false;
        snap.compExpanded = false;
    }

    // Step 2 — Attach virtual files to their parent nodes.
    for (const auto& row : fileRows) {
        if (row.install_scope == L"__folder__") continue;
        size_t sep = row.destination_path.rfind(L'\\');
        if (sep == std::wstring::npos) continue;
        std::wstring parentPath = row.destination_path.substr(0, sep);
        auto it = nodeMap.find(parentPath);
        if (it == nodeMap.end()) continue;
        VirtualFolderFile vf;
        vf.sourcePath    = row.source_path;
        vf.destination   = row.destination_path.substr(sep); // includes leading '\'
        vf.install_scope = row.install_scope;
        it->second.virtualFiles.push_back(std::move(vf));
    }

    // Step 3 — Link hierarchy bottom-up: deepest nodes first so each child is
    // fully assembled before being moved into its parent.
    std::vector<std::wstring> folderPaths;
    for (const auto& kv : nodeMap) folderPaths.push_back(kv.first);
    std::sort(folderPaths.begin(), folderPaths.end(), [](const std::wstring& a, const std::wstring& b) {
        return std::count(a.begin(), a.end(), L'\\') > std::count(b.begin(), b.end(), L'\\');
    });

    for (const auto& destPath : folderPaths) {
        auto it = nodeMap.find(destPath);
        if (it == nodeMap.end()) continue; // already moved

        std::wstring sec = findSection(destPath);
        auto sit = secIdx.find(sec);
        if (sit == secIdx.end()) continue;

        size_t sep = destPath.rfind(L'\\');
        std::wstring parentPath = destPath.substr(0, sep);

        if (parentPath == sec) {
            // Direct child of section root — move into the section vector.
            snapVecs[sit->second]->push_back(std::move(it->second));
        } else {
            auto pit = nodeMap.find(parentPath);
            if (pit != nodeMap.end())
                pit->second.children.push_back(std::move(it->second));
        }
        nodeMap.erase(it);
    }
}

// Recursively walk a snapshot vector and for every real-path folder node that has
// no virtualFiles yet (neither from DB nor from a Files-page session), scan the
// disk directory for direct-child files and populate virtualFiles from there.
// This is the fallback for real-path folders whose individual files were never
// explicitly tracked (disk is source of truth for those).  If the path does not
// exist on this machine the scan produces no entries, which is correct — the
// folder simply shows no file children in the picker on that machine.
static void PopulateSnapshotFilesFromDisk(std::vector<TreeNodeSnapshot>& nodes)
{
    for (auto& snap : nodes) {
        if (!snap.fullPath.empty() && snap.virtualFiles.empty()) {
            std::wstring glob = snap.fullPath + L"\\*";
            WIN32_FIND_DATAW wfd = {};
            HANDLE hFind = FindFirstFileW(glob.c_str(), &wfd);
            if (hFind != INVALID_HANDLE_VALUE) {
                do {
                    if (wcscmp(wfd.cFileName, L".") == 0 || wcscmp(wfd.cFileName, L"..") == 0) continue;
                    if (wfd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
                    VirtualFolderFile vf;
                    vf.sourcePath  = snap.fullPath + L"\\" + wfd.cFileName;
                    vf.destination = std::wstring(L"\\") + wfd.cFileName;
                    vf.install_scope = L"";
                    snap.virtualFiles.push_back(std::move(vf));
                } while (FindNextFileW(hFind, &wfd));
                FindClose(hFind);
            }
        }
        PopulateSnapshotFilesFromDisk(snap.children);
    }
}

// ── TreeSnapshot_* accessors (expose module-private statics for vfs_picker) ───
const std::vector<TreeNodeSnapshot>& MainWindow::TreeSnapshot_ProgramFiles()  { return s_treeSnapshot_ProgramFiles; }
const std::vector<TreeNodeSnapshot>& MainWindow::TreeSnapshot_ProgramData()   { return s_treeSnapshot_ProgramData; }
const std::vector<TreeNodeSnapshot>& MainWindow::TreeSnapshot_AppData()       { return s_treeSnapshot_AppData; }
const std::vector<TreeNodeSnapshot>& MainWindow::TreeSnapshot_AskAtInstall()  { return s_treeSnapshot_AskAtInstall; }

// Shows the virtual file system from the Files page (s_treeSnapshot_*) as a
// split-pane tree/listview browser so the user can pick files or folders as
// component sources without touching the real filesystem.

struct VFSPickerResult {
    std::wstring sourcePath;   // real disk path
    std::wstring displayName;  // leaf name for auto-fill
    std::wstring sourceType;   // L"file" or L"folder"
};

struct VFSPickerData {
    // Locale
    std::wstring titleText;
    std::wstring okText;
    std::wstring cancelText;
    std::wstring colFile;      // listview column header
    std::wstring colPath;      // listview column header
    std::wstring noSelMsg;     // shown when nothing selected
    // Output
    bool okClicked = false;
    std::vector<VFSPickerResult> results;
};

// Recursively insert snapshot children into the TreeView.
// lParam of each inserted item = const TreeNodeSnapshot* (pointer into static vectors).
static void VFSPicker_AddSubtree(HWND hTree, HTREEITEM hParent,
                                  const std::vector<TreeNodeSnapshot>& nodes)
{
    for (const auto& snap : nodes) {
        TVINSERTSTRUCTW tvis = {};
        tvis.hParent             = hParent;
        tvis.hInsertAfter        = TVI_LAST;
        tvis.item.mask           = TVIF_TEXT | TVIF_PARAM | TVIF_IMAGE | TVIF_SELECTEDIMAGE;
        tvis.item.pszText        = (LPWSTR)snap.text.c_str();
        tvis.item.lParam         = (LPARAM)&snap;
        tvis.item.iImage         = 0;  // closed folder
        tvis.item.iSelectedImage = 1;  // open folder
        HTREEITEM hNew = (HTREEITEM)SendMessageW(hTree, TVM_INSERTITEMW, 0, (LPARAM)&tvis);
        if (hNew && !snap.children.empty()) {
            VFSPicker_AddSubtree(hTree, hNew, snap.children);
            if (snap.compExpanded)
                TreeView_Expand(hTree, hNew, TVE_EXPAND);
        }
    }
}

LRESULT CALLBACK VFSPickerDlgProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg) {
    case WM_CREATE: {
        CREATESTRUCTW*  cs    = (CREATESTRUCTW*)lParam;
        HINSTANCE       hInst = cs->hInstance;
        VFSPickerData*  pData = (VFSPickerData*)cs->lpCreateParams;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)pData);

        RECT rc; GetClientRect(hwnd, &rc);
        int W = rc.right, H = rc.bottom;
        int treeW = 240, pad = 10, btnH = 36, btnY = H - pad - btnH;
        int listX = treeW + pad * 2, listW = W - listX - pad;
        int ctrlH = btnY - pad * 2 - 20; // leave room for column-header label

        // "Folders" label
        CreateWindowExW(0, L"STATIC", L"Folders",
            WS_CHILD | WS_VISIBLE | SS_LEFT,
            pad, pad, treeW, 18, hwnd, NULL, hInst, NULL);

        // TreeView
        HWND hTree = CreateWindowExW(WS_EX_CLIENTEDGE, WC_TREEVIEWW,
            L"", WS_CHILD | WS_VISIBLE | TVS_HASLINES | TVS_LINESATROOT |
                TVS_HASBUTTONS | TVS_SHOWSELALWAYS,
            pad, pad + 20, treeW, ctrlH, hwnd, (HMENU)IDC_VFSPICKER_TREE, hInst, NULL);

        // "Files in selected folder" label
        CreateWindowExW(0, L"STATIC", L"Files in selected folder",
            WS_CHILD | WS_VISIBLE | SS_LEFT,
            listX, pad, listW, 18, hwnd, NULL, hInst, NULL);

        // ListView
        HWND hList = CreateWindowExW(WS_EX_CLIENTEDGE, WC_LISTVIEWW,
            L"", WS_CHILD | WS_VISIBLE | LVS_REPORT | LVS_SHOWSELALWAYS,
            listX, pad + 20, listW, ctrlH, hwnd, (HMENU)IDC_VFSPICKER_LIST, hInst, NULL);
        ListView_SetExtendedListViewStyle(hList,
            LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES);
        { // columns
            LVCOLUMNW col = {};
            col.mask    = LVCF_TEXT | LVCF_WIDTH;
            col.cx      = 180;
            col.pszText = (LPWSTR)pData->colFile.c_str();
            ListView_InsertColumn(hList, 0, &col);
            col.cx      = listW - 190;
            col.pszText = (LPWSTR)pData->colPath.c_str();
            ListView_InsertColumn(hList, 1, &col);
        }

        // Buttons
        CreateCustomButtonWithIcon(hwnd, IDC_VFSPICKER_OK,
            pData->okText.c_str(), ButtonColor::Green,
            L"imageres.dll", 89, pad, btnY, 140, btnH, hInst);
        CreateCustomButtonWithIcon(hwnd, IDC_VFSPICKER_CANCEL,
            pData->cancelText.c_str(), ButtonColor::Red,
            L"shell32.dll", 131, pad + 150, btnY, 150, btnH, hInst);

        // Apply system font to all children
        {
            NONCLIENTMETRICSW ncm = {}; ncm.cbSize = sizeof(ncm);
            SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(ncm), &ncm, 0);
            if (ncm.lfMessageFont.lfHeight < 0)
                ncm.lfMessageFont.lfHeight = (LONG)(ncm.lfMessageFont.lfHeight * 1.2f);
            ncm.lfMessageFont.lfQuality = CLEARTYPE_QUALITY;
            HFONT hF = CreateFontIndirectW(&ncm.lfMessageFont);
            if (hF) {
                EnumChildWindows(hwnd, [](HWND hC, LPARAM lp) -> BOOL {
                    SendMessageW(hC, WM_SETFONT, (WPARAM)(HFONT)lp, TRUE);
                    return TRUE;
                }, (LPARAM)hF);
                SetPropW(hwnd, L"hPickFont", (HANDLE)hF);
            }
        }

        // Populate tree from snapshots
        // Section roots get lParam = 0 (not pickable as folder components)
        auto addRoot = [&](const wchar_t* label,
                           const std::vector<TreeNodeSnapshot>& snaps)
        {
            if (snaps.empty()) return;
            TVINSERTSTRUCTW tvis = {};
            tvis.hParent      = TVI_ROOT;
            tvis.hInsertAfter = TVI_LAST;
            tvis.item.mask    = TVIF_TEXT | TVIF_PARAM;
            tvis.item.pszText = (LPWSTR)label;
            tvis.item.lParam  = 0;  // section root — not selectable
            HTREEITEM hR = (HTREEITEM)SendMessageW(hTree, TVM_INSERTITEMW, 0, (LPARAM)&tvis);
            if (hR) {
                VFSPicker_AddSubtree(hTree, hR, snaps);
                TreeView_Expand(hTree, hR, TVE_EXPAND);
            }
        };
        addRoot(L"Program Files",    s_treeSnapshot_ProgramFiles);
        addRoot(L"ProgramData",       s_treeSnapshot_ProgramData);
        addRoot(L"AppData (Roaming)", s_treeSnapshot_AppData);
        addRoot(L"AskAtInstall",      s_treeSnapshot_AskAtInstall);

        return 0;
    }

    case WM_NOTIFY: {
        NMHDR* nmhdr = (NMHDR*)lParam;
        if (nmhdr->idFrom == IDC_VFSPICKER_TREE && nmhdr->code == TVN_SELCHANGED) {
            LPNMTREEVIEWW pnmtv = (LPNMTREEVIEWW)lParam;
            HWND hList = GetDlgItem(hwnd, IDC_VFSPICKER_LIST);
            ListView_DeleteAllItems(hList);
            const TreeNodeSnapshot* snap =
                (const TreeNodeSnapshot*)pnmtv->itemNew.lParam;
            if (snap) {
                int idx = 0;
                for (const auto& vf : snap->virtualFiles) {
                    std::wstring fname = vf.sourcePath;
                    size_t sep = fname.find_last_of(L"\\/");
                    if (sep != std::wstring::npos) fname = fname.substr(sep + 1);
                    LVITEMW lvi = {};
                    lvi.mask    = LVIF_TEXT | LVIF_PARAM;
                    lvi.iItem   = idx++;
                    lvi.pszText = (LPWSTR)fname.c_str();
                    lvi.lParam  = (LPARAM)&vf;  // pointer into snapshot's virtualFiles
                    int row = ListView_InsertItem(hList, &lvi);
                    ListView_SetItemText(hList, row, 1, (LPWSTR)vf.sourcePath.c_str());
                }
            }
        }
        return 0;
    }

    case WM_COMMAND: {
        int wmId = LOWORD(wParam);
        VFSPickerData* pData = (VFSPickerData*)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
        if (!pData) break;

        if (wmId == IDC_VFSPICKER_CANCEL || wmId == IDCANCEL) {
            pData->okClicked = false;
            DestroyWindow(hwnd);
            return 0;
        }

        if (wmId == IDC_VFSPICKER_OK) {
            HWND hList = GetDlgItem(hwnd, IDC_VFSPICKER_LIST);
            HWND hTree = GetDlgItem(hwnd, IDC_VFSPICKER_TREE);

            // Collect all selected ListView items (files)
            int lvSel = -1;
            while ((lvSel = ListView_GetNextItem(hList, lvSel, LVNI_SELECTED)) != -1) {
                LVITEMW lvi = {}; lvi.mask = LVIF_PARAM; lvi.iItem = lvSel;
                ListView_GetItem(hList, &lvi);
                const VirtualFolderFile* vf = (const VirtualFolderFile*)lvi.lParam;
                if (!vf) continue;
                std::wstring fname = vf->sourcePath;
                size_t sep = fname.find_last_of(L"\\/");
                if (sep != std::wstring::npos) fname = fname.substr(sep + 1);
                VFSPickerResult r;
                r.sourcePath  = vf->sourcePath;
                r.displayName = fname;
                r.sourceType  = L"file";
                pData->results.push_back(r);
            }

            // If nothing from ListView, try tree selection as folder
            if (pData->results.empty()) {
                HTREEITEM hSel = TreeView_GetSelection(hTree);
                if (hSel) {
                    TVITEMW tvi = {};
                    tvi.hItem = hSel;
                    tvi.mask  = TVIF_PARAM | TVIF_TEXT;
                    wchar_t nameBuf[260] = {};
                    tvi.pszText    = nameBuf;
                    tvi.cchTextMax = 260;
                    TreeView_GetItem(hTree, &tvi);
                    const TreeNodeSnapshot* snap = (const TreeNodeSnapshot*)tvi.lParam;
                    if (snap && !snap->fullPath.empty()) {
                        VFSPickerResult r;
                        r.sourcePath  = snap->fullPath;
                        r.displayName = nameBuf;
                        r.sourceType  = L"folder";
                        pData->results.push_back(r);
                    }
                }
            }

            if (pData->results.empty()) {
                MessageBoxW(hwnd, pData->noSelMsg.c_str(), L"", MB_OK | MB_ICONINFORMATION);
                return 0;
            }
            pData->okClicked = true;
            DestroyWindow(hwnd);
            return 0;
        }
        break;
    }

    case WM_DRAWITEM: {
        LPDRAWITEMSTRUCT dis = (LPDRAWITEMSTRUCT)lParam;
        if (dis->CtlID == IDC_VFSPICKER_OK || dis->CtlID == IDC_VFSPICKER_CANCEL) {
            ButtonColor color = (ButtonColor)GetWindowLongPtr(dis->hwndItem, GWLP_USERDATA);
            NONCLIENTMETRICSW ncm = {}; ncm.cbSize = sizeof(ncm);
            SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(ncm), &ncm, 0);
            if (ncm.lfMessageFont.lfHeight < 0)
                ncm.lfMessageFont.lfHeight = (LONG)(ncm.lfMessageFont.lfHeight * 1.2f);
            ncm.lfMessageFont.lfWeight  = FW_BOLD;
            ncm.lfMessageFont.lfQuality = CLEARTYPE_QUALITY;
            HFONT hFont = CreateFontIndirectW(&ncm.lfMessageFont);
            LRESULT res = DrawCustomButton(dis, color, hFont);
            if (hFont) DeleteObject(hFont);
            return res;
        }
        break;
    }

    case WM_DESTROY: {
        HFONT hF = (HFONT)GetPropW(hwnd, L"hPickFont");
        if (hF) { DeleteObject(hF); RemovePropW(hwnd, L"hPickFont"); }
        break;
    }
    case WM_CLOSE:
        DestroyWindow(hwnd);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

// ─── Component Edit Dialog ────────────────────────────────────────────────────

struct CompDlgData {
    // Input (pre-fill)
    std::wstring initName;
    std::wstring initDesc;
    int initRequired = 0;
    std::wstring initSourceType; // L"folder" or L"file"
    std::wstring initSourcePath;
    // Dependencies
    std::vector<ComponentRow> otherComponents;  // all other comps in project (for checklist)
    std::vector<int> initDependencyIds;          // currently declared deps
    // Locale strings
    std::wstring titleText;
    std::wstring nameLabel;
    std::wstring descLabel;
    std::wstring requiredLabel;
    std::wstring sourceLabel;
    std::wstring depsLabel;
    std::wstring browseText;
    std::wstring okText;
    std::wstring cancelText;
    // Output
    bool okClicked = false;
    std::wstring outName;
    std::wstring outDesc;
    int outRequired = 0;
    std::wstring outSourcePath;
    std::vector<int> outDependencyIds;
    // Notes (rich text) — in/out
    std::wstring initNotesRtf;
    std::wstring outNotesRtf;
};

LRESULT CALLBACK CompEditDlgProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_CREATE: {
        CREATESTRUCTW *cs = (CREATESTRUCTW *)lParam;
        HINSTANCE hInst = cs->hInstance;
        CompDlgData *pData = (CompDlgData *)cs->lpCreateParams;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)pData);
        // Initialise working notes from initNotesRtf
        pData->outNotesRtf = pData->initNotesRtf;

        int y = 20;
        int labelW = 150, editX = 175, editW = 400;

        // Display Name
        CreateWindowExW(0, L"STATIC", pData->nameLabel.c_str(),
            WS_CHILD | WS_VISIBLE | SS_LEFT,
            20, y, labelW, 24, hwnd, NULL, hInst, NULL);
        CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", pData->initName.c_str(),
            WS_CHILD | WS_VISIBLE | WS_BORDER | ES_LEFT | ES_AUTOHSCROLL,
            editX, y, editW, 28, hwnd, (HMENU)IDC_COMPDLG_NAME, hInst, NULL);
        y += 38;

        // Description
        CreateWindowExW(0, L"STATIC", pData->descLabel.c_str(),
            WS_CHILD | WS_VISIBLE | SS_LEFT,
            20, y, labelW, 24, hwnd, NULL, hInst, NULL);
        CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", pData->initDesc.c_str(),
            WS_CHILD | WS_VISIBLE | WS_BORDER | ES_LEFT | ES_AUTOHSCROLL,
            editX, y, editW, 28, hwnd, (HMENU)IDC_COMPDLG_DESC, hInst, NULL);
        y += 38;

        // Required checkbox
        HWND hReq = CreateWindowExW(0, L"BUTTON", pData->requiredLabel.c_str(),
            WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
            editX, y, editW, 26, hwnd, (HMENU)IDC_COMPDLG_REQUIRED, hInst, NULL);
        if (pData->initRequired) SendMessageW(hReq, BM_SETCHECK, BST_CHECKED, 0);
        y += 36;

        // Source Path
        CreateWindowExW(0, L"STATIC", pData->sourceLabel.c_str(),
            WS_CHILD | WS_VISIBLE | SS_LEFT,
            20, y, labelW, 24, hwnd, NULL, hInst, NULL);
        CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", pData->initSourcePath.c_str(),
            WS_CHILD | WS_VISIBLE | WS_BORDER | ES_LEFT | ES_AUTOHSCROLL | ES_READONLY,
            editX, y, editW - 85, 28, hwnd, (HMENU)IDC_COMPDLG_SRC, hInst, NULL);
        CreateWindowExW(0, L"BUTTON", pData->browseText.c_str(),
            WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            editX + editW - 80, y, 78, 28, hwnd, (HMENU)IDC_COMPDLG_BROWSE, hInst, NULL);
        y += 44;

        // Dependencies — multi-select listbox of other components
        if (!pData->otherComponents.empty()) {
            CreateWindowExW(0, L"STATIC", pData->depsLabel.c_str(),
                WS_CHILD | WS_VISIBLE | SS_LEFT,
                20, y, labelW, 24, hwnd, NULL, hInst, NULL);
            int listH = 100;
            HWND hDeps = CreateWindowExW(WS_EX_CLIENTEDGE, L"LISTBOX", L"",
                WS_CHILD | WS_VISIBLE | LBS_MULTIPLESEL | WS_VSCROLL | LBS_NOTIFY,
                editX, y, editW, listH, hwnd, (HMENU)IDC_COMPDLG_DEPS, hInst, NULL);
            for (int ci = 0; ci < (int)pData->otherComponents.size(); ++ci) {
                SendMessageW(hDeps, LB_ADDSTRING, 0,
                    (LPARAM)pData->otherComponents[ci].display_name.c_str());
                // Pre-select if this comp is in initDependencyIds
                int depId = pData->otherComponents[ci].id;
                for (int dId : pData->initDependencyIds) {
                    if (dId == depId) { SendMessageW(hDeps, LB_SETSEL, TRUE, ci); break; }
                }
            }
            y += listH + 10;
        }
        y += 10;

        // OK / Cancel
        CreateCustomButtonWithIcon(hwnd, IDC_COMPDLG_OK, pData->okText.c_str(), ButtonColor::Green,
            L"imageres.dll", 89, 20, y, 140, 38, hInst);
        CreateCustomButtonWithIcon(hwnd, IDC_COMPDLG_CANCEL, pData->cancelText.c_str(), ButtonColor::Red,
            L"shell32.dll", 131, 175, y, 150, 38, hInst);

        // Notes button
        {
            int notesX = 175 + 150 + 14;
            HWND hNotesBtn = CreateWindowExW(0, L"BUTTON", L"Notes / Description...",
                WS_CHILD|WS_VISIBLE|BS_PUSHBUTTON,
                notesX, y, 180, 38, hwnd, (HMENU)IDC_COMPDLG_NOTES, hInst, NULL);
            SetButtonTooltip(hNotesBtn, L"Edit rich-text notes shown as a tooltip during installation (max 500 characters)");
        }

        // Apply system font
        {
            NONCLIENTMETRICSW ncm = {};
            ncm.cbSize = sizeof(ncm);
            SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(ncm), &ncm, 0);
            if (ncm.lfMessageFont.lfHeight < 0)
                ncm.lfMessageFont.lfHeight = (LONG)(ncm.lfMessageFont.lfHeight * 1.2f);
            ncm.lfMessageFont.lfQuality = CLEARTYPE_QUALITY;
            HFONT hCtrlFont = CreateFontIndirectW(&ncm.lfMessageFont);
            if (hCtrlFont) {
                EnumChildWindows(hwnd, [](HWND hChild, LPARAM lp) -> BOOL {
                    SendMessageW(hChild, WM_SETFONT, (WPARAM)(HFONT)lp, TRUE);
                    return TRUE;
                }, (LPARAM)hCtrlFont);
                SetPropW(hwnd, L"hCtrlFont", (HANDLE)hCtrlFont);
            }
        }
        return 0;
    }

    case WM_COMMAND: {
        int wmId = LOWORD(wParam);
        CompDlgData *pData = (CompDlgData *)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
        if (!pData) break;

        if (wmId == IDC_COMPDLG_BROWSE) {
            // Browse for folder or file depending on source_type
            if (pData->initSourceType == L"folder") {
                BROWSEINFOW bi = {};
                bi.hwndOwner = hwnd;
                bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_USENEWUI;
                LPITEMIDLIST pidl = SHBrowseForFolderW(&bi);
                if (pidl) {
                    wchar_t folderPath[MAX_PATH];
                    if (SHGetPathFromIDListW(pidl, folderPath)) {
                        SetDlgItemTextW(hwnd, IDC_COMPDLG_SRC, folderPath);
                        // Auto-fill display name if empty
                        wchar_t nameBuf[256];
                        GetDlgItemTextW(hwnd, IDC_COMPDLG_NAME, nameBuf, 256);
                        if (wcslen(nameBuf) == 0) {
                            const wchar_t *leaf2 = wcsrchr(folderPath, L'\\');
                            SetDlgItemTextW(hwnd, IDC_COMPDLG_NAME, leaf2 ? leaf2 + 1 : folderPath);
                        }
                    }
                    CoTaskMemFree(pidl);
                }
            } else {
                // File picker
                wchar_t filePath[MAX_PATH] = {};
                OPENFILENAMEW ofn = {};
                ofn.lStructSize = sizeof(ofn);
                ofn.hwndOwner = hwnd;
                ofn.lpstrFile = filePath;
                ofn.nMaxFile = MAX_PATH;
                ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
                if (GetOpenFileNameW(&ofn)) {
                    SetDlgItemTextW(hwnd, IDC_COMPDLG_SRC, filePath);
                    wchar_t nameBuf[256];
                    GetDlgItemTextW(hwnd, IDC_COMPDLG_NAME, nameBuf, 256);
                    if (wcslen(nameBuf) == 0) {
                        const wchar_t *leaf2 = wcsrchr(filePath, L'\\');
                        SetDlgItemTextW(hwnd, IDC_COMPDLG_NAME, leaf2 ? leaf2 + 1 : filePath);
                    }
                }
            }
            return 0;
        }

        if (wmId == IDC_COMPDLG_OK) {
            wchar_t buf[512];
            GetDlgItemTextW(hwnd, IDC_COMPDLG_NAME, buf, 512); pData->outName = buf;
            GetDlgItemTextW(hwnd, IDC_COMPDLG_DESC, buf, 512); pData->outDesc = buf;
            pData->outRequired = (SendDlgItemMessageW(hwnd, IDC_COMPDLG_REQUIRED, BM_GETCHECK, 0, 0) == BST_CHECKED) ? 1 : 0;
            GetDlgItemTextW(hwnd, IDC_COMPDLG_SRC, buf, 512); pData->outSourcePath = buf;
            // Collect selected dependencies
            HWND hDeps = GetDlgItem(hwnd, IDC_COMPDLG_DEPS);
            if (hDeps) {
                int selCount = (int)SendMessageW(hDeps, LB_GETSELCOUNT, 0, 0);
                if (selCount > 0) {
                    std::vector<int> selIdx(selCount);
                    SendMessageW(hDeps, LB_GETSELITEMS, selCount, (LPARAM)selIdx.data());
                    for (int si : selIdx) {
                        if (si >= 0 && si < (int)pData->otherComponents.size())
                            pData->outDependencyIds.push_back(pData->otherComponents[si].id);
                    }
                }
            }
            // outNotesRtf is already updated by IDC_COMPDLG_NOTES handler
            pData->okClicked = true;
            DestroyWindow(hwnd);
            return 0;
        }

        if (wmId == IDC_COMPDLG_NOTES) {
            NotesEditorData nd;
            nd.initRtf      = pData->outNotesRtf;
            nd.titleText    = L"Edit Notes";
            nd.okText       = L"Save";
            nd.cancelText   = L"Cancel";
            nd.charsLeftFmt = L"%d characters left";
            if (OpenNotesEditor(hwnd, nd))
                pData->outNotesRtf = nd.outRtf;
            return 0;
        }

        if (wmId == IDC_COMPDLG_CANCEL || wmId == IDCANCEL) {
            pData->okClicked = false;
            DestroyWindow(hwnd);
            return 0;
        }
        break;
    }

    case WM_DRAWITEM: {
        LPDRAWITEMSTRUCT dis = (LPDRAWITEMSTRUCT)lParam;
        if (dis->CtlID == IDC_COMPDLG_OK || dis->CtlID == IDC_COMPDLG_CANCEL) {
            ButtonColor color = (ButtonColor)GetWindowLongPtr(dis->hwndItem, GWLP_USERDATA);
            NONCLIENTMETRICSW ncm = {};
            ncm.cbSize = sizeof(ncm);
            SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(ncm), &ncm, 0);
            if (ncm.lfMessageFont.lfHeight < 0)
                ncm.lfMessageFont.lfHeight = (LONG)(ncm.lfMessageFont.lfHeight * 1.2f);
            ncm.lfMessageFont.lfWeight = FW_BOLD;
            ncm.lfMessageFont.lfQuality = CLEARTYPE_QUALITY;
            HFONT hFont = CreateFontIndirectW(&ncm.lfMessageFont);
            LRESULT result = DrawCustomButton(dis, color, hFont);
            if (hFont) DeleteObject(hFont);
            return result;
        }
        break;
    }

    case WM_DESTROY: {
        HFONT hCtrlFont = (HFONT)GetPropW(hwnd, L"hCtrlFont");
        if (hCtrlFont) { DeleteObject(hCtrlFont); RemovePropW(hwnd, L"hCtrlFont"); }
        break;
    }

    case WM_CLOSE:
        DestroyWindow(hwnd);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

LRESULT CALLBACK MainWindow::WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_CREATE: {
        HINSTANCE hInst = ((LPCREATESTRUCT)lParam)->hInstance;
        // Initialize tooltip system for main window
        InitTooltipSystem(hInst);
        
        // Build About tooltip entries once at startup
        std::vector<std::wstring> availableLocales;
        wchar_t exePath[MAX_PATH];
        std::wstring exeDir = L".";
        if (GetModuleFileNameW(NULL, exePath, MAX_PATH)) {
            wchar_t *p = wcsrchr(exePath, L'\\');
            if (p) {
                *p = 0;
                exeDir = exePath;
            }
        }
        WIN32_FIND_DATAW findData;
        std::wstring searchPath = exeDir + L"\\locale\\*.txt";
        HANDLE hFind = FindFirstFileW(searchPath.c_str(), &findData);
        if (hFind != INVALID_HANDLE_VALUE) {
            do {
                std::wstring filename = findData.cFileName;
                size_t dotPos = filename.rfind(L'.');
                if (dotPos != std::wstring::npos) {
                    availableLocales.push_back(filename.substr(0, dotPos));
                }
            } while (FindNextFileW(hFind, &findData));
            FindClose(hFind);
        }
        
        s_toolbarHeight = S(78); // two button rows: S(5)+S(31)+S(4)+S(31)+S(7)
        // Create scaled body font from system NONCLIENTMETRICS so it is correct for any DPI
        if (!s_scaledFont) {
            NONCLIENTMETRICSW ncm = {};
            ncm.cbSize = sizeof(ncm);
            SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(ncm), &ncm, 0);
            // Bump 20 % larger than the system default for better readability
            if (ncm.lfMessageFont.lfHeight < 0)
                ncm.lfMessageFont.lfHeight = (LONG)(ncm.lfMessageFont.lfHeight * 1.2f);
            ncm.lfMessageFont.lfQuality = CLEARTYPE_QUALITY;
            ncm.lfMessageFont.lfCharSet = DEFAULT_CHARSET;
            s_scaledFont = CreateFontIndirectW(&ncm.lfMessageFont);
        }
        // Page headline font: same system font family, ~150 % of body size, semi-bold.
        // Using NONCLIENTMETRICS keeps it i18n-safe (correct glyph coverage on every locale).
        if (!s_hPageTitleFont) {
            NONCLIENTMETRICSW ncm = {};
            ncm.cbSize = sizeof(ncm);
            SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(ncm), &ncm, 0);
            LOGFONTW lf = ncm.lfMessageFont; // start from the system UI font
            if (lf.lfHeight < 0)
                lf.lfHeight = (LONG)(lf.lfHeight * 1.5f); // ~150 % body size
            lf.lfWeight  = FW_SEMIBOLD;
            lf.lfQuality = CLEARTYPE_QUALITY;
            lf.lfCharSet = DEFAULT_CHARSET;
            s_hPageTitleFont = CreateFontIndirectW(&lf);
        }
        s_hGuiFont = s_scaledFont; // alias so WM_CTLCOLORSTATIC drawing also uses it
        CreateMenuBar(hwnd);
        CreateToolbar(hwnd, hInst);
        CreateStatusBar(hwnd, hInst);
        // Initialize with Files page
        SwitchPage(hwnd, 0);
        // Clear any spurious modified flags triggered during control initialization
        s_hasUnsavedChanges = false;
        return 0;
    }
    
    case WM_SIZE: {
        // Resize page container and status bar
        RECT rc;
        GetClientRect(hwnd, &rc);
        
        if (s_hCurrentPage) {
            int pageY = s_toolbarHeight;
            int pageHeight = rc.bottom - s_toolbarHeight - 25;
            SetWindowPos(s_hCurrentPage, NULL, 0, pageY, rc.right, pageHeight, SWP_NOZORDER);
        }
        
        if (s_hStatus) {
            SendMessageW(s_hStatus, WM_SIZE, 0, 0);
            // Re-calculate part widths after resize
            RECT rcStatus;
            GetClientRect(s_hStatus, &rcStatus);
            int indW = S(120);
            int parts[2] = { rcStatus.right - indW, -1 };
            SendMessageW(s_hStatus, SB_SETPARTS, 2, (LPARAM)parts);
            SendMessageW(s_hStatus, SB_SETTEXT, (WPARAM)(1 | SBT_OWNERDRAW), (LPARAM)0);
        }
        
        // Reposition About icon to stay at far right of toolbar
        if (s_hAboutButton) {
            const int iconSize = S(36);
            const int iconX = rc.right - iconSize - S(10);
            const int iconY = (s_toolbarHeight - iconSize) / 2;
            SetWindowPos(s_hAboutButton, NULL, iconX, iconY, 0, 0, SWP_NOZORDER | SWP_NOSIZE);
        }
        return 0;
    }
    
    case WM_NOTIFY: {
        LPNMHDR nmhdr = (LPNMHDR)lParam;
        if (DragDrop_OnBeginDrag(nmhdr)) return 0;
        { bool scH = false; LRESULT scR = SC_OnNotify(hwnd, nmhdr, &scH); if (scH) return scR; }
        { bool depH = false; LRESULT depR = DEP_OnNotify(hwnd, nmhdr, &depH); if (depH) return depR; }

        // Drag-and-drop begin notifications are superseded by the dragdrop
        // module (subclass-based threshold detection).  No action needed here.

        // NM_CUSTOMDRAW: no visual override — ticked items use native row colours.
        if (nmhdr->idFrom == 102 && nmhdr->code == NM_CUSTOMDRAW)
            return CDRF_DODEFAULT;

        // Handle TreeView label edit
        if (nmhdr->idFrom == 102 && nmhdr->code == TVN_BEGINLABELEDIT) {
            LPNMTVDISPINFO ptvdi = (LPNMTVDISPINFO)lParam;
            bool isSysRoot = (ptvdi->item.hItem == s_hProgramFilesRoot  ||
                              ptvdi->item.hItem == s_hProgramDataRoot   ||
                              ptvdi->item.hItem == s_hAppDataRoot       ||
                              ptvdi->item.hItem == s_hAskAtInstallRoot);
            return isSysRoot ? TRUE : FALSE; // TRUE = cancel edit on system roots
        }

        // Handle TreeView label edit
        if (nmhdr->idFrom == 102 && nmhdr->code == TVN_ENDLABELEDIT) {
            LPNMTVDISPINFO ptvdi = (LPNMTVDISPINFO)lParam;
            if (ptvdi->item.pszText && wcslen(ptvdi->item.pszText) > 0) {
                // Check if this is a folder under Program Files
                HTREEITEM hParent = TreeView_GetParent(s_hTreeView, ptvdi->item.hItem);
                bool isUnderProgramFiles = (hParent == s_hProgramFilesRoot);
                
                // Check if this is THE FIRST child under Program Files
                bool isFirstChild = false;
                if (isUnderProgramFiles) {
                    HTREEITEM hFirstChild = TreeView_GetChild(s_hTreeView, s_hProgramFilesRoot);
                    isFirstChild = (ptvdi->item.hItem == hFirstChild);
                }
                
                // If editing the first folder under Program Files, always update install path
                if (isFirstChild) {
                    std::wstring folderName = ptvdi->item.pszText;
                    std::wstring newInstallPath = GetProgramFilesPath() + L"\\" + folderName;
                    SetDlgItemTextW(hwnd, IDC_INSTALL_FOLDER, newInstallPath.c_str());
                    
                    // Also update project name if this is a new project and user hasn't manually edited it
                    if (!s_projectNameManuallySet) {
                        s_currentProject.name = folderName;
                        std::wstring newTitle = L"SetupCraft - " + folderName;
                        SetWindowTextW(hwnd, newTitle.c_str());
                        
                        // Update project name field programmatically
                        s_updatingProjectNameProgrammatically = true;
                        SetDlgItemTextW(hwnd, IDC_PROJECT_NAME, folderName.c_str());
                        s_updatingProjectNameProgrammatically = false;
                    }
                }
                
                // Accept the new label
                TreeView_SetItem(s_hTreeView, &ptvdi->item);
                return TRUE;
            }
            return FALSE;
        }
        
        // Handle Registry TreeView selection change
        if (nmhdr->idFrom == IDC_REG_TREEVIEW && nmhdr->code == TVN_SELCHANGED) {
            LPNMTREEVIEW pnmtv = (LPNMTREEVIEW)lParam;
            if (s_hRegListView && IsWindow(s_hRegListView)) {
                ListView_DeleteAllItems(s_hRegListView);
                
                // Check if this tree item has registry values stored
                auto it = s_registryValues.find(pnmtv->itemNew.hItem);
                if (it != s_registryValues.end()) {
                    // Populate ListView with stored values
                    LVITEMW lvi = {};
                    lvi.mask = LVIF_TEXT;
                    int itemIndex = 0;
                    
                    for (const auto& entry : it->second) {
                        lvi.iItem = itemIndex++;
                        lvi.iSubItem = 0;
                        lvi.pszText = (LPWSTR)entry.name.c_str();
                        int idx = ListView_InsertItem(s_hRegListView, &lvi);
                        ListView_SetItemText(s_hRegListView, idx, 1, (LPWSTR)entry.type.c_str());
                        ListView_SetItemText(s_hRegListView, idx, 2, (LPWSTR)entry.data.c_str());
                    }
                }
                
                // Force ListView to redraw
                InvalidateRect(s_hRegListView, NULL, TRUE);
                UpdateWindow(s_hRegListView);
            }
            return 0;
        }

        // Handle double-click on Registry TreeView -> edit key
        if (nmhdr->idFrom == IDC_REG_TREEVIEW && nmhdr->code == NM_DBLCLK) {
            // Route to Edit Key handler
            SendMessageW(hwnd, WM_COMMAND, IDM_REG_EDIT_KEY, 0);
            return 0;
        }
        
        // Handle TreeView selection change
        if (nmhdr->idFrom == 102 && nmhdr->code == TVN_SELCHANGED) {
            LPNMTREEVIEW pnmtv = (LPNMTREEVIEW)lParam;
            if (s_hListView && IsWindow(s_hListView)) {
                // Always clear ListView first
                ListView_DeleteAllItems(s_hListView);

                HTREEITEM hItem = pnmtv->itemNew.hItem;

                // If this is a real-path node, lazily ingest its files into the virtual
                // store (one-time FindFirstFile scan, no SHGetFileInfoW per file here).
                // This fixes:  (a) the UI freeze caused by PopulateListView doing
                // SHGetFileInfoW on the UI thread, and (b) the empty Components-tree
                // panes caused by s_virtualFolderFiles never being populated for
                // real-path nodes (snapshot captured empty virtualFiles).
                if (pnmtv->itemNew.lParam) {
                    wchar_t* folderPath = (wchar_t*)pnmtv->itemNew.lParam;
                    if (folderPath && wcslen(folderPath) > 0 &&
                        s_virtualFolderFiles.find(hItem) == s_virtualFolderFiles.end()) {
                        IngestRealPathFiles(hItem, folderPath);
                    }
                }

                // Show files from virtual store (same path for both real-path and
                // virtual nodes).  Use SHGFI_USEFILEATTRIBUTES so the icon lookup
                // is extension-based only — no file-system access, no blocking.
                auto it = s_virtualFolderFiles.find(hItem);
                if (it != s_virtualFolderFiles.end()) {
                    for (const auto& fileInfo : it->second) {
                        SHFILEINFOW sfi = {};
                        SHGetFileInfoW(fileInfo.sourcePath.c_str(), FILE_ATTRIBUTE_NORMAL,
                                       &sfi, sizeof(sfi),
                                       SHGFI_SYSICONINDEX | SHGFI_SMALLICON | SHGFI_USEFILEATTRIBUTES);
                        LVITEMW lvi = {};
                        lvi.mask     = LVIF_TEXT | LVIF_PARAM | LVIF_IMAGE;
                        lvi.iItem    = ListView_GetItemCount(s_hListView);
                        lvi.iSubItem = 0;
                        lvi.pszText  = (LPWSTR)fileInfo.sourcePath.c_str();
                        lvi.iImage   = sfi.iIcon;
                        wchar_t* pathCopy = (wchar_t*)malloc((fileInfo.sourcePath.length() + 1) * sizeof(wchar_t));
                        if (pathCopy) {
                            wcscpy(pathCopy, fileInfo.sourcePath.c_str());
                            lvi.lParam = (LPARAM)pathCopy;
                        }
                        int idx = ListView_InsertItem(s_hListView, &lvi);
                        ListView_SetItemText(s_hListView, idx, 1, (LPWSTR)fileInfo.destination.c_str());
                    }
                }

                // Force ListView to redraw
                InvalidateRect(s_hListView, NULL, TRUE);
                UpdateWindow(s_hListView);
                // Show tooltip if AskAtInstall root is selected
                if (pnmtv->itemNew.hItem == s_hAskAtInstallRoot) {
                    // Get item rect
                    RECT rcItem;
                    if (TreeView_GetItemRect(s_hTreeView, s_hAskAtInstallRoot, &rcItem, TRUE)) {
                        // Use selected language only (from locale) and show below mouse pointer
                        auto it = MainWindow::GetLocale().find(L"ask_at_install_tooltip");
                        std::wstring text = (it != MainWindow::GetLocale().end()) ? it->second : L"Installer will ask the end user whether to install for current user or all users; files will go to AppData or ProgramData accordingly.";
                        std::vector<TooltipEntry> entries;
                        entries.push_back({L"", text});
                        POINT pt; GetCursorPos(&pt);
                        ShowMultilingualTooltip(entries, pt.x, pt.y + 20, hwnd);
                    } else {
                        auto it = MainWindow::GetLocale().find(L"ask_at_install_tooltip");
                        std::wstring text = (it != MainWindow::GetLocale().end()) ? it->second : L"Installer will ask the end user whether to install for current user or all users; files will go to AppData or ProgramData accordingly.";
                        std::vector<TooltipEntry> entries;
                        entries.push_back({L"", text});
                        POINT pt; GetCursorPos(&pt);
                        ShowMultilingualTooltip(entries, pt.x, pt.y + 20, hwnd);
                    }
                } else {
                    HideTooltip();
                }
            }
            return 0;
        }
        
        // Handle ListView tooltip request
        if (nmhdr->idFrom == 100 && nmhdr->code == LVN_GETINFOTIP) {
            LPNMLVGETINFOTIP pGetInfoTip = (LPNMLVGETINFOTIP)lParam;
            LVITEM lvi = {};
            lvi.mask = LVIF_PARAM;
            lvi.iItem = pGetInfoTip->iItem;
            if (ListView_GetItem(s_hListView, &lvi) && lvi.lParam) {
                wchar_t* fullPath = (wchar_t*)lvi.lParam;
                wcsncpy(pGetInfoTip->pszText, fullPath, pGetInfoTip->cchTextMax - 1);
                pGetInfoTip->pszText[pGetInfoTip->cchTextMax - 1] = L'\0';
            }
        }
        
        
        // Handle double-click on Registry ListView -> edit value
        if (nmhdr->idFrom == IDC_REG_LISTVIEW && nmhdr->code == NM_DBLCLK) {
            // Route to Edit Value handler
            SendMessageW(hwnd, WM_COMMAND, IDM_REG_EDIT_VALUE, 0);
            return 0;
        }

        // Handle double-click on Components ListView -> edit component
        if (nmhdr->idFrom == IDC_COMP_LISTVIEW && nmhdr->code == NM_DBLCLK) {
            SendMessageW(hwnd, WM_COMMAND, IDC_COMP_EDIT, 0);
            return 0;
        }

        // When a folder is selected in the Components tree, populate the file list
        if (nmhdr->idFrom == IDC_COMP_TREEVIEW && nmhdr->code == TVN_SELCHANGED) {
            if (!s_hCompListView) break;
            LPNMTREEVIEWW pnmtv = (LPNMTREEVIEWW)lParam;
            ListView_DeleteAllItems(s_hCompListView);
            const TreeNodeSnapshot* snap =
                (const TreeNodeSnapshot*)pnmtv->itemNew.lParam;
            if (snap) {
                // Build a temporary list of files to display.
                // Priority 1: virtualFiles already cached in the snapshot.
                // Priority 2: disk scan if this is a real-path node with no cache.
                std::vector<VirtualFolderFile> diskFiles;
                const std::vector<VirtualFolderFile>* files = &snap->virtualFiles;
                if (snap->virtualFiles.empty() && !snap->fullPath.empty()) {
                    std::wstring sp = snap->fullPath + L"\\*";
                    WIN32_FIND_DATAW fd = {};
                    HANDLE hFind = FindFirstFileW(sp.c_str(), &fd);
                    if (hFind != INVALID_HANDLE_VALUE) {
                        do {
                            if (wcscmp(fd.cFileName, L".") == 0 || wcscmp(fd.cFileName, L"..") == 0) continue;
                            if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
                            VirtualFolderFile vf;
                            vf.sourcePath  = snap->fullPath + L"\\" + fd.cFileName;
                            vf.destination = L"\\" + std::wstring(fd.cFileName);
                            diskFiles.push_back(std::move(vf));
                        } while (FindNextFileW(hFind, &fd));
                        FindClose(hFind);
                    }
                    files = &diskFiles;
                }

                int row = 0;
                for (const auto& vf : *files) {
                    // Find matching ComponentRow in s_components by source_path
                    int compIdx = -1;
                    for (int ci = 0; ci < (int)s_components.size(); ++ci) {
                        if (s_components[ci].source_path == vf.sourcePath) {
                            compIdx = ci; break;
                        }
                    }
                    std::wstring fname = vf.sourcePath;
                    size_t sep = fname.find_last_of(L"\\/");
                    if (sep != std::wstring::npos) fname = fname.substr(sep + 1);

                    std::wstring dispName = (compIdx >= 0) ? s_components[compIdx].display_name : fname;
                    std::wstring desc     = (compIdx >= 0) ? s_components[compIdx].description  : L"";
                    int isReq             = (compIdx >= 0) ? s_components[compIdx].is_required  : 0;
                    std::wstring srcType  = (compIdx >= 0) ? s_components[compIdx].source_type  : L"file";

                    LVITEMW lvi = {};
                    lvi.mask    = LVIF_TEXT | LVIF_PARAM;
                    lvi.iItem   = row++;
                    lvi.pszText = (LPWSTR)dispName.c_str();
                    lvi.lParam  = (LPARAM)compIdx;
                    int lrow = ListView_InsertItem(s_hCompListView, &lvi);

                    ListView_SetItemText(s_hCompListView, lrow, 1, (LPWSTR)desc.c_str());
                    auto itYes = s_locale.find(isReq ? L"yes" : L"no");
                    std::wstring reqStr = (itYes != s_locale.end()) ? itYes->second : (isReq ? L"Yes" : L"No");
                    ListView_SetItemText(s_hCompListView, lrow, 2, (LPWSTR)reqStr.c_str());
                    auto itType2 = s_locale.find(srcType == L"folder" ? L"comp_type_folder" : L"comp_type_file");
                    std::wstring typeStr = (itType2 != s_locale.end()) ? itType2->second : srcType;
                    ListView_SetItemText(s_hCompListView, lrow, 3, (LPWSTR)typeStr.c_str());
                    ListView_SetItemText(s_hCompListView, lrow, 4, (LPWSTR)vf.sourcePath.c_str());
                }
            }
            return 0;
        }

        break;
    }
    
    case WM_COMMAND: {
        int wmId = LOWORD(wParam);
        int wmEvent = HIWORD(wParam);
        if (SC_OnCommand(hwnd, wmId, wmEvent, (HWND)lParam)) return 0;
        if (DEP_OnCommand(hwnd, wmId, wmEvent, (HWND)lParam)) return 0;
        if (IDLG_OnCommand(hwnd, wmId, wmEvent, (HWND)lParam)) return 0;

        // Handle registry page field changes — persist values in statics so they
        // survive page switches (pages are destroyed/recreated on switch)
        if (wmId == IDC_REG_PUBLISHER && wmEvent == EN_CHANGE) {
            wchar_t buf[512];
            GetDlgItemTextW(hwnd, IDC_REG_PUBLISHER, buf, 512);
            s_appPublisher = buf;

            // Auto-update install path unless user has manually chosen one
            if (!s_installPathUserEdited) {
                // Derive app folder name from the current stored install path
                std::wstring base = s_currentInstallPath.empty()
                    ? s_currentProject.name
                    : s_currentInstallPath.substr(s_currentInstallPath.find_last_of(L"\\/") + 1);
                if (base.empty()) base = s_currentProject.name;

                if (!s_appPublisher.empty())
                    s_currentInstallPath = GetProgramFilesPath() + L"\\" + s_appPublisher + L"\\" + base;
                else
                    s_currentInstallPath = GetProgramFilesPath() + L"\\" + base;

                // If Files page is live, update it immediately
                HWND hPathCtrl = GetDlgItem(hwnd, IDC_INSTALL_FOLDER);
                if (hPathCtrl)
                    SetWindowTextW(hPathCtrl, s_currentInstallPath.c_str());
            }

            MarkAsModified();
            return 0;
        }

        if (wmId == IDC_REG_VERSION && wmEvent == EN_CHANGE) {
            wchar_t buf[128];
            GetDlgItemTextW(hwnd, IDC_REG_VERSION, buf, 128);
            s_currentProject.version = buf;
            // Update status bar version display
            std::wstring sb = L"Project: " + s_currentProject.name
                + L" | Version: " + s_currentProject.version
                + L" | Directory: " + s_currentProject.directory;
            SetWindowTextW(s_hStatus, sb.c_str());
            MarkAsModified();
            return 0;
        }

        // Handle project name changes
        if (wmId == IDC_PROJECT_NAME && wmEvent == EN_CHANGE) {
            // Mark as manually set if this is a user edit (not programmatic)
            if (!s_updatingProjectNameProgrammatically) {
                s_projectNameManuallySet = true;
            }
            
            // Get the new project name
            wchar_t projectName[256];
            GetDlgItemTextW(hwnd, IDC_PROJECT_NAME, projectName, 256);
            
            // Update the window title
            std::wstring title = L"SetupCraft - " + std::wstring(projectName);
            SetWindowTextW(hwnd, title.c_str());
            
            // Note: Install folder is not updated here - it reflects the actual folder structure
            
            MarkAsModified();
            return 0;
        }
        
        switch (wmId) {
        // Toolbar buttons
        case IDC_TB_FILES:
            SwitchPage(hwnd, 0);
            return 0;
            
        case IDC_TB_ADD_REGISTRY:
            SwitchPage(hwnd, 1);
            return 0;
            
        case IDC_TB_ADD_SHORTCUT:
            SwitchPage(hwnd, 2);
            return 0;

        case IDC_TB_ADD_DEPEND:
            SwitchPage(hwnd, 3);
            return 0;

        case IDC_TB_DIALOGS:
            SwitchPage(hwnd, 4);
            return 0;
            
        case IDC_TB_SETTINGS:
            SwitchPage(hwnd, 5);
            return 0;
            
        case IDC_TB_BUILD:
            SwitchPage(hwnd, 6);
            return 0;
            
        case IDC_TB_TEST:
            SwitchPage(hwnd, 7);
            return 0;
            
        case IDC_TB_SCRIPTS:
            SwitchPage(hwnd, 8);
            return 0;

        case IDC_TB_COMPONENTS:
            SwitchPage(hwnd, 9);
            return 0;
            
        case IDC_TB_SAVE:
            SendMessageW(hwnd, WM_COMMAND, MAKEWPARAM(IDM_FILE_SAVE, 0), 0);
            return 0;

        case IDC_TB_CLOSE_PROJECT:
            SendMessageW(hwnd, WM_COMMAND, MAKEWPARAM(IDM_FILE_CLOSE, 0), 0);
            return 0;

        case IDC_TB_EXIT:
            SendMessageW(hwnd, WM_CLOSE, 0, 0);
            return 0;
            
        case IDC_TB_ABOUT:
            // This case is no longer used - clicks handled in WM_LBUTTONDOWN
            return 0;
            
        // Files page buttons
        case IDC_FILES_ADD_DIR: {
            // Load last-used folder from DB for this project
            std::wstring pickerInitDir;
            std::wstring pickerKey = L"last_picker_folder_" + std::to_wstring(s_currentProject.id);
            if (s_currentProject.id > 0)
                DB::GetSetting(pickerKey, pickerInitDir);

            // Open folder picker dialog
            BROWSEINFOW bi = {};
            bi.hwndOwner = hwnd;
            bi.lpszTitle = L"Select a folder to add";
            bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE | BIF_USENEWUI;
            if (!pickerInitDir.empty()) {
                bi.lpfn   = PickerFolderCallback;
                bi.lParam = (LPARAM)pickerInitDir.c_str();
            }
            
            LPITEMIDLIST pidl = SHBrowseForFolderW(&bi);
            if (pidl) {
                wchar_t selectedPath[MAX_PATH];
                if (SHGetPathFromIDListW(pidl, selectedPath)) {
                    // Save the parent of selected folder so next open lands one level up
                    if (s_currentProject.id > 0) {
                        std::wstring sel(selectedPath);
                        size_t sl = sel.find_last_of(L"\\/");
                        DB::SetSetting(pickerKey,
                            sl != std::wstring::npos ? sel.substr(0, sl) : sel);
                    }
                    // Extract folder name from path
                    std::wstring path(selectedPath);
                    size_t lastSlash = path.find_last_of(L"\\/");
                    std::wstring folderName = (lastSlash != std::wstring::npos) ? path.substr(lastSlash + 1) : path;
                    
                    // Add folder to TreeView - use selected item as parent if any, otherwise Program Files root
                    if (s_hTreeView && s_hProgramFilesRoot) {
                        // Get currently selected item (if any)
                        HTREEITEM hSelectedItem = TreeView_GetSelection(s_hTreeView);
                        HTREEITEM hParent = s_hProgramFilesRoot;
                        
                        // If a folder is selected and it's not Program Files root, use it as parent
                        if (hSelectedItem && hSelectedItem != s_hProgramFilesRoot) {
                            hParent = hSelectedItem;
                        }
                        
                        // Check if this will be the first folder under Program Files root
                        HTREEITEM hFirstChild = TreeView_GetChild(s_hTreeView, s_hProgramFilesRoot);
                        bool isFirstFolderUnderProgramFiles = (hParent == s_hProgramFilesRoot && hFirstChild == NULL);
                        
                        HTREEITEM hRoot = AddTreeNode(s_hTreeView, hParent, folderName, path);
                        AddTreeNodeRecursive(s_hTreeView, hRoot, path);
                        TreeView_Expand(s_hTreeView, hParent, TVE_EXPAND);
                        TreeView_Expand(s_hTreeView, hRoot, TVE_EXPAND);
                        TreeView_SelectItem(s_hTreeView, hRoot);
                        
                        // Populate ListView with folder contents
                        PopulateListView(s_hListView, path);
                        
                        // If this is the first folder under Program Files, update install path and possibly project name
                        if (isFirstFolderUnderProgramFiles) {
                            std::wstring newInstallPath = L"C:\\Program Files\\" + folderName;
                            SetDlgItemTextW(hwnd, IDC_INSTALL_FOLDER, newInstallPath.c_str());
                            
                            // Also update project name if new project and not manually set
                            if (!s_projectNameManuallySet) {
                                s_currentProject.name = folderName;
                                std::wstring title = L"SetupCraft - " + folderName;
                                SetWindowTextW(hwnd, title.c_str());
                                
                                // Update project name field programmatically
                                s_updatingProjectNameProgrammatically = true;
                                SetDlgItemTextW(hwnd, IDC_PROJECT_NAME, folderName.c_str());
                                s_updatingProjectNameProgrammatically = false;
                            }
                        }
                    }
                    
                    MarkAsModified();
                    UpdateComponentsButtonState(hwnd);
                }
                CoTaskMemFree(pidl);
            }
            return 0;
        }
            
        case IDC_FILES_ADD_FILES: {
            // Load last-used folder from DB for this project
            std::wstring pickerInitDirFiles;
            std::wstring pickerKeyFiles = L"last_picker_files_" + std::to_wstring(s_currentProject.id);
            if (s_currentProject.id > 0)
                DB::GetSetting(pickerKeyFiles, pickerInitDirFiles);

            // If no DB record yet, default to %USERPROFILE% so the file picker
            // never inherits the folder picker's last-visited shell location.
            if (pickerInitDirFiles.empty()) {
                wchar_t userProfile[MAX_PATH] = {};
                if (GetEnvironmentVariableW(L"USERPROFILE", userProfile, MAX_PATH) > 0)
                    pickerInitDirFiles = userProfile;
            }

            // Open file picker dialog with multi-select
            wchar_t fileBuffer[8192] = {};
            OPENFILENAMEW ofn = {};
            ofn.lStructSize = sizeof(OPENFILENAMEW);
            ofn.hwndOwner = hwnd;
            ofn.lpstrFile = fileBuffer;
            ofn.nMaxFile = 8192;
            ofn.lpstrFilter = L"All Files (*.*)\0*.*\0";
            ofn.nFilterIndex = 1;
            ofn.lpstrTitle = L"Select files to add";
            ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST | OFN_ALLOWMULTISELECT | OFN_EXPLORER;
            if (!pickerInitDirFiles.empty())
                ofn.lpstrInitialDir = pickerInitDirFiles.c_str();
            
            if (GetOpenFileNameW(&ofn)) {
                // Save the directory portion as next starting point.
                // nFileOffset is the char-offset of the first filename within lpstrFile,
                // so fileBuffer[0..nFileOffset-2] is just the directory, no trailing backslash.
                if (s_currentProject.id > 0 && ofn.nFileOffset > 1)
                    DB::SetSetting(pickerKeyFiles,
                        std::wstring(fileBuffer, ofn.nFileOffset - 1));

                // Parse multi-select results
                std::wstring directory(fileBuffer);
                wchar_t* p = fileBuffer + directory.length() + 1;
                
                // Determine selected folder target and root for auto-creation
                HTREEITEM hTargetFolder = NULL;
                HTREEITEM hRootForAutoCreate = s_hProgramFilesRoot; // default
                HTREEITEM hSelected = NULL;
                if (s_hTreeView) {
                    hSelected = TreeView_GetSelection(s_hTreeView);
                    if (hSelected) {
                        // If a root node is selected, prefer its first child as target (if any)
                        if (hSelected == s_hProgramFilesRoot || hSelected == s_hProgramDataRoot || hSelected == s_hAppDataRoot) {
                            hTargetFolder = TreeView_GetChild(s_hTreeView, hSelected);
                            hRootForAutoCreate = hSelected;
                        } else {
                            // Regular folder selected
                            hTargetFolder = hSelected;
                            // Determine which root this folder belongs to for auto-create fallback
                            HTREEITEM ancestor = hTargetFolder;
                            HTREEITEM parent = TreeView_GetParent(s_hTreeView, ancestor);
                            while (parent) {
                                HTREEITEM grand = TreeView_GetParent(s_hTreeView, parent);
                                if (!grand) {
                                    // 'parent' is direct child of root
                                    if (parent == s_hProgramFilesRoot || parent == s_hProgramDataRoot || parent == s_hAppDataRoot) {
                                        hRootForAutoCreate = parent;
                                    }
                                    break;
                                }
                                ancestor = parent;
                                parent = grand;
                            }
                        }
                    } else {
                        // No selection: default to first child under Program Files
                        hTargetFolder = TreeView_GetChild(s_hTreeView, s_hProgramFilesRoot);
                        hRootForAutoCreate = s_hProgramFilesRoot;
                    }
                }
                
                // Check if single file or multiple
                if (*p == 0) {
                    // Single file - full path in directory variable
                    std::wstring fullPath = directory;
                    size_t lastSlash = fullPath.find_last_of(L"\\/");
                    std::wstring fileName = (lastSlash != std::wstring::npos) ? fullPath.substr(lastSlash + 1) : fullPath;
                    
                    // If no folder structure exists, create one using the application name under the chosen root
                    if (!hTargetFolder && s_hTreeView && hRootForAutoCreate) {
                        std::wstring folderName = !s_currentProject.name.empty() ? s_currentProject.name : fileName;
                        // Remove extension if folderName came from fileName fallback
                        size_t dotPos = folderName.find_last_of(L".");
                        if (dotPos != std::wstring::npos && folderName == fileName) folderName = folderName.substr(0, dotPos);

                        // Create folder under the selected root
                        hTargetFolder = AddTreeNode(s_hTreeView, hRootForAutoCreate, folderName, L"");
                        TreeView_Expand(s_hTreeView, hRootForAutoCreate, TVE_EXPAND);

                        // If created under Program Files root, update install path and project name as before
                        if (hRootForAutoCreate == s_hProgramFilesRoot) {
                            std::wstring newInstallPath = L"C:\\Program Files\\" + folderName;
                            SetDlgItemTextW(hwnd, IDC_INSTALL_FOLDER, newInstallPath.c_str());
                            if (!s_projectNameManuallySet) {
                                s_currentProject.name = folderName;
                                std::wstring title = L"SetupCraft - " + folderName;
                                SetWindowTextW(hwnd, title.c_str());
                                s_updatingProjectNameProgrammatically = true;
                                SetDlgItemTextW(hwnd, IDC_PROJECT_NAME, folderName.c_str());
                                s_updatingProjectNameProgrammatically = false;
                            }
                        }
                    }
                    
                    // Add file to ListView
                    if (s_hListView) {
                        SHFILEINFOW sfi = {};
                        SHGetFileInfoW(fullPath.c_str(), 0, &sfi, sizeof(sfi), SHGFI_SYSICONINDEX | SHGFI_SMALLICON);
                        
                        LVITEMW lvi = {};
                        lvi.mask = LVIF_TEXT | LVIF_PARAM | LVIF_IMAGE;
                        lvi.iItem = ListView_GetItemCount(s_hListView);
                        lvi.iSubItem = 0;
                        lvi.pszText = (LPWSTR)fullPath.c_str();
                        lvi.iImage = sfi.iIcon;
                        
                        wchar_t* pathCopy = (wchar_t*)malloc((fullPath.length() + 1) * sizeof(wchar_t));
                        if (pathCopy) {
                            wcscpy(pathCopy, fullPath.c_str());
                            lvi.lParam = (LPARAM)pathCopy;
                        }
                        
                        int idx = ListView_InsertItem(s_hListView, &lvi);
                        std::wstring dest = L"\\" + fileName;
                        ListView_SetItemText(s_hListView, idx, 1, (LPWSTR)dest.c_str());
                        
                        // If target folder is virtual (no physical path), store file for persistence
                        if (hTargetFolder) {
                            TVITEMW tvItem = {};
                            tvItem.mask = TVIF_PARAM;
                            tvItem.hItem = hTargetFolder;
                            TreeView_GetItem(s_hTreeView, &tvItem);
                            
                            wchar_t* folderPath = (wchar_t*)tvItem.lParam;
                            if (!folderPath || wcslen(folderPath) == 0) {
                                // Virtual folder - store file
                                VirtualFolderFile fileInfo;
                                fileInfo.sourcePath = fullPath;
                                fileInfo.destination = dest;
                                if (hRootForAutoCreate == s_hAskAtInstallRoot) {
                                    fileInfo.install_scope = L"AskAtInstall";
                                }
                                s_virtualFolderFiles[hTargetFolder].push_back(fileInfo);
                            }
                        }
                    }
                } else {
                    // Multiple files - first file used for folder and project name
                    
                    // If no folder structure exists, create one using the application name under the chosen root
                    if (!hTargetFolder && s_hTreeView && hRootForAutoCreate) {
                        std::wstring firstFileName(p);
                        std::wstring folderName = !s_currentProject.name.empty() ? s_currentProject.name : firstFileName;
                        size_t dotPos = folderName.find_last_of(L".");
                        if (dotPos != std::wstring::npos && folderName == firstFileName) folderName = folderName.substr(0, dotPos);

                        // Create folder under the selected root
                        hTargetFolder = AddTreeNode(s_hTreeView, hRootForAutoCreate, folderName, L"");
                        TreeView_Expand(s_hTreeView, hRootForAutoCreate, TVE_EXPAND);

                        // If created under Program Files root, update install path and project name as before
                        if (hRootForAutoCreate == s_hProgramFilesRoot) {
                            std::wstring newInstallPath = L"C:\\Program Files\\" + folderName;
                            SetDlgItemTextW(hwnd, IDC_INSTALL_FOLDER, newInstallPath.c_str());
                            if (!s_projectNameManuallySet) {
                                s_currentProject.name = folderName;
                                std::wstring title = L"SetupCraft - " + folderName;
                                SetWindowTextW(hwnd, title.c_str());
                                s_updatingProjectNameProgrammatically = true;
                                SetDlgItemTextW(hwnd, IDC_PROJECT_NAME, folderName.c_str());
                                s_updatingProjectNameProgrammatically = false;
                            }
                        }
                    }
                    
                    while (*p) {
                        std::wstring fileName(p);
                        std::wstring fullPath = directory + L"\\" + fileName;
                        
                        // Add file to ListView
                        if (s_hListView) {
                            SHFILEINFOW sfi = {};
                            SHGetFileInfoW(fullPath.c_str(), 0, &sfi, sizeof(sfi), SHGFI_SYSICONINDEX | SHGFI_SMALLICON);
                            
                            LVITEMW lvi = {};
                            lvi.mask = LVIF_TEXT | LVIF_PARAM | LVIF_IMAGE;
                            lvi.iItem = ListView_GetItemCount(s_hListView);
                            lvi.iSubItem = 0;
                            lvi.pszText = (LPWSTR)fullPath.c_str();
                            lvi.iImage = sfi.iIcon;
                            
                            wchar_t* pathCopy = (wchar_t*)malloc((fullPath.length() + 1) * sizeof(wchar_t));
                            if (pathCopy) {
                                wcscpy(pathCopy, fullPath.c_str());
                                lvi.lParam = (LPARAM)pathCopy;
                            }
                            
                            int idx = ListView_InsertItem(s_hListView, &lvi);
                            std::wstring dest = L"\\" + fileName;
                            ListView_SetItemText(s_hListView, idx, 1, (LPWSTR)dest.c_str());
                            
                            // If target folder is virtual (no physical path), store file for persistence
                            if (hTargetFolder) {
                                TVITEMW tvItem = {};
                                tvItem.mask = TVIF_PARAM;
                                tvItem.hItem = hTargetFolder;
                                TreeView_GetItem(s_hTreeView, &tvItem);
                                
                                wchar_t* folderPath = (wchar_t*)tvItem.lParam;
                                if (!folderPath || wcslen(folderPath) == 0) {
                                    // Virtual folder - store file
                                    VirtualFolderFile fileInfo;
                                    fileInfo.sourcePath = fullPath;
                                    fileInfo.destination = dest;
                                    if (hRootForAutoCreate == s_hAskAtInstallRoot) {
                                        fileInfo.install_scope = L"AskAtInstall";
                                    }
                                    s_virtualFolderFiles[hTargetFolder].push_back(fileInfo);
                                }
                            }
                        }
                        
                        p += fileName.length() + 1;
                    }
                }
                
                MarkAsModified();
                UpdateComponentsButtonState(hwnd);
            }
            return 0;
        }

        case IDC_ASK_AT_INSTALL: {
            // Toggle ask-at-install mode and refresh page
            s_askAtInstallEnabled = (SendMessageW(GetDlgItem(hwnd, IDC_ASK_AT_INSTALL), BM_GETCHECK, 0, 0) == BST_CHECKED);
            // Recreate Files page to reflect root changes
            SwitchPage(hwnd, 0);
            return 0;
        }
            
        case IDC_FILES_REMOVE: {
            // Determine which control to remove from based on focus
            HWND hFocus = GetFocus();
            bool removedSomething = false;
            
            // Check if ListView has focus or has selected items
            if (hFocus == s_hListView || (s_hListView && IsWindow(s_hListView) && ListView_GetSelectedCount(s_hListView) > 0)) {
                int selCount = ListView_GetSelectedCount(s_hListView);
                if (selCount > 0) {
                    std::wstring msg = (selCount == 1)
                        ? LocFmt(s_locale, L"confirm_remove_file")
                        : LocFmt(s_locale, L"confirm_remove_files", std::to_wstring(selCount));
                    if (!ShowConfirmDeleteDialog(hwnd, LocFmt(s_locale, L"confirm_remove_title"), msg, s_locale)) {
                        return 0;
                    }
                }
                // Remove all selected items from ListView
                HTREEITEM hFolder = s_hTreeView ? TreeView_GetSelection(s_hTreeView) : NULL;
                std::unordered_set<std::wstring> removedPaths;
                int count = ListView_GetItemCount(s_hListView);
                for (int i = count - 1; i >= 0; i--) {
                    if (ListView_GetItemState(s_hListView, i, LVIS_SELECTED) & LVIS_SELECTED) {
                        LVITEM lvi = {};
                        lvi.mask = LVIF_PARAM;
                        lvi.iItem = i;
                        if (ListView_GetItem(s_hListView, &lvi) && lvi.lParam) {
                            wchar_t* path = (wchar_t*)lvi.lParam;
                            removedPaths.insert(path);
                            free(path);
                        }
                        ListView_DeleteItem(s_hListView, i);
                        removedSomething = true;
                    }
                }
                if (removedSomething) {
                    // Sync the virtual-folder cache so the snapshot (and Comp tree)
                    // no longer includes the deleted files.
                    if (hFolder) {
                        auto vit = s_virtualFolderFiles.find(hFolder);
                        if (vit != s_virtualFolderFiles.end()) {
                            auto& vf = vit->second;
                            vf.erase(std::remove_if(vf.begin(), vf.end(),
                                [&](const VirtualFolderFile& f) {
                                    return removedPaths.count(f.sourcePath) > 0;
                                }), vf.end());
                        }
                    }
                    // Remove the corresponding component rows from memory
                    // (in-memory only — DB is written only on explicit Save).
                    PurgeComponentRowsByPaths(removedPaths);
                    UpdateComponentsButtonState(hwnd);
                    MarkAsModified();
                    return 0;
                }
            }
            
            // Remove multi-selected (or single selected) items from TreeView
            if (s_hTreeView && IsWindow(s_hTreeView)) {
                std::vector<HTREEITEM> selectedItems;
                for (HTREEITEM hI : s_filesTreeMultiSel) {
                    if (hI != s_hProgramFilesRoot && hI != s_hProgramDataRoot &&
                        hI != s_hAppDataRoot      && hI != s_hAskAtInstallRoot)
                        selectedItems.push_back(hI);
                }

                if (!selectedItems.empty()) {
                    std::set<HTREEITEM> selSet(selectedItems.begin(), selectedItems.end());
                    std::vector<HTREEITEM> toDelete;
                    for (HTREEITEM hI : selectedItems) {
                        bool ancestorSelected = false;
                        HTREEITEM hP = TreeView_GetParent(s_hTreeView, hI);
                        while (hP) {
                            if (selSet.count(hP)) { ancestorSelected = true; break; }
                            hP = TreeView_GetParent(s_hTreeView, hP);
                        }
                        if (!ancestorSelected)
                            toDelete.push_back(hI);
                    }
                    int n = (int)toDelete.size();
                    std::wstring msg;
                    if (n == 1)
                        msg = TreeView_GetChild(s_hTreeView, toDelete[0])
                            ? LocFmt(s_locale, L"confirm_remove_folder_subtree")
                            : LocFmt(s_locale, L"confirm_remove_folder");
                    else
                        msg = LocFmt(s_locale, L"confirm_remove_folders", std::to_wstring(n));
                    if (ShowConfirmDeleteDialog(hwnd, LocFmt(s_locale, L"confirm_remove_title"), msg, s_locale)) {
                        // Collect file paths BEFORE wiping s_virtualFolderFiles,
                        // then remove orphaned component rows from memory.
                        {
                            std::unordered_set<std::wstring> delPaths;
                            for (HTREEITEM hI : toDelete)
                                CollectSubtreePaths(s_hTreeView, hI, delPaths);
                            PurgeComponentRowsByPaths(delPaths);
                        }
                        std::function<void(HTREEITEM)> CleanupVF = [&](HTREEITEM hI) {
                            s_virtualFolderFiles.erase(hI);
                            HTREEITEM hC = TreeView_GetChild(s_hTreeView, hI);
                            while (hC) { CleanupVF(hC); hC = TreeView_GetNextSibling(s_hTreeView, hC); }
                        };
                        bool anyUnderPF = false;
                        for (HTREEITEM hSel : toDelete) {
                            HTREEITEM hParent = TreeView_GetParent(s_hTreeView, hSel);
                            if (hParent == s_hProgramFilesRoot) anyUnderPF = true;
                            CleanupVF(hSel);
                            TreeView_DeleteItem(s_hTreeView, hSel);
                        }
                        s_filesTreeMultiSel.clear();
                        if (s_hListView && IsWindow(s_hListView))
                            ListView_DeleteAllItems(s_hListView);
                        UpdateComponentsButtonState(hwnd);
                        MarkAsModified();
                        if (anyUnderPF) UpdateInstallPathFromTree(hwnd);
                    }
                    return 0;
                }

                // Fallback: single selected item when nothing is checked
                HTREEITEM hSel = TreeView_GetSelection(s_hTreeView);
                if (hSel &&
                    hSel != s_hProgramFilesRoot && hSel != s_hProgramDataRoot &&
                    hSel != s_hAppDataRoot      && hSel != s_hAskAtInstallRoot) {

                    bool shouldDelete = true;
                    wchar_t folderName[256] = {};
                    TVITEMW tvItem = {};
                    tvItem.mask = TVIF_TEXT;
                    tvItem.hItem = hSel;
                    tvItem.pszText = folderName;
                    tvItem.cchTextMax = 256;
                    TreeView_GetItem(s_hTreeView, &tvItem);
                    if (TreeView_GetChild(s_hTreeView, hSel)) {
                        std::wstring msg = LocFmt(s_locale, L"confirm_remove_folder_recursive", std::wstring(folderName));
                        shouldDelete = ShowConfirmDeleteDialog(hwnd, LocFmt(s_locale, L"confirm_remove_title"), msg, s_locale);
                    } else {
                        std::wstring msg = LocFmt(s_locale, L"confirm_remove_single", std::wstring(folderName));
                        shouldDelete = ShowConfirmDeleteDialog(hwnd, LocFmt(s_locale, L"confirm_remove_title"), msg, s_locale);
                    }
                    if (shouldDelete) {
                        // Collect file paths BEFORE wiping s_virtualFolderFiles,
                        // then remove orphaned component rows from memory.
                        {
                            std::unordered_set<std::wstring> delPaths;
                            CollectSubtreePaths(s_hTreeView, hSel, delPaths);
                            PurgeComponentRowsByPaths(delPaths);
                        }
                        std::function<void(HTREEITEM)> CleanupVF = [&](HTREEITEM hItem) {
                            s_virtualFolderFiles.erase(hItem);
                            HTREEITEM hChild = TreeView_GetChild(s_hTreeView, hItem);
                            while (hChild) {
                                CleanupVF(hChild);
                                hChild = TreeView_GetNextSibling(s_hTreeView, hChild);
                            }
                        };
                        CleanupVF(hSel);
                        HTREEITEM hParent = TreeView_GetParent(s_hTreeView, hSel);
                        bool wasUnderProgramFiles = (hParent == s_hProgramFilesRoot);
                        TreeView_DeleteItem(s_hTreeView, hSel);
                        if (s_hListView && IsWindow(s_hListView))
                            ListView_DeleteAllItems(s_hListView);
                        UpdateComponentsButtonState(hwnd);
                        MarkAsModified();
                        if (wasUnderProgramFiles) UpdateInstallPathFromTree(hwnd);
                        return 0;
                    }
                    return 0;
                }
            }

            MessageBoxW(hwnd, L"Select a folder or file to remove first.",
                        L"Remove", MB_OK | MB_ICONINFORMATION);
            return 0;
        }
            
        case IDC_BROWSE_INSTALL_DIR: {
            // Get the current install folder and extract the last component
            HWND hEdit = GetDlgItem(hwnd, IDC_INSTALL_FOLDER);
            if (!hEdit) return 0;
            
            wchar_t currentPath[MAX_PATH];
            GetWindowTextW(hEdit, currentPath, MAX_PATH);
            
            // Extract the last folder component (e.g., "SetupCraft")
            std::wstring current(currentPath);
            size_t lastSlash = current.find_last_of(L"\\/");
            std::wstring appName = (lastSlash != std::wstring::npos) ? current.substr(lastSlash + 1) : current;
            
            // Validate appName - should not be empty
            if (appName.empty() || appName == L"\\" || appName == L"/") {
                appName = s_currentProject.name.empty() ? L"SetupCraft" : s_currentProject.name;
            }
            
            // Open folder picker dialog
            BROWSEINFOW bi = {};
            bi.hwndOwner = hwnd;
            bi.lpszTitle = L"Select installation base directory";
            bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE | BIF_USENEWUI;
            
            LPITEMIDLIST pidl = SHBrowseForFolderW(&bi);
            if (pidl) {
                wchar_t selectedPath[MAX_PATH];
                if (SHGetPathFromIDListW(pidl, selectedPath)) {
                    // Build new path: selectedPath + "\\" + appName
                    std::wstring newPath = selectedPath;
                    if (!newPath.empty() && newPath.back() != L'\\' && newPath.back() != L'/') {
                        newPath += L"\\";
                    }
                    newPath += appName;
                    
                    // Update edit control
                    SetWindowTextW(hEdit, newPath.c_str());
                    s_installPathUserEdited = true; // User manually chose a path
                    s_currentInstallPath = newPath;  // Keep static in sync
                }
                CoTaskMemFree(pidl);
            }
            return 0;
        }
        
        // Registry page controls
        case IDC_REG_CHECKBOX: {
            // Toggle register in Windows setting
            HWND hCheckbox = GetDlgItem(hwnd, IDC_REG_CHECKBOX);
            if (hCheckbox) {
                s_registerInWindows = (SendMessageW(hCheckbox, BM_GETCHECK, 0, 0) == BST_CHECKED);
            }
            return 0;
        }
        
        case IDC_REG_ADD_ICON: {
            // Open file dialog to select .ico file
            OPENFILENAMEW ofn = {};
            wchar_t szFile[MAX_PATH] = {};
            
            ofn.lStructSize = sizeof(OPENFILENAMEW);
            ofn.hwndOwner = hwnd;
            ofn.lpstrFile = szFile;
            ofn.nMaxFile = MAX_PATH;
            ofn.lpstrFilter = L"Icon Files (*.ico)\0*.ico\0All Files (*.*)\0*.*\0";
            ofn.nFilterIndex = 1;
            ofn.lpstrTitle = L"Select Application Icon";
            ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST | OFN_HIDEREADONLY;
            
            if (GetOpenFileNameW(&ofn)) {
                s_appIconPath = szFile;
                
                // Update icon preview
                HWND hIconPreview = GetDlgItem(hwnd, IDC_REG_ICON_PREVIEW);
                if (hIconPreview) {
                    HICON hIcon = (HICON)LoadImageW(NULL, s_appIconPath.c_str(), IMAGE_ICON, 48, 48, LR_LOADFROMFILE);
                    if (hIcon) {
                        SendMessageW(hIconPreview, STM_SETICON, (WPARAM)hIcon, 0);
                    }
                }
            }
            return 0;
        }
        
        case IDC_REG_SHOW_REGKEY: {
            // Don't show dialog if already open
            if (s_hRegKeyDialog && IsWindow(s_hRegKeyDialog)) {
                SetForegroundWindow(s_hRegKeyDialog);
                return 0;
            }
            
            // Build dialog data
            static RegKeyDialogData dialogData;
            dialogData.regPath = L"HKEY_LOCAL_MACHINE\\SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\";
            dialogData.regPath += s_currentProject.name;
            
            // Get localized strings
            auto itTitle = s_locale.find(L"reg_regkey_title");
            std::wstring title = (itTitle != s_locale.end()) ? itTitle->second : L"Registry Key Path";
            
            auto itLabel = s_locale.find(L"reg_regkey_label");
            dialogData.labelText = (itLabel != s_locale.end()) ? itLabel->second : L"This application will be registered at:";
            
            auto itClose = s_locale.find(L"reg_regkey_close");
            dialogData.closeText = (itClose != s_locale.end()) ? itClose->second : L"Close";
            
            auto itNavigate = s_locale.find(L"reg_regkey_take_me_there");
            dialogData.navigateText = (itNavigate != s_locale.end()) ? itNavigate->second : L"Take me there";
            
            auto itCopy = s_locale.find(L"reg_regkey_copy");
            dialogData.copyText = (itCopy != s_locale.end()) ? itCopy->second : L"Copy";
            
            // Register dialog window class if needed
            static bool dialogClassRegistered = false;
            if (!dialogClassRegistered) {
                WNDCLASSEXW wcDlg = {};
                wcDlg.cbSize = sizeof(WNDCLASSEXW);
                wcDlg.style = CS_HREDRAW | CS_VREDRAW;
                wcDlg.lpfnWndProc = RegKeyDialogProc;
                wcDlg.hInstance = GetModuleHandleW(NULL);
                wcDlg.hCursor = LoadCursor(NULL, IDC_ARROW);
                wcDlg.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
                wcDlg.lpszClassName = L"RegKeyDialog";
                RegisterClassExW(&wcDlg);
                dialogClassRegistered = true;
            }
            
            // Compute DPI-aware dialog size
            int clientW = S(RK_CONT_W) + 2*S(RK_PAD_H);
            int clientH = S(RK_PAD_T) + S(RK_LBL_H) + S(RK_GAP_LE)
                        + S(RK_EDIT_H) + S(RK_GAP_EB) + S(RK_BTN_H) + S(RK_PAD_B);
            RECT wrc = { 0, 0, clientW, clientH };
            AdjustWindowRectEx(&wrc, WS_POPUP|WS_CAPTION|WS_SYSMENU, FALSE, WS_EX_TOPMOST|WS_EX_TOOLWINDOW);
            int dlgWidth  = wrc.right  - wrc.left;
            int dlgHeight = wrc.bottom - wrc.top;

            // Centre over main window
            RECT rcMain;
            GetWindowRect(hwnd, &rcMain);
            int dlgX = rcMain.left + (rcMain.right  - rcMain.left - dlgWidth)  / 2;
            int dlgY = rcMain.top  + (rcMain.bottom - rcMain.top  - dlgHeight) / 2;
            RECT rcWork; SystemParametersInfoW(SPI_GETWORKAREA, 0, &rcWork, 0);
            if (dlgX < rcWork.left) dlgX = rcWork.left;
            if (dlgY < rcWork.top)  dlgY = rcWork.top;
            if (dlgX + dlgWidth  > rcWork.right)  dlgX = rcWork.right  - dlgWidth;
            if (dlgY + dlgHeight > rcWork.bottom) dlgY = rcWork.bottom - dlgHeight;

            // Create modeless dialog
            s_hRegKeyDialog = CreateWindowExW(
                WS_EX_TOPMOST | WS_EX_TOOLWINDOW,
                L"RegKeyDialog",
                title.c_str(),
                WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_VISIBLE,
                dlgX, dlgY, dlgWidth, dlgHeight,
                hwnd,
                NULL,
                GetModuleHandleW(NULL),
                &dialogData
            );
            
            return 0;
        }
        
        case IDC_REG_ADD_KEY: {
            // Get currently selected tree item
            if (!s_hRegTreeView || !IsWindow(s_hRegTreeView)) {
                MessageBoxW(hwnd, L"No registry key selected", L"Error", MB_OK | MB_ICONERROR);
                return 0;
            }
            
            HTREEITEM hSelected = TreeView_GetSelection(s_hRegTreeView);
            if (!hSelected) {
                MessageBoxW(hwnd, L"Please select a registry key first", L"Error", MB_OK | MB_ICONERROR);
                return 0;
            }
            
            // Get locale strings
            auto itTitleAddKey = s_locale.find(L"reg_add_key_title");
            std::wstring title = (itTitleAddKey != s_locale.end()) ? itTitleAddKey->second : L"Add Registry Key";
            
            auto itName = s_locale.find(L"reg_add_key_name");
            std::wstring nameLabel = (itName != s_locale.end()) ? itName->second : L"Key Name:";
            
            auto itOk = s_locale.find(L"ok");
            std::wstring okText = (itOk != s_locale.end()) ? itOk->second : L"OK";
            
            auto itCancel = s_locale.find(L"cancel");
            std::wstring cancelText = (itCancel != s_locale.end()) ? itCancel->second : L"Cancel";
            
            // Create dialog data
            AddKeyDialogData dialogData;
            dialogData.nameText = nameLabel;
            dialogData.okText = okText;
            dialogData.cancelText = cancelText;
            dialogData.defaultKeyName = s_currentProject.name;
            dialogData.okClicked = false;
            
            // Register dialog class if not already registered
            WNDCLASSEXW wc = {};
            wc.cbSize = sizeof(WNDCLASSEXW);
            if (!GetClassInfoExW(GetModuleHandleW(NULL), L"AddKeyDialog", &wc)) {
                wc.lpfnWndProc = AddKeyDialogProc;
                wc.hInstance = GetModuleHandleW(NULL);
                wc.lpszClassName = L"AddKeyDialog";
                wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
                wc.hCursor = LoadCursor(NULL, IDC_ARROW);
                RegisterClassExW(&wc);
            }
            
            // Compute DPI-aware dialog size via AdjustWindowRectEx
            int clientW = S(AK_PAD_H)+S(AK_LBL_W)+S(AK_FLD_GAP)+S(AK_EDIT_W)+S(AK_PAD_H);
            int clientH = S(AK_PAD_T)+S(AK_ROW_H)+S(AK_GAP_EB)+S(AK_BTN_H)+S(AK_PAD_B);
            RECT wrcAK1 = {0,0,clientW,clientH};
            AdjustWindowRectEx(&wrcAK1, WS_POPUP|WS_CAPTION|WS_SYSMENU, FALSE,
                               WS_EX_TOPMOST|WS_EX_TOOLWINDOW);
            int dlgWidth  = wrcAK1.right-wrcAK1.left;
            int dlgHeight = wrcAK1.bottom-wrcAK1.top;
            RECT rcMainAK1; GetWindowRect(hwnd, &rcMainAK1);
            int dlgX = rcMainAK1.left + (rcMainAK1.right-rcMainAK1.left-dlgWidth)/2;
            int dlgY = rcMainAK1.top  + (rcMainAK1.bottom-rcMainAK1.top-dlgHeight)/2;
            RECT rcWkAK1; SystemParametersInfoW(SPI_GETWORKAREA,0,&rcWkAK1,0);
            if(dlgX<rcWkAK1.left) dlgX=rcWkAK1.left;
            if(dlgY<rcWkAK1.top)  dlgY=rcWkAK1.top;
            if(dlgX+dlgWidth>rcWkAK1.right)  dlgX=rcWkAK1.right-dlgWidth;
            if(dlgY+dlgHeight>rcWkAK1.bottom) dlgY=rcWkAK1.bottom-dlgHeight;

            // Show dialog
            HWND hDialog = CreateWindowExW(
                WS_EX_TOPMOST | WS_EX_TOOLWINDOW,
                L"AddKeyDialog",
                title.c_str(),
                WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_VISIBLE,
                dlgX, dlgY, dlgWidth, dlgHeight,
                hwnd,
                NULL,
                GetModuleHandleW(NULL),
                &dialogData
            );
            
            // Modal message loop
            MSG msg;
            while (GetMessageW(&msg, NULL, 0, 0) > 0) {
                if (!IsWindow(hDialog)) break;
                TranslateMessage(&msg);
                DispatchMessageW(&msg);
            }
            
            // If OK was clicked, add the key
            if (dialogData.okClicked && !dialogData.keyName.empty()) {
                // Add as subkey of selected item
                TVINSERTSTRUCTW tvis = {};
                tvis.hParent = hSelected;
                tvis.hInsertAfter = TVI_LAST;
                tvis.item.mask = TVIF_TEXT | TVIF_STATE;
                tvis.item.pszText = (LPWSTR)dialogData.keyName.c_str();
                tvis.item.state = 0;
                tvis.item.stateMask = TVIS_EXPANDED;
                
                HTREEITEM hNewItem = TreeView_InsertItem(s_hRegTreeView, &tvis);
                
                if (hNewItem) {
                    // Expand parent to show new key
                    TreeView_Expand(s_hRegTreeView, hSelected, TVE_EXPAND);
                    
                    // Select the new key
                    TreeView_SelectItem(s_hRegTreeView, hNewItem);
                    
                    MarkAsModified();
                }
            }
            
            return 0;
        }
        
        case IDC_REG_ADD_VALUE: {
            // Get currently selected tree item
            if (!s_hRegTreeView || !IsWindow(s_hRegTreeView)) {
                MessageBoxW(hwnd, L"No registry key selected", L"Error", MB_OK | MB_ICONERROR);
                return 0;
            }
            
            HTREEITEM hSelected = TreeView_GetSelection(s_hRegTreeView);
            if (!hSelected) {
                MessageBoxW(hwnd, L"Please select a registry key first", L"Error", MB_OK | MB_ICONERROR);
                return 0;
            }
            
            // Get locale strings
            auto itTitleAdd = s_locale.find(L"reg_add_value_title");
            std::wstring title = (itTitleAdd != s_locale.end()) ? itTitleAdd->second : L"Add Registry Value";
            
            auto itName = s_locale.find(L"reg_add_value_name");
            std::wstring nameLabel = (itName != s_locale.end()) ? itName->second : L"Value Name:";
            
            auto itType = s_locale.find(L"reg_add_value_type");
            std::wstring typeLabel = (itType != s_locale.end()) ? itType->second : L"Type:";
            
            auto itData = s_locale.find(L"reg_add_value_data");
            std::wstring dataLabel = (itData != s_locale.end()) ? itData->second : L"Data:";
            
            auto itOk = s_locale.find(L"ok");
            std::wstring okText = (itOk != s_locale.end()) ? itOk->second : L"OK";
            
            auto itCancel = s_locale.find(L"cancel");
            std::wstring cancelText = (itCancel != s_locale.end()) ? itCancel->second : L"Cancel";
            
            // Create dialog data
            AddValueDialogData dialogData;
            dialogData.nameText = nameLabel;
            dialogData.typeText = typeLabel;
            dialogData.dataText = dataLabel;
            dialogData.okText = okText;
            dialogData.cancelText = cancelText;
            dialogData.okClicked = false;
            
            // Register dialog class if not already registered
            WNDCLASSEXW wc = {};
            wc.cbSize = sizeof(WNDCLASSEXW);
            if (!GetClassInfoExW(GetModuleHandleW(NULL), L"AddValueDialog", &wc)) {
                wc.lpfnWndProc = AddValueDialogProc;
                wc.hInstance = GetModuleHandleW(NULL);
                wc.lpszClassName = L"AddValueDialog";
                wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
                wc.hCursor = LoadCursor(NULL, IDC_ARROW);
                RegisterClassExW(&wc);
            }
            
            // Compute DPI-aware dialog size via AdjustWindowRectEx
            int avClientW = S(AV_PAD_H)+S(AV_LBL_W)+S(AV_FLD_GAP)+S(AV_EDIT_W)+S(AV_PAD_H);
            int avClientH = S(AV_PAD_T)
                          + S(AV_ROW_H)+S(AV_GAP_R1)
                          + S(AV_ROW_H)+S(AV_GAP_R2)
                          + S(AV_ROW_H)+S(AV_GAP_RB)
                          + S(AV_BTN_H)+S(AV_PAD_B);
            RECT wrcAV1 = {0,0,avClientW,avClientH};
            AdjustWindowRectEx(&wrcAV1, WS_POPUP|WS_CAPTION|WS_SYSMENU, FALSE,
                               WS_EX_TOPMOST|WS_EX_TOOLWINDOW);
            int dlgWidth  = wrcAV1.right-wrcAV1.left;
            int dlgHeight = wrcAV1.bottom-wrcAV1.top;
            RECT rcMainAV1; GetWindowRect(hwnd, &rcMainAV1);
            int dlgX = rcMainAV1.left + (rcMainAV1.right-rcMainAV1.left-dlgWidth)/2;
            int dlgY = rcMainAV1.top  + (rcMainAV1.bottom-rcMainAV1.top-dlgHeight)/2;
            RECT rcWkAV1; SystemParametersInfoW(SPI_GETWORKAREA,0,&rcWkAV1,0);
            if(dlgX<rcWkAV1.left) dlgX=rcWkAV1.left;
            if(dlgY<rcWkAV1.top)  dlgY=rcWkAV1.top;
            if(dlgX+dlgWidth>rcWkAV1.right)  dlgX=rcWkAV1.right-dlgWidth;
            if(dlgY+dlgHeight>rcWkAV1.bottom) dlgY=rcWkAV1.bottom-dlgHeight;

            // Show dialog
            HWND hDialog = CreateWindowExW(
                WS_EX_TOPMOST | WS_EX_TOOLWINDOW,
                L"AddValueDialog",
                title.c_str(),
                WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_VISIBLE,
                dlgX, dlgY, dlgWidth, dlgHeight,
                hwnd,
                NULL,
                GetModuleHandleW(NULL),
                &dialogData
            );
            
            // Modal message loop
            MSG msg;
            while (GetMessageW(&msg, NULL, 0, 0) > 0) {
                if (!IsWindow(hDialog)) break;
                TranslateMessage(&msg);
                DispatchMessageW(&msg);
            }
            
            // If OK was clicked, add the value
            if (dialogData.okClicked && !dialogData.valueName.empty()) {
                // Get tree path for hive and path
                wchar_t pathBuf[512];
                std::wstring fullPath;
                HTREEITEM hItem = hSelected;
                
                // Build path from root to selected item
                std::vector<std::wstring> pathParts;
                while (hItem) {
                    TVITEMW item;
                    item.mask = TVIF_TEXT;
                    item.hItem = hItem;
                    item.pszText = pathBuf;
                    item.cchTextMax = 512;
                    if (TreeView_GetItem(s_hRegTreeView, &item)) {
                        pathParts.insert(pathParts.begin(), item.pszText);
                    }
                    hItem = TreeView_GetParent(s_hRegTreeView, hItem);
                }
                
                // Get hive (first part) and path (rest)
                std::wstring hive = pathParts.empty() ? L"HKLM" : pathParts[0];
                std::wstring path;
                for (size_t i = 1; i < pathParts.size(); i++) {
                    if (i > 1) path += L"\\";
                    path += pathParts[i];
                }
                
                // Create registry entry
                RegistryEntry entry;
                entry.hive = hive;
                entry.path = path;
                entry.name = dialogData.valueName;
                entry.type = dialogData.valueType;
                entry.data = dialogData.valueData;
                
                // Add to map
                s_registryValues[hSelected].push_back(entry);
                
                // Refresh ListView
                if (s_hRegListView && IsWindow(s_hRegListView)) {
                    ListView_DeleteAllItems(s_hRegListView);
                    
                    auto it = s_registryValues.find(hSelected);
                    if (it != s_registryValues.end()) {
                        LVITEMW lvi = {};
                        lvi.mask = LVIF_TEXT;
                        int itemIndex = 0;
                        
                        for (const auto& e : it->second) {
                            lvi.iItem = itemIndex++;
                            lvi.iSubItem = 0;
                            lvi.pszText = (LPWSTR)e.name.c_str();
                            int idx = ListView_InsertItem(s_hRegListView, &lvi);
                            ListView_SetItemText(s_hRegListView, idx, 1, (LPWSTR)e.type.c_str());
                            ListView_SetItemText(s_hRegListView, idx, 2, (LPWSTR)e.data.c_str());
                        }
                    }
                    
                    InvalidateRect(s_hRegListView, NULL, TRUE);
                    UpdateWindow(s_hRegListView);
                }
                
                MarkAsModified();
            }
            
            return 0;
        }
        
        case IDC_REG_REMOVE: {
            // Delete the selected key or value based on focus
            HWND hFocused = GetFocus();
            
            // Check if TreeView has focus and has a selected item
            if (hFocused == s_hRegTreeView) {
                HTREEITEM hSelected = TreeView_GetSelection(s_hRegTreeView);
                if (hSelected) {
                    // Call the Delete Key handler
                    SendMessageW(hwnd, WM_COMMAND, IDM_REG_DELETE_KEY, 0);
                } else {
                    auto itMsg = s_locale.find(L"reg_select_to_delete");
                    std::wstring msg = (itMsg != s_locale.end()) ? itMsg->second : L"Please select a registry key or value to delete.";
                    MessageBoxW(hwnd, msg.c_str(), L"Delete", MB_OK | MB_ICONINFORMATION);
                }
                return 0;
            }
            
            // Check if ListView has focus and has a selected item
            if (hFocused == s_hRegListView) {
                int iSelected = ListView_GetNextItem(s_hRegListView, -1, LVNI_SELECTED);
                if (iSelected != -1) {
                    // Call the Delete Value handler
                    SendMessageW(hwnd, WM_COMMAND, IDM_REG_DELETE_VALUE, 0);
                } else {
                    auto itMsg = s_locale.find(L"reg_select_to_delete");
                    std::wstring msg = (itMsg != s_locale.end()) ? itMsg->second : L"Please select a registry key or value to delete.";
                    MessageBoxW(hwnd, msg.c_str(), L"Delete", MB_OK | MB_ICONINFORMATION);
                }
                return 0;
            }
            
            // Neither control has focus - check which has the focused (blue) selection
            HTREEITEM hTreeSel = TreeView_GetSelection(s_hRegTreeView);
            int iListSel = ListView_GetNextItem(s_hRegListView, -1, LVNI_FOCUSED);
            
            // Prioritize TreeView selection (keys) over ListView (values)
            if (hTreeSel) {
                // TreeView has a selection - delete the key
                SendMessageW(hwnd, WM_COMMAND, IDM_REG_DELETE_KEY, 0);
            } else if (iListSel != -1) {
                // ListView has a focused selection - delete the value
                SendMessageW(hwnd, WM_COMMAND, IDM_REG_DELETE_VALUE, 0);
            } else {
                // Nothing selected
                auto itMsg = s_locale.find(L"reg_select_to_delete");
                std::wstring msg = (itMsg != s_locale.end()) ? itMsg->second : L"Please select a registry key or value to delete.";
                MessageBoxW(hwnd, msg.c_str(), L"Delete", MB_OK | MB_ICONINFORMATION);
            }
            
            return 0;
        }
        
        case IDC_REG_EDIT: {
            // Edit the selected key or value based on focus
            HWND hFocused = GetFocus();
            
            // Check if TreeView has focus and has a selected item
            if (hFocused == s_hRegTreeView) {
                HTREEITEM hSelected = TreeView_GetSelection(s_hRegTreeView);
                if (hSelected) {
                    // Call the Edit Key handler
                    SendMessageW(hwnd, WM_COMMAND, IDM_REG_EDIT_KEY, 0);
                } else {
                    auto itMsg = s_locale.find(L"reg_select_to_edit");
                    std::wstring msg = (itMsg != s_locale.end()) ? itMsg->second : L"Please select a registry key or value to edit.";
                    MessageBoxW(hwnd, msg.c_str(), L"Edit", MB_OK | MB_ICONINFORMATION);
                }
                return 0;
            }
            
            // Check if ListView has focus and has a selected item
            if (hFocused == s_hRegListView) {
                int iSelected = ListView_GetNextItem(s_hRegListView, -1, LVNI_SELECTED);
                if (iSelected != -1) {
                    // Call the Edit Value handler
                    SendMessageW(hwnd, WM_COMMAND, IDM_REG_EDIT_VALUE, 0);
                } else {
                    auto itMsg = s_locale.find(L"reg_select_to_edit");
                    std::wstring msg = (itMsg != s_locale.end()) ? itMsg->second : L"Please select a registry key or value to edit.";
                    MessageBoxW(hwnd, msg.c_str(), L"Edit", MB_OK | MB_ICONINFORMATION);
                }
                return 0;
            }
            
            // Neither control has focus - check which has the selected item
            HTREEITEM hTreeSel = TreeView_GetSelection(s_hRegTreeView);
            int iListSel = ListView_GetNextItem(s_hRegListView, -1, LVNI_SELECTED);
            
            // Prefer TreeView selection (editing keys) over ListView selection (values)
            if (hTreeSel) {
                // TreeView has a selection - edit (rename) the key
                SendMessageW(hwnd, WM_COMMAND, IDM_REG_EDIT_KEY, 0);
            } else if (iListSel != -1) {
                // ListView has a selected item - edit the value
                SendMessageW(hwnd, WM_COMMAND, IDM_REG_EDIT_VALUE, 0);
            } else {
                // Nothing selected
                auto itMsg = s_locale.find(L"reg_select_to_edit");
                std::wstring msg = (itMsg != s_locale.end()) ? itMsg->second : L"Please select a registry key or value to edit.";
                MessageBoxW(hwnd, msg.c_str(), L"Edit", MB_OK | MB_ICONINFORMATION);
            }
            
            return 0;
        }
        
        case IDC_REG_BACKUP: {
            // Get spinner text from locale
            auto itSpinner = s_locale.find(L"reg_backup_spinner");
            std::wstring spinnerText = (itSpinner != s_locale.end()) ? itSpinner->second : 
                L"Creating system restore point...\r\nThis may take a few moments.\r\nPlease wait.";
            
            // Create spinner dialog
            auto itSpinnerTitle = s_locale.find(L"spinner_title");
            std::wstring spinnerTitle = (itSpinnerTitle != s_locale.end()) ? itSpinnerTitle->second : L"Please Wait";
            SpinnerDialog* spinner = new SpinnerDialog(hwnd);
            spinner->Show(spinnerText, spinnerTitle);
            
            // Create restore point using PowerShell
            // Use ShellExecuteW with "runas" to request admin if needed
            SHELLEXECUTEINFOW sei = {};
            sei.cbSize = sizeof(sei);
            sei.fMask = SEE_MASK_NOCLOSEPROCESS;
            sei.hwnd = hwnd;
            sei.lpVerb = L"runas"; // Request elevation if needed
            sei.lpFile = L"powershell.exe";
            sei.lpParameters = L"-NoProfile -Command \"Checkpoint-Computer -Description 'SetupCraft Registry Backup' -RestorePointType MODIFY_SETTINGS\"";
            sei.nShow = SW_HIDE;
            
            if (ShellExecuteExW(&sei)) {
                if (sei.hProcess) {
                    // Allow the UAC consent process to take the foreground
                    AllowSetForegroundWindow(ASFW_ANY);

                    // Wait briefly for UAC prompt to appear
                    Sleep(500);

                    DWORD startTime = GetTickCount();
                    DWORD timeout = 30000;
                    DWORD waitResult = WAIT_TIMEOUT;
                    
                    while ((GetTickCount() - startTime) < timeout) {
                        waitResult = WaitForSingleObject(sei.hProcess, 100);
                        if (waitResult == WAIT_OBJECT_0) {
                            break; // Process completed
                        }
                        
                        // Process messages to keep UI responsive and spinner animated
                        MSG msg;
                        while (PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE)) {
                            TranslateMessage(&msg);
                            DispatchMessageW(&msg);
                        }
                    }
                    
                    DWORD exitCode = 0;
                    GetExitCodeProcess(sei.hProcess, &exitCode);
                    CloseHandle(sei.hProcess);
                    
                    // Hide spinner
                    spinner->Hide();
                    delete spinner;
                    
                    if (waitResult == WAIT_OBJECT_0 && exitCode == 0) {
                        auto itSuccess = s_locale.find(L"reg_backup_success");
                        std::wstring successMsg = (itSuccess != s_locale.end()) ? itSuccess->second : L"System restore point created successfully!";
                        MessageBoxW(hwnd, successMsg.c_str(), L"Backup", MB_OK | MB_ICONINFORMATION);
                    } else {
                        auto itFailed = s_locale.find(L"reg_backup_failed");
                        std::wstring failedMsg = (itFailed != s_locale.end()) ? itFailed->second : L"Failed to create system restore point. Error: ";
                        failedMsg += std::to_wstring(exitCode);
                        MessageBoxW(hwnd, failedMsg.c_str(), L"Backup", MB_OK | MB_ICONWARNING);
                    }
                } else {
                    // No process handle, hide spinner
                    spinner->Hide();
                    delete spinner;
                }
            } else {
                // Failed to execute - might be cancelled by user or no admin rights
                spinner->Hide();
                delete spinner;
                
                DWORD err = GetLastError();
                if (err == ERROR_CANCELLED) {
                    // User cancelled UAC prompt
                    MessageBoxW(hwnd, L"Backup cancelled.", L"Backup", MB_OK | MB_ICONINFORMATION);
                } else {
                    auto itAdmin = s_locale.find(L"reg_backup_admin_required");
                    std::wstring adminMsg = (itAdmin != s_locale.end()) ? itAdmin->second : 
                        L"Administrator rights required to create a system restore point. Please run as administrator.";
                    MessageBoxW(hwnd, adminMsg.c_str(), L"Backup", MB_OK | MB_ICONWARNING);
                }
            }
            
            return 0;
        }
        
        // Registry context menu commands
        case IDM_REG_ADD_KEY: {
            // Route to Add Key button handler
            SendMessageW(hwnd, WM_COMMAND, IDC_REG_ADD_KEY, 0);
            return 0;
        }
        
        case IDM_REG_ADD_VALUE: {
            // Route to Add Value button handler
            SendMessageW(hwnd, WM_COMMAND, IDC_REG_ADD_VALUE, 0);
            return 0;
        }
        
        case IDM_REG_EDIT_KEY: {
            // Rename selected key in TreeView
            HTREEITEM hSelected = TreeView_GetSelection(s_hRegTreeView);
            if (!hSelected) {
                return 0;
            }
            
            // Get current key name
            TVITEMW tvi = {};
            tvi.mask = TVIF_TEXT;
            tvi.hItem = hSelected;
            wchar_t szText[256];
            tvi.pszText = szText;
            tvi.cchTextMax = 256;
            TreeView_GetItem(s_hRegTreeView, &tvi);
            
            // Get locale strings
            auto itTitle = s_locale.find(L"reg_edit_key_title");
            std::wstring title = (itTitle != s_locale.end()) ? itTitle->second : L"Edit Registry Key";
            
            auto itName = s_locale.find(L"reg_add_key_name");
            std::wstring nameLabel = (itName != s_locale.end()) ? itName->second : L"Key Name:";
            
            auto itOk = s_locale.find(L"reg_add_key_ok");
            std::wstring okText = (itOk != s_locale.end()) ? itOk->second : L"OK";
            
            auto itCancel = s_locale.find(L"reg_add_key_cancel");
            std::wstring cancelText = (itCancel != s_locale.end()) ? itCancel->second : L"Cancel";
            
            // Create dialog structure
            AddKeyDialogData data;
            data.nameText = nameLabel;
            data.okText = okText;
            data.cancelText = cancelText;
            data.defaultKeyName = szText;
            data.okClicked = false;
            
            // Create dialog window
            HINSTANCE hInst = GetModuleHandleW(NULL);

            // Ensure dialog class is registered (AddKeyDialog)
            WNDCLASSEXW wc = {};
            wc.cbSize = sizeof(WNDCLASSEXW);
            if (!GetClassInfoExW(GetModuleHandleW(NULL), L"AddKeyDialog", &wc)) {
                wc.lpfnWndProc = AddKeyDialogProc;
                wc.hInstance = GetModuleHandleW(NULL);
                wc.lpszClassName = L"AddKeyDialog";
                wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
                wc.hCursor = LoadCursor(NULL, IDC_ARROW);
                RegisterClassExW(&wc);
            }

            RECT rcParent; GetWindowRect(hwnd, &rcParent);
            int akClientW2 = S(AK_PAD_H)+S(AK_LBL_W)+S(AK_FLD_GAP)+S(AK_EDIT_W)+S(AK_PAD_H);
            int akClientH2 = S(AK_PAD_T)+S(AK_ROW_H)+S(AK_GAP_EB)+S(AK_BTN_H)+S(AK_PAD_B);
            RECT wrcAK2 = {0,0,akClientW2,akClientH2};
            AdjustWindowRectEx(&wrcAK2, WS_POPUP|WS_CAPTION|WS_SYSMENU, FALSE,
                               WS_EX_DLGMODALFRAME|WS_EX_TOPMOST);
            int dlgWidth2  = wrcAK2.right-wrcAK2.left;
            int dlgHeight2 = wrcAK2.bottom-wrcAK2.top;
            int dlgX2 = rcParent.left + (rcParent.right-rcParent.left-dlgWidth2)/2;
            int dlgY2 = rcParent.top  + (rcParent.bottom-rcParent.top-dlgHeight2)/2;

            HWND hDialog = CreateWindowExW(WS_EX_DLGMODALFRAME | WS_EX_TOPMOST,
                L"AddKeyDialog", title.c_str(),
                WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_VISIBLE,
                dlgX2, dlgY2, dlgWidth2, dlgHeight2, hwnd, NULL, hInst, &data);
            
            if (hDialog) {
                EnableWindow(hwnd, FALSE);
                
                // Message loop for modal dialog
                MSG msg;
                while (GetMessageW(&msg, NULL, 0, 0)) {
                    if (!IsWindow(hDialog)) break;
                    TranslateMessage(&msg);
                    DispatchMessageW(&msg);
                }
                
                EnableWindow(hwnd, TRUE);
                SetForegroundWindow(hwnd);
                
                // If OK was clicked and name changed, update the tree item
                if (data.okClicked && !data.keyName.empty() && data.keyName != szText) {
                    TVITEMW tviUpdate = {};
                    tviUpdate.mask = TVIF_TEXT;
                    tviUpdate.hItem = hSelected;
                    tviUpdate.pszText = (LPWSTR)data.keyName.c_str();
                    TreeView_SetItem(s_hRegTreeView, &tviUpdate);
                }
            }
            
            return 0;
        }
        
        case IDM_REG_EDIT_VALUE: {
            // Edit selected value in ListView (fall back to last right-clicked item)
            int iSelected = ListView_GetNextItem(s_hRegListView, -1, LVNI_SELECTED);
            if (iSelected == -1 && s_rightClickedRegIndex != -1) {
                iSelected = s_rightClickedRegIndex;
            }
            if (iSelected == -1) {
                return 0;
            }
            
            // Get the selected tree item
            HTREEITEM hSelected = TreeView_GetSelection(s_hRegTreeView);
            if (!hSelected) {
                return 0;
            }
            
            // Get current value data
            auto it = s_registryValues.find(hSelected);
            if (it == s_registryValues.end() || iSelected >= (int)it->second.size()) {
                return 0;
            }
            
            RegistryEntry& entry = it->second[iSelected];
            
            // Get locale strings (use edit-specific title)
            auto itTitle = s_locale.find(L"reg_edit_value_title");
            std::wstring title = (itTitle != s_locale.end()) ? itTitle->second : L"Edit Registry Value";
            
            auto itName = s_locale.find(L"reg_add_value_name");
            std::wstring nameText = (itName != s_locale.end()) ? itName->second : L"Value Name:";
            
            auto itType = s_locale.find(L"reg_add_value_type");
            std::wstring typeText = (itType != s_locale.end()) ? itType->second : L"Type:";
            
            auto itData = s_locale.find(L"reg_add_value_data");
            std::wstring dataText = (itData != s_locale.end()) ? itData->second : L"Data:";
            
            auto itOk = s_locale.find(L"ok");
            std::wstring okText = (itOk != s_locale.end()) ? itOk->second : L"OK";
            
            auto itCancel = s_locale.find(L"cancel");
            std::wstring cancelText = (itCancel != s_locale.end()) ? itCancel->second : L"Cancel";
            
            // Create dialog data with existing values
            AddValueDialogData dialogData = {
                nameText, typeText, dataText, okText, cancelText,
                entry.name, entry.type, entry.data, false
            };
            
            // Register dialog class if not already registered (AddValueDialog)
            HINSTANCE hInst = (HINSTANCE)GetWindowLongPtr(hwnd, GWLP_HINSTANCE);
            WNDCLASSEXW wc = {};
            wc.cbSize = sizeof(WNDCLASSEXW);
            if (!GetClassInfoExW(GetModuleHandleW(NULL), L"AddValueDialog", &wc)) {
                wc.lpfnWndProc = AddValueDialogProc;
                wc.hInstance = GetModuleHandleW(NULL);
                wc.lpszClassName = L"AddValueDialog";
                wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
                wc.hCursor = LoadCursor(NULL, IDC_ARROW);
                RegisterClassExW(&wc);
            }

            // Create dialog window with DPI-aware sizing
            HWND hDialog = NULL;
            {
                int clientW = S(AV_PAD_H)+S(AV_LBL_W)+S(AV_FLD_GAP)+S(AV_EDIT_W)+S(AV_PAD_H);
                int clientH = S(AV_PAD_T)
                            + S(AV_ROW_H)+S(AV_GAP_R1)
                            + S(AV_ROW_H)+S(AV_GAP_R2)
                            + S(AV_ROW_H)+S(AV_GAP_RB)
                            + S(AV_BTN_H)+S(AV_PAD_B);
                RECT wrc = {0,0,clientW,clientH};
                AdjustWindowRectEx(&wrc, WS_POPUP|WS_CAPTION|WS_SYSMENU, FALSE,
                                   WS_EX_DLGMODALFRAME|WS_EX_TOPMOST);
                int dlgW = wrc.right-wrc.left, dlgH = wrc.bottom-wrc.top;
                RECT rcP; GetWindowRect(hwnd, &rcP);
                int x = rcP.left + (rcP.right-rcP.left-dlgW)/2;
                int y = rcP.top  + (rcP.bottom-rcP.top-dlgH)/2;
                hDialog = CreateWindowExW(WS_EX_DLGMODALFRAME | WS_EX_TOPMOST, L"AddValueDialog",
                    title.c_str(), WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_VISIBLE,
                    x, y, dlgW, dlgH, hwnd, NULL, hInst, &dialogData);
            }
            if (hDialog) {
                // Modal message loop
                EnableWindow(hwnd, FALSE);
                MSG msg;
                while (IsWindow(hDialog) && GetMessageW(&msg, NULL, 0, 0)) {
                    if (!IsDialogMessageW(hDialog, &msg)) {
                        TranslateMessage(&msg);
                        DispatchMessageW(&msg);
                    }
                }
                EnableWindow(hwnd, TRUE);
                SetForegroundWindow(hwnd);
                
                // Update value if OK was clicked
                if (dialogData.okClicked) {
                    entry.name = dialogData.valueName;
                    entry.type = dialogData.valueType;
                    entry.data = dialogData.valueData;
                    
                    // Refresh ListView to show updated value
                    ListView_SetItemText(s_hRegListView, iSelected, 0, (LPWSTR)entry.name.c_str());
                    ListView_SetItemText(s_hRegListView, iSelected, 1, (LPWSTR)entry.type.c_str());
                    ListView_SetItemText(s_hRegListView, iSelected, 2, (LPWSTR)entry.data.c_str());
                    
                    MarkAsModified();
                }
            }
            
            return 0;
        }
        
        case IDM_REG_DELETE_VALUE: {
            // Delete selected value from ListView
            int iSelected = ListView_GetNextItem(s_hRegListView, -1, LVNI_SELECTED);
            if (iSelected == -1) {
                return 0;
            }
            
            // Get the selected tree item
            HTREEITEM hSelected = TreeView_GetSelection(s_hRegTreeView);
            if (!hSelected) {
                return 0;
            }
            
            // Confirm deletion
            auto itConfirm = s_locale.find(L"confirm_delete_value");
            std::wstring confirmMsg = (itConfirm != s_locale.end()) ? itConfirm->second : L"Delete this registry value?";
            
            auto itTitle = s_locale.find(L"confirm_delete_title");
            std::wstring titleMsg = (itTitle != s_locale.end()) ? itTitle->second : L"Confirm Delete";
            
            int result = MessageBoxW(hwnd, confirmMsg.c_str(), titleMsg.c_str(), MB_YESNO | MB_ICONQUESTION);
            if (result != IDYES) {
                return 0;
            }
            
            // Remove from vector
            auto it = s_registryValues.find(hSelected);
            if (it != s_registryValues.end() && iSelected < (int)it->second.size()) {
                it->second.erase(it->second.begin() + iSelected);
                
                // Refresh ListView
                ListView_DeleteAllItems(s_hRegListView);
                for (const auto& e : it->second) {
                    LVITEMW lvi = {};
                    lvi.mask = LVIF_TEXT;
                    lvi.iItem = ListView_GetItemCount(s_hRegListView);
                    lvi.pszText = (LPWSTR)e.name.c_str();
                    int idx = ListView_InsertItem(s_hRegListView, &lvi);
                    ListView_SetItemText(s_hRegListView, idx, 1, (LPWSTR)e.type.c_str());
                    ListView_SetItemText(s_hRegListView, idx, 2, (LPWSTR)e.data.c_str());
                }
                
                MarkAsModified();
                // Clear right-click tracker now that we've used it
                s_rightClickedRegIndex = -1;
            }
            
            return 0;
        }
        
        case IDM_REG_DELETE_KEY: {
            // Delete selected key from TreeView
            HTREEITEM hSelected = TreeView_GetSelection(s_hRegTreeView);
            if (!hSelected) {
                return 0;
            }
            
            // Don't allow deleting root hive items
            HTREEITEM hParent = TreeView_GetParent(s_hRegTreeView, hSelected);
            if (!hParent) {
                MessageBoxW(hwnd, L"Cannot delete root registry hives", L"Delete", MB_OK | MB_ICONWARNING);
                return 0;
            }
            
            // Confirm deletion
            auto itConfirm = s_locale.find(L"confirm_delete_key");
            std::wstring confirmMsg = (itConfirm != s_locale.end()) ? itConfirm->second : L"Delete this registry key and all its subkeys?";
            
            auto itTitle = s_locale.find(L"confirm_delete_title");
            std::wstring titleMsg = (itTitle != s_locale.end()) ? itTitle->second : L"Confirm Delete";
            
            int result = MessageBoxW(hwnd, confirmMsg.c_str(), titleMsg.c_str(), MB_YESNO | MB_ICONQUESTION);
            if (result != IDYES) {
                return 0;
            }
            
            // Remove registry values for this item and all descendants
            std::vector<HTREEITEM> itemsToRemove;
            itemsToRemove.push_back(hSelected);
            
            // Recursively collect all child items
            for (size_t i = 0; i < itemsToRemove.size(); i++) {
                HTREEITEM hChild = TreeView_GetChild(s_hRegTreeView, itemsToRemove[i]);
                while (hChild) {
                    itemsToRemove.push_back(hChild);
                    hChild = TreeView_GetNextSibling(s_hRegTreeView, hChild);
                }
            }
            
            // Remove from map
            for (HTREEITEM hItem : itemsToRemove) {
                s_registryValues.erase(hItem);
            }
            
            // Delete from TreeView
            TreeView_DeleteItem(s_hRegTreeView, hSelected);
            
            // Clear ListView
            ListView_DeleteAllItems(s_hRegListView);
            
            MarkAsModified();
            return 0;
        }
            
        // Components page handlers
        case IDC_COMP_ENABLE: {
            if (HIWORD(wParam) != BN_CLICKED) return 0;
            // Allow toggle even for unsaved (id==0) projects — components can be
            // configured in memory and saved later.
            HWND hChk = GetDlgItem(hwnd, IDC_COMP_ENABLE);
            int checked = (hChk && SendMessageW(hChk, BM_GETCHECK, 0, 0) == BST_CHECKED) ? 1 : 0;
            s_currentProject.use_components = checked;
            // NOTE: DB::UpdateProject intentionally NOT called here — Save does it.
            if (checked) {
                // Auto-populate from VFS snapshots only if there are no components yet
                if (s_components.empty()) {
                    CollectAllFiles(s_treeSnapshot_ProgramFiles,  s_currentProject.id, s_components, L"Program Files");
                    CollectAllFiles(s_treeSnapshot_ProgramData,   s_currentProject.id, s_components, L"ProgramData");
                    CollectAllFiles(s_treeSnapshot_AppData,       s_currentProject.id, s_components, L"AppData (Roaming)");
                    CollectAllFiles(s_treeSnapshot_AskAtInstall,  s_currentProject.id, s_components, L"AskAtInstall");
                    // IDs will be assigned by the DB on Save; use 0 for new rows.
                }
            } else {
                s_components.clear();
            }
            SwitchPage(hwnd, 9);
            MarkAsModified();
            return 0;
        }

        case IDC_COMP_ADD: {
            if (s_currentProject.id <= 0) return 0;

            // Build picker data with locale strings
            auto LS = [&](const wchar_t* k, const wchar_t* fb) -> std::wstring {
                auto it = s_locale.find(k);
                return (it != s_locale.end()) ? it->second : fb;
            };
            VFSPickerData pickData;
            pickData.titleText  = LS(L"comp_add_picker_title",  L"Select files or folders to add as components");
            pickData.okText     = LS(L"ok",                     L"OK");
            pickData.cancelText = LS(L"cancel",                 L"Cancel");
            pickData.colFile    = LS(L"comp_picker_col_file",   L"Name");
            pickData.colPath    = LS(L"comp_picker_col_path",   L"Source Path");
            pickData.noSelMsg   = LS(L"comp_picker_no_sel",
                L"Select one or more files in the right pane, or select a "
                L"real-path folder in the left pane, then click OK.");

            // Register and show the picker dialog
            WNDCLASSEXW wc = {};
            wc.cbSize        = sizeof(wc);
            wc.lpfnWndProc   = VFSPickerDlgProc;
            wc.hInstance     = GetModuleHandleW(NULL);
            wc.lpszClassName = L"VFSPickerDialog";
            wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
            wc.hCursor       = LoadCursor(NULL, IDC_ARROW);
            if (!GetClassInfoExW(GetModuleHandleW(NULL), L"VFSPickerDialog", &wc))
                RegisterClassExW(&wc);

            RECT rcMain; GetWindowRect(hwnd, &rcMain);
            int dlgW = 720, dlgH = 500;
            HWND hPicker = CreateWindowExW(
                WS_EX_TOPMOST | WS_EX_TOOLWINDOW,
                L"VFSPickerDialog", pickData.titleText.c_str(),
                WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_VISIBLE,
                rcMain.left + ((rcMain.right  - rcMain.left) - dlgW) / 2,
                rcMain.top  + ((rcMain.bottom - rcMain.top)  - dlgH) / 2,
                dlgW, dlgH, hwnd, NULL, GetModuleHandleW(NULL), &pickData);

            MSG pickMsg;
            while (GetMessageW(&pickMsg, NULL, 0, 0) > 0) {
                if (!IsWindow(hPicker)) break;
                TranslateMessage(&pickMsg);
                DispatchMessageW(&pickMsg);
            }

            if (pickData.okClicked && !pickData.results.empty()) {
                for (const auto& r : pickData.results) {
                    ComponentRow comp;
                    comp.id           = 0;   // assigned by DB on Save
                    comp.project_id   = s_currentProject.id;
                    comp.display_name = r.displayName;
                    comp.description  = L"";
                    comp.is_required  = 0;
                    comp.source_type  = r.sourceType;
                    comp.source_path  = r.sourcePath;
                    comp.dest_path    = L"";
                    s_components.push_back(comp);  // memory only — written to DB on Save
                }
                SwitchPage(hwnd, 9);
                MarkAsModified();
            }
            return 0;
        }

        case IDC_COMP_EDIT: {
            if (!s_hCompListView || !IsWindow(s_hCompListView)) return 0;
            int selRow = ListView_GetNextItem(s_hCompListView, -1, LVNI_SELECTED);
            // Get the component index stored as lParam on the list item
            int selIdx = -1;
            if (selRow >= 0) {
                LVITEMW lvi = {}; lvi.mask = LVIF_PARAM; lvi.iItem = selRow;
                ListView_GetItem(s_hCompListView, &lvi);
                selIdx = (int)lvi.lParam;
            }
            if (selIdx < 0 || selIdx >= (int)s_components.size()) {
                // No list selection — try the selected tree folder
                if (s_hCompTreeView && IsWindow(s_hCompTreeView)) {
                    HTREEITEM hTreeSel = TreeView_GetSelection(s_hCompTreeView);
                    if (hTreeSel) {
                        TVITEMW tviChk = {}; tviChk.hItem = hTreeSel; tviChk.mask = TVIF_PARAM;
                        TreeView_GetItem(s_hCompTreeView, &tviChk);
                        if (tviChk.lParam != 0) {
                            SendMessageW(hwnd, WM_COMMAND, MAKEWPARAM(IDM_COMP_TREE_CTX_EDIT, 0), 0);
                            return 0;
                        }
                    }
                }
                auto itNoSel = s_locale.find(L"comp_no_selection");
                std::wstring noSelMsg = (itNoSel != s_locale.end()) ? itNoSel->second : L"Please select a component first.";
                MessageBoxW(hwnd, noSelMsg.c_str(), L"", MB_OK | MB_ICONINFORMATION);
                return 0;
            }
            const ComponentRow &cmp = s_components[selIdx];
            // Use in-memory deps if available (already edited this session), else fall back to DB
            std::vector<int> currentDeps = cmp.dependencies.empty() && cmp.id > 0
                ? DB::GetDependenciesForComponent(cmp.id)
                : cmp.dependencies;
            std::vector<ComponentRow> otherComps;
            for (const auto& oc : s_components)
                if (oc.id != cmp.id) otherComps.push_back(oc);

            auto lstrE = [&](const wchar_t *key, const wchar_t *fb) -> std::wstring {
                auto itE = s_locale.find(key); return (itE != s_locale.end()) ? itE->second : fb;
            };
            CompDlgData dlgData2;
            dlgData2.initName          = cmp.display_name;
            dlgData2.initDesc          = cmp.description;
            dlgData2.initRequired      = cmp.is_required;
            dlgData2.initSourceType    = cmp.source_type;
            dlgData2.initSourcePath    = cmp.source_path;
            dlgData2.otherComponents   = otherComps;
            dlgData2.initDependencyIds = currentDeps;
            dlgData2.initNotesRtf      = cmp.notes_rtf;
            dlgData2.titleText     = lstrE(L"comp_edit_title",    L"Edit Component");
            dlgData2.nameLabel     = lstrE(L"comp_name_label",    L"Display Name:");
            dlgData2.descLabel     = lstrE(L"comp_desc_label",    L"Description:");
            dlgData2.requiredLabel = lstrE(L"comp_required_label",L"Required (always installed)");
            dlgData2.sourceLabel   = lstrE(L"comp_source_label",  L"Source Path:");
            dlgData2.browseText    = lstrE(L"comp_browse",        L"Browse...");
            dlgData2.depsLabel     = lstrE(L"comp_deps_label",    L"Requires:");
            dlgData2.okText        = lstrE(L"ok",                 L"OK");
            dlgData2.cancelText    = lstrE(L"cancel",             L"Cancel");

            WNDCLASSEXW wcComp2 = {};
            wcComp2.cbSize = sizeof(wcComp2);
            if (!GetClassInfoExW(GetModuleHandleW(NULL), L"CompEditDialog", &wcComp2)) {
                wcComp2.lpfnWndProc = CompEditDlgProc;
                wcComp2.hInstance = GetModuleHandleW(NULL);
                wcComp2.lpszClassName = L"CompEditDialog";
                wcComp2.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
                wcComp2.hCursor = LoadCursor(NULL, IDC_ARROW);
                RegisterClassExW(&wcComp2);
            }
            RECT rcMain2; GetWindowRect(hwnd, &rcMain2);
            int dlgW2 = 620;
            int dlgH2 = otherComps.empty() ? 420 : 540;
            HWND hDlg2 = CreateWindowExW(WS_EX_TOPMOST | WS_EX_TOOLWINDOW,
                L"CompEditDialog", dlgData2.titleText.c_str(),
                WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_VISIBLE,
                rcMain2.left + ((rcMain2.right - rcMain2.left) - dlgW2) / 2,
                rcMain2.top  + ((rcMain2.bottom - rcMain2.top) - dlgH2) / 2,
                dlgW2, dlgH2, hwnd, NULL, GetModuleHandleW(NULL), &dlgData2);
            MSG msgComp2;
            while (GetMessageW(&msgComp2, NULL, 0, 0) > 0) {
                if (!IsWindow(hDlg2)) break;
                TranslateMessage(&msgComp2); DispatchMessageW(&msgComp2);
            }
            if (dlgData2.okClicked) {
                // Update in memory only — written to DB on Save.
                s_components[selIdx].display_name = dlgData2.outName;
                s_components[selIdx].description  = dlgData2.outDesc;
                s_components[selIdx].is_required  = dlgData2.outRequired;
                s_components[selIdx].source_path  = dlgData2.outSourcePath;
                s_components[selIdx].dependencies = dlgData2.outDependencyIds;
                s_components[selIdx].notes_rtf    = dlgData2.outNotesRtf;
                SwitchPage(hwnd, 9);
                MarkAsModified();
            }
            return 0;
        }

        case IDC_COMP_REMOVE: {
            if (!s_hCompListView || !IsWindow(s_hCompListView)) return 0;
            int selRow = ListView_GetNextItem(s_hCompListView, -1, LVNI_SELECTED);
            int selIdx = -1;
            if (selRow >= 0) {
                LVITEMW lvi = {}; lvi.mask = LVIF_PARAM; lvi.iItem = selRow;
                ListView_GetItem(s_hCompListView, &lvi);
                selIdx = (int)lvi.lParam;
            }
            if (selIdx < 0 || selIdx >= (int)s_components.size()) {
                auto itNoSel = s_locale.find(L"comp_no_selection");
                std::wstring noSelMsg = (itNoSel != s_locale.end()) ? itNoSel->second : L"Please select a component first.";
                MessageBoxW(hwnd, noSelMsg.c_str(), L"", MB_OK | MB_ICONINFORMATION);
                return 0;
            }
            auto itCnf = s_locale.find(L"comp_confirm_remove");
            std::wstring cnfMsg = (itCnf != s_locale.end()) ? itCnf->second : L"Remove selected component?";
            if (MessageBoxW(hwnd, cnfMsg.c_str(), L"", MB_YESNO | MB_ICONQUESTION) == IDYES) {
                s_components.erase(s_components.begin() + selIdx);  // memory only — written to DB on Save
                SwitchPage(hwnd, 9);
                MarkAsModified();
            }
            return 0;
        }

        case IDM_COMP_TREE_CTX_EDIT: {
            if (!s_hCompTreeView || !IsWindow(s_hCompTreeView)) return 0;
            HTREEITEM hSel = TreeView_GetSelection(s_hCompTreeView);
            if (!hSel) return 0;
            TVITEMW tviSel = {}; tviSel.hItem = hSel; tviSel.mask = TVIF_PARAM;
            TreeView_GetItem(s_hCompTreeView, &tviSel);
            const TreeNodeSnapshot* snap = (const TreeNodeSnapshot*)tviSel.lParam;
            if (!snap) return 0;  // section root

            auto LS = [&](const wchar_t* k, const wchar_t* fb) -> std::wstring {
                auto it = s_locale.find(k); return (it != s_locale.end()) ? it->second : fb;
            };

            std::vector<std::wstring> paths;
            CollectSnapshotPaths(*snap, paths);
            if (paths.empty() && snap->fullPath.empty()) return 0;

            std::wstring section = GetCompTreeItemSection(s_hCompTreeView, hSel);

            int reqState = -1;
            for (const auto& path : paths) {
                for (const auto& cmp : s_components) {
                    if (cmp.source_path != path) continue;
                    if (!cmp.dest_path.empty() && cmp.dest_path != section) continue;
                    if (reqState == -1) reqState = cmp.is_required;
                    else if (reqState != cmp.is_required) { reqState = -1; goto mixed_done; }
                    break;
                }
            }
            mixed_done:
            if (reqState == -1 && !snap->fullPath.empty()) {
                for (const auto& cmp : s_components) {
                    if (cmp.source_path != snap->fullPath) continue;
                    if (!cmp.dest_path.empty() && cmp.dest_path != section) continue;
                    reqState = cmp.is_required;
                    break;
                }
            }

            int preselState = -1;
            for (const auto& path : paths) {
                for (const auto& cmp : s_components) {
                    if (cmp.source_path != path) continue;
                    if (!cmp.dest_path.empty() && cmp.dest_path != section) continue;
                    if (preselState == -1) preselState = cmp.is_preselected;
                    else if (preselState != cmp.is_preselected) { preselState = -1; goto presel_mixed_done; }
                    break;
                }
            }
            presel_mixed_done:
            if (preselState == -1 && !snap->fullPath.empty()) {
                for (const auto& cmp : s_components) {
                    if (cmp.source_path != snap->fullPath) continue;
                    if (cmp.source_type  != L"folder")     continue;
                    if (!cmp.dest_path.empty() && cmp.dest_path != section) continue;
                    preselState = cmp.is_preselected;
                    break;
                }
            }

            CompFolderDlgData fd;
            fd.folderName       = snap->text;
            fd.initRequired     = (reqState    == 1) ? 1 : 0;
            fd.initPreselected  = (preselState == 1) ? 1 : 0;
            fd.titleText        = LS(L"comp_folder_edit_title",  L"Edit Folder");
            fd.requiredLabel    = LS(L"comp_required_label",     L"Required (always installed)");
            fd.preselectedLabel = LS(L"comp_preselected_label",  L"Pre-selected (ticked by default at install)");
            fd.cascadeHint      = LS(L"comp_folder_cascade_hint",
                L"Applies to all files and subfolders. You can still override individual subfolders afterwards.");
            fd.depsLabel        = LS(L"comp_deps_label",         L"Dependencies:");
            fd.chooseDepsText   = LS(L"comp_choose_deps",        L"Choose...");
            fd.okText           = LS(L"ok",     L"OK");
            fd.cancelText       = LS(L"cancel", L"Cancel");
            fd.excludeNode      = snap;
            fd.sectionName      = section;
            fd.projectId        = s_currentProject.id;

            for (const auto& oc : s_components) {
                if (!snap->fullPath.empty()) {
                    bool sameSection = oc.dest_path.empty() || oc.dest_path == section;
                    if (sameSection && oc.source_path == snap->fullPath) continue;
                    if (sameSection &&
                        oc.source_path.size() > snap->fullPath.size() &&
                        _wcsnicmp(oc.source_path.c_str(), snap->fullPath.c_str(),
                                  snap->fullPath.size()) == 0 &&
                        (oc.source_path[snap->fullPath.size()] == L'\\' ||
                         oc.source_path[snap->fullPath.size()] == L'/')) continue;
                }
                fd.otherComponents.push_back(oc);
            }
            if (!snap->fullPath.empty()) {
                for (const auto& cmp : s_components) {
                    if (cmp.source_path != snap->fullPath) continue;
                    if (cmp.source_type  != L"folder")     continue;
                    if (!cmp.dest_path.empty() && cmp.dest_path != section) continue;
                    // Always use in-memory deps — they are loaded from DB at project
                    // open and kept in sync. Never fall back to DB here, as that
                    // would discard in-memory edits the user just made.
                    fd.initDependencyIds = cmp.dependencies;
                    fd.initNotesRtf = cmp.notes_rtf;
                    break;
                }
            }

            WNDCLASSEXW wcFd = {};
            wcFd.cbSize = sizeof(wcFd);
            if (!GetClassInfoExW(GetModuleHandleW(NULL), L"CompFolderEditDialog", &wcFd)) {
                wcFd.lpfnWndProc   = CompFolderEditDlgProc;
                wcFd.hInstance     = GetModuleHandleW(NULL);
                wcFd.lpszClassName = L"CompFolderEditDialog";
                wcFd.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
                wcFd.hCursor       = LoadCursor(NULL, IDC_ARROW);
                RegisterClassExW(&wcFd);
            }
            {
                NONCLIENTMETRICSW ncmH = {}; ncmH.cbSize = sizeof(ncmH);
                SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(ncmH), &ncmH, 0);
                if (ncmH.lfMessageFont.lfHeight < 0)
                    ncmH.lfMessageFont.lfHeight = (LONG)(ncmH.lfMessageFont.lfHeight * 1.2f);
                ncmH.lfMessageFont.lfQuality = CLEARTYPE_QUALITY;
                HFONT hMF = CreateFontIndirectW(&ncmH.lfMessageFont);
                HDC hdc = GetDC(NULL);
                HFONT hOld = (HFONT)SelectObject(hdc, hMF);
                int hintMaxW = S(CFE_CONT_W);
                RECT rcHint = { 0, 0, hintMaxW, 0 };
                DrawTextW(hdc, fd.cascadeHint.c_str(), -1, &rcHint,
                          DT_CALCRECT | DT_WORDBREAK | DT_LEFT | DT_NOPREFIX);
                SelectObject(hdc, hOld);
                ReleaseDC(NULL, hdc);
                DeleteObject(hMF);
                fd.hintH = rcHint.bottom > 0 ? rcHint.bottom : S(42);
            }
            int clientW3 = S(CFE_PAD_H)+S(CFE_CONT_W)+S(CFE_PAD_H);
            int clientH3 = S(CFE_PAD_T)
                         + S(CFE_NAME_H)+S(CFE_GAP_NR)
                         + S(CFE_CHECK_H)+S(CFE_GAP_RPS)
                         + S(CFE_CHECK_H)+S(CFE_GAP_RC)
                         + fd.hintH+S(CFE_GAP_CD)
                         + S(CFE_DEPS_ROW_H)+S(CFE_GAP_LD)
                         + S(CFE_DEPLIST_H)+S(CFE_GAP_LB)
                         + S(CFE_DEP_BTNS_ROW_H)+S(CFE_GAP_DN)
                         + S(CFE_NOTES_BTN_H)+S(CFE_GAP_NB2)
                         + S(CFE_BTN_H)+S(CFE_PAD_B);
            RECT wrc3 = {0,0,clientW3,clientH3};
            AdjustWindowRectEx(&wrc3, WS_POPUP|WS_CAPTION|WS_SYSMENU, FALSE,
                               WS_EX_TOPMOST|WS_EX_TOOLWINDOW);
            int dlgW3 = wrc3.right-wrc3.left, dlgH3 = wrc3.bottom-wrc3.top;
            RECT rcMain3; GetWindowRect(hwnd, &rcMain3);
            int xFd = rcMain3.left + (rcMain3.right-rcMain3.left-dlgW3)/2;
            int yFd = rcMain3.top  + (rcMain3.bottom-rcMain3.top-dlgH3)/2;
            HWND hFd = CreateWindowExW(WS_EX_TOPMOST | WS_EX_TOOLWINDOW,
                L"CompFolderEditDialog", fd.titleText.c_str(),
                WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_VISIBLE,
                xFd, yFd, dlgW3, dlgH3, hwnd, NULL, GetModuleHandleW(NULL), &fd);
            MSG msgFd;
            while (GetMessageW(&msgFd, NULL, 0, 0) > 0) {
                if (!IsWindow(hFd)) break;
                TranslateMessage(&msgFd); DispatchMessageW(&msgFd);
            }

            if (fd.okClicked) {
                for (const auto& path : paths) {
                    for (auto& cmp : s_components) {
                        if (cmp.source_path != path) continue;
                        if (!cmp.dest_path.empty() && cmp.dest_path != section) continue;
                        cmp.is_required    = fd.outRequired;
                        cmp.is_preselected = fd.outPreselected;
                        break;
                    }
                }
                if (!snap->fullPath.empty()) {
                    bool folderRowFound = false;
                    for (auto& cmp : s_components) {
                        if (cmp.source_path != snap->fullPath) continue;
                        if (cmp.source_type  != L"folder")      continue;
                        if (!cmp.dest_path.empty() && cmp.dest_path != section) continue;
                        cmp.is_required    = fd.outRequired;
                        cmp.is_preselected = fd.outPreselected;
                        cmp.dependencies   = fd.outDependencyIds;
                        cmp.notes_rtf      = fd.outNotesRtf;
                        folderRowFound     = true;
                        break;
                    }
                    if (!folderRowFound) {
                        ComponentRow newComp;
                        newComp.id             = 0;
                        newComp.project_id     = s_currentProject.id;
                        newComp.display_name   = snap->text;
                        newComp.description    = L"";
                        newComp.is_required    = fd.outRequired;
                        newComp.is_preselected = fd.outPreselected;
                        newComp.source_type    = L"folder";
                        newComp.source_path    = snap->fullPath;
                        newComp.dest_path      = section;
                        newComp.dependencies   = fd.outDependencyIds;
                        newComp.notes_rtf      = fd.outNotesRtf;
                        s_components.push_back(newComp);
                    }
                }
                UpdateCompTreeRequiredIcons(s_hCompTreeView, TreeView_GetRoot(s_hCompTreeView));
                MarkAsModified();
                // Refresh Required/Pre-selected columns in the component listview
                // (the listview keeps stale values unless explicitly updated).
                if (s_hCompListView && IsWindow(s_hCompListView)) {
                    int nLVItems = ListView_GetItemCount(s_hCompListView);
                    for (int i = 0; i < nLVItems; ++i) {
                        LVITEMW lvi2 = {}; lvi2.mask = LVIF_PARAM; lvi2.iItem = i;
                        ListView_GetItem(s_hCompListView, &lvi2);
                        int ci = (int)lvi2.lParam;
                        if (ci >= 0 && ci < (int)s_components.size()) {
                            int req = s_components[ci].is_required;
                            auto itR = s_locale.find(req ? L"yes" : L"no");
                            std::wstring rs = (itR != s_locale.end()) ? itR->second : (req ? L"Yes" : L"No");
                            ListView_SetItemText(s_hCompListView, i, 2, (LPWSTR)rs.c_str());
                        }
                    }
                }
            }
            return 0;
        }

        // Menu commands
        case IDM_FILE_NEW: {
            HINSTANCE hInst = (HINSTANCE)GetWindowLongPtr(hwnd, GWLP_HINSTANCE);
            MainWindow::CreateNew(hInst, s_locale);
            return 0;
        }
            
        case IDM_FILE_SAVE:
        {
            // Always read the current project name from the edit field
            {
                HWND hNameEdit = GetDlgItem(hwnd, IDC_PROJECT_NAME);
                if (hNameEdit) {
                    int len = GetWindowTextLengthW(hNameEdit);
                    if (len > 0) {
                        std::wstring name(len + 1, L'\0');
                        GetWindowTextW(hNameEdit, &name[0], len + 1);
                        name.resize(len);
                        s_currentProject.name = name;
                    }
                }
            }

            // New project (never saved): insert into DB, checking for duplicate names first
            if (s_currentProject.id <= 0) {
                // Look for an existing project with the same name
                auto allProjects = DB::ListProjects();
                int existingId = -1;
                for (const auto &p : allProjects) {
                    if (p.name == s_currentProject.name) { existingId = p.id; break; }
                }

                if (existingId > 0) {
                    int choice = ShowDuplicateProjectDialog(hwnd, s_currentProject.name, s_locale);
                    if (choice == 0) return 0; // Cancel
                    if (choice == 1) {
                        // Overwrite: adopt existing project's ID (UpdateProject below will refresh all fields)
                        s_currentProject.id = existingId;
                    } else { // choice == 2: Rename
                        std::wstring newName = s_currentProject.name;
                        if (!ShowRenameProjectDialog(hwnd, newName, s_locale)) return 0;
                        s_currentProject.name = newName;
                        // Reflect new name in the edit and title bar
                        s_updatingProjectNameProgrammatically = true;
                        SetDlgItemTextW(hwnd, IDC_PROJECT_NAME, newName.c_str());
                        s_updatingProjectNameProgrammatically = false;
                        SetWindowTextW(hwnd, (L"SetupCraft - " + newName).c_str());
                    }
                }

                // Still no ID means we chose Rename (or there was no duplicate): insert fresh row
                if (s_currentProject.id <= 0) {
                    int newId = -1;
                    if (!DB::InsertProject(s_currentProject.name, s_currentProject.directory,
                                           s_currentProject.description, s_currentProject.lang,
                                           s_currentProject.version, newId) || newId <= 0) {
                        MessageBoxW(hwnd, L"Failed to create project in database.", L"Save", MB_OK | MB_ICONERROR);
                        return 0;
                    }
                    s_currentProject.id = newId;
                }
                s_isNewUnsavedProject = false;
            }

            // Persist all project metadata (name, version, directory, etc.)
            // Sync directory from the live install-path field if we are on the Files page.
            if (s_currentPageIndex == 0) {
                HWND hInstEdit = GetDlgItem(hwnd, IDC_INSTALL_FOLDER);
                if (hInstEdit) {
                    int len = GetWindowTextLengthW(hInstEdit);
                    if (len > 0) {
                        std::wstring ip(len + 1, L'\0');
                        GetWindowTextW(hInstEdit, &ip[0], len + 1);
                        ip.resize(len);
                        s_currentInstallPath = ip;
                    }
                }
            }
            if (!s_currentInstallPath.empty())
                s_currentProject.directory = s_currentInstallPath;
            DB::UpdateProject(s_currentProject);

            // Ensure snapshots are current (take a fresh one if Files page is live).
            // Only refresh roots that actually exist — inactive-mode roots keep
            // their previous snapshot so their data is preserved in the DB.
            if (s_currentPageIndex == 0 && s_hTreeView && IsWindow(s_hTreeView)) {
                if (s_hProgramFilesRoot) { s_treeSnapshot_ProgramFiles.clear();  SaveTreeSnapshot(s_hTreeView, s_hProgramFilesRoot, s_treeSnapshot_ProgramFiles); }
                if (s_hProgramDataRoot)  { s_treeSnapshot_ProgramData.clear();   SaveTreeSnapshot(s_hTreeView, s_hProgramDataRoot,  s_treeSnapshot_ProgramData);  }
                if (s_hAppDataRoot)      { s_treeSnapshot_AppData.clear();        SaveTreeSnapshot(s_hTreeView, s_hAppDataRoot,       s_treeSnapshot_AppData);      }
                if (s_hAskAtInstallRoot) { s_treeSnapshot_AskAtInstall.clear();   SaveTreeSnapshot(s_hTreeView, s_hAskAtInstallRoot,  s_treeSnapshot_AskAtInstall);  }
            }

            // Replace file rows: walk full tree snapshots so both real-path folder
            // nodes (added via "Add Folder") and virtual-file nodes are persisted.
            DB::DeleteFilesForProject(s_currentProject.id);
            SaveTreeToDb(s_currentProject.id, s_treeSnapshot_ProgramFiles,  L"Program Files");
            SaveTreeToDb(s_currentProject.id, s_treeSnapshot_ProgramData,   L"ProgramData");
            SaveTreeToDb(s_currentProject.id, s_treeSnapshot_AppData,       L"AppData (Roaming)");
            SaveTreeToDb(s_currentProject.id, s_treeSnapshot_AskAtInstall,  L"AskAtInstall");

            // Persist components: replace all rows then re-insert from memory.
            // This is the ONLY place component data is written to DB.
            if (s_currentProject.use_components) {
                // Step 1: capture old IDs and clean up existing dep rows before
                //         components are deleted (avoids orphaned dep rows).
                std::vector<int> oldCompIds(s_components.size());
                for (int ci = 0; ci < (int)s_components.size(); ci++) oldCompIds[ci] = s_components[ci].id;
                for (int oid : oldCompIds)
                    if (oid > 0) DB::DeleteDependenciesForComponent(oid);

                // Step 2: delete and re-insert all components (new IDs assigned).
                DB::DeleteComponentsForProject(s_currentProject.id);
                for (auto& comp : s_components) {
                    comp.project_id = s_currentProject.id;
                    int newId = DB::InsertComponent(comp);
                    if (newId > 0) comp.id = newId;  // update in-memory ID so deps can reference it
                }

                // Step 3: build old→new ID map, persist all in-memory dependencies to
                // the DB, and update comp.dependencies in memory so that the dep picker
                // keeps showing the correct pre-checked state after a Save.
                std::map<int,int> idMap;
                for (int ci = 0; ci < (int)s_components.size(); ci++) {
                    if (oldCompIds[ci] > 0) idMap[oldCompIds[ci]] = s_components[ci].id;
                }
                for (auto& comp : s_components) {
                    if (comp.dependencies.empty()) continue;
                    std::vector<int> newDeps;
                    for (int depOldId : comp.dependencies) {
                        auto it = idMap.find(depOldId);
                        int  depNewId = (it != idMap.end()) ? it->second : 0;
                        if (depNewId > 0) {
                            DB::InsertComponentDependency(comp.id, depNewId);
                            newDeps.push_back(depNewId);
                        }
                    }
                    comp.dependencies = std::move(newDeps); // keep in-memory IDs in sync
                }
            }

            // Persist ask-at-install preference
            DB::SetSetting(L"ask_at_install_" + std::to_wstring(s_currentProject.id),
                           s_askAtInstallEnabled ? L"1" : L"0");

            s_hasUnsavedChanges = false;
            if (s_hStatus && IsWindow(s_hStatus)) InvalidateRect(s_hStatus, NULL, TRUE);
            SC_SaveToDb(s_currentProject.id);    // persist shortcuts + menu nodes
            DEP_SaveToDb(s_currentProject.id);    // persist external dependencies
            DEP_LoadFromDb(s_currentProject.id);  // refresh IDs from DB
            IDLG_SaveToDb(s_currentProject.id);   // persist installer dialog content
            IDLG_LoadFromDb(s_currentProject.id); // refresh from DB
            return 0;
        }
            
        case IDM_FILE_SAVEAS:
            MessageBoxW(hwnd, L"Save As functionality to be implemented", L"Save As", MB_OK | MB_ICONINFORMATION);
            return 0;
            
        case IDM_FILE_CLOSE: {
            if (s_hasUnsavedChanges) {
                int result = ShowUnsavedChangesDialog(hwnd, s_locale);
                if (result == 0) {
                    // Cancel
                    return 0;
                } else if (result == 1) {
                    // Save then close
                    SendMessageW(hwnd, WM_COMMAND, MAKEWPARAM(IDM_FILE_SAVE, 0), 0);
                    // fall through to return to entry screen
                }
                // result == 3: Don't Save — fall through to return to entry screen
            } else {
                // No unsaved changes — simple close confirmation
                if (!ShowCloseProjectDialog(hwnd, s_locale)) {
                    return 0;
                }
            }
            // Return to entry screen
            HWND entryWindow = FindWindowW(L"SetupCraft_EntryScreen", NULL);
            if (entryWindow) {
                EnableWindow(entryWindow, TRUE);  // was disabled when project opened
                ShowWindow(entryWindow, SW_SHOW);
                SetForegroundWindow(entryWindow);
            }
            DestroyWindow(hwnd);
            return 0;
        }
        
        case IDM_FILE_EXIT:
            SendMessageW(hwnd, WM_CLOSE, 0, 0);
            return 0;
            
        case IDM_BUILD_COMPILE:
            MessageBoxW(hwnd, L"Compile functionality to be implemented", L"Compile", MB_OK | MB_ICONINFORMATION);
            return 0;
            
        case IDM_BUILD_TEST:
            MessageBoxW(hwnd, L"Test functionality to be implemented", L"Test", MB_OK | MB_ICONINFORMATION);
            return 0;
            
        case IDM_HELP_ABOUT:
            ShowAboutDialog(hwnd);
            return 0;
            
        case IDM_TREEVIEW_ADD_FOLDER: {
            // Create a folder with a default name
            std::wstring folderName = L"NewFolder";
            
            // Use the right-clicked item as parent (or Program Files root as fallback)
            HTREEITEM hParent = s_rightClickedItem ? s_rightClickedItem : s_hProgramFilesRoot;
            
            // Find a unique name if NewFolder already exists under this parent
            int counter = 1;
            HTREEITEM hChild = TreeView_GetChild(s_hTreeView, hParent);
            while (hChild) {
                wchar_t text[256];
                TVITEMW item = {};
                item.mask = TVIF_TEXT;
                item.hItem = hChild;
                item.pszText = text;
                item.cchTextMax = 256;
                TreeView_GetItem(s_hTreeView, &item);
                
                if (folderName == text) {
                    folderName = L"NewFolder" + std::to_wstring(++counter);
                    hChild = TreeView_GetChild(s_hTreeView, hParent); // Start over
                } else {
                    hChild = TreeView_GetNextSibling(s_hTreeView, hChild);
                }
            }
            
            // Create folder node (empty path means virtual folder)
            if (s_hTreeView && hParent) {
                HTREEITEM hFolder = AddTreeNode(s_hTreeView, hParent, folderName, L"");
                TreeView_Expand(s_hTreeView, hParent, TVE_EXPAND);
                TreeView_SelectItem(s_hTreeView, hFolder);
                TreeView_EditLabel(s_hTreeView, hFolder); // Start editing immediately
                // Note: Install path will be updated when user finishes editing in TVN_ENDLABELEDIT
            }
            
            s_rightClickedItem = NULL; // Clear the tracked item
            MarkAsModified();
            UpdateComponentsButtonState(hwnd);
            return 0;
        }
        
        case IDM_TREEVIEW_RENAME: {
            HTREEITEM hItem = s_rightClickedItem
                ? s_rightClickedItem
                : TreeView_GetSelection(s_hTreeView);
            if (hItem) TreeView_EditLabel(s_hTreeView, hItem);
            s_rightClickedItem = NULL;
            return 0;
        }

        case IDM_TREEVIEW_REMOVE_FOLDER: {
            // If the right-clicked item belongs to a multi-selection, delegate to the
            // Remove button handler so all ticked items are deleted together.
            if (s_rightClickedItem && !s_filesTreeMultiSel.empty() &&
                s_filesTreeMultiSel.count(s_rightClickedItem)) {
                s_rightClickedItem = NULL;
                SendMessageW(hwnd, WM_COMMAND, MAKEWPARAM(IDC_FILES_REMOVE, BN_CLICKED), 0);
                return 0;
            }
            // Remove the right-clicked folder from the tree
            if (s_rightClickedItem && s_rightClickedItem != s_hProgramFilesRoot) {
                // Check if folder has children
                bool hasChildren = (TreeView_GetChild(s_hTreeView, s_rightClickedItem) != NULL);
                
                // Get folder name for confirmation message
                wchar_t folderName[256];
                TVITEMW item = {};
                item.mask = TVIF_TEXT;
                item.hItem = s_rightClickedItem;
                item.pszText = folderName;
                item.cchTextMax = 256;
                TreeView_GetItem(s_hTreeView, &item);
                
                // Show confirmation dialog if folder is not empty
                bool shouldDelete = true;
                if (hasChildren) {
                    std::wstring message = LocFmt(s_locale, L"confirm_delete_nonempty", std::wstring(folderName));
                    shouldDelete = ShowConfirmDeleteDialog(hwnd, LocFmt(s_locale, L"confirm_delete_title"), message, s_locale);
                }
                
                // Delete the folder and all its children
                if (shouldDelete) {
                    // Check if deleted item was under Program Files root
                    HTREEITEM hParent = TreeView_GetParent(s_hTreeView, s_rightClickedItem);
                    bool wasUnderProgramFiles = (hParent == s_hProgramFilesRoot);

                    // Collect file paths BEFORE wiping s_virtualFolderFiles,
                    // then remove orphaned component rows from memory.
                    {
                        std::unordered_set<std::wstring> delPaths;
                        CollectSubtreePaths(s_hTreeView, s_rightClickedItem, delPaths);
                        PurgeComponentRowsByPaths(delPaths);
                    }

                    // Clean up virtual folder files recursively before deleting
                    std::function<void(HTREEITEM)> CleanupVirtualFolderFiles = [&](HTREEITEM hItem) {
                        // Remove this folder's files from the map
                        s_virtualFolderFiles.erase(hItem);
                        
                        // Recursively clean children
                        HTREEITEM hChild = TreeView_GetChild(s_hTreeView, hItem);
                        while (hChild) {
                            CleanupVirtualFolderFiles(hChild);
                            hChild = TreeView_GetNextSibling(s_hTreeView, hChild);
                        }
                    };
                    CleanupVirtualFolderFiles(s_rightClickedItem);
                    
                    TreeView_DeleteItem(s_hTreeView, s_rightClickedItem);
                    MarkAsModified();
                    UpdateComponentsButtonState(hwnd);
                    
                    // Update install path if folder was deleted from Program Files root
                    if (wasUnderProgramFiles) {
                        UpdateInstallPathFromTree(hwnd);
                    }
                }
            }
            
            s_rightClickedItem = NULL;
            return 0;
        }
        }
        break;
    }
    
    case WM_CONTEXTMENU: {
        if (SC_OnContextMenu(hwnd, (HWND)wParam, GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam))) return 0;
        // Handle Registry TreeView context menu
        HWND hWndContext = (HWND)wParam;
        if (hWndContext == s_hRegTreeView) {
            // Get cursor position
            int xPos = GET_X_LPARAM(lParam);
            int yPos = GET_Y_LPARAM(lParam);
            
            // If position is -1,-1, it was triggered by keyboard
            if (xPos == -1 && yPos == -1) {
                HTREEITEM hSelected = TreeView_GetSelection(s_hRegTreeView);
                if (hSelected) {
                    RECT rcItem;
                    TreeView_GetItemRect(s_hRegTreeView, hSelected, &rcItem, TRUE);
                    POINT pt = { rcItem.left, rcItem.bottom };
                    ClientToScreen(s_hRegTreeView, &pt);
                    xPos = pt.x;
                    yPos = pt.y;
                }
            }
            
            // Get item at cursor position
            TVHITTESTINFO ht = {};
            ht.pt.x = xPos;
            ht.pt.y = yPos;
            ScreenToClient(s_hRegTreeView, &ht.pt);
            HTREEITEM hItem = TreeView_HitTest(s_hRegTreeView, &ht);
            
            if (hItem) {
                // Select the item
                TreeView_SelectItem(s_hRegTreeView, hItem);
                
                // Get locale strings
                auto itAddKey = s_locale.find(L"reg_context_add_key");
                std::wstring addKey = (itAddKey != s_locale.end()) ? itAddKey->second : L"Add Key";
                
                auto itAddValue = s_locale.find(L"reg_context_add_value");
                std::wstring addValue = (itAddValue != s_locale.end()) ? itAddValue->second : L"Add Value";
                
                auto itEdit = s_locale.find(L"reg_context_edit");
                std::wstring editText = (itEdit != s_locale.end()) ? itEdit->second : L"Edit";
                
                auto itDelete = s_locale.find(L"reg_context_delete");
                std::wstring deleteText = (itDelete != s_locale.end()) ? itDelete->second : L"Delete";
                
                // Create context menu
                HMENU hMenu = CreatePopupMenu();
                AppendMenuW(hMenu, MF_STRING, IDM_REG_ADD_KEY, addKey.c_str());
                AppendMenuW(hMenu, MF_STRING, IDM_REG_ADD_VALUE, addValue.c_str());
                AppendMenuW(hMenu, MF_SEPARATOR, 0, NULL);
                AppendMenuW(hMenu, MF_STRING, IDM_REG_EDIT_KEY, editText.c_str());
                AppendMenuW(hMenu, MF_STRING, IDM_REG_DELETE_KEY, deleteText.c_str());
                
                // Show menu
                TrackPopupMenu(hMenu, TPM_RIGHTBUTTON, xPos, yPos, 0, hwnd, NULL);
                DestroyMenu(hMenu);
            }
            return 0;
        }
        
        // Handle Registry ListView context menu
        if (hWndContext == s_hRegListView) {
            // Get cursor position
            int xPos = GET_X_LPARAM(lParam);
            int yPos = GET_Y_LPARAM(lParam);
            
            // If position is -1,-1, it was triggered by keyboard
            if (xPos == -1 && yPos == -1) {
                // Get position of selected item
                int iSelected = ListView_GetNextItem(s_hRegListView, -1, LVNI_SELECTED);
                if (iSelected != -1) {
                    RECT rcItem;
                    ListView_GetItemRect(s_hRegListView, iSelected, &rcItem, LVIR_BOUNDS);
                    POINT pt = { rcItem.left, rcItem.bottom };
                    ClientToScreen(s_hRegListView, &pt);
                    xPos = pt.x;
                    yPos = pt.y;
                }
            }
            
            // Get item at cursor position
            LVHITTESTINFO ht = {};
            ht.pt.x = xPos;
            ht.pt.y = yPos;
            ScreenToClient(s_hRegListView, &ht.pt);
            int iItem = ListView_HitTest(s_hRegListView, &ht);
            s_rightClickedRegIndex = -1;
            if (iItem != -1) {
                s_rightClickedRegIndex = iItem;
            }
            
            // Get locale strings
            auto itAddValue = s_locale.find(L"reg_context_add_value");
            std::wstring addValue = (itAddValue != s_locale.end()) ? itAddValue->second : L"Add Value";
            
            auto itEdit = s_locale.find(L"reg_context_edit");
            std::wstring editText = (itEdit != s_locale.end()) ? itEdit->second : L"Edit";
            
            auto itDelete = s_locale.find(L"reg_context_delete");
            std::wstring deleteText = (itDelete != s_locale.end()) ? itDelete->second : L"Delete";
            
            // Create context menu
            HMENU hMenu = CreatePopupMenu();
            AppendMenuW(hMenu, MF_STRING, IDM_REG_ADD_VALUE, addValue.c_str());
            
            // Add Edit and Delete if an item is selected
            if (iItem != -1) {
                AppendMenuW(hMenu, MF_SEPARATOR, 0, NULL);
                AppendMenuW(hMenu, MF_STRING, IDM_REG_EDIT_VALUE, editText.c_str());
                AppendMenuW(hMenu, MF_STRING, IDM_REG_DELETE_VALUE, deleteText.c_str());
                
                // Select the item
                ListView_SetItemState(s_hRegListView, iItem, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
            }
            
            // Show menu
            TrackPopupMenu(hMenu, TPM_RIGHTBUTTON, xPos, yPos, 0, hwnd, NULL);
            DestroyMenu(hMenu);
            return 0;
        }

        // Handle Components ListView context menu
        if (hWndContext == s_hCompListView) {
            int xPos = GET_X_LPARAM(lParam);
            int yPos = GET_Y_LPARAM(lParam);
            if (xPos == -1 && yPos == -1) {
                int iSel = ListView_GetNextItem(s_hCompListView, -1, LVNI_SELECTED);
                if (iSel >= 0) {
                    RECT rcItem;
                    ListView_GetItemRect(s_hCompListView, iSel, &rcItem, LVIR_BOUNDS);
                    POINT pt = { rcItem.left, rcItem.bottom };
                    ClientToScreen(s_hCompListView, &pt);
                    xPos = pt.x; yPos = pt.y;
                }
            }
            int selIdx = ListView_GetNextItem(s_hCompListView, -1, LVNI_SELECTED);
            if (selIdx < 0) return 0;
            auto LS = [&](const wchar_t* k, const wchar_t* fb) -> std::wstring {
                auto it = s_locale.find(k); return (it != s_locale.end()) ? it->second : fb;
            };
            HMENU hMenu = CreatePopupMenu();
            AppendMenuW(hMenu, MF_STRING, IDM_COMP_CTX_EDIT, LS(L"comp_edit", L"Edit").c_str());
            int cmd = (int)TrackPopupMenu(hMenu, TPM_RIGHTBUTTON | TPM_RETURNCMD, xPos, yPos, 0, hwnd, NULL);
            DestroyMenu(hMenu);
            if (cmd == IDM_COMP_CTX_EDIT)
                SendMessageW(hwnd, WM_COMMAND, IDC_COMP_EDIT, 0);
            return 0;
        }

        // Handle Components TreeView context menu
        if (hWndContext == s_hCompTreeView) {
            int xPos = GET_X_LPARAM(lParam);
            int yPos = GET_Y_LPARAM(lParam);
            if (xPos == -1 && yPos == -1) {
                HTREEITEM hSel = TreeView_GetSelection(s_hCompTreeView);
                if (hSel) {
                    RECT rcItem;
                    TreeView_GetItemRect(s_hCompTreeView, hSel, &rcItem, TRUE);
                    POINT pt = { rcItem.left, rcItem.bottom };
                    ClientToScreen(s_hCompTreeView, &pt);
                    xPos = pt.x; yPos = pt.y;
                }
            }
            // Hit-test and select the item under the cursor
            TVHITTESTINFO ht = {};
            ht.pt = { xPos, yPos };
            ScreenToClient(s_hCompTreeView, &ht.pt);
            HTREEITEM hHit = TreeView_HitTest(s_hCompTreeView, &ht);
            if (!hHit) return 0;
            TreeView_SelectItem(s_hCompTreeView, hHit);

            TVITEMW tvi = {}; tvi.hItem = hHit; tvi.mask = TVIF_PARAM;
            TreeView_GetItem(s_hCompTreeView, &tvi);
            const TreeNodeSnapshot* snap = (const TreeNodeSnapshot*)tvi.lParam;
            if (!snap) return 0;  // section root — no lParam

            auto LS = [&](const wchar_t* k, const wchar_t* fb) -> std::wstring {
                auto it = s_locale.find(k); return (it != s_locale.end()) ? it->second : fb;
            };
            HMENU hMenu = CreatePopupMenu();
            AppendMenuW(hMenu, MF_STRING, IDM_COMP_TREE_CTX_EDIT, LS(L"comp_edit", L"Edit").c_str());
            int cmd = (int)TrackPopupMenu(hMenu, TPM_RIGHTBUTTON | TPM_RETURNCMD, xPos, yPos, 0, hwnd, NULL);
            DestroyMenu(hMenu);

            if (cmd == IDM_COMP_TREE_CTX_EDIT)
                SendMessageW(hwnd, WM_COMMAND, MAKEWPARAM(IDM_COMP_TREE_CTX_EDIT, 0), 0);
            return 0;
        }

        // Handle TreeView context menu
        if (hWndContext == s_hTreeView) {
            // Get cursor position
            int xPos = GET_X_LPARAM(lParam);
            int yPos = GET_Y_LPARAM(lParam);
            
            // If position is -1,-1, it was triggered by keyboard
            if (xPos == -1 && yPos == -1) {
                // Get position of selected item
                HTREEITEM hSelected = TreeView_GetSelection(s_hTreeView);
                if (hSelected) {
                    RECT rcItem;
                    TreeView_GetItemRect(s_hTreeView, hSelected, &rcItem, TRUE);
                    POINT pt = { rcItem.left, rcItem.bottom };
                    ClientToScreen(s_hTreeView, &pt);
                    xPos = pt.x;
                    yPos = pt.y;
                }
            }
            
            // Get item at cursor position
            TVHITTESTINFO ht = {};
            ht.pt.x = xPos;
            ht.pt.y = yPos;
            ScreenToClient(s_hTreeView, &ht.pt);
            HTREEITEM hItem = TreeView_HitTest(s_hTreeView, &ht);
            
            // Show menu if right-clicking on any folder node
            if (hItem) {
                s_rightClickedItem = hItem; // Remember which item was clicked

                // Localized button captions
                auto itAF = s_locale.find(L"files_add_folder");
                std::wstring addFolderText = (itAF != s_locale.end()) ? itAF->second : L"Add Folder";
                auto itAFi = s_locale.find(L"files_add_files");
                std::wstring addFilesText = (itAFi != s_locale.end()) ? itAFi->second : L"Add Files";

                // "Add Files", "Rename" and "Remove" only for user nodes, never for the four system roots
                bool isSystemRoot = (hItem == s_hProgramFilesRoot ||
                                     hItem == s_hProgramDataRoot  ||
                                     hItem == s_hAppDataRoot       ||
                                     hItem == s_hAskAtInstallRoot);

                HMENU hMenu = CreatePopupMenu();
                AppendMenuW(hMenu, MF_STRING, IDC_FILES_ADD_DIR, addFolderText.c_str());
                if (!isSystemRoot)
                    AppendMenuW(hMenu, MF_STRING, IDC_FILES_ADD_FILES, addFilesText.c_str());
                AppendMenuW(hMenu, MF_SEPARATOR, 0, NULL);
                AppendMenuW(hMenu, MF_STRING, IDM_TREEVIEW_ADD_FOLDER, L"Create Folder...");
                if (!isSystemRoot) {
                    AppendMenuW(hMenu, MF_SEPARATOR, 0, NULL);
                    AppendMenuW(hMenu, MF_STRING, IDM_TREEVIEW_RENAME,        L"Rename\tF2");
                    AppendMenuW(hMenu, MF_SEPARATOR, 0, NULL);
                    AppendMenuW(hMenu, MF_STRING, IDM_TREEVIEW_REMOVE_FOLDER, L"Remove");
                }

                // Ensure main window is foreground before showing the popup, otherwise
                // TrackPopupMenu blocks until the window is re-activated (alt-tab bug).
                // PostMessage(WM_NULL) after the call is the MSDN-documented companion fix.
                SetForegroundWindow(hwnd);
                TrackPopupMenu(hMenu, TPM_RIGHTBUTTON, xPos, yPos, 0, hwnd, NULL);
                PostMessageW(hwnd, WM_NULL, 0, 0);
                DestroyMenu(hMenu);
            }
            return 0;
        }

        // Handle Files ListView context menu (right pane)
        if (hWndContext == s_hListView && s_hListView && IsWindow(s_hListView)) {
            int xPos = GET_X_LPARAM(lParam);
            int yPos = GET_Y_LPARAM(lParam);
            if (xPos == -1 && yPos == -1) {
                POINT pt = { 20, 20 };
                ClientToScreen(s_hListView, &pt);
                xPos = pt.x; yPos = pt.y;
            }

            // Hit-test to select the item under the cursor
            LVHITTESTINFO lvht = {};
            lvht.pt = { xPos, yPos };
            ScreenToClient(s_hListView, &lvht.pt);
            int hitItem = ListView_HitTest(s_hListView, &lvht);
            if (hitItem != -1) {
                // If right-clicked item is not already selected, select only it
                if (!(ListView_GetItemState(s_hListView, hitItem, LVIS_SELECTED) & LVIS_SELECTED)) {
                    ListView_SetItemState(s_hListView, -1, 0, LVIS_SELECTED);
                    ListView_SetItemState(s_hListView, hitItem, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
                }
            }

            auto itAF  = s_locale.find(L"files_add_folder");
            std::wstring addFolderText = (itAF  != s_locale.end()) ? itAF->second  : L"Add Folder";
            auto itAFi = s_locale.find(L"files_add_files");
            std::wstring addFilesText  = (itAFi != s_locale.end()) ? itAFi->second : L"Add Files";
            auto itRem = s_locale.find(L"files_remove");
            std::wstring removeText    = (itRem != s_locale.end()) ? itRem->second  : L"Remove";

            HMENU hMenu = CreatePopupMenu();
            AppendMenuW(hMenu, MF_STRING, IDC_FILES_ADD_DIR,   addFolderText.c_str());
            AppendMenuW(hMenu, MF_STRING, IDC_FILES_ADD_FILES, addFilesText.c_str());
            int selCount = ListView_GetSelectedCount(s_hListView);
            if (selCount > 0) {
                AppendMenuW(hMenu, MF_SEPARATOR, 0, NULL);
                AppendMenuW(hMenu, MF_STRING, IDC_FILES_REMOVE, removeText.c_str());
            }
            TrackPopupMenu(hMenu, TPM_RIGHTBUTTON, xPos, yPos, 0, hwnd, NULL);
            DestroyMenu(hMenu);
            return 0;
        }
        break;
    }
    
    case WM_CTLCOLORSTATIC: {
        // Make static controls have white background like the window
        HDC hdc = (HDC)wParam;
        HWND hControl = (HWND)lParam;
        int ctrlId = GetDlgCtrlID(hControl);
        if (ctrlId == 5100 || ctrlId == 5300 || ctrlId == 5301 || ctrlId == 5302 || ctrlId == 5303 || ctrlId == 5304 || ctrlId == IDC_DEP_PAGE_TITLE || ctrlId == IDC_IDLG_PAGE_TITLE) {  // page title statics (Files + Shortcuts + SC column headings + Dependencies + Dialogs)
            if (s_hPageTitleFont) SelectObject(hdc, s_hPageTitleFont);
        } else {
            if (s_hGuiFont) SelectObject(hdc, s_hGuiFont);
        }
        
        // Special handling for install folder - dark blue text
        if (GetDlgCtrlID(hControl) == IDC_INSTALL_FOLDER) {
            SetTextColor(hdc, RGB(0, 51, 153)); // Dark blue
            SetBkMode(hdc, TRANSPARENT);
            return (LRESULT)GetSysColorBrush(COLOR_WINDOW);
        }
        
        SetBkMode(hdc, TRANSPARENT);
        return (LRESULT)GetSysColorBrush(COLOR_WINDOW);
    }
    
    case WM_CTLCOLOREDIT: {
        HDC hdc = (HDC)wParam;
        if (s_hGuiFont) SelectObject(hdc, s_hGuiFont);
        return (LRESULT)GetSysColorBrush(COLOR_WINDOW);
    }
    
    case WM_KEYDOWN: {
        // Handle Ctrl+N for New Project
        if (wParam == 'N' && GetKeyState(VK_CONTROL) < 0) {
            HINSTANCE hInst = (HINSTANCE)GetWindowLongPtr(hwnd, GWLP_HINSTANCE);
            MainWindow::CreateNew(hInst, s_locale);
            return 0;
        }
        
        // Handle Ctrl+S for Save
        if (wParam == 'S' && GetKeyState(VK_CONTROL) < 0) {
            SendMessageW(hwnd, WM_COMMAND, MAKEWPARAM(IDM_FILE_SAVE, 0), 0);
            return 0;
        }
        
        // Handle Ctrl+W to close project (same as Close Project menu)
        if (IsCtrlWPressed(msg, wParam)) {
            SendMessageW(hwnd, WM_COMMAND, IDM_FILE_CLOSE, 0);
            return 0;
        }
        break;
    }

    case WM_SETTINGCHANGE:
        // Repaint custom checkboxes and rebuild TreeView state images for new theme.
        OnCheckboxSettingChange(hwnd);
        if (s_hTreeView && IsWindow(s_hTreeView))
            UpdateTreeViewCheckboxImages(s_hTreeView, S(16));
        break;

    case WM_CLOSE: {
        if (s_hasUnsavedChanges) {
            int result = ShowUnsavedChangesDialog(hwnd, s_locale);
            if (result == 0) {
                // Cancel
                return 0;
            } else if (result == 1) {
                // Save then exit
                SendMessageW(hwnd, WM_COMMAND, MAKEWPARAM(IDM_FILE_SAVE, 0), 0);
                // fall through to destroy
            }
            // result == 2 (Don't Save) — exit without saving
        } else {
            if (!ShowQuitDialog(hwnd, s_locale)) {
                return 0;
            }
        }
        // Destroy main window then terminate the process cleanly via entry screen
        DestroyWindow(hwnd);
        {
            HWND hEntry = FindWindowW(L"SetupCraft_EntryScreen", NULL);
            if (hEntry) DestroyWindow(hEntry); // triggers entry screen WM_DESTROY → PostQuitMessage
        }
        return 0;
    }
    
    case WM_DRAWITEM: {
        LPDRAWITEMSTRUCT dis = (LPDRAWITEMSTRUCT)lParam;
        // Custom checkboxes are handled first and take priority.
        if (DrawCustomCheckbox(dis)) return TRUE;
        // Draw the save indicator part of the status bar
        if (dis->CtlID == IDC_STATUS_BAR && (int)dis->itemID == 1) {
            bool saved = !s_hasUnsavedChanges;
            // Fill with normal status bar background
            HBRUSH hBr = GetSysColorBrush(COLOR_3DFACE);
            FillRect(dis->hDC, &dis->rcItem, hBr);
            SetBkMode(dis->hDC, TRANSPARENT);
            // Colored text only
            SetTextColor(dis->hDC, saved ? RGB(30, 150, 70) : RGB(190, 40, 30));
            const wchar_t *label = saved ? L"\u2714  Saved" : L"\u25CF  Unsaved";
            if (s_hGuiFont) SelectObject(dis->hDC, s_hGuiFont);
            DrawTextW(dis->hDC, label, -1, const_cast<RECT*>(&dis->rcItem),
                      DT_CENTER | DT_VCENTER | DT_SINGLELINE);
            return TRUE;
        }

        // Handle custom button drawing for toolbar buttons and page buttons
        // Note: IDC_TB_ABOUT is now a static icon, not a button, so exclude it
        if ((dis->CtlID >= IDC_TB_FILES && dis->CtlID <= IDC_TB_SAVE) || dis->CtlID == IDC_TB_DIALOGS || dis->CtlID == IDC_TB_COMPONENTS || dis->CtlID == IDC_TB_EXIT || dis->CtlID == IDC_TB_CLOSE_PROJECT ||
            (dis->CtlID >= IDC_FILES_ADD_DIR && dis->CtlID <= IDC_FILES_REMOVE) ||
            (dis->CtlID >= IDC_REG_CHECKBOX && dis->CtlID <= IDC_REG_BACKUP) ||
            (dis->CtlID >= IDC_COMP_ADD && dis->CtlID <= IDC_COMP_REMOVE) ||
            (dis->CtlID >= IDC_SC_DESKTOP_BTN && dis->CtlID <= IDC_SC_SM_REMOVE) ||
             dis->CtlID == IDC_SC_SM_ADDSC ||
            (dis->CtlID >= IDC_DEP_ADD && dis->CtlID <= IDC_DEP_REMOVE) ||
            (dis->CtlID >= IDC_IDLG_ROW_BASE && dis->CtlID < IDC_IDLG_ROW_BASE + IDLG_COUNT * 4) ||
             dis->CtlID == IDC_IDLG_INST_CHANGE_ICON) {
            ButtonColor color = (ButtonColor)GetWindowLongPtr(dis->hwndItem, GWLP_USERDATA);
            // Create bold font for buttons (scaled for DPI)
            HFONT hFont = CreateFontW(-S(12), 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
                DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
            LRESULT result = DrawCustomButton(dis, color, hFont);
            if (hFont) DeleteObject(hFont);
            return result;
        }
        break;
    }
    
    case WM_USER + 100: {
        // Navigate to registry key in TreeView
        if (!s_hRegTreeView || !IsWindow(s_hRegTreeView)) {
            return 0;
        }
        
        // Path to navigate: HKEY_LOCAL_MACHINE -> SOFTWARE -> Microsoft -> Windows -> CurrentVersion -> Uninstall -> [AppName]
        // We need to find and expand each level
        
        // Helper lambda to find child by text
        auto FindChild = [](HWND hTree, HTREEITEM hParent, const std::wstring& text) -> HTREEITEM {
            HTREEITEM hItem = TreeView_GetChild(hTree, hParent);
            wchar_t buffer[256];
            TVITEMW tvi = {};
            tvi.mask = TVIF_TEXT;
            tvi.pszText = buffer;
            tvi.cchTextMax = 256;
            
            while (hItem) {
                tvi.hItem = hItem;
                if (TreeView_GetItem(hTree, &tvi)) {
                    if (wcscmp(buffer, text.c_str()) == 0) {
                        return hItem;
                    }
                }
                hItem = TreeView_GetNextSibling(hTree, hItem);
            }
            return NULL;
        };
        
        // Start from root
        HTREEITEM hCurrent = TreeView_GetRoot(s_hRegTreeView);
        
        // Find HKEY_LOCAL_MACHINE
        wchar_t buffer[256];
        TVITEMW tvi = {};
        tvi.mask = TVIF_TEXT;
        tvi.pszText = buffer;
        tvi.cchTextMax = 256;
        
        while (hCurrent) {
            tvi.hItem = hCurrent;
            if (TreeView_GetItem(s_hRegTreeView, &tvi)) {
                if (wcscmp(buffer, L"HKEY_LOCAL_MACHINE") == 0) {
                    break;
                }
            }
            hCurrent = TreeView_GetNextSibling(s_hRegTreeView, hCurrent);
        }
        
        if (!hCurrent) return 0;
        
        // Expand and navigate through: SOFTWARE -> Microsoft -> Windows -> CurrentVersion -> Uninstall -> [AppName]
        std::vector<std::wstring> path = {
            L"SOFTWARE",
            L"Microsoft",
            L"Windows",
            L"CurrentVersion",
            L"Uninstall",
            s_currentProject.name
        };
        
        for (const auto& nodeName : path) {
            TreeView_Expand(s_hRegTreeView, hCurrent, TVE_EXPAND);
            hCurrent = FindChild(s_hRegTreeView, hCurrent, nodeName);
            if (!hCurrent) break;
        }
        
        // Select and ensure visible
        if (hCurrent) {
            TreeView_SelectItem(s_hRegTreeView, hCurrent);
            TreeView_EnsureVisible(s_hRegTreeView, hCurrent);
            
            // Store registry values for this key if not already stored
            if (s_registryValues.find(hCurrent) == s_registryValues.end()) {
                std::vector<RegistryEntry> values;
                
                // DisplayName
                RegistryEntry displayName;
                displayName.hive = L"HKLM";
                displayName.path = L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\" + s_currentProject.name;
                displayName.name = L"DisplayName";
                displayName.type = L"REG_SZ";
                displayName.data = s_currentProject.name;
                values.push_back(displayName);
                
                // DisplayVersion
                RegistryEntry displayVersion;
                displayVersion.hive = L"HKLM";
                displayVersion.path = displayName.path;
                displayVersion.name = L"DisplayVersion";
                displayVersion.type = L"REG_SZ";
                displayVersion.data = s_currentProject.version;
                values.push_back(displayVersion);
                
                // Publisher
                if (!s_appPublisher.empty()) {
                    RegistryEntry publisher;
                    publisher.hive = L"HKLM";
                    publisher.path = displayName.path;
                    publisher.name = L"Publisher";
                    publisher.type = L"REG_SZ";
                    publisher.data = s_appPublisher;
                    values.push_back(publisher);
                }
                
                // InstallLocation
                RegistryEntry installLocation;
                installLocation.hive = L"HKLM";
                installLocation.path = displayName.path;
                installLocation.name = L"InstallLocation";
                installLocation.type = L"REG_SZ";
                installLocation.data = s_currentProject.directory;
                values.push_back(installLocation);
                
                // DisplayIcon
                if (!s_appIconPath.empty()) {
                    RegistryEntry displayIcon;
                    displayIcon.hive = L"HKLM";
                    displayIcon.path = displayName.path;
                    displayIcon.name = L"DisplayIcon";
                    displayIcon.type = L"REG_SZ";
                    displayIcon.data = s_appIconPath;
                    values.push_back(displayIcon);
                }
                
                // UninstallString
                RegistryEntry uninstallString;
                uninstallString.hive = L"HKLM";
                uninstallString.path = displayName.path;
                uninstallString.name = L"UninstallString";
                uninstallString.type = L"REG_SZ";
                uninstallString.data = s_currentProject.directory + L"\\uninstall.exe";
                values.push_back(uninstallString);
                
                // Store in map
                s_registryValues[hCurrent] = values;
            }
            
            // Populate ListView (the TVN_SELCHANGED handler will do this automatically)
            if (s_hRegListView && IsWindow(s_hRegListView)) {
                ListView_DeleteAllItems(s_hRegListView);
                
                auto it = s_registryValues.find(hCurrent);
                if (it != s_registryValues.end()) {
                    LVITEMW lvi = {};
                    lvi.mask = LVIF_TEXT;
                    int itemIndex = 0;
                    
                    for (const auto& entry : it->second) {
                        lvi.iItem = itemIndex++;
                        lvi.iSubItem = 0;
                        lvi.pszText = (LPWSTR)entry.name.c_str();
                        int idx = ListView_InsertItem(s_hRegListView, &lvi);
                        ListView_SetItemText(s_hRegListView, idx, 1, (LPWSTR)entry.type.c_str());
                        ListView_SetItemText(s_hRegListView, idx, 2, (LPWSTR)entry.data.c_str());
                    }
                }
                
                // Set focus to ListView so user can see values
                SetFocus(s_hRegListView);
            }
        }
        
        return 0;
    }
    
    case WM_LBUTTONUP:
        if (DragDrop_OnLButtonUp()) return 0;
        break;

    case WM_SETCURSOR:
        if (DragDrop_OnSetCursor()) return TRUE;
        break;

    case WM_LBUTTONDOWN: {
        // Show About dialog only when click is on the About icon
        POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
        if (s_hAboutButton) {
            RECT rcIcon;
            GetWindowRect(s_hAboutButton, &rcIcon);
            ScreenToClient(hwnd, (LPPOINT)&rcIcon.left);
            ScreenToClient(hwnd, (LPPOINT)&rcIcon.right);
            if (PtInRect(&rcIcon, pt)) {
                ShowAboutDialog(hwnd);
                return 0;
            }
        }
        // Hide any visible tooltip on any other click
        HideTooltip();
        s_aboutMouseTracking = false;
        break;
    }
    
    case WM_MOUSEMOVE: {
        if (DragDrop_OnMouseMove()) return 0;

        // About-icon tooltip is handled by AboutIcon_SubclassProc.
        // When cursor is over the disabled Components button (disabled windows
        // forward WM_MOUSEMOVE to the parent), show tooltip and start tracking
        // leave on the BUTTON itself — not the parent. TrackMouseEvent posts
        // WM_MOUSELEAVE to the button's subclass proc when the cursor leaves.
        // A timer callback runs as belt-and-suspenders in case the OS doesn't
        // deliver WM_MOUSELEAVE to a disabled window on this system.
        if (!s_compDisabledTooltipTracking) {
            HWND hCompBtn = GetDlgItem(hwnd, IDC_TB_COMPONENTS);
            if (hCompBtn && !IsWindowEnabled(hCompBtn)) {
                POINT ptCursor;
                GetCursorPos(&ptCursor);
                RECT rcBtn;
                GetWindowRect(hCompBtn, &rcBtn);
                if (PtInRect(&rcBtn, ptCursor)) {
                    auto it = s_locale.find(L"comp_disabled_tooltip");
                    std::wstring txt = (it != s_locale.end()) ? it->second
                        : L"Components are not available yet.\nAdd at least one file or folder on the Files page first.";
                    size_t pos = 0;
                    while ((pos = txt.find(L"\\n", pos)) != std::wstring::npos) {
                        txt.replace(pos, 2, L"\n");
                        pos += 1;
                    }
                    std::vector<TooltipEntry> entries;
                    entries.push_back({L"", txt});
                    ShowMultilingualTooltip(entries, ptCursor.x + 16, ptCursor.y + 20, hwnd);
                    s_compDisabledTooltipTracking = true;
                    // Timer callback polls every 60ms and hides tooltip when cursor leaves.
                    // Callback form fires promptly; WM_TIMER message dispatch is unreliable
                    // for disabled buttons (their WM_MOUSEMOVE/LEAVE go to parent, not button).
                    SetTimer(hwnd, IDT_COMP_TT, 60, CompTT_TimerCallback);
                }
            }
        }
        // Pin-to-Start button: tooltip when disabled (no eligible shortcuts yet).
        if (!s_scPinStartTtTracking) {
            HWND hPinSBtn = GetDlgItem(hwnd, IDC_SC_PINSTART_BTN);
            if (hPinSBtn && !IsWindowEnabled(hPinSBtn)) {
                POINT ptCursor; GetCursorPos(&ptCursor);
                RECT rcBtn;     GetWindowRect(hPinSBtn, &rcBtn);
                if (PtInRect(&rcBtn, ptCursor)) {
                    auto it = s_locale.find(L"sc_pin_add_first");
                    std::wstring txt = (it != s_locale.end()) ? it->second : L"Add shortcuts to pin first";
                    std::vector<TooltipEntry> entries;
                    entries.push_back({L"", txt});
                    ShowMultilingualTooltip(entries, ptCursor.x + 16, ptCursor.y + 20, hwnd);
                    s_scPinStartTtTracking = true;
                    SetTimer(hwnd, IDT_SC_PIN_START_TT, 60, ScPinStartTT_TimerCallback);
                }
            }
        }
        // Pin-to-Taskbar button: same pattern.
        if (!s_scPinTbTtTracking) {
            HWND hPinTBtn = GetDlgItem(hwnd, IDC_SC_PINTASKBAR_BTN);
            if (hPinTBtn && !IsWindowEnabled(hPinTBtn)) {
                POINT ptCursor; GetCursorPos(&ptCursor);
                RECT rcBtn;     GetWindowRect(hPinTBtn, &rcBtn);
                if (PtInRect(&rcBtn, ptCursor)) {
                    auto it = s_locale.find(L"sc_pin_add_first");
                    std::wstring txt = (it != s_locale.end()) ? it->second : L"Add shortcuts to pin first";
                    std::vector<TooltipEntry> entries;
                    entries.push_back({L"", txt});
                    ShowMultilingualTooltip(entries, ptCursor.x + 16, ptCursor.y + 20, hwnd);
                    s_scPinTbTtTracking = true;
                    SetTimer(hwnd, IDT_SC_PIN_TB_TT, 60, ScPinTbTT_TimerCallback);
                }
            }
        }
        return 0;
    }

    case WM_MOUSEWHEEL: {
        if (s_currentPageIndex == 2) {
            RECT rcMW; GetClientRect(hwnd, &rcMW);
            int statusH = 25;
            if (s_hStatus && IsWindow(s_hStatus)) {
                RECT rcSB; GetWindowRect(s_hStatus, &rcSB);
                statusH = rcSB.bottom - rcSB.top;
            }
            int spY   = s_toolbarHeight;
            int svH   = rcMW.bottom - spY - statusH;
            int scH   = s_scPageContentH + S(15) - spY;
            int maxSc = scH - svH;
            if (maxSc <= 0) break;
            int wdelta    = GET_WHEEL_DELTA_WPARAM(wParam);
            int scrollAmt = -wdelta * 3 * S(20) / WHEEL_DELTA;
            SCROLLINFO si = {}; si.cbSize = sizeof(si); si.fMask = SIF_POS;
            GetScrollInfo(hwnd, SB_VERT, &si);
            int newPos = std::max(0, std::min(si.nPos + scrollAmt, maxSc));
            if (newPos != si.nPos) {
                int dy = newPos - si.nPos;
                si.nPos = newPos;
                SetScrollInfo(hwnd, SB_VERT, &si, TRUE);
                // Move every page child by -dy.  SW_SCROLLCHILDREN is NOT used because
                // it skips children whose top starts outside the scroll rect (i.e. the
                // controls that start below the viewport and need to scroll into view).
                HWND hC = GetWindow(hwnd, GW_CHILD);
                while (hC) {
                    HWND hN = GetWindow(hC, GW_HWNDNEXT);
                    if (hC != s_hStatus && hC != s_hAboutButton) {
                        int cid = GetDlgCtrlID(hC);
                        bool isTB = (cid >= IDC_TB_FILES && cid <= IDC_TB_ABOUT) ||
                                     cid == IDC_TB_DIALOGS || cid == IDC_TB_COMPONENTS ||
                                     cid == IDC_TB_EXIT    || cid == IDC_TB_CLOSE_PROJECT;
                        if (!isTB) {
                            RECT rcC; GetWindowRect(hC, &rcC);
                            MapWindowPoints(HWND_DESKTOP, hwnd, (POINT*)&rcC, 2);
                            SetWindowPos(hC, NULL, rcC.left, rcC.top - dy, 0, 0,
                                SWP_NOZORDER | SWP_NOSIZE | SWP_NOACTIVATE);
                        }
                    }
                    hC = hN;
                }
                // Keep status bar pinned at the bottom and on top of all page controls.
                SetWindowPos(s_hStatus, HWND_TOP,
                    0, rcMW.bottom - statusH, rcMW.right, statusH,
                    SWP_NOACTIVATE);
                RECT pageRect = { 0, spY, rcMW.right, rcMW.bottom - statusH };
                InvalidateRect(hwnd, &pageRect, TRUE);
                UpdateWindow(s_hStatus);
                SC_SetScrollOffset(newPos);
            }
            return 0;
        }
        else if (s_currentPageIndex == 4) {
            RECT rcMW; GetClientRect(hwnd, &rcMW);
            int statusH = 25;
            if (s_hStatus && IsWindow(s_hStatus)) {
                RECT rcSB; GetWindowRect(s_hStatus, &rcSB);
                statusH = rcSB.bottom - rcSB.top;
            }
            int spY   = s_toolbarHeight;
            int svH   = rcMW.bottom - spY - statusH;
            int scH   = s_idlgPageContentH + S(15) - spY;
            int maxSc = scH - svH;
            if (maxSc <= 0) break;
            int wdelta    = GET_WHEEL_DELTA_WPARAM(wParam);
            int scrollAmt = -wdelta * 3 * S(20) / WHEEL_DELTA;
            SCROLLINFO si = {}; si.cbSize = sizeof(si); si.fMask = SIF_POS;
            GetScrollInfo(hwnd, SB_VERT, &si);
            int newPos = std::max(0, std::min(si.nPos + scrollAmt, maxSc));
            if (newPos != si.nPos) {
                int dy = newPos - si.nPos;
                si.nPos = newPos;
                SetScrollInfo(hwnd, SB_VERT, &si, TRUE);
                HWND hC = GetWindow(hwnd, GW_CHILD);
                while (hC) {
                    HWND hN = GetWindow(hC, GW_HWNDNEXT);
                    if (hC != s_hStatus && hC != s_hAboutButton) {
                        int cid = GetDlgCtrlID(hC);
                        bool isTB = (cid >= IDC_TB_FILES && cid <= IDC_TB_ABOUT) ||
                                     cid == IDC_TB_DIALOGS || cid == IDC_TB_COMPONENTS ||
                                     cid == IDC_TB_EXIT    || cid == IDC_TB_CLOSE_PROJECT;
                        if (!isTB) {
                            RECT rcC; GetWindowRect(hC, &rcC);
                            MapWindowPoints(HWND_DESKTOP, hwnd, (POINT*)&rcC, 2);
                            SetWindowPos(hC, NULL, rcC.left, rcC.top - dy, 0, 0,
                                SWP_NOZORDER | SWP_NOSIZE | SWP_NOACTIVATE);
                        }
                    }
                    hC = hN;
                }
                SetWindowPos(s_hStatus, HWND_TOP,
                    0, rcMW.bottom - statusH, rcMW.right, statusH,
                    SWP_NOACTIVATE);
                RECT pageRect = { 0, spY, rcMW.right, rcMW.bottom - statusH };
                InvalidateRect(hwnd, &pageRect, TRUE);
                UpdateWindow(s_hStatus);
                IDLG_SetScrollOffset(newPos);
            }
            return 0;
        }
        break;
    }

    case WM_VSCROLL: {
        if (s_currentPageIndex == 2) {
            RECT rcVS; GetClientRect(hwnd, &rcVS);
            int statusH = 25;
            if (s_hStatus && IsWindow(s_hStatus)) {
                RECT rcSB; GetWindowRect(s_hStatus, &rcSB);
                statusH = rcSB.bottom - rcSB.top;
            }
            int spY   = s_toolbarHeight;
            int svH   = rcVS.bottom - spY - statusH;
            int scH   = s_scPageContentH + S(15) - spY;
            int maxSc = scH - svH;
            if (maxSc <= 0) break;
            SCROLLINFO si = {}; si.cbSize = sizeof(si); si.fMask = SIF_ALL;
            GetScrollInfo(hwnd, SB_VERT, &si);
            int oldPos = si.nPos;
            int newPos = oldPos;
            switch (LOWORD(wParam)) {
            case SB_LINEUP:        newPos -= S(20); break;
            case SB_LINEDOWN:      newPos += S(20); break;
            case SB_PAGEUP:        newPos -= svH;   break;
            case SB_PAGEDOWN:      newPos += svH;   break;
            case SB_TOP:           newPos  = 0;     break;
            case SB_BOTTOM:        newPos  = maxSc; break;
            case SB_THUMBTRACK:
            case SB_THUMBPOSITION: {
                SCROLLINFO si2 = {}; si2.cbSize = sizeof(si2);
                si2.fMask = SIF_TRACKPOS;
                GetScrollInfo(hwnd, SB_VERT, &si2);
                newPos = si2.nTrackPos;
                break;
            }
            }
            newPos = std::max(0, std::min(newPos, maxSc));
            if (newPos != oldPos) {
                int dy   = newPos - oldPos;
                si.fMask = SIF_POS; si.nPos = newPos;
                SetScrollInfo(hwnd, SB_VERT, &si, TRUE);
                // Move every page child by -dy (same reasoning as WM_MOUSEWHEEL).
                HWND hC = GetWindow(hwnd, GW_CHILD);
                while (hC) {
                    HWND hN = GetWindow(hC, GW_HWNDNEXT);
                    if (hC != s_hStatus && hC != s_hAboutButton) {
                        int cid = GetDlgCtrlID(hC);
                        bool isTB = (cid >= IDC_TB_FILES && cid <= IDC_TB_ABOUT) ||
                                     cid == IDC_TB_DIALOGS || cid == IDC_TB_COMPONENTS ||
                                     cid == IDC_TB_EXIT    || cid == IDC_TB_CLOSE_PROJECT;
                        if (!isTB) {
                            RECT rcC; GetWindowRect(hC, &rcC);
                            MapWindowPoints(HWND_DESKTOP, hwnd, (POINT*)&rcC, 2);
                            SetWindowPos(hC, NULL, rcC.left, rcC.top - dy, 0, 0,
                                SWP_NOZORDER | SWP_NOSIZE | SWP_NOACTIVATE);
                        }
                    }
                    hC = hN;
                }
                // Keep status bar pinned at the bottom and on top of all page controls.
                SetWindowPos(s_hStatus, HWND_TOP,
                    0, rcVS.bottom - statusH, rcVS.right, statusH,
                    SWP_NOACTIVATE);
                RECT pageRect = { 0, spY, rcVS.right, rcVS.bottom - statusH };
                InvalidateRect(hwnd, &pageRect, TRUE);
                UpdateWindow(s_hStatus);
                SC_SetScrollOffset(newPos);
            }
        }
        else if (s_currentPageIndex == 4) {
            RECT rcVS; GetClientRect(hwnd, &rcVS);
            int statusH = 25;
            if (s_hStatus && IsWindow(s_hStatus)) {
                RECT rcSB; GetWindowRect(s_hStatus, &rcSB);
                statusH = rcSB.bottom - rcSB.top;
            }
            int spY   = s_toolbarHeight;
            int svH   = rcVS.bottom - spY - statusH;
            int scH   = s_idlgPageContentH + S(15) - spY;
            int maxSc = scH - svH;
            if (maxSc <= 0) break;
            SCROLLINFO si = {}; si.cbSize = sizeof(si); si.fMask = SIF_ALL;
            GetScrollInfo(hwnd, SB_VERT, &si);
            int oldPos = si.nPos;
            int newPos = oldPos;
            switch (LOWORD(wParam)) {
            case SB_LINEUP:        newPos -= S(20); break;
            case SB_LINEDOWN:      newPos += S(20); break;
            case SB_PAGEUP:        newPos -= svH;   break;
            case SB_PAGEDOWN:      newPos += svH;   break;
            case SB_TOP:           newPos  = 0;     break;
            case SB_BOTTOM:        newPos  = maxSc; break;
            case SB_THUMBTRACK:
            case SB_THUMBPOSITION: {
                SCROLLINFO si2 = {}; si2.cbSize = sizeof(si2);
                si2.fMask = SIF_TRACKPOS;
                GetScrollInfo(hwnd, SB_VERT, &si2);
                newPos = si2.nTrackPos;
                break;
            }
            }
            newPos = std::max(0, std::min(newPos, maxSc));
            if (newPos != oldPos) {
                int dy   = newPos - oldPos;
                si.fMask = SIF_POS; si.nPos = newPos;
                SetScrollInfo(hwnd, SB_VERT, &si, TRUE);
                HWND hC = GetWindow(hwnd, GW_CHILD);
                while (hC) {
                    HWND hN = GetWindow(hC, GW_HWNDNEXT);
                    if (hC != s_hStatus && hC != s_hAboutButton) {
                        int cid = GetDlgCtrlID(hC);
                        bool isTB = (cid >= IDC_TB_FILES && cid <= IDC_TB_ABOUT) ||
                                     cid == IDC_TB_DIALOGS || cid == IDC_TB_COMPONENTS ||
                                     cid == IDC_TB_EXIT    || cid == IDC_TB_CLOSE_PROJECT;
                        if (!isTB) {
                            RECT rcC; GetWindowRect(hC, &rcC);
                            MapWindowPoints(HWND_DESKTOP, hwnd, (POINT*)&rcC, 2);
                            SetWindowPos(hC, NULL, rcC.left, rcC.top - dy, 0, 0,
                                SWP_NOZORDER | SWP_NOSIZE | SWP_NOACTIVATE);
                        }
                    }
                    hC = hN;
                }
                SetWindowPos(s_hStatus, HWND_TOP,
                    0, rcVS.bottom - statusH, rcVS.right, statusH,
                    SWP_NOACTIVATE);
                RECT pageRect = { 0, spY, rcVS.right, rcVS.bottom - statusH };
                InvalidateRect(hwnd, &pageRect, TRUE);
                UpdateWindow(s_hStatus);
                IDLG_SetScrollOffset(newPos);
            }
        }
        break;
    }

    case WM_TIMER:
        // IDT_COMP_TT / IDT_SC_PIN_START_TT / IDT_SC_PIN_TB_TT are handled via
        // callback form of SetTimer (not WM_TIMER dispatch), not here.
        break;
    
    case WM_ACTIVATE:
        // Hide tooltip when window loses focus
        if (LOWORD(wParam) == WA_INACTIVE) {
            HideTooltip();
            s_aboutMouseTracking = false;
            s_warningTooltipTracking = false;
            if (s_compDisabledTooltipTracking) {
                KillTimer(hwnd, IDT_COMP_TT);
                s_compDisabledTooltipTracking = false;
            }
            if (s_scPinStartTtTracking) { KillTimer(hwnd, IDT_SC_PIN_START_TT); s_scPinStartTtTracking = false; }
            if (s_scPinTbTtTracking)   { KillTimer(hwnd, IDT_SC_PIN_TB_TT);    s_scPinTbTtTracking   = false; }
        }
        break;
    
    case WM_KILLFOCUS:
        // Hide tooltip when window loses keyboard focus
        HideTooltip();
        s_aboutMouseTracking = false;
        if (s_compDisabledTooltipTracking) {
            KillTimer(hwnd, IDT_COMP_TT);
            s_compDisabledTooltipTracking = false;
        }
        if (s_scPinStartTtTracking) { KillTimer(hwnd, IDT_SC_PIN_START_TT); s_scPinStartTtTracking = false; }
        if (s_scPinTbTtTracking)   { KillTimer(hwnd, IDT_SC_PIN_TB_TT);    s_scPinTbTtTracking   = false; }
        break;

    case WM_CAPTURECHANGED:
        DragDrop_OnCaptureChanged((HWND)lParam);
        break;

    case WM_MOUSELEAVE:
        // Guard: only hide if cursor is not still over the about icon (§9 of tooltip API).
        {
            POINT ptCursor;
            GetCursorPos(&ptCursor);
            bool overIcon = false;
            if (s_hAboutButton) {
                RECT rc;
                GetWindowRect(s_hAboutButton, &rc);
                if (PtInRect(&rc, ptCursor)) overIcon = true;
            }
            if (!overIcon) {
                HideTooltip();
                s_currentTooltipIcon = NULL;
            }
        }
        s_aboutMouseTracking = false;
        s_mouseTracking = false;
        s_warningTooltipTracking = false;
        if (s_compDisabledTooltipTracking) {
            KillTimer(hwnd, IDT_COMP_TT);
            s_compDisabledTooltipTracking = false;
        }
        if (s_scPinStartTtTracking) { KillTimer(hwnd, IDT_SC_PIN_START_TT); s_scPinStartTtTracking = false; }
        if (s_scPinTbTtTracking)   { KillTimer(hwnd, IDT_SC_PIN_TB_TT);    s_scPinTbTtTracking   = false; }
        return 0;
    
    case WM_DESTROY:
        // Clean up drag-and-drop module
        DragDrop_Unregister();
        // Clean up tooltip system
        CleanupTooltipSystem();
        // Don't call PostQuitMessage here - just close the window
        // The entry screen will be shown again
        return 0;
    }
    
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

void MainWindow::ShowTooltip(HWND hwnd, int x, int y, const std::wstring &text) {
    if (!s_hTooltip || !IsWindow(s_hTooltip)) {
        // Create tooltip window
        s_hTooltip = CreateWindowExW(
            WS_EX_TOPMOST | WS_EX_TOOLWINDOW,
            L"STATIC",
            text.c_str(),
            WS_POPUP | WS_BORDER | SS_CENTER,
            x, y, 200, 40,
            hwnd,
            NULL,
            (HINSTANCE)GetWindowLongPtr(hwnd, GWLP_HINSTANCE),
            NULL
        );
        
        if (s_hTooltip) {
            // Set tooltip background color
            SetClassLongPtr(s_hTooltip, GCLP_HBRBACKGROUND, (LONG_PTR)GetStockObject(WHITE_BRUSH));
            
            // Set font
            HFONT hFont = CreateFontW(-12, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
            if (hFont) SendMessageW(s_hTooltip, WM_SETFONT, (WPARAM)hFont, TRUE);
            
            ShowWindow(s_hTooltip, SW_SHOW);
        }
    } else {
        // Update existing tooltip
        SetWindowTextW(s_hTooltip, text.c_str());
        SetWindowPos(s_hTooltip, HWND_TOPMOST, x, y, 0, 0, SWP_NOSIZE | SWP_SHOWWINDOW);
    }
}

void MainWindow::HideTooltip() {
    if (s_hTooltip && IsWindow(s_hTooltip)) {
        ShowWindow(s_hTooltip, SW_HIDE);
    }
}
