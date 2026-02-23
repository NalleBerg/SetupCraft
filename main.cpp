#include <windows.h>
#include <windowsx.h>
#include <commctrl.h>
#include <shlobj.h>
#include <string>
#include <map>
#include <vector>
#include <algorithm>
#include <cwctype>
#include <fstream>
#include <sstream>
#include <ctime>
#include "languages.h"
#include "db.h"
#include "button.h"
#include "ctrlw.h"
#include "mainwindow.h"

// IDs
#ifndef IDC_LANG_COMBO
#define IDC_LANG_COMBO 101
#endif
#ifndef IDC_GLOBE_ICON
#define IDC_GLOBE_ICON 102
#endif
#ifndef IDC_NEW_PROJECT_BTN
#define IDC_NEW_PROJECT_BTN 103
#endif
#ifndef IDC_OPEN_PROJECT_BTN
#define IDC_OPEN_PROJECT_BTN 104
#endif
#ifndef IDC_EXIT_BTN
#define IDC_EXIT_BTN 106
#endif
#ifndef IDC_DELETE_PROJECT_BTN
#define IDC_DELETE_PROJECT_BTN 107
#endif
#define IDC_PROJECT_LIST 105
#define IDD_OPEN_PROJECT 200
#define IDD_DELETE_PROJECT 201

// New Project Dialog IDs
#define IDD_NEW_PROJECT 202
#define IDC_NEW_PROJ_NAME 210
#define IDC_NEW_PROJ_DIR 211
#define IDC_NEW_PROJ_BROWSE 212
#define IDC_NEW_PROJ_DESC 213
#define IDC_NEW_PROJ_LANG 214
#define IDC_NEW_PROJ_VERSION 215

// Simple Win32 app with one OK button. This is intended as the skeleton
// main window for new applications.

const wchar_t CLASS_NAME[] = L"SkeletonAppWindowClass";
const wchar_t TOOLTIP_CLASS_NAME[] = L"CustomTooltipClass";

static std::map<std::wstring, std::wstring> g_locale;
static std::vector<std::wstring> g_availableLocales;
static HFONT g_guiFont = NULL;
static HFONT g_globeFont = NULL;
static HFONT g_tooltipFont = NULL;
static std::wstring g_tooltipText;
static std::vector<std::pair<std::wstring, std::wstring>> g_tooltipEntries; // country code, text
static HWND g_tooltipWindow = NULL;
static HWND g_globeIcon = NULL;
static bool g_mouseTracking = false;

static std::wstring Utf8ToW(const std::string &s) {
    if (s.empty()) return {};
    int required = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), NULL, 0);
    if (required == 0) return {};
    std::wstring out(required, 0);
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), &out[0], required);
    return out;
}

static std::wstring GetExeDir() {
    wchar_t exePath[MAX_PATH];
    if (!GetModuleFileNameW(NULL, exePath, _countof(exePath))) return L".";
    wchar_t *p = wcsrchr(exePath, L'\\');
    if (!p) return L".";
    *p = 0;
    return std::wstring(exePath);
}

static void TrimW(std::wstring &s) {
    size_t a = s.find_first_not_of(L" \t\r\n");
    if (a == std::wstring::npos) { s.clear(); return; }
    size_t b = s.find_last_not_of(L" \t\r\n");
    s = s.substr(a, b - a + 1);
}

// Forward declaration
static bool LoadLocaleFile(const std::wstring &code, std::map<std::wstring, std::wstring> &out);

static std::wstring GetCountryCode(const std::wstring &code) {
    // Extract country code (last 2 chars of locale code like en_GB -> GB)
    if (code.size() < 2) return L"";
    size_t pos = code.find(L'_');
    if (pos == std::wstring::npos) return L"";
    std::wstring country = code.substr(pos + 1);
    if (country.size() == 2) {
        return L"[" + country + L"] ";
    }
    return L"";
}

static std::wstring BuildMultilingualTooltip() {
    // Load select_language from all available locale files with country codes
    g_tooltipEntries.clear();
    for (const auto &code : g_availableLocales) {
        std::map<std::wstring, std::wstring> tempLocale;
        if (LoadLocaleFile(code, tempLocale)) {
            auto it = tempLocale.find(L"select_language");
            if (it != tempLocale.end() && !it->second.empty()) {
                std::wstring cc = GetCountryCode(code);
                g_tooltipEntries.push_back({cc, it->second});
            }
        }
    }
    
    // Sort by country code ascending
    std::sort(g_tooltipEntries.begin(), g_tooltipEntries.end(),
        [](const std::pair<std::wstring, std::wstring> &a, const std::pair<std::wstring, std::wstring> &b) {
            return a.first < b.first;
        });
    
    return L""; // Not used anymore, we draw directly
}

// Custom tooltip window procedure
LRESULT CALLBACK TooltipWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);
        
        // Get client area
        RECT rc;
        GetClientRect(hwnd, &rc);
        
        // Fill background with light yellow
        HBRUSH hBrush = CreateSolidBrush(RGB(255, 255, 225));
        FillRect(hdc, &rc, hBrush);
        DeleteObject(hBrush);
        
        // Draw border
        HPEN hPen = CreatePen(PS_SOLID, 1, RGB(0, 0, 0));
        HPEN hOldPen = (HPEN)SelectObject(hdc, hPen);
        MoveToEx(hdc, rc.left, rc.top, NULL);
        LineTo(hdc, rc.right - 1, rc.top);
        LineTo(hdc, rc.right - 1, rc.bottom - 1);
        LineTo(hdc, rc.left, rc.bottom - 1);
        LineTo(hdc, rc.left, rc.top);
        SelectObject(hdc, hOldPen);
        DeleteObject(hPen);
        
        // Draw table with translations in 4 columns: Code | Text | Code | Text
        SetBkMode(hdc, TRANSPARENT);
        if (g_tooltipFont) SelectObject(hdc, g_tooltipFont);
        
        const int startX = 10;
        const int startY = 10;
        const int rowHeight = 22;
        const int textCol1X = 65;      // First text column (codes right-aligned before this)
        const int textCol2X = 410;     // Second text column (codes right-aligned before this)
        
        // Process 2 entries per row
        for (size_t i = 0; i < g_tooltipEntries.size(); i += 2) {
            int row = (int)(i / 2);
            int y = startY + row * rowHeight;
            
            // Draw first entry (left side)
            // Country code in royal blue, right-aligned
            SetTextColor(hdc, RGB(65, 105, 225));
            SIZE sz1;
            GetTextExtentPoint32W(hdc, g_tooltipEntries[i].first.c_str(), (int)g_tooltipEntries[i].first.length(), &sz1);
            TextOutW(hdc, textCol1X - sz1.cx - 5, y, g_tooltipEntries[i].first.c_str(), (int)g_tooltipEntries[i].first.length());
            // Translation text in black
            SetTextColor(hdc, RGB(0, 0, 0));
            TextOutW(hdc, textCol1X, y, g_tooltipEntries[i].second.c_str(), (int)g_tooltipEntries[i].second.length());
            
            // Draw second entry (right side) if it exists
            if (i + 1 < g_tooltipEntries.size()) {
                // Country code in royal blue, right-aligned
                SetTextColor(hdc, RGB(65, 105, 225));
                SIZE sz2;
                GetTextExtentPoint32W(hdc, g_tooltipEntries[i + 1].first.c_str(), (int)g_tooltipEntries[i + 1].first.length(), &sz2);
                TextOutW(hdc, textCol2X - sz2.cx - 5, y, g_tooltipEntries[i + 1].first.c_str(), (int)g_tooltipEntries[i + 1].first.length());
                // Translation text in black
                SetTextColor(hdc, RGB(0, 0, 0));
                TextOutW(hdc, textCol2X, y, g_tooltipEntries[i + 1].second.c_str(), (int)g_tooltipEntries[i + 1].second.length());
            }
        }
        
        EndPaint(hwnd, &ps);
        return 0;
    }
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

