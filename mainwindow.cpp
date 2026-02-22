#include "mainwindow.h"
#include <commctrl.h>
#include <shlwapi.h>
#include <shlobj.h>
#include <functional>
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
int MainWindow::s_toolbarHeight = 50;
int MainWindow::s_currentPageIndex = 0;

// Menu IDs
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

// Files dialog button IDs
#define IDC_FILES_ADD_DIR   5020
#define IDC_FILES_ADD_FILES 5021
#define IDC_FILES_DLG       5022
#define IDC_BROWSE_INSTALL_DIR 5023
#define IDC_FILES_REMOVE    5024

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

void MainWindow::CreateMenuBar(HWND hwnd) {
    HMENU hMenuBar = CreateMenu();
    
    // File menu
    HMENU hFileMenu = CreatePopupMenu();
    AppendMenuW(hFileMenu, MF_STRING, IDM_FILE_SAVE, L"&Save\tCtrl+S");
    AppendMenuW(hFileMenu, MF_STRING, IDM_FILE_SAVEAS, L"Save &As...");
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
    const int buttonWidth = 110;
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
        L"shell32.dll", 154, x, startY, 145, buttonHeight, hInst);
    x += 145 + buttonGap;
    
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
    
    s_currentPageIndex = pageIndex;
    
    RECT rc;
    GetClientRect(hwnd, &rc);
    HINSTANCE hInst = (HINSTANCE)GetWindowLongPtr(hwnd, GWLP_HINSTANCE);
    
    // Calculate page area (below toolbar, above status bar)
    int pageY = s_toolbarHeight;
    int pageHeight = rc.bottom - s_toolbarHeight - 25;
    
    // Create container for page content
    s_hCurrentPage = CreateWindowExW(
        0, L"STATIC", L"",
        WS_CHILD | WS_VISIBLE,
        0, pageY, rc.right, pageHeight,
        hwnd, NULL, hInst, NULL);
    
    // Create page-specific content
    switch (pageIndex) {
    case 0: // Files page
    {
        // H3 headline
        HWND hTitle = CreateWindowExW(0, L"STATIC", L"Files Management",
            WS_CHILD | WS_VISIBLE | SS_LEFT,
            20, 15, rc.right - 40, 30,
            s_hCurrentPage, NULL, hInst, NULL);
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
        
        // Install directory label
        HWND hInstallLabel = CreateWindowExW(0, L"STATIC", L"Install directory:",
            WS_CHILD | WS_VISIBLE | SS_LEFT,
            290, 55, 150, 20,
            s_hCurrentPage, NULL, hInst, NULL);
        
        // Install directory edit field
        std::wstring defaultPath = L"C:\\Program Files\\" + s_currentProject.name;
        HWND hInstallEdit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", defaultPath.c_str(),
            WS_CHILD | WS_VISIBLE | WS_BORDER | ES_LEFT | ES_AUTOHSCROLL,
            290, 75, rc.right - 355, 22,
            s_hCurrentPage, (HMENU)101, hInst, NULL);
        
        // Browse button for install directory (child of main window for WM_DRAWITEM)
        CreateCustomButtonWithIcon(hwnd, IDC_BROWSE_INSTALL_DIR, L"...", ButtonColor::Blue,
            L"shell32.dll", 4, rc.right - 55, s_toolbarHeight + 75, 35, 22, hInst);
        
        // Calculate split pane dimensions (TreeView 30%, ListView 70%)
        int viewTop = 150;  // Moved down to make room for Remove button
        int viewHeight = pageHeight - 160;
        int treeWidth = (int)((rc.right - 50) * 0.3);
        int listWidth = (rc.right - 50) - treeWidth - 5; // 5px gap
        
        // TreeView on the left (folder hierarchy) - child of main window to receive notifications
        s_hTreeView = CreateWindowExW(WS_EX_CLIENTEDGE, WC_TREEVIEW, NULL,
            WS_CHILD | WS_VISIBLE | WS_BORDER | TVS_HASLINES | TVS_HASBUTTONS | TVS_LINESATROOT | TVS_SHOWSELALWAYS | TVS_CHECKBOXES,
            20, s_toolbarHeight + viewTop, treeWidth, viewHeight,
            hwnd, (HMENU)102, hInst, NULL);
        
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
        
        // Populate TreeView with source directory structure
        if (!s_currentProject.directory.empty()) {
            PopulateTreeView(s_hTreeView, s_currentProject.directory, defaultPath);
        }
        
        break;
    }
    case 1: // Registry page
    {
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
    // Extract directory components from install path
    // e.g., "C:\\Program Files\\SetupCraft" -> show "Program Files" as root -> "SetupCraft" as child
    
    // Find last two directory components
    std::wstring installPathCopy = installPath;
    size_t lastSlash = installPathCopy.find_last_of(L"\\/");
    std::wstring appName = (lastSlash != std::wstring::npos) ? installPathCopy.substr(lastSlash + 1) : installPathCopy;
    
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
    
    // Add parent node (e.g., "Program Files")
    HTREEITEM hParent = AddTreeNode(hTree, TVI_ROOT, parentPath, L"");
    
    // Add app node as child (e.g., "SetupCraft")
    HTREEITEM hRoot = AddTreeNode(hTree, hParent, appName, rootPath);
    
    // Recursively add all subdirectories under the app node
    AddTreeNodeRecursive(hTree, hRoot, rootPath);
    
    // Expand parent and root nodes
    TreeView_Expand(hTree, hParent, TVE_EXPAND);
    TreeView_Expand(hTree, hRoot, TVE_EXPAND);
    
    // Select app root and populate list with root contents
    TreeView_SelectItem(hTree, hRoot);
    PopulateListView(s_hListView, rootPath);
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
        
        // Handle TreeView selection change
        if (nmhdr->idFrom == 102 && nmhdr->code == TVN_SELCHANGED) {
            LPNMTREEVIEW pnmtv = (LPNMTREEVIEW)lParam;
            if (s_hListView && IsWindow(s_hListView) && pnmtv->itemNew.lParam) {
                // Get path from tree item
                wchar_t* folderPath = (wchar_t*)pnmtv->itemNew.lParam;
                if (folderPath && wcslen(folderPath) > 0) {
                    // Clear and repopulate ListView
                    ListView_DeleteAllItems(s_hListView);
                    PopulateListView(s_hListView, folderPath);
                    // Force ListView to redraw
                    InvalidateRect(s_hListView, NULL, TRUE);
                    UpdateWindow(s_hListView);
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
        
        break;
    }
    
    case WM_COMMAND: {
        int wmId = LOWORD(wParam);
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
        case IDC_FILES_ADD_DIR:
            MessageBoxW(hwnd, L"Add Folder functionality to be implemented", L"Add Folder", MB_OK | MB_ICONINFORMATION);
            return 0;
            
        case IDC_FILES_ADD_FILES:
            MessageBoxW(hwnd, L"Add Files functionality to be implemented", L"Add Files", MB_OK | MB_ICONINFORMATION);
            return 0;
            
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
            // Get the current install directory and extract the last component
            HWND hEdit = GetDlgItem(s_hCurrentPage, 101);
            if (!hEdit) return 0;
            
            wchar_t currentPath[MAX_PATH];
            GetWindowTextW(hEdit, currentPath, MAX_PATH);
            
            // Extract the last directory component (e.g., "SetupCraft")
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
        }
        break;
    }
    
    case WM_KEYDOWN: {
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
        // Handle custom button drawing for toolbar buttons
        if ((dis->CtlID >= IDC_TB_FILES && dis->CtlID <= IDC_TB_SCRIPTS) ||
            (dis->CtlID >= IDC_FILES_ADD_DIR && dis->CtlID <= IDC_FILES_REMOVE)) {
            ButtonColor color = (ButtonColor)GetWindowLongPtr(dis->hwndItem, GWLP_USERDATA);
            // Create bold font for buttons
            HFONT hFont = CreateFontW(-14, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
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
