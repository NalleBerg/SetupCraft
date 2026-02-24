#pragma once
#include <windows.h>
#include <string>

enum class ButtonColor {
    Blue,
    Red,
    Green
};

// Icon information for buttons
struct ButtonIconInfo {
    const wchar_t* dllName;  // e.g., L"imageres.dll" or L"shell32.dll"
    int iconIndex;           // Icon index in the DLL
    int iconSize;            // Icon size (16 or 32 recommended)
};

// Button creation and management
HWND CreateCustomButton(HWND hwndParent, int id, const std::wstring &text, ButtonColor color, int x, int y, int width, int height, HINSTANCE hInst);
HWND CreateCustomButtonWithIcon(HWND hwndParent, int id, const std::wstring &text, ButtonColor color, const wchar_t* iconDll, int iconIndex, int x, int y, int width, int height, HINSTANCE hInst);
HWND CreateCustomButtonWithCompositeIcon(HWND hwndParent, int id, const std::wstring &text, ButtonColor color, const wchar_t* iconDll1, int iconIndex1, const wchar_t* iconDll2, int iconIndex2, int x, int y, int width, int height, HINSTANCE hInst);
void UpdateButtonText(HWND hButton, const std::wstring &text);

// Button subclass procedure for hover effects
LRESULT CALLBACK ButtonSubclassProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam, UINT_PTR uIdSubclass, DWORD_PTR dwRefData);

// WM_DRAWITEM handler
BOOL DrawCustomButton(LPDRAWITEMSTRUCT dis, ButtonColor color, HFONT hFont);