static void NormalizeDisplayName(std::wstring &s) {
    // remove parenthetical part like " (Country)"
    size_t pos = s.find(L'(');
    if (pos != std::wstring::npos) {
        // drop any preceding space as well
        if (pos > 0 && s[pos-1] == L' ') pos--; 
        s = s.substr(0, pos);
    }
    TrimW(s);
    if (!s.empty()) {
        s[0] = (wchar_t)std::towupper(s[0]);
    }
}

static bool ReadSavedLocale(std::wstring &outCode) {
    return DB::GetSetting(L"language", outCode);
}

static void WriteSavedLocale(const std::wstring &code) {
    DB::SetSetting(L"language", code);
}

// Helper function to calculate centered position for a window
static void GetCenteredPosition(HWND hwndParent, int width, int height, int &outX, int &outY) {
    RECT rc;
    if (hwndParent && GetWindowRect(hwndParent, &rc)) {
        // Center on parent window
        outX = rc.left + (rc.right - rc.left - width) / 2;
        outY = rc.top + (rc.bottom - rc.top - height) / 2;
    } else {
        // Center on screen work area
        SystemParametersInfoW(SPI_GETWORKAREA, 0, &rc, 0);
        outX = rc.left + (rc.right - rc.left - width) / 2;
        outY = rc.top + (rc.bottom - rc.top - height) / 2;
    }
    
    // Ensure window is visible on screen
    if (outX < rc.left) outX = rc.left;
    if (outY < rc.top) outY = rc.top;
    if (outX + width > rc.right) outX = rc.right - width;
    if (outY + height > rc.bottom) outY = rc.bottom - height;
}

static bool LoadLocaleFile(const std::wstring &code, std::map<std::wstring, std::wstring> &out) {
    wchar_t path[MAX_PATH];
    swprintf_s(path, L"locale\\%s.txt", code.c_str());
    std::ifstream f;
    std::string narrowPath;
    // convert wide path to UTF-8 narrow path for ifstream
        // Build full path relative to executable directory: <exeDir>\locale\<code>.txt
        std::wstring exeDir = GetExeDir();
        std::wstring fullPath = exeDir + L"\\locale\\" + code + L".txt";
        int size_needed = WideCharToMultiByte(CP_UTF8, 0, fullPath.c_str(), -1, NULL, 0, NULL, NULL);
    if (size_needed <= 0) return false;
    narrowPath.resize(size_needed - 1);
        WideCharToMultiByte(CP_UTF8, 0, fullPath.c_str(), -1, &narrowPath[0], size_needed, NULL, NULL);
    f.open(narrowPath);
    if (!f.is_open()) return false;
    out.clear();
    std::string line;
    while (std::getline(f, line)) {
        if (line.empty()) continue;
        if (line[0] == '#') continue;
        auto pos = line.find('=');
        if (pos == std::string::npos) continue;
        std::string key = line.substr(0, pos);
        std::string val = line.substr(pos + 1);
        // trim
        auto trim = [](std::string &s){
            size_t a = s.find_first_not_of(" \t\r\n");
            size_t b = s.find_last_not_of(" \t\r\n");
            if (a==std::string::npos) { s.clear(); return; }
            s = s.substr(a, b - a + 1);
        };
        trim(key); trim(val);
        out[Utf8ToW(key)] = Utf8ToW(val);
    }
    return true;
}

static void LoadAvailableLocales(std::vector<std::wstring> &out) {
    out.clear();
    WIN32_FIND_DATAW fd;
        // Search under <exeDir>\locale\*.txt so packaged exe finds files next to it
        std::wstring exeDir = GetExeDir();
        std::wstring searchPath = exeDir + L"\\locale\\*.txt";
        HANDLE h = FindFirstFileW(searchPath.c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return;
    do {
        if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
            std::wstring name = fd.cFileName; // e.g. en_GB.txt
            size_t pos = name.rfind(L".txt");
            if (pos != std::wstring::npos) {
                std::wstring code = name.substr(0, pos);
                out.push_back(code);
            }
        }
    } while (FindNextFileW(h, &fd));
    FindClose(h);
}

