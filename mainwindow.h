#pragma once
#include <windows.h>
#include <commctrl.h>
#include <string>
#include <map>
#include <vector>
#include "db.h"

// Shared struct: file inside a virtual folder on the Files page
struct VirtualFolderFile {
    std::wstring sourcePath;
    std::wstring destination;
    std::wstring install_scope;
};

// Recursive snapshot of a single TreeView node (used to persist the Files page tree)
struct TreeNodeSnapshot {
    std::wstring text;
    std::wstring fullPath;
    std::vector<VirtualFolderFile> virtualFiles;
    std::vector<TreeNodeSnapshot> children;
};

// Main window class
class MainWindow {
public:
    static HWND Create(HINSTANCE hInstance, const ProjectRow &project, const std::map<std::wstring, std::wstring> &locale);
    static HWND CreateNew(HINSTANCE hInstance, const std::map<std::wstring, std::wstring> &locale);
    static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
    static void MarkAsModified();
    static void MarkAsSaved();
    static bool HasUnsavedChanges();
    static bool IsNewUnsavedProject();
    // Accessors for components needed by non-member helpers
    static HTREEITEM GetAskAtInstallRoot();
    static const std::map<std::wstring, std::wstring>& GetLocale();
    // Accessors for drag-and-drop helpers
    static HWND     GetFilesTreeView()      { return s_hTreeView; }
    static HTREEITEM GetProgramFilesRoot()  { return s_hProgramFilesRoot; }
    static HTREEITEM GetProgramDataRoot()   { return s_hProgramDataRoot; }
    static HTREEITEM GetAppDataRoot()       { return s_hAppDataRoot; }
    static HTREEITEM AddTreeNode(HWND hTree, HTREEITEM hParent, const std::wstring &text, const std::wstring &fullPath);
    
private:
    static void CreateMenuBar(HWND hwnd);
    static void CreateToolbar(HWND hwnd, HINSTANCE hInst);
    static void CreateTabControl(HWND hwnd, HINSTANCE hInst);
    static void CreateStatusBar(HWND hwnd, HINSTANCE hInst);
    static void SwitchTab(HWND hwnd, int tabIndex);
    static void SwitchPage(HWND hwnd, int pageIndex);
    static void ShowTooltip(HWND hwnd, int x, int y, const std::wstring &text);
    static void HideTooltip();
    static void PopulateTreeView(HWND hTree, const std::wstring &rootPath, const std::wstring &installPath);
    static void PopulateListView(HWND hList, const std::wstring &folderPath);
    static void UpdateInstallPathFromTree(HWND hwnd);
    static void UpdateComponentsButtonState(HWND hwnd);
    static void AddTreeNodeRecursive(HWND hTree, HTREEITEM hParent, const std::wstring &folderPath);
    static void SaveTreeSnapshot(HWND hTree, HTREEITEM hParent, std::vector<TreeNodeSnapshot> &out);
    static void RestoreTreeSnapshot(HWND hTree, HTREEITEM hParent, const std::vector<TreeNodeSnapshot> &nodes);
    
    // Store project data
    static ProjectRow s_currentProject;
    static std::map<std::wstring, std::wstring> s_locale;
    static HWND s_hTab;
    static HWND s_hStatus;
    static HWND s_hCurrentTabContent;
    static HWND s_hCurrentPage;
    static HWND s_hTooltip;
    static HWND s_hPageButton1;  // Track page-specific buttons
    static HWND s_hPageButton2;
    static HWND s_hTreeView;     // Track TreeView for file management
    static HWND s_hListView;     // Track ListView for file management
    static HTREEITEM s_hProgramFilesRoot; // Track Program Files root node
    static HTREEITEM s_hProgramDataRoot; // Track ProgramData root node
    static HTREEITEM s_hAppDataRoot;     // Track AppData (Roaming) root node
    static HTREEITEM s_hAskAtInstallRoot; // Virtual root for Ask-at-install scope
    static bool s_askAtInstallEnabled;    // Whether AskAtInstall mode is enabled for project
    static int s_toolbarHeight;
    static int s_currentPageIndex;
};
