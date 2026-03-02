#include "about.h"
#include <windows.h>
#include "dpi.h"
#include <richedit.h>
#include <string>
#include <sstream>
#include <gdiplus.h>
#pragma comment(lib, "gdiplus.lib")

using namespace Gdiplus;

// Version info - loaded from curver.txt at runtime
static std::wstring g_aboutPublished = L"26.02.2026 12:00";
static std::wstring g_aboutVersion = L"2026.02.26.08";

// Generic strings
static const wchar_t* ABOUT_TITLE = L"About SetupCraft";
static const wchar_t* LICENSE_TITLE = L"GNU General Public License v2";

// Forward declarations
static LRESULT CALLBACK AboutWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
static LRESULT CALLBACK LicenseWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
void AppendRichText(HWND hEdit, const std::wstring& text, bool bold, COLORREF color, int fontSize = 9, bool centered = false);

// Global for logo image
static Image* g_logoImage = nullptr;
static WNDPROC g_origEditProc = NULL;

// Load version info from curver.txt
void LoadVersionInfo() {
    wchar_t exePath[MAX_PATH];
    GetModuleFileNameW(NULL, exePath, MAX_PATH);
    std::wstring exeDir = exePath;
    size_t lastSlash = exeDir.find_last_of(L"\\");
    if (lastSlash != std::wstring::npos) {
        exeDir = exeDir.substr(0, lastSlash);
    }
    
    std::wstring curverPath = exeDir + L"\\curver.txt";
    HANDLE hFile = CreateFileW(curverPath.c_str(), GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    
    if (hFile != INVALID_HANDLE_VALUE) {
        DWORD fileSize = GetFileSize(hFile, NULL);
        if (fileSize > 0 && fileSize < 1024) {
            char* buffer = new char[fileSize + 1];
            DWORD bytesRead;
            if (ReadFile(hFile, buffer, fileSize, &bytesRead, NULL)) {
                buffer[bytesRead] = '\0';
                
                // Convert to wide string
                int wideSize = MultiByteToWideChar(CP_UTF8, 0, buffer, bytesRead, NULL, 0);
                wchar_t* wideBuffer = new wchar_t[wideSize + 1];
                MultiByteToWideChar(CP_UTF8, 0, buffer, bytesRead, wideBuffer, wideSize);
                wideBuffer[wideSize] = L'\0';
                
                // Parse the content
                std::wstring content(wideBuffer);
                delete[] wideBuffer;
                
                // Extract Published and Version lines
                size_t pubPos = content.find(L"Published: ");
                if (pubPos != std::wstring::npos) {
                    size_t pubEnd = content.find(L"\n", pubPos);
                    if (pubEnd != std::wstring::npos) {
                        g_aboutPublished = content.substr(pubPos + 11, pubEnd - pubPos - 11);
                        // Trim trailing \r if present
                        if (!g_aboutPublished.empty() && g_aboutPublished.back() == L'\r') {
                            g_aboutPublished.pop_back();
                        }
                    }
                }
                
                size_t verPos = content.find(L"Version: ");
                if (verPos != std::wstring::npos) {
                    size_t verEnd = content.find(L"\n", verPos);
                    if (verEnd != std::wstring::npos) {
                        g_aboutVersion = content.substr(verPos + 9, verEnd - verPos - 9);
                        // Trim trailing \r if present
                        if (!g_aboutVersion.empty() && g_aboutVersion.back() == L'\r') {
                            g_aboutVersion.pop_back();
                        }
                    } else {
                        // Last line, no newline
                        g_aboutVersion = content.substr(verPos + 9);
                        // Trim trailing \r if present
                        if (!g_aboutVersion.empty() && g_aboutVersion.back() == L'\r') {
                            g_aboutVersion.pop_back();
                        }
                    }
                }
            }
            delete[] buffer;
        }
        CloseHandle(hFile);
    }
}

// Helper to append formatted text to RichEdit control
void AppendRichText(HWND hEdit, const std::wstring& text, bool bold, COLORREF color, int fontSize, bool centered) {
    CHARFORMAT2W cf = {};
    cf.cbSize = sizeof(CHARFORMAT2W);
    cf.dwMask = CFM_COLOR | CFM_BOLD | CFM_SIZE | CFM_FACE;
    cf.crTextColor = color;
    cf.dwEffects = bold ? CFE_BOLD : 0;
    cf.yHeight = fontSize * 20; // twips
    wcscpy_s(cf.szFaceName, L"Segoe UI");
    
    // Get current text length and select end
    GETTEXTLENGTHEX gtl = {};
    gtl.flags = GTL_DEFAULT;
    gtl.codepage = 1200; // Unicode
    LONG len = (LONG)SendMessageW(hEdit, EM_GETTEXTLENGTHEX, (WPARAM)&gtl, 0);
    SendMessageW(hEdit, EM_SETSEL, len, len);
    
    // Apply paragraph formatting if centered
    if (centered) {
        PARAFORMAT2 pf = {};
        pf.cbSize = sizeof(PARAFORMAT2);
        pf.dwMask = PFM_ALIGNMENT;
        pf.wAlignment = PFA_CENTER;
        SendMessageW(hEdit, EM_SETPARAFORMAT, 0, (LPARAM)&pf);
    }
    
    // Set character format and insert text
    SendMessageW(hEdit, EM_SETCHARFORMAT, SCF_SELECTION, (LPARAM)&cf);
    SendMessageW(hEdit, EM_REPLACESEL, FALSE, (LPARAM)text.c_str());
    
    // Reset to left alignment
    if (centered) {
        PARAFORMAT2 pf = {};
        pf.cbSize = sizeof(PARAFORMAT2);
        pf.dwMask = PFM_ALIGNMENT;
        pf.wAlignment = PFA_LEFT;
        SendMessageW(hEdit, EM_SETPARAFORMAT, 0, (LPARAM)&pf);
    }
}

// Subclass procedure for RichEdit to draw logo at top
static LRESULT CALLBACK EditSubclassProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (msg == WM_PAINT && g_logoImage) {
        // Let RichEdit draw first
        CallWindowProcW(g_origEditProc, hwnd, msg, wParam, lParam);
        
        // Draw logo on top
        HDC hdc = GetDC(hwnd);
        Graphics graphics(hdc);
        
        // Get scroll position
        POINT pt = {0, 0};
        SendMessageW(hwnd, EM_GETSCROLLPOS, 0, (LPARAM)&pt);
        
        // Draw logo centered at top (scaled to 75%), offset by scroll
        RECT rc;
        GetClientRect(hwnd, &rc);
        int logoW = (int)(g_logoImage->GetWidth() * 0.75);
        int logoH = (int)(g_logoImage->GetHeight() * 0.75);
        int x = (rc.right - logoW) / 2;
        int y = 10 - pt.y; // Offset by scroll position
        
        // Only draw if at least partially visible
        if (y + logoH > 0 && y < rc.bottom) {
            graphics.DrawImage(g_logoImage, x, y, logoW, logoH);
        }
        
        ReleaseDC(hwnd, hdc);
        return 0;
    }
    return CallWindowProcW(g_origEditProc, hwnd, msg, wParam, lParam);
}

