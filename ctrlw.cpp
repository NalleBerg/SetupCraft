#include "ctrlw.h"
#include "button.h"
#include <commctrl.h>

// Global for dialog result
static bool g_quitDialogResult = false;

// Dialog procedure for quit confirmation
LRESULT CALLBACK QuitDialogProc(HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_CREATE: {
        CREATESTRUCTW* cs = (CREATESTRUCTW*)lParam;
        std::map<std::wstring, std::wstring>* pLocale = 
            (std::map<std::wstring, std::wstring>*)cs->lpCreateParams;
        
        if (pLocale) {
            SetWindowLongPtr(hDlg, GWLP_USERDATA, (LONG_PTR)pLocale);
            
            HINSTANCE hInst = cs->hInstance;
            
            // Create static text for message
            auto itMsg = pLocale->find(L"quit_message");
            std::wstring message = (itMsg != pLocale->end()) ? itMsg->second : L"Are you sure you want to quit?";
            
            HWND hText = CreateWindowExW(0, L"STATIC", message.c_str(),
                WS_CHILD | WS_VISIBLE | SS_CENTER,
                20, 20, 360, 40,
                hDlg, NULL, hInst, NULL);
            
            // Set font for static text
            HFONT hFont = CreateFontW(-14, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
            if (hFont) SendMessageW(hText, WM_SETFONT, (WPARAM)hFont, TRUE);
            
            // Create Yes button
            auto itYes = pLocale->find(L"yes");
            std::wstring yesText = (itYes != pLocale->end()) ? itYes->second : L"Yes";
            CreateCustomButtonWithIcon(hDlg, IDYES, yesText, ButtonColor::Green,
                L"shell32.dll", 112, 80, 80, 100, 30, hInst);
            
            // Create No button
            auto itNo = pLocale->find(L"no");
            std::wstring noText = (itNo != pLocale->end()) ? itNo->second : L"No";
            CreateCustomButtonWithIcon(hDlg, IDNO, noText, ButtonColor::Red,
                L"shell32.dll", 131, 220, 80, 100, 30, hInst);
        }
        
        return 0;
    }
    case WM_COMMAND:
        if (LOWORD(wParam) == IDYES) {
            g_quitDialogResult = true;
            DestroyWindow(hDlg);
            return 0;
        }
        if (LOWORD(wParam) == IDNO) {
            g_quitDialogResult = false;
            DestroyWindow(hDlg);
            return 0;
        }
        break;
    case WM_DRAWITEM: {
        LPDRAWITEMSTRUCT dis = (LPDRAWITEMSTRUCT)lParam;
        if (dis->CtlID == IDYES || dis->CtlID == IDNO) {
            ButtonColor color = (ButtonColor)GetWindowLongPtr(dis->hwndItem, GWLP_USERDATA);
            std::map<std::wstring, std::wstring>* pLocale = 
                (std::map<std::wstring, std::wstring>*)GetWindowLongPtr(hDlg, GWLP_USERDATA);
            if (pLocale) {
                // Get font from parent or create default
                HFONT hFont = CreateFontW(-14, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
                    DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                    CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
                LRESULT result = DrawCustomButton(dis, color, hFont);
                if (hFont) DeleteObject(hFont);
                return result;
            }
        }
        break;
    }
    case WM_CTLCOLORSTATIC: {
        // Make static text background transparent
        HDC hdcStatic = (HDC)wParam;
        SetBkMode(hdcStatic, TRANSPARENT);
        return (LRESULT)GetStockObject(NULL_BRUSH);
    }
    case WM_CLOSE:
        g_quitDialogResult = false;
        DestroyWindow(hDlg);
        return 0;
    case WM_DESTROY:
        return 0;
    case WM_KEYDOWN:
        if (wParam == VK_ESCAPE) {
            g_quitDialogResult = false;
            DestroyWindow(hDlg);
            return 0;
        }
        break;
    }
    return DefWindowProcW(hDlg, msg, wParam, lParam);
}

bool ShowQuitDialog(HWND hwndParent, const std::map<std::wstring, std::wstring>& locale) {
    // Register dialog class if not already registered
    static bool dialogClassRegistered = false;
    if (!dialogClassRegistered) {
        WNDCLASSEXW wcDlg = {};
        wcDlg.cbSize = sizeof(WNDCLASSEXW);
        wcDlg.style = CS_HREDRAW | CS_VREDRAW;
        wcDlg.lpfnWndProc = QuitDialogProc;
        wcDlg.hInstance = GetModuleHandle(NULL);
        wcDlg.hCursor = LoadCursor(NULL, IDC_ARROW);
        wcDlg.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
        wcDlg.lpszClassName = L"QuitDialogClass";
        RegisterClassExW(&wcDlg);
        dialogClassRegistered = true;
    }
    
    // Get title from locale
    auto itTitle = locale.find(L"quit_title");
    std::wstring title = (itTitle != locale.end()) ? itTitle->second : L"Exit";
    
    // Calculate centered position
    RECT rcParent;
    if (hwndParent && GetWindowRect(hwndParent, &rcParent)) {
        // Center on parent
    } else {
        // Center on screen work area
        SystemParametersInfoW(SPI_GETWORKAREA, 0, &rcParent, 0);
    }
    
    int width = 400, height = 160;
    int x = rcParent.left + (rcParent.right - rcParent.left - width) / 2;
    int y = rcParent.top + (rcParent.bottom - rcParent.top - height) / 2;
    
    // Ensure dialog is visible on screen
    RECT rcWork;
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &rcWork, 0);
    if (x < rcWork.left) x = rcWork.left;
    if (y < rcWork.top) y = rcWork.top;
    if (x + width > rcWork.right) x = rcWork.right - width;
    if (y + height > rcWork.bottom) y = rcWork.bottom - height;
    
    // Create a non-const copy of locale to pass as parameter
    std::map<std::wstring, std::wstring>* pLocaleCopy = 
        new std::map<std::wstring, std::wstring>(locale);
    
    HWND hDlg = CreateWindowExW(
        WS_EX_DLGMODALFRAME | WS_EX_TOPMOST,
        L"QuitDialogClass",
        title.c_str(),
        WS_POPUP | WS_CAPTION | WS_SYSMENU,
        x, y, width, height,
        hwndParent, NULL, GetModuleHandle(NULL), pLocaleCopy);
    
    if (hDlg) {
        // Disable parent window
        EnableWindow(hwndParent, FALSE);
        ShowWindow(hDlg, SW_SHOW);
        
        // Message loop for modal behavior
        MSG msg;
        while (GetMessageW(&msg, NULL, 0, 0)) {
            if (!IsWindow(hDlg)) break;
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        
        // Re-enable parent window
        EnableWindow(hwndParent, TRUE);
        SetForegroundWindow(hwndParent);
    }
    
    delete pLocaleCopy;
    return g_quitDialogResult;
}

bool IsCtrlWPressed(UINT msg, WPARAM wParam) {
    if (msg == WM_KEYDOWN && wParam == 'W') {
        return (GetKeyState(VK_CONTROL) & 0x8000) != 0;
    }
    return false;
}

// Global for unsaved changes dialog result
static int g_unsavedChangesDialogResult = 0;

// Dialog procedure for unsaved changes confirmation
LRESULT CALLBACK UnsavedChangesDialogProc(HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_CREATE: {
        CREATESTRUCTW* cs = (CREATESTRUCTW*)lParam;
        std::map<std::wstring, std::wstring>* pLocale = 
            (std::map<std::wstring, std::wstring>*)cs->lpCreateParams;
        
        if (pLocale) {
            SetWindowLongPtr(hDlg, GWLP_USERDATA, (LONG_PTR)pLocale);
            
            HINSTANCE hInst = cs->hInstance;
            
            // Create static text for message
            auto itMsg = pLocale->find(L"close_unsaved_message");
            std::wstring message = (itMsg != pLocale->end()) ? itMsg->second : L"You have unsaved changes. Do you want to save before closing?";
            
            HWND hText = CreateWindowExW(0, L"STATIC", message.c_str(),
                WS_CHILD | WS_VISIBLE | SS_CENTER,
                20, 20, 460, 40,
                hDlg, NULL, hInst, NULL);
            
            // Set font for static text
            HFONT hFont = CreateFontW(-14, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
            if (hFont) SendMessageW(hText, WM_SETFONT, (WPARAM)hFont, TRUE);
            
            // Create Save button
            auto itSave = pLocale->find(L"save");
            std::wstring saveText = (itSave != pLocale->end()) ? itSave->second : L"Save";
            CreateCustomButtonWithIcon(hDlg, 1, saveText, ButtonColor::Green,
                L"shell32.dll", 258, 30, 80, 110, 30, hInst);
            
            // Create Don't Save button
            auto itDontSave = pLocale->find(L"dont_save");
            std::wstring dontSaveText = (itDontSave != pLocale->end()) ? itDontSave->second : L"Don't Save";
            CreateCustomButtonWithIcon(hDlg, 2, dontSaveText, ButtonColor::Blue,
                L"shell32.dll", 240, 195, 80, 110, 30, hInst);
            
            // Create Cancel button
            auto itCancel = pLocale->find(L"cancel");
            std::wstring cancelText = (itCancel != pLocale->end()) ? itCancel->second : L"Cancel";
            CreateCustomButtonWithIcon(hDlg, IDCANCEL, cancelText, ButtonColor::Red,
                L"shell32.dll", 131, 360, 80, 110, 30, hInst);
        }
        
        return 0;
    }
    
    case WM_COMMAND:
        if (LOWORD(wParam) == 1) {  // Save
            g_unsavedChangesDialogResult = 1;
            DestroyWindow(hDlg);
            return 0;
        }
        if (LOWORD(wParam) == 2) {  // Don't Save
            g_unsavedChangesDialogResult = 2;
            DestroyWindow(hDlg);
            return 0;
        }
        if (LOWORD(wParam) == IDCANCEL) {  // Cancel
            g_unsavedChangesDialogResult = 0;
            DestroyWindow(hDlg);
            return 0;
        }
        break;
        
    case WM_CLOSE:
        g_unsavedChangesDialogResult = 0;  // Cancel
        DestroyWindow(hDlg);
        return 0;
        
    case WM_DRAWITEM: {
        LPDRAWITEMSTRUCT dis = (LPDRAWITEMSTRUCT)lParam;
        if (dis->CtlID == 1 || dis->CtlID == 2 || dis->CtlID == IDCANCEL) {
            ButtonColor color = (ButtonColor)GetWindowLongPtr(dis->hwndItem, GWLP_USERDATA);
            HFONT hFont = CreateFontW(-12, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
                DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
            LRESULT result = DrawCustomButton(dis, color, hFont);
            if (hFont) DeleteObject(hFont);
            return result;
        }
        break;
    }
    }
    return DefWindowProcW(hDlg, msg, wParam, lParam);
}

int ShowUnsavedChangesDialog(HWND hwndParent, const std::map<std::wstring, std::wstring>& locale) {
    // Register dialog class if not already registered
    static bool dialogClassRegistered = false;
    if (!dialogClassRegistered) {
        WNDCLASSEXW wcDlg = {};
        wcDlg.cbSize = sizeof(WNDCLASSEXW);
        wcDlg.style = CS_HREDRAW | CS_VREDRAW;
        wcDlg.lpfnWndProc = UnsavedChangesDialogProc;
        wcDlg.hInstance = GetModuleHandle(NULL);
        wcDlg.hCursor = LoadCursor(NULL, IDC_ARROW);
        wcDlg.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
        wcDlg.lpszClassName = L"UnsavedChangesDialogClass";
        RegisterClassExW(&wcDlg);
        dialogClassRegistered = true;
    }
    
    // Get title from locale
    auto itTitle = locale.find(L"close_unsaved_title");
    std::wstring title = (itTitle != locale.end()) ? itTitle->second : L"Unsaved Changes";
    
    // Calculate centered position
    RECT rcParent;
    if (hwndParent && GetWindowRect(hwndParent, &rcParent)) {
        // Center on parent
    } else {
        // Center on screen work area
        SystemParametersInfoW(SPI_GETWORKAREA, 0, &rcParent, 0);
    }
    
    int width = 500, height = 160;
    int x = rcParent.left + (rcParent.right - rcParent.left - width) / 2;
    int y = rcParent.top + (rcParent.bottom - rcParent.top - height) / 2;
    
    // Ensure dialog is visible on screen
    RECT rcWork;
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &rcWork, 0);
    if (x < rcWork.left) x = rcWork.left;
    if (y < rcWork.top) y = rcWork.top;
    if (x + width > rcWork.right) x = rcWork.right - width;
    if (y + height > rcWork.bottom) y = rcWork.bottom - height;
    
    // Create a non-const copy of locale to pass as parameter
    std::map<std::wstring, std::wstring>* pLocaleCopy = 
        new std::map<std::wstring, std::wstring>(locale);
    
    HWND hDlg = CreateWindowExW(
        WS_EX_DLGMODALFRAME | WS_EX_TOPMOST,
        L"UnsavedChangesDialogClass",
        title.c_str(),
        WS_POPUP | WS_CAPTION | WS_SYSMENU,
        x, y, width, height,
        hwndParent, NULL, GetModuleHandle(NULL), pLocaleCopy);
    
    if (hDlg) {
        // Disable parent window
        EnableWindow(hwndParent, FALSE);
        ShowWindow(hDlg, SW_SHOW);
        
        // Message loop for modal behavior
        MSG msg;
        while (GetMessageW(&msg, NULL, 0, 0)) {
            if (!IsWindow(hDlg)) break;
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        
        // Re-enable parent window
        EnableWindow(hwndParent, TRUE);
        SetForegroundWindow(hwndParent);
    }
    
    delete pLocaleCopy;
    return g_unsavedChangesDialogResult;
}
