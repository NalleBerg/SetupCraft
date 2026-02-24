#include "button.h"
#include <commctrl.h>

#pragma comment(lib, "comctl32.lib")

// Declare PrivateExtractIconsW for transparent icon extraction
extern "C" UINT WINAPI PrivateExtractIconsW(
    LPCWSTR szFileName,
    int nIconIndex,
    int cxIcon,
    int cyIcon,
    HICON *phicon,
    UINT *piconid,
    UINT nIcons,
    UINT flags
);

struct ButtonColors {
    COLORREF base;
    COLORREF hover;
    COLORREF pressed;
};

static ButtonColors GetButtonColors(ButtonColor color) {
    switch (color) {
    case ButtonColor::Blue:
        // Windows native button style - light gray
        return { RGB(225,225,225), RGB(229,241,251), RGB(204,228,247) };
    case ButtonColor::Red:
        // Slightly reddish tint for destructive actions
        return { RGB(225,225,225), RGB(251,229,229), RGB(247,204,204) };
    case ButtonColor::Green:
        // Slightly greenish tint for success actions
        return { RGB(225,225,225), RGB(229,251,229), RGB(204,247,204) };
    default:
        return { RGB(225,225,225), RGB(229,241,251), RGB(204,228,247) };
    }
}

HWND CreateCustomButton(HWND hwndParent, int id, const std::wstring &text, ButtonColor color, int x, int y, int width, int height, HINSTANCE hInst) {
    HWND hButton = CreateWindowExW(
        0,
        L"BUTTON",
        text.c_str(),
        WS_CHILD | WS_VISIBLE | BS_OWNERDRAW | WS_TABSTOP,
        x, y, width, height,
        hwndParent,
        (HMENU)(INT_PTR)id,
        hInst,
        NULL
    );
    
    if (hButton) {
        // Store color as user data
        SetWindowLongPtrW(hButton, GWLP_USERDATA, (LONG_PTR)color);
        
        // Subclass for hover effects
        SetWindowSubclass(hButton, ButtonSubclassProc, 0, 0);
    }
    
    return hButton;
}

HWND CreateCustomButtonWithIcon(HWND hwndParent, int id, const std::wstring &text, ButtonColor color, const wchar_t* iconDll, int iconIndex, int x, int y, int width, int height, HINSTANCE hInst) {
    HWND hButton = CreateWindowExW(
        0,
        L"BUTTON",
        text.c_str(),
        WS_CHILD | WS_VISIBLE | BS_OWNERDRAW | WS_TABSTOP,
        x, y, width, height,
        hwndParent,
        (HMENU)(INT_PTR)id,
        hInst,
        NULL
    );
    
    if (hButton) {
        // Store color as user data
        SetWindowLongPtrW(hButton, GWLP_USERDATA, (LONG_PTR)color);
        
        // Store icon info as window properties
        if (iconDll && iconIndex >= 0) {
            // Allocate and store DLL name
            size_t len = wcslen(iconDll) + 1;
            wchar_t* dllCopy = (wchar_t*)malloc(len * sizeof(wchar_t));
            if (dllCopy) {
                wcscpy(dllCopy, iconDll);
                SetPropW(hButton, L"IconDLL", (HANDLE)dllCopy);
            }
            SetPropW(hButton, L"IconIndex", (HANDLE)(INT_PTR)iconIndex);
        }
        
        // Subclass for hover effects
        SetWindowSubclass(hButton, ButtonSubclassProc, 0, 0);
    }
    
    return hButton;
}

HWND CreateCustomButtonWithCompositeIcon(HWND hwndParent, int id, const std::wstring &text, ButtonColor color, const wchar_t* iconDll1, int iconIndex1, const wchar_t* iconDll2, int iconIndex2, int x, int y, int width, int height, HINSTANCE hInst) {
    HWND hButton = CreateWindowExW(
        0,
        L"BUTTON",
        text.c_str(),
        WS_CHILD | WS_VISIBLE | BS_OWNERDRAW | WS_TABSTOP,
        x, y, width, height,
        hwndParent,
        (HMENU)(INT_PTR)id,
        hInst,
        NULL
    );
    
    if (hButton) {
        // Store color as user data
        SetWindowLongPtrW(hButton, GWLP_USERDATA, (LONG_PTR)color);
        
        // Store first icon info
        if (iconDll1 && iconIndex1 >= 0) {
            size_t len = wcslen(iconDll1) + 1;
            wchar_t* dllCopy = (wchar_t*)malloc(len * sizeof(wchar_t));
            if (dllCopy) {
                wcscpy(dllCopy, iconDll1);
                SetPropW(hButton, L"IconDLL", (HANDLE)dllCopy);
            }
            SetPropW(hButton, L"IconIndex", (HANDLE)(INT_PTR)iconIndex1);
        }
        
        // Store second icon info (overlay)
        if (iconDll2 && iconIndex2 >= 0) {
            size_t len = wcslen(iconDll2) + 1;
            wchar_t* dllCopy = (wchar_t*)malloc(len * sizeof(wchar_t));
            if (dllCopy) {
                wcscpy(dllCopy, iconDll2);
                SetPropW(hButton, L"IconDLL2", (HANDLE)dllCopy);
            }
            SetPropW(hButton, L"IconIndex2", (HANDLE)(INT_PTR)iconIndex2);
        }
        
        // Subclass for hover effects
        SetWindowSubclass(hButton, ButtonSubclassProc, 0, 0);
    }
    
    return hButton;
}

