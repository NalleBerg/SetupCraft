#include <windows.h>
#include <commctrl.h>
#include <string>
#include <vector>
#include <shlwapi.h>

#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "shlwapi.lib")

const wchar_t CLASS_NAME[] = L"IconViewerWindow";
const int ICON_SIZE = 32;
const int ICON_SPACING = 80;
const int ICONS_PER_ROW = 8;

struct IconInfo {
    HICON icon;
    int index;
};

std::vector<IconInfo> g_icons;
std::wstring g_dllPath;
int g_scrollPos = 0;

void LoadIconsFromDLL(const std::wstring& dllPath, int maxIcons = 336) {
    // Clear existing icons
    for (auto& info : g_icons) {
        if (info.icon) DestroyIcon(info.icon);
    }
    g_icons.clear();
    
    // Build full path
    wchar_t fullPath[MAX_PATH];
    if (PathIsRelativeW(dllPath.c_str())) {
        GetSystemDirectoryW(fullPath, MAX_PATH);
        wcscat_s(fullPath, L"\\");
        wcscat_s(fullPath, dllPath.c_str());
    } else {
        wcscpy_s(fullPath, dllPath.c_str());
    }
    
    g_dllPath = fullPath;
    
    // Extract icons - always add entry to maintain index consistency
    for (int i = 0; i < maxIcons; i++) {
        HICON icon = ExtractIconW(NULL, fullPath, i);
        IconInfo info;
        info.index = i;
        // Accept the icon even if it's NULL or invalid - we'll show the index number
        if (icon == (HICON)1 || icon == NULL) {
            info.icon = NULL;
        } else {
            info.icon = icon;
        }
        g_icons.push_back(info);
    }
}

void UpdateScrollbar(HWND hwnd) {
    RECT rc;
    GetClientRect(hwnd, &rc);
    
    int totalRows = ((int)g_icons.size() + ICONS_PER_ROW - 1) / ICONS_PER_ROW;
    // Calculate total content height: starting position + (rows * spacing) + extra space for labels
    int totalContentHeight = 50 + (totalRows * ICON_SPACING) + 30; // 30 extra for last row labels
    int maxScroll = totalContentHeight - rc.bottom;
    if (maxScroll < 0) maxScroll = 0;
    
    SCROLLINFO si = {};
    si.cbSize = sizeof(SCROLLINFO);
    si.fMask = SIF_RANGE | SIF_PAGE | SIF_POS;
    si.nMin = 0;
    si.nMax = totalContentHeight;
    si.nPage = rc.bottom;
    si.nPos = g_scrollPos;
    SetScrollInfo(hwnd, SB_VERT, &si, TRUE);
}

LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    switch (uMsg) {
    case WM_CREATE: {
        LoadIconsFromDLL(L"shell32.dll");
        UpdateScrollbar(hwnd);
        return 0;
    }
    
    case WM_SIZE:
        UpdateScrollbar(hwnd);
        InvalidateRect(hwnd, NULL, TRUE);
        return 0;
    
    case WM_VSCROLL: {
        int oldPos = g_scrollPos;
        SCROLLINFO si = {};
        si.cbSize = sizeof(SCROLLINFO);
        si.fMask = SIF_ALL;
        GetScrollInfo(hwnd, SB_VERT, &si);
        
        switch (LOWORD(wParam)) {
        case SB_LINEUP:
            g_scrollPos -= ICON_SPACING;
            break;
        case SB_LINEDOWN:
            g_scrollPos += ICON_SPACING;
            break;
        case SB_PAGEUP:
            g_scrollPos -= si.nPage;
            break;
        case SB_PAGEDOWN:
            g_scrollPos += si.nPage;
            break;
        case SB_THUMBTRACK:
        case SB_THUMBPOSITION:
            g_scrollPos = si.nTrackPos;
            break;
        }
        
        if (g_scrollPos < 0) g_scrollPos = 0;
        if (g_scrollPos > si.nMax - (int)si.nPage) {
            g_scrollPos = si.nMax - si.nPage;
            if (g_scrollPos < 0) g_scrollPos = 0;
        }
        
        if (g_scrollPos != oldPos) {
            SetScrollPos(hwnd, SB_VERT, g_scrollPos, TRUE);
            InvalidateRect(hwnd, NULL, TRUE);
        }
        return 0;
    }
    
    case WM_MOUSEWHEEL: {
        int delta = GET_WHEEL_DELTA_WPARAM(wParam);
        SendMessage(hwnd, WM_VSCROLL, delta > 0 ? SB_LINEUP : SB_LINEDOWN, 0);
        return 0;
    }
    
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);
        
        RECT rc;
        GetClientRect(hwnd, &rc);
        
        // White background
        HBRUSH hBrush = CreateSolidBrush(RGB(255, 255, 255));
        FillRect(hdc, &rc, hBrush);
        DeleteObject(hBrush);
        
        // Draw title
        SetBkMode(hdc, TRANSPARENT);
        HFONT hTitleFont = CreateFontW(-20, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
        HFONT hOldFont = (HFONT)SelectObject(hdc, hTitleFont);
        
        wchar_t title[512];
        swprintf_s(title, L"Icon Viewer - %s (%zu icons)", PathFindFileNameW(g_dllPath.c_str()), g_icons.size());
        RECT titleRect = {10, 10, rc.right - 10, 40};
        SetTextColor(hdc, RGB(0, 0, 0));
        DrawTextW(hdc, title, -1, &titleRect, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        
        SelectObject(hdc, hOldFont);
        DeleteObject(hTitleFont);
        
        // Draw icons and labels
        HFONT hFont = CreateFontW(-12, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
        SelectObject(hdc, hFont);
        
        int startY = 50 - g_scrollPos;
        int x = 20;
        int y = startY;
        int col = 0;
        
        for (size_t i = 0; i < g_icons.size(); i++) {
            // Skip if not visible
            if (y + ICON_SIZE + 20 < 0 || y > rc.bottom) {
                col++;
                if (col >= ICONS_PER_ROW) {
                    col = 0;
                    y += ICON_SPACING;
                }
                x = 20 + col * ICON_SPACING;
                continue;
            }
            
            // Draw icon (if available)
            if (g_icons[i].icon) {
                DrawIconEx(hdc, x, y, g_icons[i].icon, ICON_SIZE, ICON_SIZE, 0, NULL, DI_NORMAL);
            } else {
                // Draw placeholder for missing icon
                RECT placeholderRect = {x, y, x + ICON_SIZE, y + ICON_SIZE};
                HBRUSH hGrayBrush = CreateSolidBrush(RGB(220, 220, 220));
                FillRect(hdc, &placeholderRect, hGrayBrush);
                DeleteObject(hGrayBrush);
                
                // Draw "No img" text on the placeholder
                HFONT hSmallFont = CreateFontW(-9, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                    DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                    CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
                HFONT hOldSmallFont = (HFONT)SelectObject(hdc, hSmallFont);
                SetTextColor(hdc, RGB(100, 100, 100));
                DrawTextW(hdc, L"No img", -1, &placeholderRect, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
                SelectObject(hdc, hOldSmallFont);
                DeleteObject(hSmallFont);
            }
            
            // Draw index number below icon
            wchar_t label[16];
            swprintf_s(label, L"#%d", g_icons[i].index);
            RECT labelRect = {x - 10, y + ICON_SIZE + 2, x + ICON_SIZE + 10, y + ICON_SIZE + 20};
            SetTextColor(hdc, RGB(0, 0, 0));
            DrawTextW(hdc, label, -1, &labelRect, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
            
            col++;
            if (col >= ICONS_PER_ROW) {
                col = 0;
                y += ICON_SPACING;
            }
            x = 20 + col * ICON_SPACING;
        }
        
        SelectObject(hdc, hOldFont);
        DeleteObject(hFont);
        
        EndPaint(hwnd, &ps);
        return 0;
    }
    
    case WM_LBUTTONDOWN: {
        int mouseX = LOWORD(lParam);
        int mouseY = HIWORD(lParam);
        
        RECT rc;
        GetClientRect(hwnd, &rc);
        
        int startY = 50 - g_scrollPos;
        int x = 20;
        int y = startY;
        int col = 0;
        
        for (size_t i = 0; i < g_icons.size(); i++) {
            if (mouseX >= x && mouseX <= x + ICON_SIZE &&
                mouseY >= y && mouseY <= y + ICON_SIZE) {
                wchar_t msg[256];
                if (g_icons[i].icon) {
                    swprintf_s(msg, L"Icon Index: %d\n\nDLL: %s\n\nClick OK to copy index to clipboard.",
                        g_icons[i].index, g_dllPath.c_str());
                } else {
                    swprintf_s(msg, L"Icon Index: %d (empty/missing)\n\nDLL: %s\n\nClick OK to copy index to clipboard.",
                        g_icons[i].index, g_dllPath.c_str());
                }
                
                if (MessageBoxW(hwnd, msg, L"Icon Information", MB_OKCANCEL | MB_ICONINFORMATION) == IDOK) {
                    // Copy index to clipboard
                    wchar_t clipText[16];
                    swprintf_s(clipText, L"%d", g_icons[i].index);
                    
                    if (OpenClipboard(hwnd)) {
                        EmptyClipboard();
                        size_t size = (wcslen(clipText) + 1) * sizeof(wchar_t);
                        HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE, size);
                        if (hMem) {
                            memcpy(GlobalLock(hMem), clipText, size);
                            GlobalUnlock(hMem);
                            SetClipboardData(CF_UNICODETEXT, hMem);
                        }
                        CloseClipboard();
                    }
                }
                break;
            }
            
            col++;
            if (col >= ICONS_PER_ROW) {
                col = 0;
                y += ICON_SPACING;
            }
            x = 20 + col * ICON_SPACING;
        }
        return 0;
    }
    
    case WM_RBUTTONDOWN: {
        // Right click to change DLL
        wchar_t buffer[MAX_PATH] = L"shell32.dll";
        
        // Simple input dialog
        if (MessageBoxW(hwnd, L"Load different DLL?\n\nOK = shell32.dll\nCancel = imageres.dll", 
            L"Change DLL", MB_OKCANCEL | MB_ICONQUESTION) == IDOK) {
            wcscpy_s(buffer, L"shell32.dll");
        } else {
            wcscpy_s(buffer, L"imageres.dll");
        }
        
        LoadIconsFromDLL(buffer);
        g_scrollPos = 0;
        UpdateScrollbar(hwnd);
        InvalidateRect(hwnd, NULL, TRUE);
        return 0;
    }
    
    case WM_KEYDOWN:
        if (wParam == VK_F5) {
            LoadIconsFromDLL(PathFindFileNameW(g_dllPath.c_str()));
            g_scrollPos = 0;
            UpdateScrollbar(hwnd);
            InvalidateRect(hwnd, NULL, TRUE);
        }
        return 0;
    
    case WM_DESTROY:
        for (auto& info : g_icons) {
            if (info.icon) DestroyIcon(info.icon);
        }
        PostQuitMessage(0);
        return 0;
    }
    
    return DefWindowProc(hwnd, uMsg, wParam, lParam);
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
    WNDCLASSW wc = {};
    wc.lpfnWndProc = WindowProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = CLASS_NAME;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    
    RegisterClassW(&wc);
    
    HWND hwnd = CreateWindowExW(
        0,
        CLASS_NAME,
        L"Icon Viewer",
        WS_OVERLAPPEDWINDOW | WS_VSCROLL,
        CW_USEDEFAULT, CW_USEDEFAULT, 680, 600,
        NULL, NULL, hInstance, NULL
    );
    
    if (!hwnd) return 0;
    
    ShowWindow(hwnd, nCmdShow);
    UpdateWindow(hwnd);
    
    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
    
    return 0;
}
