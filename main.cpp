#include <windows.h>
#include <string>
#include <map>
#include <vector>
#include <algorithm>
#include <cwctype>
#include <fstream>
#include <sstream>
#include "languages.h"

// IDs
#ifndef IDC_LANG_COMBO
#define IDC_LANG_COMBO 101
#endif

// Simple Win32 app with one OK button. This is intended as the skeleton
// main window for new applications.

const wchar_t CLASS_NAME[] = L"SkeletonAppWindowClass";

static std::map<std::wstring, std::wstring> g_locale;
static std::vector<std::wstring> g_availableLocales;
static HFONT g_guiFont = NULL;

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

static std::wstring GetAppDataDir() {
    wchar_t buf[MAX_PATH];
    DWORD len = GetEnvironmentVariableW(L"APPDATA", buf, _countof(buf));
    if (len == 0 || len > _countof(buf)) return L"";
    std::wstring path(buf);
    path += L"\\SetupCraft";
    DWORD attrs = GetFileAttributesW(path.c_str());
    if (attrs == INVALID_FILE_ATTRIBUTES) {
        // try to create (ignore failures)
        CreateDirectoryW(path.c_str(), NULL);
    }
    return path;
}

static bool ReadSavedLocale(std::wstring &outCode) {
    outCode.clear();
    std::wstring appdir = GetAppDataDir();
    if (appdir.empty()) return false;
    std::wstring ini = appdir + L"\\SetupCraft.ini";
    int size_needed = WideCharToMultiByte(CP_UTF8, 0, ini.c_str(), -1, NULL, 0, NULL, NULL);
    if (size_needed <= 0) return false;
    std::string narrow(size_needed - 1, '\0');
    WideCharToMultiByte(CP_UTF8, 0, ini.c_str(), -1, &narrow[0], size_needed, NULL, NULL);
    std::ifstream f(narrow);
    if (!f.is_open()) return false;
    std::string line;
    while (std::getline(f, line)) {
        if (line.empty() || line[0] == '#') continue;
        auto pos = line.find('=');
        if (pos == std::string::npos) continue;
        std::string key = line.substr(0, pos);
        std::string val = line.substr(pos + 1);
        auto trim = [](std::string &s){ size_t a = s.find_first_not_of(" \t\r\n"); if (a==std::string::npos) { s.clear(); return; } size_t b = s.find_last_not_of(" \t\r\n"); s = s.substr(a, b - a + 1); };
        trim(key); trim(val);
        if (key == "language" || key == "lang") { outCode = Utf8ToW(val); return true; }
    }
    return false;
}

static void WriteSavedLocale(const std::wstring &code) {
    std::wstring appdir = GetAppDataDir();
    if (appdir.empty()) return;
    std::wstring ini = appdir + L"\\SetupCraft.ini";
    int size_needed = WideCharToMultiByte(CP_UTF8, 0, ini.c_str(), -1, NULL, 0, NULL, NULL);
    if (size_needed <= 0) return;
    std::string narrow(size_needed - 1, '\0');
    WideCharToMultiByte(CP_UTF8, 0, ini.c_str(), -1, &narrow[0], size_needed, NULL, NULL);
    std::ofstream f(narrow, std::ios::trunc);
    if (!f.is_open()) return;
    int sz = WideCharToMultiByte(CP_UTF8, 0, code.c_str(), -1, NULL, 0, NULL, NULL);
    std::string codeUtf8;
    if (sz > 0) {
        codeUtf8.resize(sz - 1);
        WideCharToMultiByte(CP_UTF8, 0, code.c_str(), -1, &codeUtf8[0], sz, NULL, NULL);
    }
    f << "language=" << codeUtf8 << "\n";
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
        // make the combo wider so the dropdown arrow isn't overlapped by the OK button
        const int comboLeft = 10;
        const int comboWidth = 260;
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
            g_guiFont = CreateFontW(-14, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                                    DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                    CLEARTYPE_QUALITY, VARIABLE_PITCH | FF_SWISS, L"Segoe UI");
        }
        if (g_guiFont) SendMessageW(hCombo, WM_SETFONT, (WPARAM)g_guiFont, TRUE);

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
                            auto itOk = g_locale.find(L"ok");
                            if (itOk != g_locale.end()) SetWindowTextW(GetDlgItem(hwnd, IDOK), itOk->second.c_str());
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

        // OK button (positioned to the right of the combo)
        const int okLeft = comboLeft + comboWidth + 10;
        std::wstring okText = L"OK";
        auto it = g_locale.find(L"ok");
        if (it != g_locale.end() && !it->second.empty()) okText = it->second;

        HWND hOk = CreateWindowW(L"BUTTON", okText.c_str(),
            WS_TABSTOP | WS_VISIBLE | WS_CHILD | BS_DEFPUSHBUTTON,
            okLeft, 10, 75, 25,
            hwnd, (HMENU)IDOK, hInst, NULL);
        if (g_guiFont) SendMessageW(hOk, WM_SETFONT, (WPARAM)g_guiFont, TRUE);
        return 0;
    }
    case WM_COMMAND:
        if (LOWORD(wParam) == IDOK) {
            PostMessage(hwnd, WM_CLOSE, 0, 0);
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
                        auto itOk = g_locale.find(L"ok");
                        if (itOk != g_locale.end()) SetWindowTextW(GetDlgItem(hwnd, IDOK), itOk->second.c_str());
                        // persist selection to AppData
                        WriteSavedLocale(code);
                    }
                }
            }
            return 0;
        }
        return 0;
    case WM_DESTROY:
        if (g_guiFont) { DeleteObject(g_guiFont); g_guiFont = NULL; }
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE, PWSTR pCmdLine, int nCmdShow) {
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

    HWND hwnd = CreateWindowExW(0, CLASS_NAME, L"Skeleton App",
        WS_OVERLAPPEDWINDOW & ~WS_MAXIMIZEBOX & ~WS_THICKFRAME,
        CW_USEDEFAULT, CW_USEDEFAULT, 420, 200,
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
