#include "mainwindow.h"
#include <commctrl.h>
#include <shlwapi.h>
#include <shlobj.h>
#include <shellapi.h>
#include <windowsx.h>
#include <functional>
#include <vector>
#include "ctrlw.h"
#include "button.h"
#include "spinner_dialog.h"
#include "about.h"
#include "tooltip.h"
#include "about_icon.h"

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
int MainWindow::s_toolbarHeight = 50;
int MainWindow::s_currentPageIndex = 0;
static bool s_hasUnsavedChanges = false;
static bool s_isNewUnsavedProject = false;
static HTREEITEM s_rightClickedItem = NULL; // Track which TreeView item was right-clicked
static int s_rightClickedRegIndex = -1; // Track which ListView item was right-clicked
static bool s_projectNameManuallySet = false; // Track if user manually edited project name
static bool s_updatingProjectNameProgrammatically = false; // Prevent EN_CHANGE during programmatic updates
static HWND s_hAboutButton = NULL; // Track About button for tooltip
static bool s_aboutMouseTracking = false; // Track mouse for About button tooltip
// Mirror entry-screen tooltip tracking state
static HWND s_currentTooltipIcon = NULL; // Which icon currently has the tooltip shown
static bool s_mouseTracking = false; // General mouse tracking flag for tooltip

// Store files added to virtual folders (keyed by HTREEITEM)
struct VirtualFolderFile {
    std::wstring sourcePath;
    std::wstring destination;
};
static std::map<HTREEITEM, std::vector<VirtualFolderFile>> s_virtualFolderFiles;

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
static HWND s_hWarningTooltip = NULL;
static bool s_warningTooltipTracking = false;
static std::wstring s_warningTooltipText;
static HFONT s_warningTooltipFont = NULL;

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

// Files dialog button IDs
#define IDC_FILES_ADD_DIR   5020
#define IDC_FILES_ADD_FILES 5021
#define IDC_FILES_DLG       5022
#define IDC_BROWSE_INSTALL_DIR 5023
#define IDC_FILES_REMOVE    5024
#define IDC_PROJECT_NAME    5025
#define IDC_INSTALL_FOLDER  5026

// Context menu IDs
#define IDM_TREEVIEW_ADD_FOLDER 5030
#define IDM_TREEVIEW_REMOVE_FOLDER 5031

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

const wchar_t WARNING_TOOLTIP_CLASS_NAME[] = L"WarningTooltipClass";

// Warning tooltip window procedure
LRESULT CALLBACK WarningTooltipWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);
        if (s_warningTooltipFont) SelectObject(hdc, s_warningTooltipFont);
        RECT rc;
        GetClientRect(hwnd, &rc);
        SetBkMode(hdc, TRANSPARENT);
        DrawTextW(hdc, s_warningTooltipText.c_str(), -1, &rc, DT_WORDBREAK);
        EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_NCDESTROY:
        return 0;
    default:
        return DefWindowProcW(hwnd, msg, wParam, lParam);
    }
}