void UpdateButtonText(HWND hButton, const std::wstring &text) {
    if (hButton) {
        SetWindowTextW(hButton, text.c_str());
        InvalidateRect(hButton, NULL, TRUE);
    }
}

LRESULT CALLBACK ButtonSubclassProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam, UINT_PTR uIdSubclass, DWORD_PTR dwRefData) {
    // Use window property to store per-button hover state
    BOOL isHovering = (BOOL)(INT_PTR)GetPropW(hwnd, L"IsHovering");
    
    switch (uMsg) {
    case WM_MOUSEMOVE: {
        if (!isHovering) {
            isHovering = TRUE;
            SetPropW(hwnd, L"IsHovering", (HANDLE)1);
            InvalidateRect(hwnd, NULL, FALSE);
            
            TRACKMOUSEEVENT tme = {};
            tme.cbSize = sizeof(tme);
            tme.dwFlags = TME_LEAVE;
            tme.hwndTrack = hwnd;
            tme.dwHoverTime = HOVER_DEFAULT;
            TrackMouseEvent(&tme);
        }
        break;
    }
    case WM_MOUSELEAVE:
        SetPropW(hwnd, L"IsHovering", (HANDLE)0);
        InvalidateRect(hwnd, NULL, FALSE);
        break;
    case WM_LBUTTONUP:
        // Force redraw on mouse up to clear pressed state
        InvalidateRect(hwnd, NULL, TRUE);
        break;
    case WM_NCDESTROY: {
        // Clean up icon DLL string if allocated
        wchar_t* dllName = (wchar_t*)GetPropW(hwnd, L"IconDLL");
        if (dllName) {
            free(dllName);
            RemovePropW(hwnd, L"IconDLL");
        }
        RemovePropW(hwnd, L"IconIndex");
        
        // Clean up second icon DLL string if allocated
        wchar_t* dllName2 = (wchar_t*)GetPropW(hwnd, L"IconDLL2");
        if (dllName2) {
            free(dllName2);
            RemovePropW(hwnd, L"IconDLL2");
        }
        RemovePropW(hwnd, L"IconIndex2");
        
        RemovePropW(hwnd, L"IsHovering");
        RemoveWindowSubclass(hwnd, ButtonSubclassProc, uIdSubclass);
        break;
    }
    }
    
    return DefSubclassProc(hwnd, uMsg, wParam, lParam);
}