// Dialog procedure for New Project dialog
LRESULT CALLBACK NewProjectDlgProc(HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_CREATE: {
        HINSTANCE hInst = (HINSTANCE)GetWindowLongPtr(hDlg, GWLP_HINSTANCE);
        
        // Title
        auto itTitle = g_locale.find(L"new_project_title");
        std::wstring titleText = (itTitle != g_locale.end()) ? itTitle->second : L"Create New Project";
        HWND hTitle = CreateWindowExW(0, L"STATIC", titleText.c_str(),
            WS_CHILD | WS_VISIBLE | SS_LEFT,
            20, 15, 500, 25, hDlg, NULL, hInst, NULL);
        HFONT hTitleFont = CreateFontW(-18, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
        if (hTitleFont) SendMessageW(hTitle, WM_SETFONT, (WPARAM)hTitleFont, TRUE);
        
        // Project Name label and edit
        auto itNameLabel = g_locale.find(L"new_proj_name_label");
        std::wstring nameLabelText = (itNameLabel != g_locale.end()) ? itNameLabel->second : L"Project Name:";
        CreateWindowExW(0, L"STATIC", nameLabelText.c_str(),
            WS_CHILD | WS_VISIBLE | SS_LEFT,
            20, 55, 150, 20, hDlg, NULL, hInst, NULL);
        HWND hNameEdit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
            WS_CHILD | WS_VISIBLE | WS_BORDER | ES_LEFT | ES_AUTOHSCROLL,
            20, 75, 500, 25, hDlg, (HMENU)IDC_NEW_PROJ_NAME, hInst, NULL);
        if (g_guiFont) SendMessageW(hNameEdit, WM_SETFONT, (WPARAM)g_guiFont, TRUE);
        
        // Directory label, edit, and browse button
        auto itDirLabel = g_locale.find(L"new_proj_dir_label");
        std::wstring dirLabelText = (itDirLabel != g_locale.end()) ? itDirLabel->second : L"Source Directory:";
        CreateWindowExW(0, L"STATIC", dirLabelText.c_str(),
            WS_CHILD | WS_VISIBLE | SS_LEFT,
            20, 110, 150, 20, hDlg, NULL, hInst, NULL);
        HWND hDirEdit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
            WS_CHILD | WS_VISIBLE | WS_BORDER | ES_LEFT | ES_AUTOHSCROLL,
            20, 130, 440, 25, hDlg, (HMENU)IDC_NEW_PROJ_DIR, hInst, NULL);
        if (g_guiFont) SendMessageW(hDirEdit, WM_SETFONT, (WPARAM)g_guiFont, TRUE);
        CreateCustomButtonWithIcon(hDlg, IDC_NEW_PROJ_BROWSE, L"...", ButtonColor::Blue,
            L"shell32.dll", 4, 470, 130, 50, 25, hInst);
        
        // Description label and edit
        auto itDescLabel = g_locale.find(L"new_proj_desc_label");
        std::wstring descLabelText = (itDescLabel != g_locale.end()) ? itDescLabel->second : L"Description (optional):";
        CreateWindowExW(0, L"STATIC", descLabelText.c_str(),
            WS_CHILD | WS_VISIBLE | SS_LEFT,
            20, 165, 200, 20, hDlg, NULL, hInst, NULL);
        HWND hDescEdit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
            WS_CHILD | WS_VISIBLE | WS_BORDER | ES_LEFT | ES_MULTILINE | ES_AUTOVSCROLL | WS_VSCROLL,
            20, 185, 500, 60, hDlg, (HMENU)IDC_NEW_PROJ_DESC, hInst, NULL);
        if (g_guiFont) SendMessageW(hDescEdit, WM_SETFONT, (WPARAM)g_guiFont, TRUE);
        
        // Language label and combobox
        auto itLangLabel = g_locale.find(L"new_proj_lang_label");
        std::wstring langLabelText = (itLangLabel != g_locale.end()) ? itLangLabel->second : L"Default Language:";
        CreateWindowExW(0, L"STATIC", langLabelText.c_str(),
            WS_CHILD | WS_VISIBLE | SS_LEFT,
            20, 255, 200, 20, hDlg, NULL, hInst, NULL);
        HWND hLangCombo = CreateWindowExW(0, L"COMBOBOX", L"",
            WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | WS_VSCROLL,
            20, 275, 240, 200, hDlg, (HMENU)IDC_NEW_PROJ_LANG, hInst, NULL);
        if (g_guiFont) SendMessageW(hLangCombo, WM_SETFONT, (WPARAM)g_guiFont, TRUE);
        
        // Populate language combobox
        auto displayNames = GetCanonicalDisplayNames();
        int englishIdx = 0;
        int idx = 0;
        for (const auto &pair : displayNames) {
            ComboBox_AddString(hLangCombo, pair.second.c_str());
            ComboBox_SetItemData(hLangCombo, idx, new std::wstring(pair.first));
            if (pair.first == L"en_GB") englishIdx = idx;
            idx++;
        }
        ComboBox_SetCurSel(hLangCombo, englishIdx);
        
        // Version label and edit
        auto itVersionLabel = g_locale.find(L"new_proj_version_label");
        std::wstring versionLabelText = (itVersionLabel != g_locale.end()) ? itVersionLabel->second : L"Version:";
        CreateWindowExW(0, L"STATIC", versionLabelText.c_str(),
            WS_CHILD | WS_VISIBLE | SS_LEFT,
            280, 255, 100, 20, hDlg, NULL, hInst, NULL);
        HWND hVersionEdit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"1.0.0",
            WS_CHILD | WS_VISIBLE | WS_BORDER | ES_LEFT | ES_AUTOHSCROLL,
            280, 275, 240, 25, hDlg, (HMENU)IDC_NEW_PROJ_VERSION, hInst, NULL);
        if (g_guiFont) SendMessageW(hVersionEdit, WM_SETFONT, (WPARAM)g_guiFont, TRUE);
        
        // Create OK and Cancel buttons
        auto itOk = g_locale.find(L"ok");
        std::wstring okText = (itOk != g_locale.end()) ? itOk->second : L"OK";
        CreateCustomButtonWithIcon(hDlg, IDOK, okText, ButtonColor::Blue,
            L"imageres.dll", 89, 420, 320, 100, 30, hInst);
        
        auto itCancel = g_locale.find(L"cancel");
        std::wstring cancelText = (itCancel != g_locale.end()) ? itCancel->second : L"Cancel";
        CreateCustomButtonWithIcon(hDlg, IDCANCEL, cancelText, ButtonColor::Red,
            L"shell32.dll", 131, 290, 320, 120, 30, hInst);
        
        return 0;
    }
    case WM_COMMAND:
        if (LOWORD(wParam) == IDC_NEW_PROJ_BROWSE) {
            // Browse for source directory
            auto itBrowseTitle = g_locale.find(L"new_proj_browse_title");
            std::wstring browseTitleText = (itBrowseTitle != g_locale.end()) ? itBrowseTitle->second : L"Select source directory for the project";
            
            BROWSEINFOW bi = {};
            bi.hwndOwner = hDlg;
            bi.lpszTitle = browseTitleText.c_str();
            bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE | BIF_USENEWUI;
            
            LPITEMIDLIST pidl = SHBrowseForFolderW(&bi);
            if (pidl) {
                wchar_t path[MAX_PATH];
                if (SHGetPathFromIDListW(pidl, path)) {
                    SetDlgItemTextW(hDlg, IDC_NEW_PROJ_DIR, path);
                }
                CoTaskMemFree(pidl);
            }
            return 0;
        }
        if (LOWORD(wParam) == IDOK) {
            // Get values from controls
            wchar_t name[256] = {0};
            wchar_t dir[MAX_PATH] = {0};
            wchar_t desc[1024] = {0};
            wchar_t version[64] = {0};
            
            GetDlgItemTextW(hDlg, IDC_NEW_PROJ_NAME, name, 256);
            GetDlgItemTextW(hDlg, IDC_NEW_PROJ_DIR, dir, MAX_PATH);
            GetDlgItemTextW(hDlg, IDC_NEW_PROJ_DESC, desc, 1024);
            GetDlgItemTextW(hDlg, IDC_NEW_PROJ_VERSION, version, 64);
            
            // Validate required fields
            if (wcslen(name) == 0) {
                auto itErrNoName = g_locale.find(L"new_proj_err_no_name");
                std::wstring errNoNameText = (itErrNoName != g_locale.end()) ? itErrNoName->second : L"Please enter a project name";
                MessageBoxW(hDlg, errNoNameText.c_str(), L"Validation Error", MB_OK | MB_ICONWARNING);
                return 0;
            }
            if (wcslen(dir) == 0) {
                auto itErrNoDir = g_locale.find(L"new_proj_err_no_dir");
                std::wstring errNoDirText = (itErrNoDir != g_locale.end()) ? itErrNoDir->second : L"Please select a source directory";
                MessageBoxW(hDlg, errNoDirText.c_str(), L"Validation Error", MB_OK | MB_ICONWARNING);
                return 0;
            }
            
            // Check if directory exists
            DWORD attrs = GetFileAttributesW(dir);
            if (attrs == INVALID_FILE_ATTRIBUTES || !(attrs & FILE_ATTRIBUTE_DIRECTORY)) {
                auto itErrInvalidDir = g_locale.find(L"new_proj_err_invalid_dir");
                std::wstring errInvalidDirText = (itErrInvalidDir != g_locale.end()) ? itErrInvalidDir->second : L"The selected directory does not exist";
                MessageBoxW(hDlg, errInvalidDirText.c_str(), L"Validation Error", MB_OK | MB_ICONWARNING);
                return 0;
            }
            
            // Get selected language code
            HWND hLangCombo = GetDlgItem(hDlg, IDC_NEW_PROJ_LANG);
            int sel = ComboBox_GetCurSel(hLangCombo);
            std::wstring langCode = L"en_GB";
            if (sel >= 0) {
                std::wstring* pCode = (std::wstring*)ComboBox_GetItemData(hLangCombo, sel);
                if (pCode) langCode = *pCode;
            }
            
            // Insert project into database
            int newId = 0;
            if (DB::InsertProject(name, dir, desc, langCode, version, newId)) {
                // Close this dialog
                HWND hParent = GetParent(hDlg);
                if (hParent) EnableWindow(hParent, TRUE);
                DestroyWindow(hDlg);
                
                // Open main window with the new project
                ProjectRow newProj;
                if (DB::GetProject(newId, newProj)) {
                    HINSTANCE hInst = (HINSTANCE)GetWindowLongPtr(hDlg, GWLP_HINSTANCE);
                    MainWindow::Create(hInst, newProj, g_locale);
                    if (hParent) {
                        ShowWindow(hParent, SW_HIDE);
                    }
                }
            } else {
                auto itErrDb = g_locale.find(L"new_proj_err_db");
                std::wstring errDbText = (itErrDb != g_locale.end()) ? itErrDb->second : L"Failed to create project in database";
                MessageBoxW(hDlg, errDbText.c_str(), L"Error", MB_OK | MB_ICONERROR);
            }
            
            return 0;
        }
        if (LOWORD(wParam) == IDCANCEL) {
            HWND hParent = GetParent(hDlg);
            if (hParent) EnableWindow(hParent, TRUE);
            DestroyWindow(hDlg);
            return 0;
        }
        break;
    case WM_DRAWITEM: {
        LPDRAWITEMSTRUCT dis = (LPDRAWITEMSTRUCT)lParam;
        if (dis->CtlID == IDOK || dis->CtlID == IDCANCEL || dis->CtlID == IDC_NEW_PROJ_BROWSE) {
            ButtonColor color = (ButtonColor)GetWindowLongPtr(dis->hwndItem, GWLP_USERDATA);
            LRESULT result = DrawCustomButton(dis, color, g_guiFont);
            return result;
        }
        break;
    }
    case WM_DESTROY: {
        // Clean up language combo item data
        HWND hLangCombo = GetDlgItem(hDlg, IDC_NEW_PROJ_LANG);
        if (hLangCombo) {
            int count = ComboBox_GetCount(hLangCombo);
            for (int i = 0; i < count; i++) {
                std::wstring* pCode = (std::wstring*)ComboBox_GetItemData(hLangCombo, i);
                delete pCode;
            }
        }
        break;
    }
    }
    return DefWindowProc(hDlg, msg, wParam, lParam);
}