void ShowAboutDialog(HWND parent) {
    LoadLibraryW(L"Riched20.dll");
    
    // Load version info from curver.txt
    LoadVersionInfo();
    
    // Initialize GDI+
    GdiplusStartupInput gdiplusStartupInput;
    ULONG_PTR gdiplusToken;
    GdiplusStartup(&gdiplusToken, &gdiplusStartupInput, NULL);
    
    // Load logo image with transparency
    wchar_t logoPath[MAX_PATH];
    GetModuleFileNameW(NULL, logoPath, MAX_PATH);
    wchar_t* lastSlash = wcsrchr(logoPath, L'\\');
    if (lastSlash) {
        *(lastSlash + 1) = 0;
        wcscat_s(logoPath, L"SetupCraft.png");
    }
    g_logoImage = Image::FromFile(logoPath);
    
    // Create window
    const int W = S(420), H = S(560); // Increased height for logo
    RECT pr = {0,0,0,0};
    if (parent && IsWindow(parent)) GetWindowRect(parent, &pr);
    int px = (pr.right + pr.left) / 2;
    int py = (pr.bottom + pr.top) / 2;
    int x = (px==0) ? CW_USEDEFAULT : (px - W/2);
    int y = (py==0) ? CW_USEDEFAULT : (py - H/2);
    
    // Ensure dialog is not positioned above screen (minimum 30px from top)
    if (y < 30) y = 30;

    HINSTANCE hi = GetModuleHandleW(NULL);
    WNDCLASSW wc = {};
    wc.lpfnWndProc = AboutWndProc;
    wc.hInstance = hi;
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.lpszClassName = L"SetupCraftAboutClass";
    if (!GetClassInfoW(hi, wc.lpszClassName, &wc)) {
        RegisterClassW(&wc);
    }

    HWND dlg = CreateWindowExW(WS_EX_DLGMODALFRAME | WS_EX_WINDOWEDGE, 
        wc.lpszClassName, ABOUT_TITLE, 
        WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_VISIBLE, 
        x, y, W, H, parent, NULL, hi, NULL);
    
    if (!dlg) {
        MessageBoxW(parent, L"Unable to create About window.", L"Error", MB_OK | MB_ICONERROR);
        return;
    }
    
    // Set app icon
    HICON hIcon = LoadIcon(NULL, IDI_APPLICATION);
    if (hIcon) {
        SendMessageW(dlg, WM_SETICON, ICON_SMALL, (LPARAM)hIcon);
        SendMessageW(dlg, WM_SETICON, ICON_BIG, (LPARAM)hIcon);
    }

    // Create RichEdit control - fills entire area, logo will scroll with content
    HWND hEdit = CreateWindowExW(0, L"RichEdit20W", NULL,
        WS_CHILD | WS_VISIBLE | ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL | WS_VSCROLL,
        S(10), S(10), W - S(20), H - S(80),
        dlg, (HMENU)100, hi, NULL);
    
    if (!hEdit) {
        DestroyWindow(dlg);
        return;
    }
    
    // Subclass the RichEdit to draw logo
    g_origEditProc = (WNDPROC)SetWindowLongPtrW(hEdit, GWLP_WNDPROC, (LONG_PTR)EditSubclassProc);
    
    SendMessageW(hEdit, EM_SETTARGETDEVICE, 0, 0); // Enable word wrap
    SendMessageW(hEdit, EM_SETEVENTMASK, 0, ENM_LINK);
    
    // Add space for logo at top
    int logoH = g_logoImage ? (int)(g_logoImage->GetHeight() * 0.75) : 0;
    int logoSpaceLines = (logoH + 20) / 15; // Approximate lines needed
    for (int i = 0; i < logoSpaceLines; i++) {
        AppendRichText(hEdit, L"\r\n", false, RGB(0, 0, 0), 9, false);
    }
    
    // Build formatted About content
    AppendRichText(hEdit, L"SetupCraft\r\n", true, RGB(0, 70, 140), 16, true);
    AppendRichText(hEdit, L"Professional Windows Installer Creation Tool\r\n\r\n", false, RGB(80, 80, 80), 10, true);
    
    // Version info divider
    AppendRichText(hEdit, L"═════════════════════════════\r\n", false, RGB(100, 140, 180), 9, true);
    AppendRichText(hEdit, L"Published: ", true, RGB(0, 0, 0), 9, true);
    AppendRichText(hEdit, g_aboutPublished + L"\r\n", false, RGB(0, 0, 0), 9, true);
    AppendRichText(hEdit, L"Version: ", true, RGB(0, 0, 0), 9, true);
    AppendRichText(hEdit, g_aboutVersion + L"\r\n", false, RGB(0, 0, 0), 9, true);
    AppendRichText(hEdit, L"═════════════════════════════\r\n\r\n", false, RGB(100, 140, 180), 9, true);
    
    // Main description
    AppendRichText(hEdit, L"SetupCraft is a powerful Windows installer creation tool designed for developers and IT professionals. Create professional installation packages with full multilingual support for 20 languages.\r\n\r\n", false, RGB(40, 40, 40), 9, false);
    
    // Key features
    AppendRichText(hEdit, L"Key Features:\r\n\r\n", true, RGB(0, 70, 140), 10, false);
    
    AppendRichText(hEdit, L"• ", true, RGB(0, 70, 140), 9, false);
    AppendRichText(hEdit, L"File Management: ", true, RGB(0, 0, 0), 9, false);
    AppendRichText(hEdit, L"Add files and folders to your installation package with flexible organization.\r\n\r\n", false, RGB(60, 60, 60), 9, false);
    
    AppendRichText(hEdit, L"• ", true, RGB(0, 70, 140), 9, false);
    AppendRichText(hEdit, L"Registry Integration: ", true, RGB(0, 0, 0), 9, false);
    AppendRichText(hEdit, L"Configure Windows registry keys and values for seamless application integration.\r\n\r\n", false, RGB(60, 60, 60), 9, false);
    
    AppendRichText(hEdit, L"• ", true, RGB(0, 70, 140), 9, false);
    AppendRichText(hEdit, L"Shortcuts & Icons: ", true, RGB(0, 0, 0), 9, false);
    AppendRichText(hEdit, L"Create desktop and Start Menu shortcuts with custom icons.\r\n\r\n", false, RGB(60, 60, 60), 9, false);
    
    AppendRichText(hEdit, L"• ", true, RGB(0, 70, 140), 9, false);
    AppendRichText(hEdit, L"Dependencies: ", true, RGB(0, 0, 0), 9, false);
    AppendRichText(hEdit, L"Manage application dependencies and prerequisites.\r\n\r\n", false, RGB(60, 60, 60), 9, false);
    
    AppendRichText(hEdit, L"• ", true, RGB(0, 70, 140), 9, false);
    AppendRichText(hEdit, L"Custom Scripts: ", true, RGB(0, 0, 0), 9, false);
    AppendRichText(hEdit, L"Include pre-install and post-install scripts for advanced setup tasks.\r\n\r\n", false, RGB(60, 60, 60), 9, false);
    
    AppendRichText(hEdit, L"• ", true, RGB(0, 70, 140), 9, false);
    AppendRichText(hEdit, L"Multilingual Support: ", true, RGB(0, 0, 0), 9, false);
    AppendRichText(hEdit, L"Built-in support for 20 languages including English, German, French, Spanish, Italian, Dutch, Norwegian, Danish, Swedish, Polish, Portuguese, Romanian, Ukrainian, Greek, and more.\r\n\r\n", false, RGB(60, 60, 60), 9, false);
    
    AppendRichText(hEdit, L"SetupCraft generates native Windows installation packages ready for distribution. Perfect for software developers, system administrators, and anyone needing professional installer creation.\r\n\r\n", false, RGB(40, 40, 40), 9, false);
    
    // License divider
    AppendRichText(hEdit, L"═════════════════════════════\r\n\r\n", false, RGB(100, 140, 180), 9, true);
    AppendRichText(hEdit, L"Licensed under GNU General Public License v2\r\n\r\n", true, RGB(0, 70, 140), 9, false);
    
    // Scroll to top
    SendMessageW(hEdit, EM_SETSEL, 0, 0);
    SendMessageW(hEdit, EM_SCROLLCARET, 0, 0);
    
    // Create buttons at bottom (centered with gap)
    int btnY = H - S(65);  // Account for title bar and borders
    int btnWidth = S(90);
    int btnGap = S(10);
    int totalWidth = btnWidth * 2 + btnGap;
    int startX = (W - totalWidth) / 2;
    
    HWND btnLicense = CreateWindowExW(0, L"Button", L"View License",
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON | WS_TABSTOP,
        startX, btnY, btnWidth, S(30),
        dlg, (HMENU)1001, hi, NULL);
    
    HWND btnClose = CreateWindowExW(0, L"Button", L"Close",
        WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON | WS_TABSTOP,
        startX + btnWidth + btnGap, btnY, btnWidth, S(30),
        dlg, (HMENU)IDOK, hi, NULL);
    
    // Set font for buttons
    HFONT hFont = (HFONT)GetStockObject(DEFAULT_GUI_FONT);
    SendMessageW(btnLicense, WM_SETFONT, (WPARAM)hFont, TRUE);
    SendMessageW(btnClose, WM_SETFONT, (WPARAM)hFont, TRUE);
    
    // Modal loop
    if (parent && IsWindow(parent)) EnableWindow(parent, FALSE);
    SetFocus(btnClose);
    
    MSG msg;
    while (GetMessageW(&msg, NULL, 0, 0)) {
        if (!IsDialogMessageW(dlg, &msg)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }
    
    DestroyWindow(dlg);
    
    // Cleanup GDI+ resources
    if (g_logoImage) {
        delete g_logoImage;
        g_logoImage = nullptr;
    }
    GdiplusShutdown(gdiplusToken);
    
    if (parent && IsWindow(parent)) {
        EnableWindow(parent, TRUE);
        SetForegroundWindow(parent);
        BringWindowToTop(parent);
    }
}


void ShowLicenseDialog(HWND parent) {
    LoadLibraryW(L"Riched20.dll");
    
    // Create license window - larger to accommodate full GPL text
    const int W = S(650), H = S(600);
    RECT pr = {0,0,0,0};
    if (parent && IsWindow(parent)) GetWindowRect(parent, &pr);
    int px = (pr.right + pr.left) / 2;
    int py = (pr.bottom + pr.top) / 2;
    int x = (px==0) ? CW_USEDEFAULT : (px - W/2);
    int y = (py==0) ? CW_USEDEFAULT : (py - H/2);

    HINSTANCE hi = GetModuleHandleW(NULL);
    WNDCLASSW wc = {};
    wc.lpfnWndProc = LicenseWndProc;
    wc.hInstance = hi;
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.lpszClassName = L"SetupCraftLicenseClass";
    if (!GetClassInfoW(hi, wc.lpszClassName, &wc)) {
        RegisterClassW(&wc);
    }

    HWND dlg = CreateWindowExW(WS_EX_DLGMODALFRAME | WS_EX_WINDOWEDGE, 
        wc.lpszClassName, LICENSE_TITLE, 
        WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_VISIBLE, 
        x, y, W, H, parent, NULL, hi, NULL);
    
    if (!dlg) {
        MessageBoxW(parent, L"Unable to create License window.", L"Error", MB_OK | MB_ICONERROR);
        return;
    }
    
    // Set app icon
    HICON hIcon = LoadIcon(NULL, IDI_APPLICATION);
    if (hIcon) {
        SendMessageW(dlg, WM_SETICON, ICON_SMALL, (LPARAM)hIcon);
        SendMessageW(dlg, WM_SETICON, ICON_BIG, (LPARAM)hIcon);
    }
    
    // Load and display GNU logo at top
    wchar_t exePath[MAX_PATH];
    GetModuleFileNameW(NULL, exePath, MAX_PATH);
    std::wstring exeDir = exePath;
    size_t lastSlash = exeDir.find_last_of(L"\\");
    if (lastSlash != std::wstring::npos) {
        exeDir = exeDir.substr(0, lastSlash);
    }
    
    // Try to load GNU logo (BMP format)
    std::wstring logoPath = exeDir + L"\\GnuLogo.bmp";
    HBITMAP hLogoBitmap = (HBITMAP)LoadImageW(NULL, logoPath.c_str(), IMAGE_BITMAP, 0, 0, LR_LOADFROMFILE);
    int logoHeight = 0;
    
    if (hLogoBitmap) {
        BITMAP bmp;
        GetObject(hLogoBitmap, sizeof(BITMAP), &bmp);
        logoHeight = bmp.bmHeight + 10; // Add some padding
        
        HWND hLogoWnd = CreateWindowExW(0, L"Static", NULL,
            WS_CHILD | WS_VISIBLE | SS_BITMAP | SS_CENTERIMAGE,
            (W - bmp.bmWidth) / 2, 10, bmp.bmWidth, bmp.bmHeight,
            dlg, (HMENU)101, hi, NULL);
        SendMessageW(hLogoWnd, STM_SETIMAGE, IMAGE_BITMAP, (LPARAM)hLogoBitmap);
    }

    // Create RichEdit control for license text - positioned below logo
    int editTop = logoHeight > 0 ? logoHeight + 20 : 10;
    HWND hEdit = CreateWindowExW(0, L"RichEdit20W", NULL,
        WS_CHILD | WS_VISIBLE | ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL | WS_VSCROLL,
        S(10), editTop, W - S(20), H - editTop - S(70),
        dlg, (HMENU)100, hi, NULL);
    
    if (!hEdit) {
        DestroyWindow(dlg);
        return;
    }
    
    SendMessageW(hEdit, EM_SETTARGETDEVICE, 0, 0); // Enable word wrap
    SendMessageW(hEdit, EM_SETEVENTMASK, 0, ENM_LINK);
    
    // Load and parse GPLv2.md file
    std::wstring licensePath = exeDir + L"\\GPLv2.md";
    HANDLE hFile = CreateFileW(licensePath.c_str(), GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    
    if (hFile != INVALID_HANDLE_VALUE) {
        DWORD fileSize = GetFileSize(hFile, NULL);
        if (fileSize > 0 && fileSize < 1024*1024) { // Max 1MB
            char* buffer = new char[fileSize + 1];
            DWORD bytesRead;
            if (ReadFile(hFile, buffer, fileSize, &bytesRead, NULL)) {
                buffer[bytesRead] = '\0';
                
                // Convert to wide string
                int wideSize = MultiByteToWideChar(CP_UTF8, 0, buffer, bytesRead, NULL, 0);
                wchar_t* wideBuffer = new wchar_t[wideSize + 1];
                MultiByteToWideChar(CP_UTF8, 0, buffer, bytesRead, wideBuffer, wideSize);
                wideBuffer[wideSize] = L'\0';
                
                // Parse and format the license text
                std::wstring text(wideBuffer);
                delete[] wideBuffer;
                
                // Add title
                AppendRichText(hEdit, L"GNU GENERAL PUBLIC LICENSE\r\n", true, RGB(0, 70, 140), 14, true);
                AppendRichText(hEdit, L"Version 2, June 1991\r\n\r\n", false, RGB(0, 70, 140), 10, true);
                
                // Parse the content
                size_t pos = 0;
                std::wstring line;
                std::wstringstream ss(text);
                
                while (std::getline(ss, line)) {
                    // Remove carriage return if present
                    if (!line.empty() && line.back() == L'\r') {
                        line.pop_back();
                    }
                    
                    // Check for major section headers
                    if (line.find(L"TERMS AND CONDITIONS") != std::wstring::npos) {
                        AppendRichText(hEdit, L"\r\n" + line + L"\r\n", true, RGB(139, 0, 0), 11, false);
                    } else if (line.find(L"NO WARRANTY") != std::wstring::npos) {
                        AppendRichText(hEdit, L"\r\n" + line + L"\r\n", true, RGB(139, 0, 0), 11, false);
                    } else if (line == L"Preamble") {
                        AppendRichText(hEdit, L"\r\n" + line + L"\r\n", true, RGB(0, 70, 140), 12, false);
                    } else if (line.length() > 0 && line.length() < 150 && 
                               (line.find(L"0.") == 0 || line.find(L"1.") == 0 || line.find(L"2.") == 0 || 
                                line.find(L"3.") == 0 || line.find(L"4.") == 0 || line.find(L"5.") == 0 || 
                                line.find(L"6.") == 0 || line.find(L"7.") == 0 || line.find(L"8.") == 0 || 
                                line.find(L"9.") == 0 || line.find(L"10.") == 0 || line.find(L"11.") == 0 || 
                                line.find(L"12.") == 0)) {
                        // Numbered sections
                        AppendRichText(hEdit, L"\r\n" + line + L"\r\n", true, RGB(0, 0, 139), 10, false);
                    } else if (line.length() > 0 && line.length() < 150 && 
                               (line.find(L"a)") == 0 || line.find(L"b)") == 0 || line.find(L"c)") == 0)) {
                        // Sub-sections
                        AppendRichText(hEdit, line + L"\r\n", true, RGB(0, 0, 0), 9, false);
                    } else {
                        // Regular text
                        AppendRichText(hEdit, line + L"\r\n", false, RGB(40, 40, 40), 9, false);
                    }
                }
            }
            delete[] buffer;
        }
        CloseHandle(hFile);
    } else {
        AppendRichText(hEdit, L"GPLv2.md file not found.\r\n\r\n", true, RGB(139, 0, 0), 10, false);
        AppendRichText(hEdit, L"Please see the GPLv2.md file in the installation directory.", false, RGB(40, 40, 40), 9, false);
    }
    
    // Scroll to top
    SendMessageW(hEdit, EM_SETSEL, 0, 0);
    SendMessageW(hEdit, EM_SCROLLCARET, 0, 0);
    
    // Create OK button
    int btnWidth = S(80);
    int btnX = (W - btnWidth) / 2;
    int btnY = H - S(61);
    
    HWND btnOK = CreateWindowExW(0, L"Button", L"OK",
        WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON | WS_TABSTOP,
        btnX, btnY, btnWidth, S(30),
        dlg, (HMENU)IDOK, hi, NULL);
    
    HFONT hFont = (HFONT)GetStockObject(DEFAULT_GUI_FONT);
    SendMessageW(btnOK, WM_SETFONT, (WPARAM)hFont, TRUE);
    
    // Modal loop
    if (parent && IsWindow(parent)) EnableWindow(parent, FALSE);
    SetFocus(btnOK);
    
    MSG msg;
    while (GetMessageW(&msg, NULL, 0, 0)) {
        if (!IsDialogMessageW(dlg, &msg)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }
    
    DestroyWindow(dlg);
    if (parent && IsWindow(parent)) {
        EnableWindow(parent, TRUE);
        SetForegroundWindow(parent);
        BringWindowToTop(parent);
    }
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
