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
                20, 25, 380, 50,
                hDlg, NULL, hInst, NULL);
            
            // Use system font (same size as main window labels)
            HFONT hFont = NULL;
            {
                NONCLIENTMETRICSW ncm = {};
                ncm.cbSize = sizeof(ncm);
                SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(ncm), &ncm, 0);
                if (ncm.lfMessageFont.lfHeight < 0)
                    ncm.lfMessageFont.lfHeight = (LONG)(ncm.lfMessageFont.lfHeight * 1.2f);
                ncm.lfMessageFont.lfQuality = CLEARTYPE_QUALITY;
                hFont = CreateFontIndirectW(&ncm.lfMessageFont);
            }
            if (hFont) SendMessageW(hText, WM_SETFONT, (WPARAM)hFont, TRUE);
            
            // Create Yes button
            auto itYes = pLocale->find(L"yes");
            std::wstring yesText = (itYes != pLocale->end()) ? itYes->second : L"Yes";
            CreateCustomButtonWithIcon(hDlg, IDYES, yesText, ButtonColor::Green,
                L"shell32.dll", 112, 80, 108, 110, 34, hInst);
            
            // Create No button
            auto itNo = pLocale->find(L"no");
            std::wstring noText = (itNo != pLocale->end()) ? itNo->second : L"No";
            CreateCustomButtonWithIcon(hDlg, IDNO, noText, ButtonColor::Red,
                L"shell32.dll", 131, 230, 108, 110, 34, hInst);
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
            NONCLIENTMETRICSW ncm = {}; ncm.cbSize = sizeof(ncm);
            SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(ncm), &ncm, 0);
            if (ncm.lfMessageFont.lfHeight < 0)
                ncm.lfMessageFont.lfHeight = (LONG)(ncm.lfMessageFont.lfHeight * 1.2f);
            ncm.lfMessageFont.lfWeight = FW_BOLD;
            ncm.lfMessageFont.lfQuality = CLEARTYPE_QUALITY;
            HFONT hFont = CreateFontIndirectW(&ncm.lfMessageFont);
            LRESULT result = DrawCustomButton(dis, color, hFont);
            if (hFont) DeleteObject(hFont);
            return result;
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
    
    int width = 420, height = 205;
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
    
    // Reset result to safe default before showing dialog
    g_quitDialogResult = false;
    
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
        MSG msg = {};
        while (IsWindow(hDlg)) {
            BOOL bRet = GetMessageW(&msg, NULL, 0, 0);
            if (bRet == 0) { PostQuitMessage((int)msg.wParam); break; } // WM_QUIT: re-post and exit
            if (bRet == -1) break; // error
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

bool ShowCloseProjectDialog(HWND hwndParent, const std::map<std::wstring, std::wstring>& locale) {
    // Reuse the quit dialog but with close-project specific strings
    std::map<std::wstring, std::wstring> patched = locale;
    auto itTitle = locale.find(L"close_project_title");
    auto itMsg   = locale.find(L"close_project_message");
    patched[L"quit_title"]   = (itTitle != locale.end()) ? itTitle->second : L"Close Project";
    patched[L"quit_message"] = (itMsg   != locale.end()) ? itMsg->second   : L"Do you want to close this project?";
    return ShowQuitDialog(hwndParent, patched);
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
                15, 23, 590, 65,
                hDlg, NULL, hInst, NULL);
            
            // Use system font (same size as main window labels)
            HFONT hFont = NULL;
            {
                NONCLIENTMETRICSW ncm = {};
                ncm.cbSize = sizeof(ncm);
                SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(ncm), &ncm, 0);
                if (ncm.lfMessageFont.lfHeight < 0)
                    ncm.lfMessageFont.lfHeight = (LONG)(ncm.lfMessageFont.lfHeight * 1.2f);
                ncm.lfMessageFont.lfQuality = CLEARTYPE_QUALITY;
                hFont = CreateFontIndirectW(&ncm.lfMessageFont);
            }
            if (hFont) SendMessageW(hText, WM_SETFONT, (WPARAM)hFont, TRUE);
            
            // 3 buttons: Save(160) + gap(20) + Don'tSave(230) + gap(20) + Cancel(160) + margins(15 each) = 620
            const int bY = 110, bH = 34, gX = 20;
            const int bW0 = 160, bW1 = 230, bW2 = 160;
            const int bX0 = 15, bX1 = bX0+bW0+gX, bX2 = bX1+bW1+gX;

            // Save button
            auto itSave = pLocale->find(L"save");
            std::wstring saveText = (itSave != pLocale->end()) ? itSave->second : L"Save";
            CreateCustomButtonWithIcon(hDlg, 1, saveText, ButtonColor::Green,
                L"shell32.dll", 258, bX0, bY, bW0, bH, hInst);
            
            // Don't Save button
            auto itDontSave = pLocale->find(L"dont_save");
            std::wstring dontSaveText = (itDontSave != pLocale->end()) ? itDontSave->second : L"Don't Save";
            CreateCustomButtonWithIcon(hDlg, 3, dontSaveText, ButtonColor::Blue,
                L"shell32.dll", 240, bX1, bY, bW1, bH, hInst);
            
            // Cancel button
            auto itCancel = pLocale->find(L"cancel");
            std::wstring cancelText = (itCancel != pLocale->end()) ? itCancel->second : L"Cancel";
            CreateCustomButtonWithIcon(hDlg, IDCANCEL, cancelText, ButtonColor::Red,
                L"shell32.dll", 131, bX2, bY, bW2, bH, hInst);
        }
        
        return 0;
    }
    
    case WM_COMMAND:
        if (LOWORD(wParam) == 1) {  // Save
            g_unsavedChangesDialogResult = 1;
            DestroyWindow(hDlg);
            return 0;
        }
        if (LOWORD(wParam) == 3) {  // Don't Save
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
        
    case WM_CTLCOLORSTATIC: {
        HDC hdcStatic = (HDC)wParam;
        SetBkMode(hdcStatic, TRANSPARENT);
        return (LRESULT)GetStockObject(NULL_BRUSH);
    }

    case WM_DRAWITEM: {
        LPDRAWITEMSTRUCT dis = (LPDRAWITEMSTRUCT)lParam;
        if (dis->CtlID == 1 || dis->CtlID == 3 || dis->CtlID == IDCANCEL) {
            ButtonColor color = (ButtonColor)GetWindowLongPtr(dis->hwndItem, GWLP_USERDATA);
            NONCLIENTMETRICSW ncm = {}; ncm.cbSize = sizeof(ncm);
            SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(ncm), &ncm, 0);
            if (ncm.lfMessageFont.lfHeight < 0)
                ncm.lfMessageFont.lfHeight = (LONG)(ncm.lfMessageFont.lfHeight * 1.2f);
            ncm.lfMessageFont.lfWeight = FW_BOLD;
            ncm.lfMessageFont.lfQuality = CLEARTYPE_QUALITY;
            HFONT hFont = CreateFontIndirectW(&ncm.lfMessageFont);
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
    
    int width = 620, height = 205;
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
    
    // Reset result to safe default before showing dialog
    g_unsavedChangesDialogResult = 0;
    
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
        MSG msg = {};
        while (IsWindow(hDlg)) {
            BOOL bRet = GetMessageW(&msg, NULL, 0, 0);
            if (bRet == 0) { PostQuitMessage((int)msg.wParam); break; } // WM_QUIT: re-post and exit
            if (bRet == -1) break; // error
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