// Dialog procedure for Delete Project dialog
LRESULT CALLBACK DeleteProjectDlgProc(HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_CREATE: {
        HINSTANCE hInst = (HINSTANCE)GetWindowLongPtr(hDlg, GWLP_HINSTANCE);
        
        // Create a ListView to show projects as a table
        HWND hList = CreateWindowExW(WS_EX_CLIENTEDGE, WC_LISTVIEW, NULL,
            WS_CHILD | WS_VISIBLE | LVS_REPORT | LVS_SINGLESEL | WS_BORDER,
            10, 10, 560, 300,
            hDlg, (HMENU)IDC_PROJECT_LIST, hInst, NULL);
        
        ListView_SetExtendedListViewStyle(hList, LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES);
        if (g_guiFont) SendMessageW(hList, WM_SETFONT, (WPARAM)g_guiFont, TRUE);
        
        // Add columns with i18n
        auto itColId = g_locale.find(L"col_id");
        std::wstring colIdText = (itColId != g_locale.end()) ? itColId->second : L"ID";
        
        auto itColName = g_locale.find(L"col_name");
        std::wstring colNameText = (itColName != g_locale.end()) ? itColName->second : L"Name";
        
        auto itColVersion = g_locale.find(L"col_version");
        std::wstring colVersionText = (itColVersion != g_locale.end()) ? itColVersion->second : L"Version";
        
        auto itColLastUpdated = g_locale.find(L"col_last_updated");
        std::wstring colLastUpdatedText = (itColLastUpdated != g_locale.end()) ? itColLastUpdated->second : L"Last Updated";
        
        LVCOLUMNW col = {};
        col.mask = LVCF_TEXT | LVCF_WIDTH;
        col.cx = 50;
        col.pszText = (LPWSTR)colIdText.c_str();
        ListView_InsertColumn(hList, 0, &col);
        
        col.cx = 200;
        col.pszText = (LPWSTR)colNameText.c_str();
        ListView_InsertColumn(hList, 1, &col);
        
        col.cx = 120;
        col.pszText = (LPWSTR)colVersionText.c_str();
        ListView_InsertColumn(hList, 2, &col);
        
        col.cx = 140;
        col.pszText = (LPWSTR)colLastUpdatedText.c_str();
        ListView_InsertColumn(hList, 3, &col);
        
        // Load projects from database
        auto projects = DB::ListProjects();
        for (const auto &proj : projects) {
            wchar_t idBuf[32];
            swprintf(idBuf, 32, L"%d", proj.id);
            
            // Convert timestamp to readable format
            time_t t = (time_t)proj.last_updated;
            struct tm *timeinfo = localtime(&t);
            wchar_t timeBuf[64];
            wcsftime(timeBuf, 64, L"%Y-%m-%d %H:%M", timeinfo);
            
            // Insert item
            LVITEMW item = {};
            item.mask = LVIF_TEXT | LVIF_PARAM;
            item.iItem = ListView_GetItemCount(hList);
            item.iSubItem = 0;
            item.pszText = idBuf;
            item.lParam = (LPARAM)proj.id;
            int idx = ListView_InsertItem(hList, &item);
            
            // Set subitems
            ListView_SetItemText(hList, idx, 1, (LPWSTR)proj.name.c_str());
            ListView_SetItemText(hList, idx, 2, (LPWSTR)proj.version.c_str());
            ListView_SetItemText(hList, idx, 3, timeBuf);
        }
        
        // Create OK and Cancel buttons
        auto itOk = g_locale.find(L"ok");
        std::wstring okText = (itOk != g_locale.end()) ? itOk->second : L"OK";
        CreateCustomButtonWithIcon(hDlg, IDOK, okText, ButtonColor::Blue,
            L"imageres.dll", 89, 480, 320, 100, 30, hInst);
        
        auto itCancel = g_locale.find(L"cancel");
        std::wstring cancelText = (itCancel != g_locale.end()) ? itCancel->second : L"Cancel";
        CreateCustomButtonWithIcon(hDlg, IDCANCEL, cancelText, ButtonColor::Red,
            L"shell32.dll", 131, 350, 320, 120, 30, hInst);
        
        return 0;
    }
    case WM_COMMAND:
        if (LOWORD(wParam) == IDOK) {
            HWND hList = GetDlgItem(hDlg, IDC_PROJECT_LIST);
            int sel = ListView_GetNextItem(hList, -1, LVNI_SELECTED);
            if (sel >= 0) {
                LVITEMW item = {};
                item.mask = LVIF_PARAM;
                item.iItem = sel;
                ListView_GetItem(hList, &item);
                int projId = (int)item.lParam;
                
                // Show confirmation dialog
                int result = MessageBoxW(hDlg, L"Are you sure you want to delete this project?", 
                    L"Confirm Delete", MB_YESNO | MB_ICONWARNING);
                
                if (result == IDYES) {
                    // TODO: Actually delete the project from database
                    MessageBoxW(hDlg, L"Project deletion not yet implemented", L"Info", MB_OK);
                }
                
                // Return to main window
                HWND hParent = GetParent(hDlg);
                if (hParent) EnableWindow(hParent, TRUE);
                DestroyWindow(hDlg);
            } else {
                MessageBoxW(hDlg, L"Please select a project", L"Info", MB_OK | MB_ICONINFORMATION);
            }
            return 0;
        }
        if (LOWORD(wParam) == IDCANCEL) {
            HWND hParent = GetParent(hDlg);
            if (hParent) EnableWindow(hParent, TRUE);
            DestroyWindow(hDlg);
            return 0;
        }
        break;
    case WM_NOTIFY: {
        NMHDR *nmhdr = (NMHDR*)lParam;
        if (nmhdr->idFrom == IDC_PROJECT_LIST) {
            if (nmhdr->code == NM_DBLCLK) {
                // Double-click on list item = same as OK
                SendMessageW(hDlg, WM_COMMAND, IDOK, 0);
                return 0;
            }
            else if (nmhdr->code == NM_CUSTOMDRAW) {
                NMLVCUSTOMDRAW *cd = (NMLVCUSTOMDRAW*)lParam;
                if (cd->nmcd.dwDrawStage == CDDS_PREPAINT) {
                    return CDRF_NOTIFYITEMDRAW;
                }
                else if (cd->nmcd.dwDrawStage == CDDS_ITEMPREPAINT) {
                    return CDRF_NOTIFYSUBITEMDRAW;
                }
                else if (cd->nmcd.dwDrawStage == (CDDS_ITEMPREPAINT | CDDS_SUBITEM)) {
                    // Make column 1 (Name) bold
                    if (cd->iSubItem == 1) {
                        if (g_guiFont) {
                            LOGFONTW lf = {};
                            GetObjectW(g_guiFont, sizeof(LOGFONTW), &lf);
                            lf.lfWeight = FW_BOLD;
                            HFONT hBoldFont = CreateFontIndirectW(&lf);
                            SelectObject(cd->nmcd.hdc, hBoldFont);
                            // Note: Font will leak, but for simplicity we'll allow it
                            // In production, should cache and manage fonts properly
                        }
                    }
                    return CDRF_NEWFONT;
                }
            }
        }
        break;
    }
    case WM_DRAWITEM: {
        LPDRAWITEMSTRUCT dis = (LPDRAWITEMSTRUCT)lParam;
        if (dis->CtlID == IDOK || dis->CtlID == IDCANCEL) {
            // Get stored color from GWLP_USERDATA
            ButtonColor color = (ButtonColor)GetWindowLongPtr(dis->hwndItem, GWLP_USERDATA);
            return DrawCustomButton(dis, color, g_guiFont);
        }
        break;
    }
    case WM_CLOSE:
        {
            HWND hParent = GetParent(hDlg);
            if (hParent) EnableWindow(hParent, TRUE);
            DestroyWindow(hDlg);
        }
        return 0;
    case WM_DESTROY:
        // Don't call PostQuitMessage - just let the dialog close and return to main window
        return 0;
    }
    return DefWindowProcW(hDlg, msg, wParam, lParam);
}