BOOL DrawCustomButton(LPDRAWITEMSTRUCT dis, ButtonColor color, HFONT hFont) {
    if (!dis || dis->CtlType != ODT_BUTTON) return FALSE;
    
    HDC hdc = dis->hDC;
    RECT rc = dis->rcItem;
    BOOL pressed = (dis->itemState & ODS_SELECTED) != 0;
    BOOL focused = (dis->itemState & ODS_FOCUS) != 0;
    
    // Get hover state from button property
    BOOL hover = (BOOL)(INT_PTR)GetPropW(dis->hwndItem, L"IsHovering") && !pressed;
    
    // Get colors
    ButtonColors colors = GetButtonColors(color);
    COLORREF bgColor = pressed ? colors.pressed : (hover ? colors.hover : colors.base);
    
    // Fill background
    HBRUSH hBrush = CreateSolidBrush(bgColor);
    FillRect(hdc, &rc, hBrush);
    DeleteObject(hBrush);
    
    // Draw border like native Windows buttons
    HPEN hPen = CreatePen(PS_SOLID, 1, RGB(173,173,173));
    HPEN hOldPen = (HPEN)SelectObject(hdc, hPen);
    
    // Draw outer border
    MoveToEx(hdc, rc.left, rc.bottom - 1, NULL);
    LineTo(hdc, rc.left, rc.top);
    LineTo(hdc, rc.right - 1, rc.top);
    LineTo(hdc, rc.right - 1, rc.bottom - 1);
    LineTo(hdc, rc.left, rc.bottom - 1);
    
    SelectObject(hdc, hOldPen);
    DeleteObject(hPen);
    
    SetBkMode(hdc, TRANSPARENT);
    
    // Check if button has an icon
    wchar_t* iconDll = (wchar_t*)GetPropW(dis->hwndItem, L"IconDLL");
    int iconIndex = (int)(INT_PTR)GetPropW(dis->hwndItem, L"IconIndex");
    HICON hIcon = NULL;
    
    if (iconDll && iconIndex >= 0) {
        // Build full path to system DLL
        wchar_t dllPath[MAX_PATH];
        GetSystemDirectoryW(dllPath, MAX_PATH);
        wcscat(dllPath, L"\\");
        wcscat(dllPath, iconDll);
        
        // Extract icon from DLL using PrivateExtractIconsW for transparent background
        UINT extracted = PrivateExtractIconsW(dllPath, iconIndex, 20, 20, &hIcon, NULL, 1, 0);
        if (extracted == 0 || !hIcon) {
            // Fallback to ExtractIconW if PrivateExtractIconsW fails
            hIcon = ExtractIconW(NULL, dllPath, iconIndex);
        }
    }
    
    // Create bold font from the passed font for regular text
    HFONT hBoldFont = NULL;
    HFONT hOldFont = NULL;
    if (hFont) {
        LOGFONTW lf = {};
        if (GetObjectW(hFont, sizeof(LOGFONTW), &lf)) {
            lf.lfWeight = FW_BOLD;
            hBoldFont = CreateFontIndirectW(&lf);
            hOldFont = (HFONT)SelectObject(hdc, hBoldFont);
        } else {
            hOldFont = (HFONT)SelectObject(hdc, hFont);
        }
    }
    
    // Get button text (without emoji prefix if icons are used)
    wchar_t text[256] = {0};
    GetWindowTextW(dis->hwndItem, text, 256);
    int textLen = (int)wcslen(text);
    
    // Skip emoji prefix if present (since we're using real icons now)
    const wchar_t* displayText = text;
    if (hIcon && textLen > 0) {
        // Skip emoji characters at the start
        int skip = 0;
        for (int i = 0; i < textLen; i++) {
            wchar_t c = text[i];
            if ((c >= 0x2600 && c <= 0x27BF) || c == 0x2714 || c == 0x2716 || c == 0x2795 ||
                (c >= 0xD800 && c <= 0xDBFF)) {
                if (c >= 0xD800 && c <= 0xDBFF && i + 1 < textLen) {
                    i++; // Skip surrogate pair
                }
                skip = i + 1;
            } else if (c == L' ' && skip > 0) {
                skip = i + 1;
                break;
            } else {
                break;
            }
        }
        displayText = text + skip;
    }
    
    if (hIcon) {
        // Draw icon + text
        int iconSize = 20;
        int iconX = rc.left + 10;
        int iconY = rc.top + (rc.bottom - rc.top - iconSize) / 2;
        
        DrawIconEx(hdc, iconX, iconY, hIcon, iconSize, iconSize, 0, NULL, DI_NORMAL);
        DestroyIcon(hIcon);
        
        // Check for overlay icon
        wchar_t* iconDll2 = (wchar_t*)GetPropW(dis->hwndItem, L"IconDLL2");
        INT_PTR iconIndex2 = (INT_PTR)GetPropW(dis->hwndItem, L"IconIndex2");
        
        if (iconDll2 && iconIndex2 > 0) {
            HMODULE hIconDll2 = LoadLibraryExW(iconDll2, NULL, LOAD_LIBRARY_AS_DATAFILE);
            if (hIconDll2) {
                HICON hIcon2 = (HICON)LoadImageW(hIconDll2, MAKEINTRESOURCEW(iconIndex2), IMAGE_ICON, 20, 20, 0);
                if (hIcon2) {
                    // Draw overlay on top of base icon at full size
                    DrawIconEx(hdc, iconX, iconY, hIcon2, 20, 20, 0, NULL, DI_NORMAL);
                    DestroyIcon(hIcon2);
                }
                FreeLibrary(hIconDll2);
            }
        }
        
        // Draw text in dark blue
        SetTextColor(hdc, RGB(7, 50, 116));
        RECT textRect = rc;
        textRect.left += iconSize + 15;
        DrawTextW(hdc, displayText, -1, &textRect, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    } else {
        // No icon - draw text centered
        SetTextColor(hdc, RGB(7, 50, 116));
        DrawTextW(hdc, text, -1, &rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    }
    
    // Clean up font
    if (hBoldFont) {
        SelectObject(hdc, hOldFont);
        DeleteObject(hBoldFont);
    }
    
    // Draw focus rectangle
    if (dis->itemState & ODS_FOCUS) {
        DrawFocusRect(hdc, &rc);
    }
    
    return TRUE;
}
