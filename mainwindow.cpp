#include "mainwindow.h"
#include <commctrl.h>
#include <shlwapi.h>
#include <shlobj.h>
#include <windowsx.h>
#include <functional>
#include <vector>
#include "ctrlw.h"
#include "button.h"

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
static bool s_projectNameManuallySet = false; // Track if user manually edited project name
static bool s_updatingProjectNameProgrammatically = false; // Prevent EN_CHANGE during programmatic updates

// Store files added to virtual folders (keyed by HTREEITEM)
struct VirtualFolderFile {
    std::wstring sourcePath;
    std::wstring destination;
};
static std::map<HTREEITEM, std::vector<VirtualFolderFile>> s_virtualFolderFiles;

// Menu IDs
#define IDM_FILE_NEW        4000
#define IDM_FILE_SAVE       4001
#define IDM_FILE_SAVEAS     4002
#define IDM_FILE_CLOSE      4003
#define IDM_FILE_EXIT       4004
#define IDM_EDIT_UNDO       4011
#define IDM_EDIT_REDO       4012
#define IDM_BUILD_COMPILE   4021
#define IDM_BUILD_TEST      4022
#define IDM_HELP_ABOUT      4031

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

HWND MainWindow::Create(HINSTANCE hInstance, const ProjectRow &project, const std::map<std::wstring, std::wstring> &locale) {
    s_currentProject = project;
    s_locale = locale;
    
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
    
    // Add Shortcut button
    auto itAddShortcut = s_locale.find(L"tb_add_shortcut");
    std::wstring addShortcutText = (itAddShortcut != s_locale.end()) ? itAddShortcut->second : L"Shortcuts";
    CreateCustomButtonWithIcon(hwnd, IDC_TB_ADD_SHORTCUT, addShortcutText, ButtonColor::Blue,
        L"shell32.dll", 1, x, startY, buttonWidth, buttonHeight, hInst);
    x += buttonWidth + buttonGap;
    
    // Add Dependency button (wider to fit text)
    auto itAddDep = s_locale.find(L"tb_add_dependency");
    std::wstring addDepText = (itAddDep != s_locale.end()) ? itAddDep->second : L"Dependencies";
    CreateCustomButtonWithIcon(hwnd, IDC_TB_ADD_DEPEND, addDepText, ButtonColor::Blue,
        L"shell32.dll", 154, x, startY, 125, buttonHeight, hInst);
    x += 125 + buttonGap;
    
    // Settings button
    auto itSettings = s_locale.find(L"tb_settings");
    std::wstring settingsText = (itSettings != s_locale.end()) ? itSettings->second : L"Settings";
    CreateCustomButtonWithIcon(hwnd, IDC_TB_SETTINGS, settingsText, ButtonColor::Blue,
        L"shell32.dll", 316, x, startY, buttonWidth, buttonHeight, hInst);
    x += buttonWidth + buttonGap;
    
    // Build button
    auto itBuild = s_locale.find(L"tb_build");
    std::wstring buildText = (itBuild != s_locale.end()) ? itBuild->second : L"Build (F7)";
    CreateCustomButtonWithIcon(hwnd, IDC_TB_BUILD, buildText, ButtonColor::Green,
        L"imageres.dll", 109, x, startY, buttonWidth, buttonHeight, hInst);
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
        L"shell32.dll", 166, x, startY, buttonWidth, buttonHeight, hInst);
    x += buttonWidth + buttonGap;
    
    // Save button
    auto itSave = s_locale.find(L"tb_save");
    std::wstring saveText = (itSave != s_locale.end()) ? itSave->second : L"Save";
    CreateCustomButtonWithIcon(hwnd, IDC_TB_SAVE, saveText, ButtonColor::Green,
        L"shell32.dll", 258, x, startY, buttonWidth, buttonHeight, hInst);  // Icon #258 is floppy disk save icon
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
    
    // Destroy previous browse button if exists
    HWND hBrowseBtn = GetDlgItem(hwnd, IDC_BROWSE_INSTALL_DIR);
    if (hBrowseBtn) {
        DestroyWindow(hBrowseBtn);
    }
    
    // Destroy previous remove button if exists
    HWND hRemoveBtn = GetDlgItem(hwnd, IDC_FILES_REMOVE);
    if (hRemoveBtn) {
        DestroyWindow(hRemoveBtn);
    }
    
    // Destroy TreeView and ListView if they exist (they're children of main window now)
    if (s_hTreeView && IsWindow(s_hTreeView)) {
        DestroyWindow(s_hTreeView);
    }
    if (s_hListView && IsWindow(s_hListView)) {
        DestroyWindow(s_hListView);
    }
    
    // Clear tree/list handles
    s_hTreeView = NULL;
    s_hListView = NULL;
    s_hProgramFilesRoot = NULL;
    
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
            L"shell32.dll", 4, 20, s_toolbarHeight + 55, 120, 35, hInst);
        
        // Add Files button (child of main window, positioned relative to it)
        s_hPageButton2 = CreateCustomButtonWithIcon(hwnd, IDC_FILES_ADD_FILES, L"Add Files", ButtonColor::Blue,
            L"shell32.dll", 71, 150, s_toolbarHeight + 55, 120, 35, hInst);
        
        // Remove button (for removing selected items)
        HWND hRemoveBtn = CreateCustomButtonWithIcon(hwnd, IDC_FILES_REMOVE, L"Remove", ButtonColor::Red,
            L"shell32.dll", 131, 20, s_toolbarHeight + 100, 250, 35, hInst);
        
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
        // Create container for page content
        s_hCurrentPage = CreateWindowExW(
            0, L"STATIC", L"",
            WS_CHILD | WS_VISIBLE,
            0, pageY, rc.right, pageHeight,
            hwnd, NULL, hInst, NULL);
        
        HWND hTitle = CreateWindowExW(0, L"STATIC", L"Registry Entries",
            WS_CHILD | WS_VISIBLE | SS_LEFT,
            20, 20, rc.right - 40, 30,
            s_hCurrentPage, NULL, hInst, NULL);
        HFONT hTitleFont = CreateFontW(-18, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
        if (hTitleFont) SendMessageW(hTitle, WM_SETFONT, (WPARAM)hTitleFont, TRUE);
        
        CreateWindowExW(0, L"STATIC", L"Registry entries to be implemented",
            WS_CHILD | WS_VISIBLE | SS_LEFT,
            20, 60, rc.right - 40, 20,
            s_hCurrentPage, NULL, hInst, NULL);
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

LRESULT CALLBACK MainWindow::WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_CREATE: {
        HINSTANCE hInst = ((LPCREATESTRUCT)lParam)->hInstance;
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
            
        case IDM_FILE_CLOSE:
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
            MessageBoxW(hwnd, L"SetupCraft - Installer Creation Tool\nVersion 2026.02.22", L"About", MB_OK | MB_ICONINFORMATION);
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
        // Handle TreeView context menu
        HWND hWndContext = (HWND)wParam;
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
        // Handle custom button drawing for toolbar buttons (including Save button)
        if ((dis->CtlID >= IDC_TB_FILES && dis->CtlID <= IDC_TB_SAVE) ||
            (dis->CtlID >= IDC_FILES_ADD_DIR && dis->CtlID <= IDC_FILES_REMOVE)) {
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
    
    case WM_DESTROY:
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