// Dialog procedure for Open Project dialog
static bool g_projectOpened = false;

LRESULT CALLBACK OpenProjectDlgProc(HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_CREATE: {
        HINSTANCE hInst = (HINSTANCE)GetWindowLongPtr(hDlg, GWLP_HINSTANCE);
        
        // Create a ListView to show projects as a table
        HWND hList = CreateWindowExW(WS_EX_CLIENTEDGE, WC_LISTVIEW, NULL,
            WS_CHILD | WS_VISIBLE | LVS_REPORT | LVS_SINGLESEL | WS_BORDER,
            10, 10, 560, 300,
            hDlg, (HMENU)IDC_PROJECT_LIST, hInst, NULL);
        
        ListView_SetExtendedListViewStyle(hList, LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES);
        if (g_guiFont) SendMessageW(hList, WM_SETFONT, (WPARAM)g_guiFont, TRUE);
        
        // Add columns with i18n
        auto itColId = g_locale.find(L"col_id");
        std::wstring colIdText = (itColId != g_locale.end()) ? itColId->second : L"ID";
        
        auto itColName = g_locale.find(L"col_name");
        std::wstring colNameText = (itColName != g_locale.end()) ? itColName->second : L"Name";
        
        auto itColVersion = g_locale.find(L"col_version");
        std::wstring colVersionText = (itColVersion != g_locale.end()) ? itColVersion->second : L"Version";
        
        auto itColLastUpdated = g_locale.find(L"col_last_updated");
        std::wstring colLastUpdatedText = (itColLastUpdated != g_locale.end()) ? itColLastUpdated->second : L"Last Updated";
        
        LVCOLUMNW col = {};
        col.mask = LVCF_TEXT | LVCF_WIDTH;
        col.cx = 50;
        col.pszText = (LPWSTR)colIdText.c_str();
        ListView_InsertColumn(hList, 0, &col);
        
        col.cx = 200;
        col.pszText = (LPWSTR)colNameText.c_str();
        ListView_InsertColumn(hList, 1, &col);
        
        col.cx = 120;
        col.pszText = (LPWSTR)colVersionText.c_str();
        ListView_InsertColumn(hList, 2, &col);
        
        col.cx = 140;
        col.pszText = (LPWSTR)colLastUpdatedText.c_str();
        ListView_InsertColumn(hList, 3, &col);
        
        // Load projects from database
        auto projects = DB::ListProjects();
        for (const auto &proj : projects) {
            wchar_t idBuf[32];
            swprintf(idBuf, 32, L"%d", proj.id);
            
            // Convert timestamp to readable format
            time_t t = (time_t)proj.last_updated;
            struct tm *timeinfo = localtime(&t);
            wchar_t timeBuf[64];
            wcsftime(timeBuf, 64, L"%Y-%m-%d %H:%M", timeinfo);
            
            // Insert item
            LVITEMW item = {};
            item.mask = LVIF_TEXT | LVIF_PARAM;
            item.iItem = ListView_GetItemCount(hList);
            item.iSubItem = 0;
            item.pszText = idBuf;
            item.lParam = (LPARAM)proj.id;
            int idx = ListView_InsertItem(hList, &item);
            
            // Set subitems
            ListView_SetItemText(hList, idx, 1, (LPWSTR)proj.name.c_str());
            ListView_SetItemText(hList, idx, 2, (LPWSTR)proj.version.c_str());
            ListView_SetItemText(hList, idx, 3, timeBuf);
        }
        
        // Create OK and Cancel buttons
        auto itOk = g_locale.find(L"ok");
        std::wstring okText = (itOk != g_locale.end()) ? itOk->second : L"OK";
        CreateCustomButtonWithIcon(hDlg, IDOK, okText, ButtonColor::Blue,
            L"imageres.dll", 89, 480, 320, 100, 30, hInst);
        
        auto itCancel = g_locale.find(L"cancel");
        std::wstring cancelText = (itCancel != g_locale.end()) ? itCancel->second : L"Cancel";
        CreateCustomButtonWithIcon(hDlg, IDCANCEL, cancelText, ButtonColor::Red,
            L"shell32.dll", 131, 350, 320, 120, 30, hInst);
        
        return 0;
    }
    case WM_COMMAND:
        if (LOWORD(wParam) == IDOK) {
            HWND hList = GetDlgItem(hDlg, IDC_PROJECT_LIST);
            int sel = ListView_GetNextItem(hList, -1, LVNI_SELECTED);
            if (sel >= 0) {
                LVITEMW item = {};
                item.mask = LVIF_PARAM;
                item.iItem = sel;
                ListView_GetItem(hList, &item);
                int projId = (int)item.lParam;
                
                // Load project from database
                ProjectRow project;
                if (DB::GetProject(projId, project)) {
                    // Create main window
                    HINSTANCE hInst = (HINSTANCE)GetWindowLongPtr(hDlg, GWLP_HINSTANCE);
                    MainWindow::Create(hInst, project, g_locale);
                    g_projectOpened = true;
                }
                
                DestroyWindow(hDlg);
            } else {
                MessageBoxW(hDlg, L"Please select a project", L"Info", MB_OK | MB_ICONINFORMATION);
            }
            return 0;
        }
        if (LOWORD(wParam) == IDCANCEL) {
            DestroyWindow(hDlg);
            return 0;
        }
        break;
    case WM_NOTIFY: {
        NMHDR *nmhdr = (NMHDR*)lParam;
        if (nmhdr->idFrom == IDC_PROJECT_LIST) {
            if (nmhdr->code == NM_DBLCLK) {
                // Double-click on list item = same as OK
                SendMessageW(hDlg, WM_COMMAND, IDOK, 0);
                return 0;
            }
            else if (nmhdr->code == NM_CUSTOMDRAW) {
                NMLVCUSTOMDRAW *cd = (NMLVCUSTOMDRAW*)lParam;
                if (cd->nmcd.dwDrawStage == CDDS_PREPAINT) {
                    return CDRF_NOTIFYITEMDRAW;
                }
                else if (cd->nmcd.dwDrawStage == CDDS_ITEMPREPAINT) {
                    return CDRF_NOTIFYSUBITEMDRAW;
                }
                else if (cd->nmcd.dwDrawStage == (CDDS_ITEMPREPAINT | CDDS_SUBITEM)) {
                    // Make column 1 (Name) bold
                    if (cd->iSubItem == 1) {
                        if (g_guiFont) {
                            LOGFONTW lf = {};
                            GetObjectW(g_guiFont, sizeof(LOGFONTW), &lf);
                            lf.lfWeight = FW_BOLD;
                            HFONT hBoldFont = CreateFontIndirectW(&lf);
                            SelectObject(cd->nmcd.hdc, hBoldFont);
                            // Note: Font will leak, but for simplicity we'll allow it
                            // In production, should cache and manage fonts properly
                        }
                    }
                    return CDRF_NEWFONT;
                }
            }
        }
        break;
    }
    case WM_DRAWITEM: {
        LPDRAWITEMSTRUCT dis = (LPDRAWITEMSTRUCT)lParam;
        if (dis->CtlID == IDOK || dis->CtlID == IDCANCEL) {
            // Get stored color from GWLP_USERDATA
            ButtonColor color = (ButtonColor)GetWindowLongPtr(dis->hwndItem, GWLP_USERDATA);
            return DrawCustomButton(dis, color, g_guiFont);
        }
        break;
    }
    case WM_CLOSE:
        DestroyWindow(hDlg);
        return 0;
    case WM_DESTROY:
        // Don't call PostQuitMessage - just let the dialog close and return to main window
        return 0;
    }
    return DefWindowProcW(hDlg, msg, wParam, lParam);
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_CREATE: {
        HINSTANCE hInst = (HINSTANCE)GetWindowLongPtr(hwnd, GWLP_HINSTANCE);

        // Load available locales from locale\*.txt
        LoadAvailableLocales(g_availableLocales);
        // Normalize available locales now so counts/order are consistent
        std::sort(g_availableLocales.begin(), g_availableLocales.end());
        g_availableLocales.erase(std::unique(g_availableLocales.begin(), g_availableLocales.end()), g_availableLocales.end());
        // default load en_GB if present
        if (!LoadLocaleFile(L"en_GB", g_locale)) {
            if (!g_availableLocales.empty()) LoadLocaleFile(g_availableLocales[0], g_locale);
        }

        // Set initial window title from locale if available
        auto itApp = g_locale.find(L"app_name");
        if (itApp != g_locale.end() && !itApp->second.empty()) {
            SetWindowTextW(hwnd, itApp->second.c_str());
        }

        // Language combo (show friendly/native names; store index as item data)
        // Center the combo with a globe icon to its left
        const int comboWidth = 260;
        const int iconSize = 30; // slightly bigger than dropdown height
        const int iconComboGap = 8;
        const int totalWidth = iconSize + iconComboGap + comboWidth;
        const int clientWidth = 420 - 16; // approximate client area
        const int startX = (clientWidth - totalWidth) / 2;
        const int iconLeft = startX;
        const int comboLeft = iconLeft + iconSize + iconComboGap;
        // compute dropdown height to try to show all locales (cap to avoid huge lists)
        int itemCount = (int)g_availableLocales.size();
        int itemHeight = 20; // approx per-item height in pixels
        int dropHeight = itemCount * itemHeight + 6;
        if (dropHeight > 400) dropHeight = 400;
        HWND hCombo = CreateWindowW(L"COMBOBOX", NULL,
            WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST,
            comboLeft, 10, comboWidth, dropHeight,
            hwnd, (HMENU)IDC_LANG_COMBO, hInst, NULL);

        // create and apply a GUI font that supports Cyrillic (Segoe UI)
        if (!g_guiFont) {
            g_guiFont = CreateFontW(-12, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,  // Reduced from -14 to -12 for smaller toolbar buttons
                                    DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                    CLEARTYPE_QUALITY, VARIABLE_PITCH | FF_SWISS, L"Segoe UI");
        }
        if (g_guiFont) SendMessageW(hCombo, WM_SETFONT, (WPARAM)g_guiFont, TRUE);

        // Create font for tooltip
        if (!g_tooltipFont) {
            g_tooltipFont = CreateFontW(-14, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                                        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                        CLEARTYPE_QUALITY, VARIABLE_PITCH | FF_SWISS, L"Segoe UI");
        }

        // Globe icon to the left of combo (using shell32.dll icon #13)
        HWND hIcon = CreateWindowW(L"STATIC", NULL,
            WS_CHILD | WS_VISIBLE | SS_ICON | SS_CENTERIMAGE,
            iconLeft, 8, iconSize, iconSize,
            hwnd, (HMENU)IDC_GLOBE_ICON, hInst, NULL);
        
        // Load planet icon from shell32.dll using PrivateExtractIconsW for transparent background
        wchar_t dllPath[MAX_PATH];
        GetSystemDirectoryW(dllPath, MAX_PATH);
        wcscat(dllPath, L"\\shell32.dll");
        
        HICON hGlobeIcon = NULL;
        UINT extracted = PrivateExtractIconsW(dllPath, 13, iconSize, iconSize, &hGlobeIcon, NULL, 1, 0);
        if (extracted > 0 && hGlobeIcon) {
            SendMessageW(hIcon, STM_SETICON, (WPARAM)hGlobeIcon, 0);
        }
        
        g_globeIcon = hIcon; // Store for later use

        // Build tooltip text
        g_tooltipText = BuildMultilingualTooltip();

                // Use canonical mapping from languages.cpp, fall back to system names or prettified codes
                auto canon = GetCanonicalDisplayNames();
                std::map<std::wstring, std::wstring> displayName;
                for (const auto &code : g_availableLocales) {
                    auto itCanon = canon.find(code);
                    if (itCanon != canon.end()) {
                        displayName[code] = itCanon->second;
                        continue;
                    }
                    // Fallback: ask system for native name (convert en_GB -> en-GB)
                    std::wstring localeName = code;
                    for (auto &ch : localeName) if (ch == L'_') ch = L'-';
                    size_t dashPos = localeName.find(L'-');
                    if (dashPos != std::wstring::npos && dashPos + 1 < localeName.size()) {
                        for (size_t i = dashPos + 1; i < localeName.size(); ++i) localeName[i] = std::towupper(localeName[i]);
                    }
                    wchar_t buf[256] = {0};
                    int ret = GetLocaleInfoEx(localeName.c_str(), LOCALE_SNATIVEDISPLAYNAME, buf, _countof(buf));
                    if (ret > 0) {
                        displayName[code] = std::wstring(buf);
                    } else {
                        ret = GetLocaleInfoEx(localeName.c_str(), LOCALE_SLOCALIZEDDISPLAYNAME, buf, _countof(buf));
                        if (ret > 0) displayName[code] = std::wstring(buf);
                        else {
                            std::wstring gen = code;
                            for (auto &c : gen) if (c == L'_') c = L' ';
                            NormalizeDisplayName(gen);
                            displayName[code] = gen;
                        }
                    }
                }

        // populate combo with display names, store index in item data
        for (size_t i = 0; i < g_availableLocales.size(); ++i) {
            std::wstring code = g_availableLocales[i];
            std::wstring name;
            auto itn = displayName.find(code);
            if (itn != displayName.end()) name = itn->second; else name = code;
            LRESULT idx = SendMessageW(hCombo, CB_ADDSTRING, 0, (LPARAM)name.c_str());
            SendMessageW(hCombo, CB_SETITEMDATA, (WPARAM)idx, (LPARAM)i);
        }

        // If user has previously selected a language, read it from AppData and set selection
        std::wstring savedLocale;
        if (ReadSavedLocale(savedLocale) && !savedLocale.empty()) {
            auto it = std::find(g_availableLocales.begin(), g_availableLocales.end(), savedLocale);
            if (it != g_availableLocales.end()) {
                int localeIdx = (int)std::distance(g_availableLocales.begin(), it);
                int count = (int)SendMessageW(hCombo, CB_GETCOUNT, 0, 0);
                for (int ci = 0; ci < count; ++ci) {
                    LRESULT itemData = SendMessageW(hCombo, CB_GETITEMDATA, ci, 0);
                    if ((int)itemData == localeIdx) {
                        SendMessageW(hCombo, CB_SETCURSEL, ci, 0);
                        // load that locale and update UI strings
                        if (LoadLocaleFile(savedLocale, g_locale)) {
                            auto itName = g_locale.find(L"app_name");
                            if (itName != g_locale.end()) SetWindowTextW(hwnd, itName->second.c_str());
                        }
                        break;
                    }
                }
            }
        } else {
            // default to en_GB if available
            for (int idx = 0; idx < (int)SendMessageW(hCombo, CB_GETCOUNT, 0, 0); ++idx) {
                LRESULT itemData = SendMessageW(hCombo, CB_GETITEMDATA, idx, 0);
                int li = (int)itemData;
                if (li >= 0 && li < (int)g_availableLocales.size() && g_availableLocales[li] == L"en_GB") {
                    SendMessageW(hCombo, CB_SETCURSEL, idx, 0);
                    break;
                }
            }
        }

        // Create buttons in 2x2 grid: New/Open on top, Delete/Exit on bottom
        const int buttonWidth = 180;
        const int buttonHeight = 30;
        const int buttonGapH = 10;  // horizontal gap
        const int buttonGapV = 10;  // vertical gap
        const int buttonsStartX = (clientWidth - (2 * buttonWidth + buttonGapH)) / 2;
        const int row1Y = 55;
        const int row2Y = row1Y + buttonHeight + buttonGapV;
        
        // Row 1, Column 1: New Project button
        auto itNewProj = g_locale.find(L"new_project");
        std::wstring newProjText = (itNewProj != g_locale.end()) ? itNewProj->second : L"New project";
        CreateCustomButtonWithIcon(hwnd, IDC_NEW_PROJECT_BTN, newProjText, ButtonColor::Blue,
            L"imageres.dll", 111, buttonsStartX, row1Y, buttonWidth, buttonHeight, hInst);
        
        // Row 1, Column 2: Open Project button
        auto itOpenProj = g_locale.find(L"open_project");
        std::wstring openProjText = (itOpenProj != g_locale.end()) ? itOpenProj->second : L"Open project";
        CreateCustomButtonWithIcon(hwnd, IDC_OPEN_PROJECT_BTN, openProjText, ButtonColor::Blue,
            L"shell32.dll", 4, buttonsStartX + buttonWidth + buttonGapH, row1Y, buttonWidth, buttonHeight, hInst);

        // Row 2, Column 1: Delete Project button
        auto itDelete = g_locale.find(L"delete_project");
        std::wstring deleteText = (itDelete != g_locale.end()) ? itDelete->second : L"Delete project";
        CreateCustomButtonWithIcon(hwnd, IDC_DELETE_PROJECT_BTN, deleteText, ButtonColor::Blue,
            L"shell32.dll", 31, buttonsStartX, row2Y, buttonWidth, buttonHeight, hInst);

        // Row 2, Column 2: Exit button
        auto itExit = g_locale.find(L"exit");
        std::wstring exitText = (itExit != g_locale.end()) ? itExit->second : L"Exit";
        CreateCustomButtonWithIcon(hwnd, IDC_EXIT_BTN, exitText, ButtonColor::Blue,
            L"shell32.dll", 27, buttonsStartX + buttonWidth + buttonGapH, row2Y, buttonWidth, buttonHeight, hInst);

        return 0;
    }
    case WM_COMMAND:
        if (LOWORD(wParam) == IDC_NEW_PROJECT_BTN) {
            // Open main window directly without dialog
            HINSTANCE hInst = (HINSTANCE)GetWindowLongPtr(hwnd, GWLP_HINSTANCE);
            MainWindow::CreateNew(hInst, g_locale);
            ShowWindow(hwnd, SW_HIDE);
            return 0;
        }
        if (LOWORD(wParam) == IDC_EXIT_BTN) {
            if (ShowQuitDialog(hwnd, g_locale)) {
                PostQuitMessage(0);
            }
            return 0;
        }
        if (LOWORD(wParam) == IDC_DELETE_PROJECT_BTN) {
            // Show delete project list dialog
            HINSTANCE hInst = (HINSTANCE)GetWindowLongPtr(hwnd, GWLP_HINSTANCE);
            
            // Register dialog class if not already registered
            static bool deleteDialogClassRegistered = false;
            if (!deleteDialogClassRegistered) {
                WNDCLASSEXW wcDlg = {};
                wcDlg.cbSize = sizeof(WNDCLASSEXW);
                wcDlg.style = CS_HREDRAW | CS_VREDRAW;
                wcDlg.lpfnWndProc = DeleteProjectDlgProc;
                wcDlg.hInstance = hInst;
                wcDlg.hCursor = LoadCursor(NULL, IDC_ARROW);
                wcDlg.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
                wcDlg.lpszClassName = L"DeleteProjectDialogClass";
                RegisterClassExW(&wcDlg);
                deleteDialogClassRegistered = true;
            }
            
            // Calculate centered position
            int x, y;
            GetCenteredPosition(hwnd, 600, 400, x, y);
            
            HWND hDlg = CreateWindowExW(
                WS_EX_DLGMODALFRAME | WS_EX_TOPMOST,
                L"DeleteProjectDialogClass",
                L"Delete Project",
                WS_POPUP | WS_CAPTION | WS_SYSMENU,
                x, y, 600, 400,
                hwnd, NULL, hInst, NULL);
            
            if (hDlg) {
                EnableWindow(hwnd, FALSE);
                ShowWindow(hDlg, SW_SHOW);
            }
            return 0;
        }
        if (LOWORD(wParam) == IDC_OPEN_PROJECT_BTN) {
            // Show project list dialog
            HINSTANCE hInst = (HINSTANCE)GetWindowLongPtr(hwnd, GWLP_HINSTANCE);
            
            // Register dialog class if not already registered
            static bool dialogClassRegistered = false;
            if (!dialogClassRegistered) {
                WNDCLASSEXW wcDlg = { };
                wcDlg.cbSize = sizeof(WNDCLASSEXW);
                wcDlg.lpfnWndProc = OpenProjectDlgProc;
                wcDlg.hInstance = hInst;
                wcDlg.lpszClassName = L"OpenProjectDlgClass";
                wcDlg.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
                wcDlg.hCursor = LoadCursorW(NULL, IDC_ARROW);
                RegisterClassExW(&wcDlg);
                dialogClassRegistered = true;
            }
            
            // Create modal dialog window
            auto itTitle = g_locale.find(L"open_project");
            std::wstring title = (itTitle != g_locale.end()) ? itTitle->second : L"Open Project";
            
            // Calculate centered position
            int x, y;
            GetCenteredPosition(hwnd, 600, 400, x, y);
            
            HWND hDlg = CreateWindowExW(
                WS_EX_DLGMODALFRAME | WS_EX_WINDOWEDGE,
                L"OpenProjectDlgClass",
                title.c_str(),
                WS_POPUP | WS_CAPTION | WS_SYSMENU,
                x, y, 600, 400,
                hwnd, NULL, hInst, NULL);
            
            if (hDlg) {
                
                // Show dialog modally
                EnableWindow(hwnd, FALSE);
                ShowWindow(hDlg, SW_SHOW);
                
                // Message loop for modal dialog
                g_projectOpened = false;
                MSG msg;
                while (IsWindow(hDlg) && GetMessageW(&msg, NULL, 0, 0)) {
                    TranslateMessage(&msg);
                    DispatchMessageW(&msg);
                }
                
                // If a project was opened, hide the entry screen
                if (g_projectOpened) {
                    ShowWindow(hwnd, SW_HIDE);
                } else {
                    EnableWindow(hwnd, TRUE);
                    SetForegroundWindow(hwnd);
                }
            }
            return 0;
        }
        if (LOWORD(wParam) == IDC_LANG_COMBO && HIWORD(wParam) == CBN_SELCHANGE) {
            HWND hCombo = GetDlgItem(hwnd, IDC_LANG_COMBO);
            int sel = (int)SendMessageW(hCombo, CB_GETCURSEL, 0, 0);
            if (sel >= 0) {
                // retrieve the stored locale index from item data
                LRESULT itemData = SendMessageW(hCombo, CB_GETITEMDATA, sel, 0);
                int localeIndex = (int)itemData;
                if (localeIndex >= 0 && localeIndex < (int)g_availableLocales.size()) {
                    std::wstring code = g_availableLocales[localeIndex];
                    if (LoadLocaleFile(code, g_locale)) {
                        // update UI strings
                        auto itName = g_locale.find(L"app_name");
                        if (itName != g_locale.end()) SetWindowTextW(hwnd, itName->second.c_str());
                        
                        // Update button texts
                        auto itNewProj = g_locale.find(L"new_project");
                        if (itNewProj != g_locale.end()) {
                            HWND hNewBtn = GetDlgItem(hwnd, IDC_NEW_PROJECT_BTN);
                            if (hNewBtn) UpdateButtonText(hNewBtn, itNewProj->second);
                        }
                        auto itOpenProj = g_locale.find(L"open_project");
                        if (itOpenProj != g_locale.end()) {
                            HWND hOpenBtn = GetDlgItem(hwnd, IDC_OPEN_PROJECT_BTN);
                            if (hOpenBtn) UpdateButtonText(hOpenBtn, itOpenProj->second);
                        }
                        auto itDelete = g_locale.find(L"delete_project");
                        if (itDelete != g_locale.end()) {
                            HWND hDeleteBtn = GetDlgItem(hwnd, IDC_DELETE_PROJECT_BTN);
                            if (hDeleteBtn) UpdateButtonText(hDeleteBtn, itDelete->second);
                        }
                        auto itExit = g_locale.find(L"exit");
                        if (itExit != g_locale.end()) {
                            HWND hExitBtn = GetDlgItem(hwnd, IDC_EXIT_BTN);
                            if (hExitBtn) UpdateButtonText(hExitBtn, itExit->second);
                        }
                        
                        // persist selection to AppData
                        WriteSavedLocale(code);
                    }
                }
            }
            return 0;
        }
        return 0;
    case WM_MOUSEMOVE: {
        // Check if mouse is over globe icon
        POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
        RECT rcIcon;
        GetWindowRect(g_globeIcon, &rcIcon);
        ScreenToClient(hwnd, (LPPOINT)&rcIcon.left);
        ScreenToClient(hwnd, (LPPOINT)&rcIcon.right);
        
        if (PtInRect(&rcIcon, pt)) {
            if (!g_tooltipWindow || !IsWindowVisible(g_tooltipWindow)) {
                // Show tooltip
                if (!g_tooltipWindow) {
                    HINSTANCE hInst = (HINSTANCE)GetWindowLongPtr(hwnd, GWLP_HINSTANCE);
                    // Calculate dynamic height: rows = (entries + 1) / 2, height = rows * rowHeight + padding
                    int numRows = ((int)g_tooltipEntries.size() + 1) / 2;
                    int tooltipHeight = numRows * 22 + 30;
                    g_tooltipWindow = CreateWindowExW(WS_EX_TOPMOST | WS_EX_TOOLWINDOW,
                        TOOLTIP_CLASS_NAME, L"",
                        WS_POPUP | WS_BORDER,
                        0, 0, 700, tooltipHeight,
                        hwnd, NULL, hInst, NULL);
                }
                
                if (g_tooltipWindow) {
                    // Position tooltip below the globe icon
                    POINT ptIcon = { rcIcon.left, rcIcon.bottom + 5 };
                    ClientToScreen(hwnd, &ptIcon);
                    SetWindowPos(g_tooltipWindow, HWND_TOPMOST, ptIcon.x, ptIcon.y, 0, 0, SWP_NOSIZE | SWP_SHOWWINDOW | SWP_NOACTIVATE);
                }
            }
            
            // Track mouse to detect when it leaves
            if (!g_mouseTracking) {
                TRACKMOUSEEVENT tme = { 0 };
                tme.cbSize = sizeof(TRACKMOUSEEVENT);
                tme.dwFlags = TME_LEAVE;
                tme.hwndTrack = hwnd;
                TrackMouseEvent(&tme);
                g_mouseTracking = true;
            }
        } else {
            // Hide tooltip if mouse is not over icon
            if (g_tooltipWindow && IsWindowVisible(g_tooltipWindow)) {
                ShowWindow(g_tooltipWindow, SW_HIDE);
            }
        }
        return 0;
    }
    case WM_MOUSELEAVE:
        g_mouseTracking = false;
        if (g_tooltipWindow && IsWindowVisible(g_tooltipWindow)) {
            ShowWindow(g_tooltipWindow, SW_HIDE);
        }
        return 0;
    case WM_CTLCOLORSTATIC: {
        HDC hdc = (HDC)wParam;
        HWND hControl = (HWND)lParam;
        if (GetDlgCtrlID(hControl) == IDC_GLOBE_ICON) {
            SetBkMode(hdc, TRANSPARENT);
            return (LRESULT)GetSysColorBrush(COLOR_WINDOW);
        }
        break;
    }
    case WM_DRAWITEM: {
        LPDRAWITEMSTRUCT dis = (LPDRAWITEMSTRUCT)lParam;
        if (dis->CtlID == IDC_NEW_PROJECT_BTN || dis->CtlID == IDC_OPEN_PROJECT_BTN || 
            dis->CtlID == IDC_EXIT_BTN || dis->CtlID == IDC_DELETE_PROJECT_BTN) {
            // Get stored color from GWLP_USERDATA
            ButtonColor color = (ButtonColor)GetWindowLongPtr(dis->hwndItem, GWLP_USERDATA);
            return DrawCustomButton(dis, color, g_guiFont);
        }
        break;
    }
    case WM_KEYDOWN:
        if (IsCtrlWPressed(msg, wParam)) {
            if (ShowQuitDialog(hwnd, g_locale)) {
                PostQuitMessage(0);
            }
            return 0;
        }
        break;
    case WM_CLOSE:
        if (ShowQuitDialog(hwnd, g_locale)) {
            DestroyWindow(hwnd);
        }
        return 0;
    case WM_DESTROY:
        if (g_tooltipWindow) { DestroyWindow(g_tooltipWindow); g_tooltipWindow = NULL; }
        if (g_guiFont) { DeleteObject(g_guiFont); g_guiFont = NULL; }
        if (g_globeFont) { DeleteObject(g_globeFont); g_globeFont = NULL; }
        if (g_tooltipFont) { DeleteObject(g_tooltipFont); g_tooltipFont = NULL; }
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE, PWSTR pCmdLine, int nCmdShow) {
    // Initialize database
    DB::InitDb();

    // Register tooltip window class
    WNDCLASSEXW wcTooltip = { };
    wcTooltip.cbSize = sizeof(WNDCLASSEXW);
    wcTooltip.lpfnWndProc = TooltipWndProc;
    wcTooltip.hInstance = hInstance;
    wcTooltip.lpszClassName = TOOLTIP_CLASS_NAME;
    wcTooltip.hbrBackground = (HBRUSH)(COLOR_INFOBK + 1);
    wcTooltip.hCursor = LoadCursorW(NULL, IDC_ARROW);
    RegisterClassExW(&wcTooltip);

    WNDCLASSEXW wc = { };
    wc.cbSize = sizeof(WNDCLASSEXW);
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = CLASS_NAME;
    wc.hCursor = LoadCursorW(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);

    // Load embedded icon resource (resource id 1). Use a large and a small icon.
    wc.hIcon = (HICON)LoadImageW(hInstance, MAKEINTRESOURCEW(1), IMAGE_ICON, 0, 0, LR_DEFAULTCOLOR);
    wc.hIconSm = (HICON)LoadImageW(hInstance, MAKEINTRESOURCEW(1), IMAGE_ICON, 16, 16, LR_DEFAULTCOLOR);

    RegisterClassExW(&wc);

    // Add mingw_dlls folder (if present) to DLL search path so runtime can find bundled DLLs
    wchar_t exePath[MAX_PATH];
    if (GetModuleFileNameW(NULL, exePath, _countof(exePath))) {
        wchar_t *p = wcsrchr(exePath, L'\\');
        if (p) {
            *p = 0; // terminate at directory
            wchar_t dllPath[MAX_PATH];
            swprintf_s(dllPath, L"%s\\mingw_dlls", exePath);
            SetDllDirectoryW(dllPath);
        }
    }

    // Calculate centered position for entry window
    int x, y;
    GetCenteredPosition(NULL, 420, 180, x, y);
    
    HWND hwnd = CreateWindowExW(0, CLASS_NAME, L"Skeleton App",
        WS_OVERLAPPEDWINDOW & ~WS_MAXIMIZEBOX & ~WS_THICKFRAME,
        x, y, 420, 180,
        NULL, NULL, hInstance, NULL);

    if (!hwnd) return 0;

    ShowWindow(hwnd, nCmdShow);

    MSG msg;
    while (GetMessageW(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    return (int)msg.wParam;
}