HWND MainWindow::Create(HINSTANCE hInstance, const ProjectRow &project, const std::map<std::wstring, std::wstring> &locale) {
    s_currentProject = project;
    s_locale = locale;
    
    // Register tooltip window class
    static bool tooltipClassRegistered = false;
    if (!tooltipClassRegistered) {
        WNDCLASSEXW wcTooltip = { };
        wcTooltip.cbSize = sizeof(WNDCLASSEXW);
        wcTooltip.lpfnWndProc = WarningTooltipWndProc;
        wcTooltip.hInstance = hInstance;
        wcTooltip.lpszClassName = WARNING_TOOLTIP_CLASS_NAME;
        wcTooltip.hbrBackground = (HBRUSH)(COLOR_INFOBK + 1);
        wcTooltip.hCursor = LoadCursorW(NULL, IDC_ARROW);
        RegisterClassExW(&wcTooltip);
        tooltipClassRegistered = true;
    }
    
    // Create tooltip font (same as globe tooltip)
    if (!s_warningTooltipFont) {
        s_warningTooltipFont = CreateFontW(-14, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                                    DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                    CLEARTYPE_QUALITY, VARIABLE_PITCH | FF_SWISS, L"Segoe UI");
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
    int width = 1024;
    int height = 691;
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
    
    return Create(hInstance, tempProject, locale);
}

void MainWindow::MarkAsModified() {
    s_hasUnsavedChanges = true;
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
    const int buttonWidth = 95;  // Optimized width for toolbar buttons
    const int buttonHeight = 40;
    const int buttonGap = 5;
    const int startX = 10;
    const int startY = 5;
    
    int x = startX;
    
    // Files button (opens file structure dialog)
    auto itFiles = s_locale.find(L"tb_files");
    std::wstring filesText = (itFiles != s_locale.end()) ? itFiles->second : L"Files";
    CreateCustomButtonWithIcon(hwnd, IDC_TB_FILES, filesText, ButtonColor::Blue,
        L"shell32.dll", 4, x, startY, buttonWidth, buttonHeight, hInst);
    x += buttonWidth + buttonGap;
    
    // Add Registry button
    auto itAddReg = s_locale.find(L"tb_add_registry");
    std::wstring addRegText = (itAddReg != s_locale.end()) ? itAddReg->second : L"Registry";
    CreateCustomButtonWithIcon(hwnd, IDC_TB_ADD_REGISTRY, addRegText, ButtonColor::Blue,
        L"shell32.dll", 166, x, startY, buttonWidth, buttonHeight, hInst);
    x += buttonWidth + buttonGap;
    
    // Add Shortcut button (composite icon: 257 with 29 overlay)
    auto itAddShortcut = s_locale.find(L"tb_add_shortcut");
    std::wstring addShortcutText = (itAddShortcut != s_locale.end()) ? itAddShortcut->second : L"Shortcuts";
    CreateCustomButtonWithCompositeIcon(hwnd, IDC_TB_ADD_SHORTCUT, addShortcutText, ButtonColor::Blue,
        L"shell32.dll", 257, L"shell32.dll", 29, x, startY, buttonWidth, buttonHeight, hInst);
    x += buttonWidth + buttonGap;
    
    // Add Dependency button (wider to fit text)
    auto itAddDep = s_locale.find(L"tb_add_dependency");
    std::wstring addDepText = (itAddDep != s_locale.end()) ? itAddDep->second : L"Dependencies";
    CreateCustomButtonWithIcon(hwnd, IDC_TB_ADD_DEPEND, addDepText, ButtonColor::Blue,
        L"shell32.dll", 278, x, startY, 125, buttonHeight, hInst);
    x += 125 + buttonGap;
    
    // Settings button
    auto itSettings = s_locale.find(L"tb_settings");
    std::wstring settingsText = (itSettings != s_locale.end()) ? itSettings->second : L"Settings";
    CreateCustomButtonWithIcon(hwnd, IDC_TB_SETTINGS, settingsText, ButtonColor::Blue,
        L"shell32.dll", 314, x, startY, buttonWidth, buttonHeight, hInst);
    x += buttonWidth + buttonGap;
    
    // Build button
    auto itBuild = s_locale.find(L"tb_build");
    std::wstring buildText = (itBuild != s_locale.end()) ? itBuild->second : L"Build (F7)";
    CreateCustomButtonWithIcon(hwnd, IDC_TB_BUILD, buildText, ButtonColor::Green,
        L"shell32.dll", 80, x, startY, buttonWidth, buttonHeight, hInst);
    x += buttonWidth + buttonGap;
    
    // Test button
    auto itTest = s_locale.find(L"tb_test");
    std::wstring testText = (itTest != s_locale.end()) ? itTest->second : L"Test (F5)";
    CreateCustomButtonWithIcon(hwnd, IDC_TB_TEST, testText, ButtonColor::Blue,
        L"shell32.dll", 138, x, startY, buttonWidth, buttonHeight, hInst);
    x += buttonWidth + buttonGap;
    
    // Scripts button
    auto itScripts = s_locale.find(L"tb_scripts");
    std::wstring scriptsText = (itScripts != s_locale.end()) ? itScripts->second : L"Scripts";
    CreateCustomButtonWithIcon(hwnd, IDC_TB_SCRIPTS, scriptsText, ButtonColor::Blue,
        L"shell32.dll", 310, x, startY, buttonWidth, buttonHeight, hInst);
    x += buttonWidth + buttonGap;
    
    // Save button
    auto itSave = s_locale.find(L"tb_save");
    std::wstring saveText = (itSave != s_locale.end()) ? itSave->second : L"Save";
    CreateCustomButtonWithIcon(hwnd, IDC_TB_SAVE, saveText, ButtonColor::Green,
        L"shell32.dll", 258, x, startY, buttonWidth, buttonHeight, hInst);  // Icon #258 is floppy disk save icon
    x += buttonWidth + buttonGap;
    
    // About icon (static control with custom tooltip)
    const int aboutIconSize = 32;
    const int aboutIconY = startY + (buttonHeight - aboutIconSize) / 2;  // Center vertically with buttons
    // Create About icon using about_icon module
    s_hAboutButton = CreateAboutIconControl(hwnd, hInst, x, aboutIconY, aboutIconSize, IDC_TB_ABOUT, s_locale);
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


void MainWindow::SwitchPage(HWND hwnd, int pageIndex) {
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
        5100, 5101, 5102, 5103, 5104, 5105, 5106, 5107, 5108, 5109, 5110 // Labels and other static controls
    };
    
    for (int id : controlIds) {
        HWND hCtrl = GetDlgItem(hwnd, id);
        if (hCtrl) {
            DestroyWindow(hCtrl);
        }
    }
    
    // Destroy warning tooltip if it exists
    if (s_hWarningTooltip) {
        DestroyWindow(s_hWarningTooltip);
        s_hWarningTooltip = NULL;
        s_warningTooltipTracking = false;
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
            if (childId < IDC_TB_FILES || childId > IDC_TB_ABOUT) {
                if (hChild != s_hTreeView && hChild != s_hListView && 
                    hChild != s_hRegTreeView && hChild != s_hRegListView &&
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
    s_hTreeView = NULL;
    s_hListView = NULL;
    s_hProgramFilesRoot = NULL;
    s_hRegTreeView = NULL;
    s_hRegListView = NULL;
    
    // Clear registry values map
    s_registryValues.clear();
    
    // Hide About tooltip when switching pages
    HideTooltip();
    s_aboutMouseTracking = false;
    
    // Force complete window redraw
    RedrawWindow(hwnd, NULL, NULL, RDW_ERASE | RDW_INVALIDATE | RDW_ALLCHILDREN);
    
    s_currentPageIndex = pageIndex;
    
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
        HWND hTitle = CreateWindowExW(0, L"STATIC", L"Files Management",
            WS_CHILD | WS_VISIBLE | SS_LEFT,
            20, pageY + 15, rc.right - 40, 30,
            hwnd, (HMENU)5100, hInst, NULL); // Give it an ID for WM_CTLCOLORSTATIC
        HFONT hTitleFont = CreateFontW(-18, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
        if (hTitleFont) SendMessageW(hTitle, WM_SETFONT, (WPARAM)hTitleFont, TRUE);
        
        // Add Folder button (child of main window, positioned relative to it)
        s_hPageButton1 = CreateCustomButtonWithIcon(hwnd, IDC_FILES_ADD_DIR, L"Add Folder", ButtonColor::Blue,
            L"shell32.dll", 296, 20, s_toolbarHeight + 55, 120, 35, hInst);
        
        // Add Files button (child of main window, positioned relative to it)
        s_hPageButton2 = CreateCustomButtonWithIcon(hwnd, IDC_FILES_ADD_FILES, L"Add Files", ButtonColor::Blue,
            L"shell32.dll", 71, 150, s_toolbarHeight + 55, 120, 35, hInst);
        
        // Remove button (for removing selected items)
        HWND hRemoveBtn = CreateCustomButtonWithIcon(hwnd, IDC_FILES_REMOVE, L"Remove", ButtonColor::Red,
            L"shell32.dll", 234, 20, s_toolbarHeight + 100, 250, 35, hInst);
        
        // Project name label and field
        HWND hProjectLabel = CreateWindowExW(0, L"STATIC", L"Project name:",
            WS_CHILD | WS_VISIBLE | SS_LEFT | SS_CENTERIMAGE,
            290, pageY + 55, 100, 22,
            hwnd, NULL, hInst, NULL);
        
        // Set programmatic flag to prevent EN_CHANGE from marking as manually edited
        s_updatingProjectNameProgrammatically = true;
        HWND hProjectEdit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", s_currentProject.name.c_str(),
            WS_CHILD | WS_VISIBLE | WS_BORDER | ES_LEFT | ES_AUTOHSCROLL,
            395, pageY + 55, rc.right - 460, 22,
            hwnd, (HMENU)IDC_PROJECT_NAME, hInst, NULL);
        s_updatingProjectNameProgrammatically = false;
        
        // Install folder label and field (aligned with project name field)
        HWND hInstallLabel = CreateWindowExW(0, L"STATIC", L"Install folder:",
            WS_CHILD | WS_VISIBLE | SS_LEFT | SS_CENTERIMAGE,
            290, pageY + 82, 100, 22,
            hwnd, NULL, hInst, NULL);
        
        std::wstring defaultPath = L"C:\\Program Files\\" + s_currentProject.name;
        HWND hInstallEdit = CreateWindowExW(0, L"STATIC", defaultPath.c_str(),
            WS_CHILD | WS_VISIBLE | SS_LEFT | SS_CENTERIMAGE,
            395, pageY + 82, rc.right - 460, 22,
            hwnd, (HMENU)IDC_INSTALL_FOLDER, hInst, NULL);
        
        // Browse button for install folder (aligned with install folder field)
        CreateCustomButtonWithIcon(hwnd, IDC_BROWSE_INSTALL_DIR, L"...", ButtonColor::Blue,
            L"shell32.dll", 4, rc.right - 55, s_toolbarHeight + 82, 35, 22, hInst);
        
        // Calculate split pane dimensions (TreeView 30%, ListView 70%)
        int viewTop = 150;  // Moved down to make room for Remove button
        int viewHeight = pageHeight - 160;
        int treeWidth = (int)((rc.right - 50) * 0.3);
        int listWidth = (rc.right - 50) - treeWidth - 5; // 5px gap
        
        // TreeView on the left (folder hierarchy) - child of main window to receive notifications
        s_hTreeView = CreateWindowExW(WS_EX_CLIENTEDGE, WC_TREEVIEW, NULL,
            WS_CHILD | WS_VISIBLE | WS_BORDER | TVS_HASLINES | TVS_HASBUTTONS | TVS_LINESATROOT | TVS_SHOWSELALWAYS | TVS_CHECKBOXES | TVS_EDITLABELS,
            20, s_toolbarHeight + viewTop, treeWidth, viewHeight,
            hwnd, (HMENU)102, hInst, NULL);
        
        // Set indent width to make hierarchy more visible
        TreeView_SetIndent(s_hTreeView, 19);
        
        // Create image list for folder icons
        HIMAGELIST hImageList = ImageList_Create(16, 16, ILC_COLOR32 | ILC_MASK, 2, 2);
        if (hImageList) {
            // Load folder icons from shell32.dll
            HMODULE hShell32 = LoadLibraryW(L"shell32.dll");
            if (hShell32) {
                HICON hFolderClosed = (HICON)LoadImageW(hShell32, MAKEINTRESOURCEW(4), IMAGE_ICON, 16, 16, 0);
                HICON hFolderOpen = (HICON)LoadImageW(hShell32, MAKEINTRESOURCEW(5), IMAGE_ICON, 16, 16, 0);
                if (hFolderClosed) ImageList_AddIcon(hImageList, hFolderClosed);
                if (hFolderOpen) ImageList_AddIcon(hImageList, hFolderOpen);
                if (hFolderClosed) DestroyIcon(hFolderClosed);
                if (hFolderOpen) DestroyIcon(hFolderOpen);
                FreeLibrary(hShell32);
            }
            TreeView_SetImageList(s_hTreeView, hImageList, TVSIL_NORMAL);
        }
        
        // ListView on the right (current folder contents - files only) - child of main window
        s_hListView = CreateWindowExW(WS_EX_CLIENTEDGE, WC_LISTVIEW, NULL,
            WS_CHILD | WS_VISIBLE | LVS_REPORT | LVS_SHAREIMAGELISTS | WS_BORDER,
            20 + treeWidth + 5, s_toolbarHeight + viewTop, listWidth, viewHeight,
            hwnd, (HMENU)100, hInst, NULL);
        
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
        
        // Always create "Program Files" root node
        std::wstring defaultInstallPath = L"C:\\Program Files\\" + s_currentProject.name;
        std::wstring installPathCopy = defaultInstallPath;
        size_t lastSlash = installPathCopy.find_last_of(L"\\/");
        std::wstring parentPath = L"Program Files";
        if (lastSlash != std::wstring::npos) {
            std::wstring temp = installPathCopy.substr(0, lastSlash);
            size_t secondLastSlash = temp.find_last_of(L"\\/");
            if (secondLastSlash != std::wstring::npos) {
                parentPath = temp.substr(secondLastSlash + 1);
            }
        }
        s_hProgramFilesRoot = AddTreeNode(s_hTreeView, TVI_ROOT, parentPath, L"");
        TreeView_Expand(s_hTreeView, s_hProgramFilesRoot, TVE_EXPAND);
        
        // Populate TreeView with source folder structure if available
        if (!s_currentProject.directory.empty()) {
            PopulateTreeView(s_hTreeView, s_currentProject.directory, defaultPath);
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
            20, pageY + 15, rc.right - 40, 30,
            hwnd, (HMENU)5100, hInst, NULL);
        HFONT hTitleFont = CreateFontW(-18, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
        if (hTitleFont) SendMessageW(hTitle, WM_SETFONT, (WPARAM)hTitleFont, TRUE);
        
        int currentY = pageY + 55;
        
        // Checkbox for "Register in Windows Installed Programs"
        HWND hCheckbox = CreateWindowExW(0, L"BUTTON", regRegister.c_str(),
            WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
            20, currentY, 400, 25,
            hwnd, (HMENU)IDC_REG_CHECKBOX, hInst, NULL);
        SendMessageW(hCheckbox, BM_SETCHECK, s_registerInWindows ? BST_CHECKED : BST_UNCHECKED, 0);
        currentY += 35;
        
        // Layout: Icon + Buttons on left, Fields on right
        // Icon preview area (48x48 static control with border)
        HWND hIconPreview = CreateWindowExW(WS_EX_CLIENTEDGE, L"STATIC", L"",
            WS_CHILD | WS_VISIBLE | SS_ICON | SS_CENTERIMAGE,
            20, currentY, 48, 48,
            hwnd, (HMENU)IDC_REG_ICON_PREVIEW, hInst, NULL);
        
        // Load default generic icon from shell32.dll (icon #2 - generic file icon)
        if (hIconPreview) {
            HICON hDefaultIcon = ExtractIconW(hInst, L"shell32.dll", 2);
            if (hDefaultIcon) {
                SendMessageW(hIconPreview, STM_SETICON, (WPARAM)hDefaultIcon, 0);
            }
        }
        
        // Add Icon button (with shell32.dll icon #127)
        s_hPageButton1 = CreateCustomButtonWithIcon(hwnd, IDC_REG_ADD_ICON, addIcon.c_str(), ButtonColor::Blue,
            L"shell32.dll", 127, 80, currentY, 120, 30, hInst);
        
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
        
        CreateCustomButtonWithIcon(hwnd, IDC_REG_SHOW_REGKEY, showRegkeyText.c_str(), ButtonColor::Blue,
            L"shell32.dll", 268, 80, currentY + 35, 120, 30, hInst);
        
        // Display Name field (right side, aligned with icon)
        CreateWindowExW(0, L"STATIC", displayName.c_str(),
            WS_CHILD | WS_VISIBLE | SS_LEFT | SS_CENTERIMAGE,
            220, currentY, 100, 22,
            hwnd, (HMENU)5101, hInst, NULL);
        
        HWND hDisplayNameEdit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", s_currentProject.name.c_str(),
            WS_CHILD | WS_VISIBLE | WS_BORDER | ES_LEFT | ES_AUTOHSCROLL,
            325, currentY, 300, 22,
            hwnd, (HMENU)IDC_REG_DISPLAY_NAME, hInst, NULL);
        currentY += 27;
        
        // Version field
        CreateWindowExW(0, L"STATIC", versionText.c_str(),
            WS_CHILD | WS_VISIBLE | SS_LEFT | SS_CENTERIMAGE,
            220, currentY, 100, 22,
            hwnd, (HMENU)5102, hInst, NULL);
        
        HWND hVersionEdit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", s_currentProject.version.c_str(),
            WS_CHILD | WS_VISIBLE | WS_BORDER | ES_LEFT | ES_AUTOHSCROLL,
            325, currentY, 300, 22,
            hwnd, (HMENU)IDC_REG_VERSION, hInst, NULL);
        currentY += 27;
        
        // Publisher field
        CreateWindowExW(0, L"STATIC", publisherText.c_str(),
            WS_CHILD | WS_VISIBLE | SS_LEFT | SS_CENTERIMAGE,
            220, currentY, 100, 22,
            hwnd, (HMENU)5103, hInst, NULL);
        
        HWND hPublisherEdit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", s_appPublisher.c_str(),
            WS_CHILD | WS_VISIBLE | WS_BORDER | ES_LEFT | ES_AUTOHSCROLL,
            325, currentY, 300, 22,
            hwnd, (HMENU)IDC_REG_PUBLISHER, hInst, NULL);
        currentY += 40;
        
        // Divider line
        CreateWindowExW(0, L"STATIC", L"",
            WS_CHILD | WS_VISIBLE | SS_ETCHEDHORZ,
            20, currentY, rc.right - 40, 2,
            hwnd, (HMENU)5104, hInst, NULL);
        currentY += 20;
        
        // Custom Registry Entries section with warning icon and backup button
        CreateWindowExW(0, L"STATIC", L"Custom Registry Entries",
            WS_CHILD | WS_VISIBLE | SS_LEFT,
            20, currentY, 300, 20,
            hwnd, (HMENU)5105, hInst, NULL);
        
        // Backup Registry button (icon 238 from shell32.dll) - Create Restore Point
        auto itBackup = s_locale.find(L"reg_backup");
        std::wstring backupText = (itBackup != s_locale.end()) ? itBackup->second : L"Create Restore Point";
        
        CreateCustomButtonWithIcon(hwnd, IDC_REG_BACKUP, backupText.c_str(), ButtonColor::Green,
            L"shell32.dll", 238, rc.right - 190, currentY, 170, 40, hInst);
        
        // Warning icon (imageres.dll icon 244) - positioned just to left of backup button
        HWND hWarningIcon = CreateWindowExW(0, L"STATIC", L"",
            WS_CHILD | WS_VISIBLE | SS_ICON,
            rc.right - 230, currentY + 4, 32, 32,
            hwnd, (HMENU)IDC_REG_WARNING_ICON, hInst, NULL);
        
        // Load and set warning icon from imageres.dll
        HICON hWarnIcon = NULL;
        HMODULE hImageres = LoadLibraryW(L"imageres.dll");
        if (hImageres) {
            // Try loading with LoadImageW for better size control
            hWarnIcon = (HICON)LoadImageW(hImageres, MAKEINTRESOURCEW(244), IMAGE_ICON, 24, 24, LR_DEFAULTCOLOR);
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
        
        currentY += 48;
        
        // Split pane: TreeView (left 40%) + ListView (right 60%)
        int treeWidth = (int)((rc.right - 40) * 0.4);
        int listWidth = (rc.right - 40) - treeWidth - 10;
        int paneHeight = rc.bottom - currentY - 90; // Space for bottom buttons
        
        // TreeView for registry hive structure with horizontal scrolling
        s_hRegTreeView = CreateWindowExW(WS_EX_CLIENTEDGE, WC_TREEVIEW, L"",
            WS_CHILD | WS_VISIBLE | WS_BORDER | TVS_HASLINES | TVS_HASBUTTONS | TVS_LINESATROOT | TVS_SHOWSELALWAYS | WS_VSCROLL | WS_HSCROLL,
            20, currentY, treeWidth, paneHeight,
            hwnd, (HMENU)IDC_REG_TREEVIEW, hInst, NULL);
        
        // Populate TreeView with template registry structure
        CreateTemplateRegistryTree(s_hRegTreeView, s_currentProject.name, s_appPublisher, 
                                   s_currentProject.version, s_currentProject.directory, s_appIconPath);
        
        // ListView for registry entries with horizontal scrolling
        s_hRegListView = CreateWindowExW(WS_EX_CLIENTEDGE, WC_LISTVIEW, L"",
            WS_CHILD | WS_VISIBLE | WS_BORDER | LVS_REPORT | LVS_SINGLESEL | WS_VSCROLL | WS_HSCROLL,
            30 + treeWidth, currentY, listWidth, paneHeight,
            hwnd, (HMENU)IDC_REG_LISTVIEW, hInst, NULL);
        
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
        
        currentY += paneHeight + 10;
        
        // Buttons: Add Key, Add Value, Edit, Delete
        s_hPageButton2 = CreateCustomButtonWithIcon(hwnd, IDC_REG_ADD_KEY, addKeyText.c_str(), ButtonColor::Blue,
            L"shell32.dll", 4, 20, currentY, 110, 35, hInst);
        
        CreateCustomButtonWithIcon(hwnd, IDC_REG_ADD_VALUE, addValueText.c_str(), ButtonColor::Blue,
            L"shell32.dll", 70, 140, currentY, 110, 35, hInst);
        
        CreateCustomButtonWithIcon(hwnd, IDC_REG_EDIT, editText.c_str(), ButtonColor::Blue,
            L"shell32.dll", 269, 260, currentY, 110, 35, hInst);
        
        CreateCustomButtonWithIcon(hwnd, IDC_REG_REMOVE, removeText.c_str(), ButtonColor::Red,
            L"shell32.dll", 234, 380, currentY, 110, 35, hInst);
        
        // Create tooltip for warning icon - removed (using custom tooltip instead)
        
        break;
    }
    case 2: // Shortcuts page
    {
        // Create container for page content
        s_hCurrentPage = CreateWindowExW(
            0, L"STATIC", L"",
            WS_CHILD | WS_VISIBLE,
            0, pageY, rc.right, pageHeight,
            hwnd, NULL, hInst, NULL);
        
        HWND hTitle = CreateWindowExW(0, L"STATIC", L"Shortcuts",
            WS_CHILD | WS_VISIBLE | SS_LEFT,
            20, 20, rc.right - 40, 30,
            s_hCurrentPage, NULL, hInst, NULL);
        HFONT hTitleFont = CreateFontW(-18, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
        if (hTitleFont) SendMessageW(hTitle, WM_SETFONT, (WPARAM)hTitleFont, TRUE);
        
        CreateWindowExW(0, L"STATIC", L"Shortcuts configuration to be implemented",
            WS_CHILD | WS_VISIBLE | SS_LEFT,
            20, 60, rc.right - 40, 20,
            s_hCurrentPage, NULL, hInst, NULL);
        break;
    }
    case 3: // Dependencies page
    {
        // Create container for page content
        s_hCurrentPage = CreateWindowExW(
            0, L"STATIC", L"",
            WS_CHILD | WS_VISIBLE,
            0, pageY, rc.right, pageHeight,
            hwnd, NULL, hInst, NULL);
        
        HWND hTitle = CreateWindowExW(0, L"STATIC", L"Dependencies",
            WS_CHILD | WS_VISIBLE | SS_LEFT,
            20, 20, rc.right - 40, 30,
            s_hCurrentPage, NULL, hInst, NULL);
        HFONT hTitleFont = CreateFontW(-18, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
        if (hTitleFont) SendMessageW(hTitle, WM_SETFONT, (WPARAM)hTitleFont, TRUE);
        
        CreateWindowExW(0, L"STATIC", L"Dependencies management to be implemented",
            WS_CHILD | WS_VISIBLE | SS_LEFT,
            20, 60, rc.right - 40, 20,
            s_hCurrentPage, NULL, hInst, NULL);
        break;
    }
    case 4: // Settings page
    {
        // Create container for page content
        s_hCurrentPage = CreateWindowExW(
            0, L"STATIC", L"",
            WS_CHILD | WS_VISIBLE,
            0, pageY, rc.right, pageHeight,
            hwnd, NULL, hInst, NULL);
        
        HWND hTitle = CreateWindowExW(0, L"STATIC", L"Installer Settings",
            WS_CHILD | WS_VISIBLE | SS_LEFT,
            20, 20, rc.right - 40, 30,
            s_hCurrentPage, NULL, hInst, NULL);
        HFONT hTitleFont = CreateFontW(-18, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
        if (hTitleFont) SendMessageW(hTitle, WM_SETFONT, (WPARAM)hTitleFont, TRUE);
        
        CreateWindowExW(0, L"STATIC", L"Settings (License, OS requirements, etc.) to be implemented",
            WS_CHILD | WS_VISIBLE | SS_LEFT,
            20, 60, rc.right - 40, 20,
            s_hCurrentPage, NULL, hInst, NULL);
        break;
    }
    case 5: // Build page
    {
        // Create container for page content
        s_hCurrentPage = CreateWindowExW(
            0, L"STATIC", L"",
            WS_CHILD | WS_VISIBLE,
            0, pageY, rc.right, pageHeight,
            hwnd, NULL, hInst, NULL);
        
        HWND hTitle = CreateWindowExW(0, L"STATIC", L"Build Installer",
            WS_CHILD | WS_VISIBLE | SS_LEFT,
            20, 20, rc.right - 40, 30,
            s_hCurrentPage, NULL, hInst, NULL);
        HFONT hTitleFont = CreateFontW(-18, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
        if (hTitleFont) SendMessageW(hTitle, WM_SETFONT, (WPARAM)hTitleFont, TRUE);
        
        CreateWindowExW(0, L"STATIC", L"Build/compile functionality to be implemented",
            WS_CHILD | WS_VISIBLE | SS_LEFT,
            20, 60, rc.right - 40, 20,
            s_hCurrentPage, NULL, hInst, NULL);
        break;
    }
    case 6: // Test page
    {
        // Create container for page content
        s_hCurrentPage = CreateWindowExW(
            0, L"STATIC", L"",
            WS_CHILD | WS_VISIBLE,
            0, pageY, rc.right, pageHeight,
            hwnd, NULL, hInst, NULL);
        
        HWND hTitle = CreateWindowExW(0, L"STATIC", L"Test Installer",
            WS_CHILD | WS_VISIBLE | SS_LEFT,
            20, 20, rc.right - 40, 30,
            s_hCurrentPage, NULL, hInst, NULL);
        HFONT hTitleFont = CreateFontW(-18, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
        if (hTitleFont) SendMessageW(hTitle, WM_SETFONT, (WPARAM)hTitleFont, TRUE);
        
        CreateWindowExW(0, L"STATIC", L"Test functionality to be implemented",
            WS_CHILD | WS_VISIBLE | SS_LEFT,
            20, 60, rc.right - 40, 20,
            s_hCurrentPage, NULL, hInst, NULL);
        break;
    }
    case 7: // Scripts page
    {
        // Create container for page content
        s_hCurrentPage = CreateWindowExW(
            0, L"STATIC", L"",
            WS_CHILD | WS_VISIBLE,
            0, pageY, rc.right, pageHeight,
            hwnd, NULL, hInst, NULL);
        
        HWND hTitle = CreateWindowExW(0, L"STATIC", L"Run Scripts",
            WS_CHILD | WS_VISIBLE | SS_LEFT,
            20, 20, rc.right - 40, 30,
            s_hCurrentPage, NULL, hInst, NULL);
        HFONT hTitleFont = CreateFontW(-18, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
        if (hTitleFont) SendMessageW(hTitle, WM_SETFONT, (WPARAM)hTitleFont, TRUE);
        
        CreateWindowExW(0, L"STATIC", L"Configure scripts and executables to run before/after installation",
            WS_CHILD | WS_VISIBLE | SS_LEFT,
            20, 60, rc.right - 40, 20,
            s_hCurrentPage, NULL, hInst, NULL);
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
    
    return TreeView_InsertItem(hTree, &tvis);
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
            parentPath = L"Program Files";
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
        std::wstring newInstallPath = L"C:\\Program Files\\" + std::wstring(folderName);
        SetDlgItemTextW(hwnd, IDC_INSTALL_FOLDER, newInstallPath.c_str());
        
        // Also update project name if this is a new project and user hasn't manually edited it
        if (s_isNewUnsavedProject && !s_projectNameManuallySet) {
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
        std::wstring defaultPath = L"C:\\Program Files\\New Project";
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
    
    // Set status bar text with project info
    std::wstring statusText = L"Project: " + s_currentProject.name + 
                              L"  |  Version: " + s_currentProject.version +
                              L"  |  Directory: " + s_currentProject.directory;
    SendMessageW(s_hStatus, SB_SETTEXT, 0, (LPARAM)statusText.c_str());
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

// Dialog procedure for Show Registry Key dialog
LRESULT CALLBACK RegKeyDialogProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_CREATE: {
        CREATESTRUCTW* cs = (CREATESTRUCTW*)lParam;
        HINSTANCE hInst = cs->hInstance;
        
        // Get dialog data from user data
        RegKeyDialogData* pData = (RegKeyDialogData*)cs->lpCreateParams;
        
        // Store pointer in window data for later use
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)pData);
        
        // Create label
        CreateWindowExW(0, L"STATIC", pData->labelText.c_str(),
            WS_CHILD | WS_VISIBLE | SS_LEFT,
            20, 20, 560, 20,
            hwnd, NULL, hInst, NULL);
        
        // Create registry path EDIT control (read-only, multiline for full path visibility)
        HWND hEdit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", pData->regPath.c_str(),
            WS_CHILD | WS_VISIBLE | ES_LEFT | ES_READONLY | ES_MULTILINE | ES_AUTOHSCROLL | WS_VSCROLL,
            20, 50, 560, 60,
            hwnd, (HMENU)IDC_REGKEY_DLG_EDIT, hInst, NULL);
        
        // Create "Take me there" button with icon 267
        CreateCustomButtonWithIcon(hwnd, IDC_REGKEY_DLG_NAVIGATE, pData->navigateText.c_str(), ButtonColor::Blue,
            L"shell32.dll", 267, 20, 130, 140, 35, hInst);
        
        // Create "Copy" button with icon 64 (centered)
        CreateCustomButtonWithIcon(hwnd, IDC_REGKEY_DLG_COPY, pData->copyText.c_str(), ButtonColor::Blue,
            L"shell32.dll", 64, 230, 130, 140, 35, hInst);
        
        // Create "Close" button with icon 300
        CreateCustomButtonWithIcon(hwnd, IDC_REGKEY_DLG_CLOSE, pData->closeText.c_str(), ButtonColor::Blue,
            L"shell32.dll", 300, 440, 130, 140, 35, hInst);
        
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
            HFONT hFont = CreateFontW(-12, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
                DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
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
    
    case WM_CLOSE:
        DestroyWindow(hwnd);
        s_hRegKeyDialog = NULL;
        return 0;
    
    case WM_DESTROY:
        s_hRegKeyDialog = NULL;
        return 0;
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
LRESULT CALLBACK AddValueDialogProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_CREATE: {
        CREATESTRUCTW* cs = (CREATESTRUCTW*)lParam;
        HINSTANCE hInst = cs->hInstance;
        
        // Get dialog data
        AddValueDialogData* pData = (AddValueDialogData*)cs->lpCreateParams;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)pData);
        
        int y = 20;
        
        // Value Name label and edit
        CreateWindowExW(0, L"STATIC", pData->nameText.c_str(),
            WS_CHILD | WS_VISIBLE | SS_LEFT,
            20, y, 120, 20, hwnd, NULL, hInst, NULL);
        HWND hNameEdit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", pData->valueName.c_str(),
            WS_CHILD | WS_VISIBLE | WS_BORDER | ES_LEFT | ES_AUTOHSCROLL,
            150, y, 330, 22, hwnd, (HMENU)IDC_ADDVAL_NAME, hInst, NULL);
        // Ensure the edit control text is set (explicit prefill for edit mode)
        SetDlgItemTextW(hwnd, IDC_ADDVAL_NAME, pData->valueName.c_str());
        y += 35;
        
        // Type label and combobox
        CreateWindowExW(0, L"STATIC", pData->typeText.c_str(),
            WS_CHILD | WS_VISIBLE | SS_LEFT,
            20, y, 120, 20, hwnd, NULL, hInst, NULL);
        HWND hCombo = CreateWindowExW(WS_EX_CLIENTEDGE, L"COMBOBOX", L"",
            WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | WS_VSCROLL,
            150, y, 330, 200, hwnd, (HMENU)IDC_ADDVAL_TYPE, hInst, NULL);
        
        // Populate type combobox - all Windows Registry types with descriptions
        SendMessageW(hCombo, CB_ADDSTRING, 0, (LPARAM)L"REG_SZ - Text string value");
        SendMessageW(hCombo, CB_ADDSTRING, 0, (LPARAM)L"REG_BINARY - Binary data (any length)");
        SendMessageW(hCombo, CB_ADDSTRING, 0, (LPARAM)L"REG_DWORD - 32-bit number");
        SendMessageW(hCombo, CB_ADDSTRING, 0, (LPARAM)L"REG_QWORD - 64-bit number");
        SendMessageW(hCombo, CB_ADDSTRING, 0, (LPARAM)L"REG_MULTI_SZ - Multiple text strings");
        SendMessageW(hCombo, CB_ADDSTRING, 0, (LPARAM)L"REG_EXPAND_SZ - Expandable string (with %variables%)");
        
        // Select the appropriate type if editing
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
        y += 35;
        
        // Data label and edit
        CreateWindowExW(0, L"STATIC", pData->dataText.c_str(),
            WS_CHILD | WS_VISIBLE | SS_LEFT,
            20, y, 120, 20, hwnd, NULL, hInst, NULL);
        CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", pData->valueData.c_str(),
            WS_CHILD | WS_VISIBLE | WS_BORDER | ES_LEFT | ES_AUTOHSCROLL,
            150, y, 330, 22, hwnd, (HMENU)IDC_ADDVAL_DATA, hInst, NULL);
        // Ensure data edit is filled (explicit prefill)
        SetDlgItemTextW(hwnd, IDC_ADDVAL_DATA, pData->valueData.c_str());
        y += 45;
        
        // OK and Cancel buttons
        CreateCustomButtonWithIcon(hwnd, IDC_ADDVAL_OK, pData->okText.c_str(), ButtonColor::Green,
            L"imageres.dll", 89, 150, y, 120, 35, hInst);
        CreateCustomButtonWithIcon(hwnd, IDC_ADDVAL_CANCEL, pData->cancelText.c_str(), ButtonColor::Red,
            L"shell32.dll", 131, 280, y, 120, 35, hInst);
        
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
            HFONT hFont = CreateFontW(-12, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
                DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
            LRESULT result = DrawCustomButton(dis, color, hFont);
            if (hFont) DeleteObject(hFont);
            return result;
        }
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

// Dialog procedure for Add Registry Key dialog
LRESULT CALLBACK AddKeyDialogProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_CREATE: {
        CREATESTRUCTW* cs = (CREATESTRUCTW*)lParam;
        HINSTANCE hInst = cs->hInstance;
        
        // Get dialog data
        AddKeyDialogData* pData = (AddKeyDialogData*)cs->lpCreateParams;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)pData);
        
        int y = 20;
        
        // Key Name label and edit (pre-fill with default key name)
        CreateWindowExW(0, L"STATIC", pData->nameText.c_str(),
            WS_CHILD | WS_VISIBLE | SS_LEFT,
            20, y, 120, 20, hwnd, NULL, hInst, NULL);
        CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", pData->defaultKeyName.c_str(),
            WS_CHILD | WS_VISIBLE | WS_BORDER | ES_LEFT | ES_AUTOHSCROLL,
            150, y, 330, 22, hwnd, (HMENU)IDC_ADDKEY_NAME, hInst, NULL);
        // Explicitly set edit text to ensure prefill works in Edit mode
        SetDlgItemTextW(hwnd, IDC_ADDKEY_NAME, pData->defaultKeyName.c_str());
        y += 45;
        
        // OK and Cancel buttons
        CreateCustomButtonWithIcon(hwnd, IDC_ADDKEY_OK, pData->okText.c_str(), ButtonColor::Green,
            L"imageres.dll", 89, 150, y, 120, 35, hInst);
        CreateCustomButtonWithIcon(hwnd, IDC_ADDKEY_CANCEL, pData->cancelText.c_str(), ButtonColor::Red,
            L"shell32.dll", 131, 280, y, 120, 35, hInst);
        
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
            HFONT hFont = CreateFontW(-12, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
                DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
            LRESULT result = DrawCustomButton(dis, color, hFont);
            if (hFont) DeleteObject(hFont);
            return result;
        }
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
        
        CreateMenuBar(hwnd);
        CreateToolbar(hwnd, hInst);
        CreateStatusBar(hwnd, hInst);
        // Initialize with Files page
        SwitchPage(hwnd, 0);
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
        }
        return 0;
    }
    
    case WM_NOTIFY: {
        LPNMHDR nmhdr = (LPNMHDR)lParam;
        
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
                    std::wstring newInstallPath = L"C:\\Program Files\\" + folderName;
                    SetDlgItemTextW(hwnd, IDC_INSTALL_FOLDER, newInstallPath.c_str());
                    
                    // Also update project name if this is a new project and user hasn't manually edited it
                    if (s_isNewUnsavedProject && !s_projectNameManuallySet) {
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
                
                // Check if folder has a physical path
                bool hasPhysicalPath = false;
                if (pnmtv->itemNew.lParam) {
                    wchar_t* folderPath = (wchar_t*)pnmtv->itemNew.lParam;
                    if (folderPath && wcslen(folderPath) > 0) {
                        // Physical folder - populate from disk
                        PopulateListView(s_hListView, folderPath);
                        hasPhysicalPath = true;
                    }
                }
                
                // If no physical path, check for virtual folder files
                if (!hasPhysicalPath) {
                    auto it = s_virtualFolderFiles.find(pnmtv->itemNew.hItem);
                    if (it != s_virtualFolderFiles.end()) {
                        for (const auto& fileInfo : it->second) {
                            // Get file icon
                            SHFILEINFOW sfi = {};
                            SHGetFileInfoW(fileInfo.sourcePath.c_str(), 0, &sfi, sizeof(sfi), SHGFI_SYSICONINDEX | SHGFI_SMALLICON);
                            
                            LVITEMW lvi = {};
                            lvi.mask = LVIF_TEXT | LVIF_PARAM | LVIF_IMAGE;
                            lvi.iItem = ListView_GetItemCount(s_hListView);
                            lvi.iSubItem = 0;
                            lvi.pszText = (LPWSTR)fileInfo.sourcePath.c_str();
                            lvi.iImage = sfi.iIcon;
                            
                            wchar_t* pathCopy = (wchar_t*)malloc((fileInfo.sourcePath.length() + 1) * sizeof(wchar_t));
                            if (pathCopy) {
                                wcscpy(pathCopy, fileInfo.sourcePath.c_str());
                                lvi.lParam = (LPARAM)pathCopy;
                            }
                            
                            int idx = ListView_InsertItem(s_hListView, &lvi);
                            ListView_SetItemText(s_hListView, idx, 1, (LPWSTR)fileInfo.destination.c_str());
                        }
                    }
                }
                
                // Force ListView to redraw
                InvalidateRect(s_hListView, NULL, TRUE);
                UpdateWindow(s_hListView);
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

        break;
    }
    
    case WM_COMMAND: {
        int wmId = LOWORD(wParam);
        int wmEvent = HIWORD(wParam);
        
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
            
        case IDC_TB_SETTINGS:
            SwitchPage(hwnd, 4);
            return 0;
            
        case IDC_TB_BUILD:
            SwitchPage(hwnd, 5);
            return 0;
            
        case IDC_TB_TEST:
            SwitchPage(hwnd, 6);
            return 0;
            
        case IDC_TB_SCRIPTS:
            SwitchPage(hwnd, 7);
            return 0;
            
        case IDC_TB_SAVE:
            // TODO: Implement save functionality
            return 0;
            
        case IDC_TB_ABOUT:
            // This case is no longer used - clicks handled in WM_LBUTTONDOWN
            return 0;
            
        // Files page buttons
        case IDC_FILES_ADD_DIR: {
            // Open folder picker dialog
            BROWSEINFOW bi = {};
            bi.hwndOwner = hwnd;
            bi.lpszTitle = L"Select a folder to add";
            bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE | BIF_USENEWUI;
            
            LPITEMIDLIST pidl = SHBrowseForFolderW(&bi);
            if (pidl) {
                wchar_t selectedPath[MAX_PATH];
                if (SHGetPathFromIDListW(pidl, selectedPath)) {
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
                            if (s_isNewUnsavedProject && !s_projectNameManuallySet) {
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
                }
                CoTaskMemFree(pidl);
            }
            return 0;
        }
            
        case IDC_FILES_ADD_FILES: {
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
            
            if (GetOpenFileNameW(&ofn)) {
                // Parse multi-select results
                std::wstring directory(fileBuffer);
                wchar_t* p = fileBuffer + directory.length() + 1;
                
                // Get currently selected folder as target, or first child under Program Files
                HTREEITEM hTargetFolder = NULL;
                if (s_hTreeView) {
                    hTargetFolder = TreeView_GetSelection(s_hTreeView);
                    // If nothing selected or Program Files root selected, use first child
                    if (!hTargetFolder || hTargetFolder == s_hProgramFilesRoot) {
                        hTargetFolder = TreeView_GetChild(s_hTreeView, s_hProgramFilesRoot);
                    }
                }
                
                // Check if single file or multiple
                if (*p == 0) {
                    // Single file - full path in directory variable
                    std::wstring fullPath = directory;
                    size_t lastSlash = fullPath.find_last_of(L"\\/");
                    std::wstring fileName = (lastSlash != std::wstring::npos) ? fullPath.substr(lastSlash + 1) : fullPath;
                    
                    // If no folder structure exists, create one based on filename (without extension)
                    if (!hTargetFolder && s_hTreeView && s_hProgramFilesRoot) {
                        size_t dotPos = fileName.find_last_of(L".");
                        std::wstring baseName = (dotPos != std::wstring::npos) ? fileName.substr(0, dotPos) : fileName;
                        
                        // Create folder under Program Files
                        hTargetFolder = AddTreeNode(s_hTreeView, s_hProgramFilesRoot, baseName, L"");
                        TreeView_Expand(s_hTreeView, s_hProgramFilesRoot, TVE_EXPAND);
                        
                        // Update install path and project name
                        std::wstring newInstallPath = L"C:\\Program Files\\" + baseName;
                        SetDlgItemTextW(hwnd, IDC_INSTALL_FOLDER, newInstallPath.c_str());
                        
                        if (s_isNewUnsavedProject && !s_projectNameManuallySet) {
                            s_currentProject.name = baseName;
                            std::wstring title = L"SetupCraft - " + baseName;
                            SetWindowTextW(hwnd, title.c_str());
                            
                            // Update project name field programmatically
                            s_updatingProjectNameProgrammatically = true;
                            SetDlgItemTextW(hwnd, IDC_PROJECT_NAME, baseName.c_str());
                            s_updatingProjectNameProgrammatically = false;
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
                                s_virtualFolderFiles[hTargetFolder].push_back(fileInfo);
                            }
                        }
                    }
                } else {
                    // Multiple files - first file used for folder and project name
                    
                    // If no folder structure exists, create one based on first filename (without extension)
                    if (!hTargetFolder && s_hTreeView && s_hProgramFilesRoot && *p) {
                        std::wstring firstFileName(p);
                        size_t dotPos = firstFileName.find_last_of(L".");
                        std::wstring baseName = (dotPos != std::wstring::npos) ? firstFileName.substr(0, dotPos) : firstFileName;
                        
                        // Create folder under Program Files
                        hTargetFolder = AddTreeNode(s_hTreeView, s_hProgramFilesRoot, baseName, L"");
                        TreeView_Expand(s_hTreeView, s_hProgramFilesRoot, TVE_EXPAND);
                        
                        // Update install path and project name
                        std::wstring newInstallPath = L"C:\\Program Files\\" + baseName;
                        SetDlgItemTextW(hwnd, IDC_INSTALL_FOLDER, newInstallPath.c_str());
                        
                        if (s_isNewUnsavedProject && !s_projectNameManuallySet) {
                            s_currentProject.name = baseName;
                            std::wstring title = L"SetupCraft - " + baseName;
                            SetWindowTextW(hwnd, title.c_str());
                            
                            // Update project name field programmatically
                            s_updatingProjectNameProgrammatically = true;
                            SetDlgItemTextW(hwnd, IDC_PROJECT_NAME, baseName.c_str());
                            s_updatingProjectNameProgrammatically = false;
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
                                    s_virtualFolderFiles[hTargetFolder].push_back(fileInfo);
                                }
                            }
                        }
                        
                        p += fileName.length() + 1;
                    }
                }
                
                MarkAsModified();
            }
            return 0;
        }
            
        case IDC_FILES_REMOVE: {
            // Determine which control to remove from based on focus
            HWND hFocus = GetFocus();
            bool removedSomething = false;
            
            // Check if ListView has focus or has selected items
            if (hFocus == s_hListView || (s_hListView && IsWindow(s_hListView) && ListView_GetSelectedCount(s_hListView) > 0)) {
                // Remove all selected items from ListView
                int count = ListView_GetItemCount(s_hListView);
                for (int i = count - 1; i >= 0; i--) {
                    if (ListView_GetItemState(s_hListView, i, LVIS_SELECTED) & LVIS_SELECTED) {
                        // Free the stored path memory
                        LVITEM lvi = {};
                        lvi.mask = LVIF_PARAM;
                        lvi.iItem = i;
                        if (ListView_GetItem(s_hListView, &lvi) && lvi.lParam) {
                            wchar_t* path = (wchar_t*)lvi.lParam;
                            free(path);
                        }
                        ListView_DeleteItem(s_hListView, i);
                        removedSomething = true;
                    }
                }
                if (removedSomething) return 0;
            }
            
            // Check TreeView for checked items
            if (s_hTreeView && IsWindow(s_hTreeView)) {
                // Function to recursively remove checked items
                std::function<void(HTREEITEM)> RemoveCheckedItems;
                RemoveCheckedItems = [&](HTREEITEM hItem) {
                    while (hItem) {
                        HTREEITEM hNext = TreeView_GetNextSibling(s_hTreeView, hItem);
                        
                        // Check children first
                        HTREEITEM hChild = TreeView_GetChild(s_hTreeView, hItem);
                        if (hChild) {
                            RemoveCheckedItems(hChild);
                        }
                        
                        // Check if this item is checked
                        if (TreeView_GetCheckState(s_hTreeView, hItem)) {
                            // Get the item to free path memory
                            TVITEMW item = {};
                            item.mask = TVIF_PARAM;
                            item.hItem = hItem;
                            TreeView_GetItem(s_hTreeView, &item);
                            
                            if (item.lParam) {
                                wchar_t* path = (wchar_t*)item.lParam;
                                free(path);
                            }
                            
                            TreeView_DeleteItem(s_hTreeView, hItem);
                            removedSomething = true;
                        }
                        
                        hItem = hNext;
                    }
                };
                
                HTREEITEM hRoot = TreeView_GetRoot(s_hTreeView);
                if (hRoot) {
                    RemoveCheckedItems(hRoot);
                }
                
                if (removedSomething) {
                    // Clear ListView since folders were removed
                    if (s_hListView && IsWindow(s_hListView)) {
                        ListView_DeleteAllItems(s_hListView);
                    }
                    return 0;
                }
            }
            
            MessageBoxW(hwnd, L"Please select items to remove:\n\n" 
                        L"- In ListView: Click/Ctrl+Click files to select\n"
                        L"- In TreeView: Check folders to remove", 
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
            
            // Center dialog over main window
            RECT rcMain;
            GetWindowRect(hwnd, &rcMain);
            int dlgWidth = 600;
            int dlgHeight = 220;
            int dlgX = rcMain.left + (rcMain.right - rcMain.left - dlgWidth) / 2;
            int dlgY = rcMain.top + (rcMain.bottom - rcMain.top - dlgHeight) / 2;
            
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
            
            // Get locale strings (compose Add / Edit title)
            auto itTitleAddKey = s_locale.find(L"reg_add_key_title");
            auto itTitleEditKey = s_locale.find(L"reg_edit_key_title");
            std::wstring addKeyTitle = (itTitleAddKey != s_locale.end()) ? itTitleAddKey->second : L"Add Registry Key";
            std::wstring editKeyTitle = (itTitleEditKey != s_locale.end()) ? itTitleEditKey->second : L"Edit Registry Key";
            std::wstring title = addKeyTitle + L" / " + editKeyTitle;
            
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
            
            // Center dialog over main window
            RECT rcMain;
            GetWindowRect(hwnd, &rcMain);
            int dlgWidth = 520;
            int dlgHeight = 150;
            int dlgX = rcMain.left + ((rcMain.right - rcMain.left) - dlgWidth) / 2;
            int dlgY = rcMain.top + ((rcMain.bottom - rcMain.top) - dlgHeight) / 2;
            
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
            
            // Get locale strings (compose Add / Edit title)
            auto itTitleAdd = s_locale.find(L"reg_add_value_title");
            auto itTitleEdit = s_locale.find(L"reg_edit_value_title");
            std::wstring addTitle = (itTitleAdd != s_locale.end()) ? itTitleAdd->second : L"Add Registry Value";
            std::wstring editTitle = (itTitleEdit != s_locale.end()) ? itTitleEdit->second : L"Edit Registry Value";
            std::wstring title = addTitle + L" / " + editTitle;
            
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
            
            // Center dialog over main window
            RECT rcMain;
            GetWindowRect(hwnd, &rcMain);
            int dlgWidth = 520;
            int dlgHeight = 230;
            int dlgX = rcMain.left + ((rcMain.right - rcMain.left) - dlgWidth) / 2;
            int dlgY = rcMain.top + ((rcMain.bottom - rcMain.top) - dlgHeight) / 2;
            
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
            SpinnerDialog* spinner = new SpinnerDialog(hwnd);
            spinner->Show(spinnerText);
            
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
                    // Wait briefly for UAC prompt to appear and become foreground
                    Sleep(500);
                    
                    // Enumerate all windows to find and bring UAC to front
                    HWND hUAC = FindWindowW(NULL, L"User Account Control");
                    if (hUAC) {
                        SetForegroundWindow(hUAC);
                    }
                    
                    // Wait for process to complete with message pump (timeout 30 seconds)
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

            RECT rcParent;
            GetWindowRect(hwnd, &rcParent);
            int dlgX = rcParent.left + (rcParent.right - rcParent.left - 520) / 2;
            int dlgY = rcParent.top + (rcParent.bottom - rcParent.top - 160) / 2;

            HWND hDialog = CreateWindowExW(WS_EX_DLGMODALFRAME | WS_EX_TOPMOST,
                L"AddKeyDialog", title.c_str(),
                WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_VISIBLE,
                dlgX, dlgY, 520, 160, hwnd, NULL, hInst, &data);
            
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

            // Create dialog window
            HWND hDialog = CreateWindowExW(WS_EX_DLGMODALFRAME | WS_EX_TOPMOST, L"AddValueDialog",
                title.c_str(), WS_POPUP | WS_CAPTION | WS_SYSMENU,
                CW_USEDEFAULT, CW_USEDEFAULT, 520, 250, hwnd, NULL, hInst, &dialogData);
            
            if (hDialog) {
                // Center dialog
                RECT rcParent, rcDialog;
                GetWindowRect(hwnd, &rcParent);
                GetWindowRect(hDialog, &rcDialog);
                int x = rcParent.left + (rcParent.right - rcParent.left - (rcDialog.right - rcDialog.left)) / 2;
                int y = rcParent.top + (rcParent.bottom - rcParent.top - (rcDialog.bottom - rcDialog.top)) / 2;
                SetWindowPos(hDialog, NULL, x, y, 0, 0, SWP_NOSIZE | SWP_NOZORDER);
                
                ShowWindow(hDialog, SW_SHOW);
                
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
            
        // Menu items
        case IDM_FILE_NEW: {
            HINSTANCE hInst = (HINSTANCE)GetWindowLongPtr(hwnd, GWLP_HINSTANCE);
            MainWindow::CreateNew(hInst, s_locale);
            return 0;
        }
            
        case IDM_FILE_SAVE:
            MessageBoxW(hwnd, L"Save functionality to be implemented", L"Save", MB_OK | MB_ICONINFORMATION);
            return 0;
            
        case IDM_FILE_SAVEAS:
            MessageBoxW(hwnd, L"Save As functionality to be implemented", L"Save As", MB_OK | MB_ICONINFORMATION);
            return 0;
            
        case IDM_FILE_CLOSE: {
            // Check for unsaved changes
            if (s_hasUnsavedChanges) {
                int result = ShowUnsavedChangesDialog(hwnd, s_locale);
                if (result == 0) {
                    // Cancel - don't close
                    return 0;
                } else if (result == 1) {
                    // Save - TODO: implement save functionality
                    MessageBoxW(hwnd, L"Save functionality to be implemented", L"Save", MB_OK | MB_ICONINFORMATION);
                    return 0;
                }
                // result == 2: Don't Save - proceed with close
            }
            
            // Close project and return to entry screen
            HWND entryWindow = FindWindowW(L"SetupCraft_EntryScreen", NULL);
            if (entryWindow) {
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
            return 0;
        }
        
        case IDM_TREEVIEW_REMOVE_FOLDER: {
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
                    std::wstring message = L"The folder '" + std::wstring(folderName) + 
                                          L"' is not empty.\n\nDo you want to delete it and all its contents?";
                    int result = MessageBoxW(hwnd, message.c_str(), L"Confirm Delete", 
                                           MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2);
                    shouldDelete = (result == IDYES);
                }
                
                // Delete the folder and all its children
                if (shouldDelete) {
                    // Check if deleted item was under Program Files root
                    HTREEITEM hParent = TreeView_GetParent(s_hTreeView, s_rightClickedItem);
                    bool wasUnderProgramFiles = (hParent == s_hProgramFilesRoot);
                    
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
            
            // Show menu if right-clicking on Program Files root or any folder node
            if (hItem) {
                s_rightClickedItem = hItem; // Remember which item was clicked
                
                // Create context menu
                HMENU hMenu = CreatePopupMenu();
                AppendMenuW(hMenu, MF_STRING, IDM_TREEVIEW_ADD_FOLDER, L"Create Folder...");
                
                // Add "Remove folder" option if this is not the Program Files root
                if (hItem != s_hProgramFilesRoot) {
                    AppendMenuW(hMenu, MF_STRING, IDM_TREEVIEW_REMOVE_FOLDER, L"Remove Folder");
                }
                
                // Show menu
                TrackPopupMenu(hMenu, TPM_RIGHTBUTTON, xPos, yPos, 0, hwnd, NULL);
                DestroyMenu(hMenu);
            }
            return 0;
        }
        break;
    }
    
    case WM_CTLCOLORSTATIC: {
        // Make static controls have white background like the window
        HDC hdc = (HDC)wParam;
        HWND hControl = (HWND)lParam;
        
        // Special handling for install folder - dark blue text
        if (GetDlgCtrlID(hControl) == IDC_INSTALL_FOLDER) {
            SetTextColor(hdc, RGB(0, 51, 153)); // Dark blue
            SetBkMode(hdc, TRANSPARENT);
            return (LRESULT)GetSysColorBrush(COLOR_WINDOW);
        }
        
        SetBkMode(hdc, TRANSPARENT);
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
        
        // Handle Ctrl+W to close project
        if (IsCtrlWPressed(msg, wParam)) {
            if (ShowQuitDialog(hwnd, s_locale)) {
                DestroyWindow(hwnd);
            }
            return 0;
        }
        break;
    }
    
    case WM_CLOSE: {
        // Show quit dialog
        if (ShowQuitDialog(hwnd, s_locale)) {
            DestroyWindow(hwnd);
        }
        return 0;
    }
    
    case WM_DRAWITEM: {
        LPDRAWITEMSTRUCT dis = (LPDRAWITEMSTRUCT)lParam;
        // Handle custom button drawing for toolbar buttons and page buttons
        // Note: IDC_TB_ABOUT is now a static icon, not a button, so exclude it
        if ((dis->CtlID >= IDC_TB_FILES && dis->CtlID <= IDC_TB_SAVE) ||
            (dis->CtlID >= IDC_FILES_ADD_DIR && dis->CtlID <= IDC_FILES_REMOVE) ||
            (dis->CtlID >= IDC_REG_CHECKBOX && dis->CtlID <= IDC_REG_BACKUP)) {
            ButtonColor color = (ButtonColor)GetWindowLongPtr(dis->hwndItem, GWLP_USERDATA);
            // Create bold font for buttons (reduced from -14 to -12 for smaller buttons)
            HFONT hFont = CreateFontW(-12, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
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
    
    case WM_LBUTTONDOWN: {
        // Forward to about icon module (handles clicks on icon)
        AboutIcon_OnLButtonDown(hwnd, wParam, lParam);
        // Hide tooltip when clicking anywhere else
        HideTooltip();
        s_aboutMouseTracking = false;
        break;
    }
    
    case WM_MOUSEMOVE: {
        POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
        bool overAboutIcon = false;
        RECT rcIcon;
        
        // Check if mouse is over About button (in toolbar)
        if (s_hAboutButton) {
            GetWindowRect(s_hAboutButton, &rcIcon);
            ScreenToClient(hwnd, (LPPOINT)&rcIcon.left);
            ScreenToClient(hwnd, (LPPOINT)&rcIcon.right);
            
            if (PtInRect(&rcIcon, pt)) {
                overAboutIcon = true;
                
                if (!IsTooltipVisible()) {
                    // Show simple tooltip with current language text only
                    auto it = s_locale.find(L"about_setupcraft");
                    std::wstring tooltipText = (it != s_locale.end()) ? it->second : L"About SetupCraft";
                    
                    // Create single entry for simple tooltip
                    std::vector<std::pair<std::wstring, std::wstring>> simpleEntry;
                    simpleEntry.push_back({L"", tooltipText}); // Empty country code for simple tooltip
                    
                    // Position tooltip below the about icon
                    POINT ptIcon = { rcIcon.left, rcIcon.bottom + 5 };
                    ClientToScreen(hwnd, &ptIcon);
                    ShowMultilingualTooltip(simpleEntry, ptIcon.x, ptIcon.y, hwnd);
                    s_currentTooltipIcon = s_hAboutButton;
                }
            }
        }
        
        if (overAboutIcon) {
            // Track mouse to detect when it leaves
            if (!s_aboutMouseTracking) {
                TRACKMOUSEEVENT tme = { 0 };
                tme.cbSize = sizeof(TRACKMOUSEEVENT);
                tme.dwFlags = TME_LEAVE;
                tme.hwndTrack = hwnd;
                TrackMouseEvent(&tme);
                s_aboutMouseTracking = true;
                s_mouseTracking = true;
            }
        } else {
            // Hide tooltip if mouse is not over About button
            if (IsTooltipVisible()) {
                HideTooltip();
                s_currentTooltipIcon = NULL;
                s_mouseTracking = false;
            }
            s_aboutMouseTracking = false;
        }
        
        // Check if mouse is over warning icon
        if (s_currentPageIndex == 1) { // Registry page
            HWND hWarnIcon = GetDlgItem(hwnd, IDC_REG_WARNING_ICON);
            
            if (hWarnIcon) {
                RECT rcIcon;
                GetWindowRect(hWarnIcon, &rcIcon);
                ScreenToClient(hwnd, (LPPOINT)&rcIcon.left);
                ScreenToClient(hwnd, (LPPOINT)&rcIcon.right);
                
                if (PtInRect(&rcIcon, pt)) {
                    if (!s_hWarningTooltip || !IsWindowVisible(s_hWarningTooltip)) {
                        // Create tooltip window
                        if (!s_hWarningTooltip) {
                            HINSTANCE hInst = (HINSTANCE)GetWindowLongPtrW(hwnd, GWLP_HINSTANCE);
                            
                            // Get tooltip text
                            auto itTooltip = s_locale.find(L"reg_warning_tooltip");
                            s_warningTooltipText = (itTooltip != s_locale.end()) ? itTooltip->second : 
                                L"Editing the registry can change the machine's behaviour and maybe harm it, so edit at your own risk, and backup registry before changing.";
                            
                            // Convert escape sequences to actual characters (like SpinnerDialog)
                            size_t pos = 0;
                            while ((pos = s_warningTooltipText.find(L"\\r\\n", pos)) != std::wstring::npos) {
                                s_warningTooltipText.replace(pos, 4, L"\r\n");
                                pos += 2;
                            }
                            pos = 0;
                            while ((pos = s_warningTooltipText.find(L"\\n", pos)) != std::wstring::npos) {
                                s_warningTooltipText.replace(pos, 2, L"\r\n");
                                pos += 2;
                            }
                            
                            // Calculate height based on text with font
                            HDC hdc = GetDC(hwnd);
                            if (s_warningTooltipFont) SelectObject(hdc, s_warningTooltipFont);
                            RECT rcText = { 0, 0, 400, 0 };
                            DrawTextW(hdc, s_warningTooltipText.c_str(), -1, &rcText, DT_CALCRECT | DT_WORDBREAK);
                            ReleaseDC(hwnd, hdc);
                            int tooltipHeight = rcText.bottom + 20;
                            
                            s_hWarningTooltip = CreateWindowExW(WS_EX_TOPMOST | WS_EX_TOOLWINDOW,
                                WARNING_TOOLTIP_CLASS_NAME, L"",
                                WS_POPUP,
                                0, 0, 420, tooltipHeight,
                                hwnd, NULL, hInst, NULL);
                        }
                        
                        if (s_hWarningTooltip) {
                            // Get window client rect to keep tooltip inside
                            RECT rcWnd;
                            GetClientRect(hwnd, &rcWnd);
                            ClientToScreen(hwnd, (LPPOINT)&rcWnd.left);
                            ClientToScreen(hwnd, (LPPOINT)&rcWnd.right);
                            
                            // Get tooltip size
                            RECT rcTooltip;
                            GetWindowRect(s_hWarningTooltip, &rcTooltip);
                            int tooltipWidth = rcTooltip.right - rcTooltip.left;
                            int tooltipHeight = rcTooltip.bottom - rcTooltip.top;
                            
                            // Position tooltip below and to the left of the warning icon
                            POINT ptIcon = { rcIcon.left, rcIcon.bottom + 5 };
                            ClientToScreen(hwnd, &ptIcon);
                            
                            // Adjust X to keep tooltip inside window (align to left side of app)
                            int tooltipX = ptIcon.x - tooltipWidth + 32; // Shift left, align with icon right edge
                            if (tooltipX < rcWnd.left + 10) {
                                tooltipX = rcWnd.left + 10; // Keep 10px margin from left edge
                            }
                            if (tooltipX + tooltipWidth > rcWnd.right - 10) {
                                tooltipX = rcWnd.right - tooltipWidth - 10; // Keep inside right edge
                            }
                            
                            SetWindowPos(s_hWarningTooltip, HWND_TOPMOST, tooltipX, ptIcon.y, 0, 0, SWP_NOSIZE | SWP_SHOWWINDOW | SWP_NOACTIVATE);
                        }
                    }
                    
                    // Track mouse to detect when it leaves
                    if (!s_warningTooltipTracking) {
                        TRACKMOUSEEVENT tme = { 0 };
                        tme.cbSize = sizeof(TRACKMOUSEEVENT);
                        tme.dwFlags = TME_LEAVE;
                        tme.hwndTrack = hwnd;
                        TrackMouseEvent(&tme);
                        s_warningTooltipTracking = true;
                    }
                } else {
                    // Hide tooltip if mouse is not over icon
                    if (s_hWarningTooltip && IsWindowVisible(s_hWarningTooltip)) {
                        ShowWindow(s_hWarningTooltip, SW_HIDE);
                    }
                }
            }
        }
        return 0;
    }
    
    case WM_ACTIVATE:
        // Hide tooltip when window loses focus
        if (LOWORD(wParam) == WA_INACTIVE) {
            HideTooltip();
            s_aboutMouseTracking = false;
            if (s_hWarningTooltip && IsWindowVisible(s_hWarningTooltip)) {
                ShowWindow(s_hWarningTooltip, SW_HIDE);
            }
            s_warningTooltipTracking = false;
        }
        break;
    
    case WM_KILLFOCUS:
        // Hide tooltip when window loses keyboard focus
        HideTooltip();
        s_aboutMouseTracking = false;
        break;
    
    case WM_MOUSELEAVE:
        s_warningTooltipTracking = false;
        if (s_hWarningTooltip && IsWindowVisible(s_hWarningTooltip)) {
            ShowWindow(s_hWarningTooltip, SW_HIDE);
        }
        // Hide About button tooltip
        s_aboutMouseTracking = false;
        s_mouseTracking = false;
        s_currentTooltipIcon = NULL;
        HideTooltip();
        return 0;
    
    case WM_DESTROY:
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
