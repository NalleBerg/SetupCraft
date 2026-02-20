#include "about.h"
#include <windows.h>
#include <richedit.h>
#include <string>
#include <sstream>

// Generic strings
static const wchar_t* ABOUT_TITLE = L"About Skeleton App";
static const wchar_t* LICENSE_TITLE = L"GNU General Public License v2";

// Forward declarations
static LRESULT CALLBACK AboutWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
static LRESULT CALLBACK LicenseWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

void ShowAboutDialog(HWND parent) {
    LoadLibraryW(L"Riched20.dll");

    const int W = 420, H = 260;
    HINSTANCE hi = GetModuleHandleW(NULL);

    WNDCLASSW wc = {};
    wc.lpfnWndProc = AboutWndProc;
    wc.hInstance = hi;
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.lpszClassName = L"SkeletonAboutClass";
    RegisterClassW(&wc);

    RECT pr = {0,0,0,0};
    if (parent && IsWindow(parent)) GetWindowRect(parent, &pr);
    int px = (pr.right + pr.left) / 2;
    int py = (pr.bottom + pr.top) / 2;
    int x = (px==0) ? CW_USEDEFAULT : (px - W/2);
    int y = (py==0) ? CW_USEDEFAULT : (py - H/2);

    HWND dlg = CreateWindowExW(WS_EX_DLGMODALFRAME, wc.lpszClassName, ABOUT_TITLE,
        WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_VISIBLE,
        x, y, W, H, parent, NULL, hi, NULL);

    if (!dlg) return;

    // Icon
    HICON hIcon = LoadIcon(NULL, IDI_APPLICATION);
    if (hIcon) {
        SendMessageW(dlg, WM_SETICON, ICON_SMALL, (LPARAM)hIcon);
        SendMessageW(dlg, WM_SETICON, ICON_BIG, (LPARAM)hIcon);
    }

    // Title text (static)
    CreateWindowExW(0, L"STATIC", L"Skeleton App",
        WS_CHILD | WS_VISIBLE | SS_CENTER,
        10, 10, W-20, 24, dlg, NULL, hi, NULL);

    // Description
    CreateWindowExW(0, L"STATIC", L"A minimal application template. Replace with your app code.",
        WS_CHILD | WS_VISIBLE | SS_LEFT,
        10, 40, W-20, 40, dlg, NULL, hi, NULL);

    // Buttons: View License, OK
    HWND btnLicense = CreateWindowExW(0, L"BUTTON", L"View License",
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        80, H - 60, 100, 30, dlg, (HMENU)1001, hi, NULL);

    HWND btnOK = CreateWindowExW(0, L"BUTTON", L"OK",
        WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON,
        240, H - 60, 100, 30, dlg, (HMENU)IDOK, hi, NULL);

    HFONT hFont = (HFONT)GetStockObject(DEFAULT_GUI_FONT);
    SendMessageW(btnLicense, WM_SETFONT, (WPARAM)hFont, TRUE);
    SendMessageW(btnOK, WM_SETFONT, (WPARAM)hFont, TRUE);

    if (parent && IsWindow(parent)) EnableWindow(parent, FALSE);

    MSG msg;
    while (GetMessageW(&msg, NULL, 0, 0)) {
        if (msg.hwnd == dlg && msg.message == WM_COMMAND && (LOWORD(msg.wParam) == IDOK)) break;
        if (!IsDialogMessageW(dlg, &msg)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }

    DestroyWindow(dlg);
    if (parent && IsWindow(parent)) EnableWindow(parent, TRUE);
}

void ShowLicenseDialog(HWND parent) {
    LoadLibraryW(L"Riched20.dll");

    const int W = 650, H = 500;
    HINSTANCE hi = GetModuleHandleW(NULL);

    WNDCLASSW wc = {};
    wc.lpfnWndProc = LicenseWndProc;
    wc.hInstance = hi;
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.lpszClassName = L"SkeletonLicenseClass";
    RegisterClassW(&wc);

    RECT pr = {0,0,0,0};
    if (parent && IsWindow(parent)) GetWindowRect(parent, &pr);
    int px = (pr.right + pr.left) / 2;
    int py = (pr.bottom + pr.top) / 2;
    int x = (px==0) ? CW_USEDEFAULT : (px - W/2);
    int y = (py==0) ? CW_USEDEFAULT : (py - H/2);

    HWND dlg = CreateWindowExW(WS_EX_DLGMODALFRAME, wc.lpszClassName, LICENSE_TITLE,
        WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_VISIBLE,
        x, y, W, H, parent, NULL, hi, NULL);

    if (!dlg) return;

    HICON hIcon = LoadIcon(NULL, IDI_APPLICATION);
    if (hIcon) {
        SendMessageW(dlg, WM_SETICON, ICON_SMALL, (LPARAM)hIcon);
        SendMessageW(dlg, WM_SETICON, ICON_BIG, (LPARAM)hIcon);
    }

    // Try load GnuLogo.bmp from exe dir (optional)
    wchar_t exePath[MAX_PATH];
    GetModuleFileNameW(NULL, exePath, MAX_PATH);
    std::wstring exeDir = exePath;
    size_t pos = exeDir.find_last_of(L"\\");
    if (pos != std::wstring::npos) exeDir = exeDir.substr(0, pos);

    std::wstring logoPath = exeDir + L"\\GnuLogo.bmp";
    HBITMAP hLogo = (HBITMAP)LoadImageW(NULL, logoPath.c_str(), IMAGE_BITMAP, 0, 0, LR_LOADFROMFILE);
    int logoHeight = 0;
    if (hLogo) {
        BITMAP bm;
        GetObject(hLogo, sizeof(bm), &bm);
        HWND hLogoWnd = CreateWindowExW(0, L"STATIC", NULL, WS_CHILD | WS_VISIBLE | SS_BITMAP | SS_CENTERIMAGE,
            (W - bm.bmWidth) / 2, 10, bm.bmWidth, bm.bmHeight, dlg, NULL, hi, NULL);
        SendMessageW(hLogoWnd, STM_SETIMAGE, IMAGE_BITMAP, (LPARAM)hLogo);
        logoHeight = bm.bmHeight + 10;
    }

    int editTop = logoHeight > 0 ? logoHeight + 20 : 10;
    HWND hEdit = CreateWindowExW(0, L"RichEdit20W", NULL,
        WS_CHILD | WS_VISIBLE | ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL | WS_VSCROLL,
        10, editTop, W - 20, H - editTop - 70, dlg, NULL, hi, NULL);

    if (!hEdit) { DestroyWindow(dlg); return; }

    SendMessageW(hEdit, EM_SETTARGETDEVICE, 0, 0);

    // Load GPLv2.md from exe dir
    std::wstring licensePath = exeDir + L"\\GPLv2.md";
    HANDLE hFile = CreateFileW(licensePath.c_str(), GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile != INVALID_HANDLE_VALUE) {
        DWORD fileSize = GetFileSize(hFile, NULL);
        if (fileSize > 0 && fileSize < 2*1024*1024) {
            char* buffer = new char[fileSize + 1];
            DWORD bytesRead = 0;
            if (ReadFile(hFile, buffer, fileSize, &bytesRead, NULL)) {
                buffer[bytesRead] = '\0';
                int wideSize = MultiByteToWideChar(CP_UTF8, 0, buffer, bytesRead, NULL, 0);
                wchar_t* wbuf = new wchar_t[wideSize + 1];
                MultiByteToWideChar(CP_UTF8, 0, buffer, bytesRead, wbuf, wideSize);
                wbuf[wideSize] = L'\0';
                SendMessageW(hEdit, EM_REPLACESEL, FALSE, (LPARAM)wbuf);
                delete[] wbuf;
            }
            delete[] buffer;
        }
        CloseHandle(hFile);
    } else {
        const wchar_t* notfound = L"GPLv2.md not found in application folder.";
        SendMessageW(hEdit, EM_REPLACESEL, FALSE, (LPARAM)notfound);
    }

    // OK button
    HWND btnOK = CreateWindowExW(0, L"BUTTON", L"OK",
        WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON,
        (W-80)/2, H-60, 80, 30, dlg, (HMENU)IDOK, hi, NULL);
    SendMessageW(btnOK, WM_SETFONT, (WPARAM)GetStockObject(DEFAULT_GUI_FONT), TRUE);

    if (parent && IsWindow(parent)) EnableWindow(parent, FALSE);
    MSG msg;
    while (GetMessageW(&msg, NULL, 0, 0)) {
        if (msg.hwnd == dlg && msg.message == WM_COMMAND && LOWORD(msg.wParam) == IDOK) break;
        if (!IsDialogMessageW(dlg, &msg)) { TranslateMessage(&msg); DispatchMessageW(&msg); }
    }

    DestroyWindow(dlg);
    if (hLogo) DeleteObject(hLogo);
    if (parent && IsWindow(parent)) EnableWindow(parent, TRUE);
}

static LRESULT CALLBACK AboutWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_COMMAND:
        if (LOWORD(wParam) == IDOK) {
            PostQuitMessage(0);
            return 0;
        }
        if (LOWORD(wParam) == 1001) {
            ShowLicenseDialog(hwnd);
            return 0;
        }
        break;
    case WM_CLOSE:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

static LRESULT CALLBACK LicenseWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_COMMAND:
        if (LOWORD(wParam) == IDOK) { PostQuitMessage(0); return 0; }
        break;
    case WM_CLOSE:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}
