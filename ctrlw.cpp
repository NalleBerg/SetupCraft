#include "ctrlw.h"
#include "button.h"
#include "dpi.h"
#include <commctrl.h>
#include <shlwapi.h>

extern "C" __declspec(dllimport) UINT WINAPI PrivateExtractIconsW(
    LPCWSTR szFileName, int nIconIndex, int cxIcon, int cyIcon,
    HICON* phicon, UINT* piconid, UINT nIcons, UINT flags);

// ─── Dialog layout constants (design-time pixels at 96 DPI) ─────────────────
static const int DLG_PAD_H   = 20;   // left/right padding
static const int DLG_PAD_T   = 20;   // top padding (above text)
static const int DLG_PAD_B   = 15;   // bottom padding (below buttons)
static const int DLG_GAP_TB  = 15;   // gap between text bottom and button top
static const int DLG_BTN_H   = 34;   // button height
static const int DLG_BTN_W   = 120;  // each button width
static const int DLG_BTN_GAP = 15;   // gap between the two buttons
static const int DLG_CONT_W  = 380;  // text column wrap width
static const int VAL_CONT_W  = 260;  // narrower width for validation/info dialogs

// Convert literal \n / \r\n escape sequences (as stored in locale files)
// into real newline characters so STATIC controls and DrawText display them.
static std::wstring ExpandEscapes(std::wstring s) {
    for (size_t p; (p = s.find(L"\\r\\n")) != std::wstring::npos; )
        s.replace(p, 4, L"\n");
    for (size_t p; (p = s.find(L"\\n"))   != std::wstring::npos; )
        s.replace(p, 2, L"\n");
    return s;
}

// Measure the pixel height that 'text' occupies when word-wrapped into
// 'maxW' logical pixels, using the same NONCLIENTMETRICS font the dialogs use.
static int MeasureDialogTextHeight(const std::wstring& text, int maxW) {
    NONCLIENTMETRICSW ncm = {}; ncm.cbSize = sizeof(ncm);
    SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(ncm), &ncm, 0);
    if (ncm.lfMessageFont.lfHeight < 0)
        ncm.lfMessageFont.lfHeight = (LONG)(ncm.lfMessageFont.lfHeight * 1.2f);
    ncm.lfMessageFont.lfQuality = CLEARTYPE_QUALITY;
    HFONT hFont = CreateFontIndirectW(&ncm.lfMessageFont);
    HDC hdc = GetDC(NULL);
    HFONT hOld = (HFONT)SelectObject(hdc, hFont);
    RECT rc = { 0, 0, maxW, 0 };
    DrawTextW(hdc, text.c_str(), -1, &rc,
              DT_CALCRECT | DT_WORDBREAK | DT_CENTER | DT_NOPREFIX);
    SelectObject(hdc, hOld);
    ReleaseDC(NULL, hdc);
    DeleteObject(hFont);
    return rc.bottom; // rc.top is always 0 here
}

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

            // Build font (identical spec used during pre-measurement in ShowQuitDialog)
            NONCLIENTMETRICSW ncm = {}; ncm.cbSize = sizeof(ncm);
            SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(ncm), &ncm, 0);
            if (ncm.lfMessageFont.lfHeight < 0)
                ncm.lfMessageFont.lfHeight = (LONG)(ncm.lfMessageFont.lfHeight * 1.2f);
            ncm.lfMessageFont.lfQuality = CLEARTYPE_QUALITY;
            HFONT hFont = CreateFontIndirectW(&ncm.lfMessageFont);

            // Retrieve pre-measured text height stored by ShowQuitDialog
            auto itTH = pLocale->find(L"__dlg_textH");
            int textH = (itTH != pLocale->end()) ? std::stoi(itTH->second) : S(50);

            // Retrieve message (already had escapes expanded in ShowQuitDialog)
            auto itMsg = pLocale->find(L"quit_message");
            std::wstring message = (itMsg != pLocale->end()) ? itMsg->second
                                                              : L"Are you sure you want to quit?";

            // Use actual client rect so layout is always correct
            RECT rcC; GetClientRect(hDlg, &rcC);
            int cW = rcC.right;
            int cH = rcC.bottom;

            // Text — fills content column, height from measurement
            HWND hText = CreateWindowExW(0, L"STATIC", message.c_str(),
                WS_CHILD | WS_VISIBLE | SS_CENTER,
                S(DLG_PAD_H), S(DLG_PAD_T),
                cW - 2 * S(DLG_PAD_H), textH,
                hDlg, NULL, hInst, NULL);
            if (hFont) SendMessageW(hText, WM_SETFONT, (WPARAM)hFont, TRUE);

            // Buttons — centred horizontally, pinned to bottom
            auto itYes = pLocale->find(L"yes");
            auto itNo  = pLocale->find(L"no");
            std::wstring yesText = (itYes != pLocale->end()) ? itYes->second : L"Yes";
            std::wstring noText  = (itNo  != pLocale->end()) ? itNo->second  : L"No";
            int wYes = MeasureButtonWidth(yesText, true);
            int wNo  = MeasureButtonWidth(noText,  true);
            int totalBtnW = wYes + S(DLG_BTN_GAP) + wNo;
            int startX    = (cW - totalBtnW) / 2;
            int btnY      = cH - S(DLG_PAD_B) - S(DLG_BTN_H);

            CreateCustomButtonWithIcon(hDlg, IDYES, yesText, ButtonColor::Green,
                L"shell32.dll", 112,
                startX, btnY, wYes, S(DLG_BTN_H), hInst);

            CreateCustomButtonWithIcon(hDlg, IDNO, noText, ButtonColor::Red,
                L"shell32.dll", 131,
                startX + wYes + S(DLG_BTN_GAP), btnY,
                wNo, S(DLG_BTN_H), hInst);
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
    
    // Measure message text to compute dialog size dynamically
    auto itMsg2 = locale.find(L"quit_message");
    std::wstring msgRaw = (itMsg2 != locale.end()) ? itMsg2->second
                                                   : L"Are you sure you want to quit?";
    std::wstring msgExpanded = ExpandEscapes(msgRaw);
    int textH  = MeasureDialogTextHeight(msgExpanded, S(DLG_CONT_W));
    if (textH < S(20)) textH = S(20); // floor: at least one line

    int clientW = S(DLG_CONT_W) + 2 * S(DLG_PAD_H);
    int clientH = S(DLG_PAD_T) + textH + S(DLG_GAP_TB) + S(DLG_BTN_H) + S(DLG_PAD_B);

    // Convert client size to outer window size
    RECT wrc = { 0, 0, clientW, clientH };
    AdjustWindowRectEx(&wrc, WS_POPUP | WS_CAPTION | WS_SYSMENU,
                       FALSE, WS_EX_DLGMODALFRAME);
    int width  = wrc.right  - wrc.left;
    int height = wrc.bottom - wrc.top;

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
    // Inject the pre-measured text height and expanded message so WM_CREATE
    // can do layout without re-measuring.
    std::map<std::wstring, std::wstring>* pLocaleCopy =
        new std::map<std::wstring, std::wstring>(locale);
    (*pLocaleCopy)[L"quit_message"] = msgExpanded; // already has real newlines
    (*pLocaleCopy)[L"__dlg_textH"]  = std::to_wstring(textH);

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

// ── Duplicate project name dialog ─────────────────────────────────────────
// Returns: 0=Cancel  1=Overwrite  2=Rename
static int g_dupDialogResult = 0;

// Button widths for the 3-button row (design px at 96 DPI)
static const int DUP_BTN_W0 = 140; // Overwrite
static const int DUP_BTN_W1 = 175; // Rename this one
static const int DUP_BTN_W2 = 120; // Cancel

LRESULT CALLBACK DupDialogProc(HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_CREATE: {
        CREATESTRUCTW* cs = (CREATESTRUCTW*)lParam;
        std::map<std::wstring,std::wstring>* pLocale =
            (std::map<std::wstring,std::wstring>*)cs->lpCreateParams;
        HINSTANCE hInst = cs->hInstance;
        SetWindowLongPtr(hDlg, GWLP_USERDATA, (LONG_PTR)pLocale);

        NONCLIENTMETRICSW ncm = {}; ncm.cbSize = sizeof(ncm);
        SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(ncm), &ncm, 0);
        if (ncm.lfMessageFont.lfHeight < 0)
            ncm.lfMessageFont.lfHeight = (LONG)(ncm.lfMessageFont.lfHeight * 1.2f);
        ncm.lfMessageFont.lfQuality = CLEARTYPE_QUALITY;
        HFONT hFont = CreateFontIndirectW(&ncm.lfMessageFont);

        // Pre-measured text height and expanded message injected by ShowDuplicateProjectDialog
        auto itTH  = pLocale->find(L"__dlg_textH");
        auto itMsg = pLocale->find(L"dup_proj_message");
        int textH  = (itTH  != pLocale->end()) ? std::stoi(itTH->second) : S(50);
        std::wstring message = (itMsg != pLocale->end()) ? itMsg->second : L"";

        RECT rcC; GetClientRect(hDlg, &rcC);
        int cW = rcC.right, cH = rcC.bottom;

        // Centred message text
        HWND hText = CreateWindowExW(0, L"STATIC", message.c_str(),
            WS_CHILD | WS_VISIBLE | SS_CENTER,
            S(DLG_PAD_H), S(DLG_PAD_T), cW - 2*S(DLG_PAD_H), textH,
            hDlg, NULL, hInst, NULL);
        if (hFont) SendMessageW(hText, WM_SETFONT, (WPARAM)hFont, TRUE);

        // 3 buttons centred horizontally, pinned to bottom
        auto itOvr = pLocale->find(L"dup_proj_overwrite");
        auto itRen = pLocale->find(L"dup_proj_rename");
        auto itCnl = pLocale->find(L"cancel");
        std::wstring ovrTxt = (itOvr != pLocale->end()) ? itOvr->second : L"Overwrite";
        std::wstring renTxt = (itRen != pLocale->end()) ? itRen->second : L"Rename this one";
        std::wstring cnlTxt = (itCnl != pLocale->end()) ? itCnl->second : L"Cancel";

        auto itW0 = pLocale->find(L"__btn_w0");
        auto itW1 = pLocale->find(L"__btn_w1");
        auto itW2 = pLocale->find(L"__btn_w2");
        int w0 = (itW0 != pLocale->end()) ? std::stoi(itW0->second) : MeasureButtonWidth(ovrTxt, true);
        int w1 = (itW1 != pLocale->end()) ? std::stoi(itW1->second) : MeasureButtonWidth(renTxt, true);
        int w2 = (itW2 != pLocale->end()) ? std::stoi(itW2->second) : MeasureButtonWidth(cnlTxt, true);

        int totalBtnW = w0 + w1 + w2 + 2*S(DLG_BTN_GAP);
        int startX    = (cW - totalBtnW) / 2;
        int btnY      = cH - S(DLG_PAD_B) - S(DLG_BTN_H);

        CreateCustomButtonWithIcon(hDlg, 1, ovrTxt, ButtonColor::Red,
            L"shell32.dll", 240,
            startX, btnY, w0, S(DLG_BTN_H), hInst);
        CreateCustomButtonWithIcon(hDlg, 2, renTxt, ButtonColor::Blue,
            L"shell32.dll", 296,
            startX+w0+S(DLG_BTN_GAP), btnY,
            w1, S(DLG_BTN_H), hInst);
        CreateCustomButtonWithIcon(hDlg, IDCANCEL, cnlTxt, ButtonColor::Red,
            L"shell32.dll", 131,
            startX+w0+S(DLG_BTN_GAP)+w1+S(DLG_BTN_GAP), btnY,
            w2, S(DLG_BTN_H), hInst);
        return 0;
    }
    case WM_COMMAND:
        if (LOWORD(wParam)==1)        { g_dupDialogResult=1; DestroyWindow(hDlg); return 0; }
        if (LOWORD(wParam)==2)        { g_dupDialogResult=2; DestroyWindow(hDlg); return 0; }
        if (LOWORD(wParam)==IDCANCEL) { g_dupDialogResult=0; DestroyWindow(hDlg); return 0; }
        break;
    case WM_DRAWITEM: {
        LPDRAWITEMSTRUCT dis = (LPDRAWITEMSTRUCT)lParam;
        if (dis->CtlID==1 || dis->CtlID==2 || dis->CtlID==IDCANCEL) {
            ButtonColor color = (ButtonColor)GetWindowLongPtr(dis->hwndItem, GWLP_USERDATA);
            NONCLIENTMETRICSW ncm={}; ncm.cbSize=sizeof(ncm);
            SystemParametersInfoW(SPI_GETNONCLIENTMETRICS,sizeof(ncm),&ncm,0);
            if (ncm.lfMessageFont.lfHeight<0)
                ncm.lfMessageFont.lfHeight=(LONG)(ncm.lfMessageFont.lfHeight*1.2f);
            ncm.lfMessageFont.lfWeight=FW_BOLD;
            ncm.lfMessageFont.lfQuality=CLEARTYPE_QUALITY;
            HFONT hF=CreateFontIndirectW(&ncm.lfMessageFont);
            LRESULT r=DrawCustomButton(dis,color,hF);
            if(hF) DeleteObject(hF);
            return r;
        }
        break;
    }
    case WM_CTLCOLORSTATIC: {
        SetBkMode((HDC)wParam, TRANSPARENT);
        return (LRESULT)GetStockObject(NULL_BRUSH);
    }
    case WM_CLOSE:
        g_dupDialogResult=0; DestroyWindow(hDlg); return 0;
    }
    return DefWindowProcW(hDlg, msg, wParam, lParam);
}

int ShowDuplicateProjectDialog(HWND hwndParent, const std::wstring& projectName,
                               const std::map<std::wstring,std::wstring>& locale) {
    static bool reg = false;
    if (!reg) {
        WNDCLASSEXW wc={}; wc.cbSize=sizeof(wc);
        wc.style=CS_HREDRAW|CS_VREDRAW;
        wc.lpfnWndProc=DupDialogProc;
        wc.hInstance=GetModuleHandle(NULL);
        wc.hCursor=LoadCursor(NULL,IDC_ARROW);
        wc.hbrBackground=(HBRUSH)(COLOR_WINDOW+1);
        wc.lpszClassName=L"DupProjDialogClass";
        RegisterClassExW(&wc); reg=true;
    }

    // Substitute project name into message template and expand escape sequences
    auto itMsg = locale.find(L"dup_proj_message");
    std::wstring msgRaw = (itMsg != locale.end()) ? itMsg->second
        : L"A project named \u201c{0}\u201d already exists.\n\nWhat do you want to do?";
    auto p = msgRaw.find(L"{0}");
    if (p != std::wstring::npos) msgRaw.replace(p, 3, projectName);
    std::wstring msgExpanded = ExpandEscapes(msgRaw);

    // Content width driven by the 3-button row (auto-measured for multilanguage)
    auto itOvr2 = locale.find(L"dup_proj_overwrite");
    auto itRen2 = locale.find(L"dup_proj_rename");
    auto itCnl2 = locale.find(L"cancel");
    std::wstring ovrTxt2 = (itOvr2 != locale.end()) ? itOvr2->second : L"Overwrite";
    std::wstring renTxt2 = (itRen2 != locale.end()) ? itRen2->second : L"Rename this one";
    std::wstring cnlTxt2 = (itCnl2 != locale.end()) ? itCnl2->second : L"Cancel";
    int dw0 = MeasureButtonWidth(ovrTxt2, true);
    int dw1 = MeasureButtonWidth(renTxt2, true);
    int dw2 = MeasureButtonWidth(cnlTxt2, true);
    int contW = dw0 + dw1 + dw2 + 2*S(DLG_BTN_GAP) + 2*S(DLG_PAD_H);
    int textW = contW - 2*S(DLG_PAD_H);
    int textH = MeasureDialogTextHeight(msgExpanded, textW);
    if (textH < S(20)) textH = S(20);

    int clientW = contW;
    int clientH = S(DLG_PAD_T) + textH + S(DLG_GAP_TB) + S(DLG_BTN_H) + S(DLG_PAD_B);
    RECT wrc = { 0, 0, clientW, clientH };
    AdjustWindowRectEx(&wrc, WS_POPUP|WS_CAPTION|WS_SYSMENU, FALSE, WS_EX_DLGMODALFRAME);
    int w = wrc.right-wrc.left, h = wrc.bottom-wrc.top;

    RECT rcP; GetWindowRect(hwndParent, &rcP);
    int x=rcP.left+(rcP.right-rcP.left-w)/2;
    int y=rcP.top+(rcP.bottom-rcP.top-h)/2;
    RECT rcW; SystemParametersInfoW(SPI_GETWORKAREA,0,&rcW,0);
    if(x<rcW.left) x=rcW.left; if(y<rcW.top) y=rcW.top;
    if(x+w>rcW.right) x=rcW.right-w; if(y+h>rcW.bottom) y=rcW.bottom-h;

    auto itTitle = locale.find(L"dup_proj_title");
    std::wstring title = (itTitle != locale.end()) ? itTitle->second : L"Project Already Exists";

    // Pass locale copy with pre-computed values to WM_CREATE
    std::map<std::wstring,std::wstring>* pLocCopy =
        new std::map<std::wstring,std::wstring>(locale);
    (*pLocCopy)[L"dup_proj_message"] = msgExpanded;
    (*pLocCopy)[L"__dlg_textH"]     = std::to_wstring(textH);
    (*pLocCopy)[L"__btn_w0"]        = std::to_wstring(dw0);
    (*pLocCopy)[L"__btn_w1"]        = std::to_wstring(dw1);
    (*pLocCopy)[L"__btn_w2"]        = std::to_wstring(dw2);

    g_dupDialogResult=0;
    HWND hDlg = CreateWindowExW(WS_EX_DLGMODALFRAME|WS_EX_TOPMOST,
        L"DupProjDialogClass", title.c_str(),
        WS_POPUP|WS_CAPTION|WS_SYSMENU,
        x, y, w, h, hwndParent, NULL, GetModuleHandle(NULL), pLocCopy);
    if (hDlg) {
        EnableWindow(hwndParent, FALSE);
        ShowWindow(hDlg, SW_SHOW);
        MSG msg={};
        while (IsWindow(hDlg)) {
            BOOL bRet=GetMessageW(&msg,NULL,0,0);
            if(bRet==0){PostQuitMessage((int)msg.wParam);break;}
            if(bRet==-1||!IsWindow(hDlg)) break;
            TranslateMessage(&msg); DispatchMessageW(&msg);
        }
        EnableWindow(hwndParent, TRUE);
        SetForegroundWindow(hwndParent);
    }
    delete pLocCopy;
    return g_dupDialogResult;
}

// ── Rename project input dialog ────────────────────────────────────────────
static std::wstring g_renameResult;
static bool        g_renameConfirmed = false;

static const int REN_EDIT_H = 26; // edit box height (design px)
static const int REN_LBL_H  = 22; // label height
static const int REN_GAP_LE =  6; // gap between label and edit
static const int REN_BTN_W  = 120; // OK / Cancel button width

LRESULT CALLBACK RenameDialogProc(HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_CREATE: {
        CREATESTRUCTW* cs = (CREATESTRUCTW*)lParam;
        std::map<std::wstring,std::wstring>* pLocale =
            (std::map<std::wstring,std::wstring>*)cs->lpCreateParams;
        HINSTANCE hInst = cs->hInstance;
        SetWindowLongPtr(hDlg, GWLP_USERDATA, (LONG_PTR)pLocale);

        NONCLIENTMETRICSW ncm={}; ncm.cbSize=sizeof(ncm);
        SystemParametersInfoW(SPI_GETNONCLIENTMETRICS,sizeof(ncm),&ncm,0);
        if (ncm.lfMessageFont.lfHeight<0)
            ncm.lfMessageFont.lfHeight=(LONG)(ncm.lfMessageFont.lfHeight*1.2f);
        ncm.lfMessageFont.lfQuality=CLEARTYPE_QUALITY;
        HFONT hFont=CreateFontIndirectW(&ncm.lfMessageFont);

        auto itLbl  = pLocale->find(L"rename_proj_label");
        auto itName = pLocale->find(L"__dlg_initName");
        std::wstring lblTxt  = (itLbl  != pLocale->end()) ? itLbl->second  : L"Enter a new project name:";
        std::wstring initVal = (itName != pLocale->end()) ? itName->second : L"";

        RECT rcC; GetClientRect(hDlg, &rcC);
        int cW = rcC.right;
        int editW = cW - 2*S(DLG_PAD_H);

        // Label
        HWND hLbl=CreateWindowExW(0,L"STATIC",lblTxt.c_str(),
            WS_CHILD|WS_VISIBLE|SS_LEFT,
            S(DLG_PAD_H), S(DLG_PAD_T), editW, S(REN_LBL_H),
            hDlg, NULL, hInst, NULL);
        if(hFont) SendMessageW(hLbl,WM_SETFONT,(WPARAM)hFont,TRUE);

        // Edit box
        int editY = S(DLG_PAD_T) + S(REN_LBL_H) + S(REN_GAP_LE);
        HWND hEdit=CreateWindowExW(WS_EX_CLIENTEDGE,L"EDIT",initVal.c_str(),
            WS_CHILD|WS_VISIBLE|WS_BORDER|ES_LEFT|ES_AUTOHSCROLL,
            S(DLG_PAD_H), editY, editW, S(REN_EDIT_H),
            hDlg,(HMENU)100,hInst,NULL);
        if(hFont) SendMessageW(hEdit,WM_SETFONT,(WPARAM)hFont,TRUE);
        SendMessageW(hEdit,EM_SETSEL,0,-1);
        SetFocus(hEdit);

        // Buttons centred, pinned to bottom
        int totalBtnW = 2*S(REN_BTN_W)+S(DLG_BTN_GAP);
        int startX    = (cW - totalBtnW) / 2;
        int btnY      = rcC.bottom - S(DLG_PAD_B) - S(DLG_BTN_H);

        auto itOK  = pLocale->find(L"ok");
        auto itCnl = pLocale->find(L"cancel");
        std::wstring okTxt  = (itOK  != pLocale->end()) ? itOK->second  : L"OK";
        std::wstring cnlTxt = (itCnl != pLocale->end()) ? itCnl->second : L"Cancel";

        int wOK_r  = MeasureButtonWidth(okTxt,  true);
        int wCnl_r = MeasureButtonWidth(cnlTxt, true);
        totalBtnW = wOK_r + S(DLG_BTN_GAP) + wCnl_r;
        startX    = (cW - totalBtnW) / 2;

        CreateCustomButtonWithIcon(hDlg,IDOK,okTxt,ButtonColor::Green,
            L"shell32.dll",258,
            startX, btnY, wOK_r, S(DLG_BTN_H), hInst);
        CreateCustomButtonWithIcon(hDlg,IDCANCEL,cnlTxt,ButtonColor::Red,
            L"shell32.dll",131,
            startX+wOK_r+S(DLG_BTN_GAP), btnY,
            wCnl_r, S(DLG_BTN_H), hInst);
        return 0;
    }
    case WM_COMMAND:
        if (LOWORD(wParam)==IDOK) {
            HWND hEdit=GetDlgItem(hDlg,100);
            int len=GetWindowTextLengthW(hEdit);
            if(len>0){
                g_renameResult.resize(len+1);
                GetWindowTextW(hEdit,&g_renameResult[0],len+1);
                g_renameResult.resize(len);
            } else {
                g_renameResult.clear();
            }
            g_renameConfirmed=true;
            DestroyWindow(hDlg); return 0;
        }
        if (LOWORD(wParam)==IDCANCEL) {
            g_renameConfirmed=false; DestroyWindow(hDlg); return 0;
        }
        break;
    case WM_DRAWITEM: {
        LPDRAWITEMSTRUCT dis=(LPDRAWITEMSTRUCT)lParam;
        if(dis->CtlID==IDOK||dis->CtlID==IDCANCEL){
            ButtonColor color=(ButtonColor)GetWindowLongPtr(dis->hwndItem,GWLP_USERDATA);
            NONCLIENTMETRICSW ncm={}; ncm.cbSize=sizeof(ncm);
            SystemParametersInfoW(SPI_GETNONCLIENTMETRICS,sizeof(ncm),&ncm,0);
            if(ncm.lfMessageFont.lfHeight<0)
                ncm.lfMessageFont.lfHeight=(LONG)(ncm.lfMessageFont.lfHeight*1.2f);
            ncm.lfMessageFont.lfWeight=FW_BOLD;
            ncm.lfMessageFont.lfQuality=CLEARTYPE_QUALITY;
            HFONT hF=CreateFontIndirectW(&ncm.lfMessageFont);
            LRESULT r=DrawCustomButton(dis,color,hF);
            if(hF) DeleteObject(hF);
            return r;
        }
        break;
    }
    case WM_CTLCOLORSTATIC: {
        SetBkMode((HDC)wParam,TRANSPARENT);
        return (LRESULT)GetStockObject(NULL_BRUSH);
    }
    case WM_CTLCOLOREDIT:
        return (LRESULT)GetSysColorBrush(COLOR_WINDOW);
    case WM_KEYDOWN:
        if(wParam==VK_RETURN)  { SendMessageW(hDlg,WM_COMMAND,IDOK,0);     return 0; }
        if(wParam==VK_ESCAPE)  { SendMessageW(hDlg,WM_COMMAND,IDCANCEL,0); return 0; }
        break;
    case WM_CLOSE:
        g_renameConfirmed=false; DestroyWindow(hDlg); return 0;
    }
    return DefWindowProcW(hDlg, msg, wParam, lParam);
}

bool ShowRenameProjectDialog(HWND hwndParent, std::wstring& inOutName,
                             const std::map<std::wstring,std::wstring>& locale) {
    static bool reg=false;
    if(!reg){
        WNDCLASSEXW wc={}; wc.cbSize=sizeof(wc);
        wc.style=CS_HREDRAW|CS_VREDRAW;
        wc.lpfnWndProc=RenameDialogProc;
        wc.hInstance=GetModuleHandle(NULL);
        wc.hCursor=LoadCursor(NULL,IDC_ARROW);
        wc.hbrBackground=(HBRUSH)(COLOR_WINDOW+1);
        wc.lpszClassName=L"RenameProjDialogClass";
        RegisterClassExW(&wc); reg=true;
    }

    // Client size: text column + padding, label + edit + gap + buttons
    int editW   = S(DLG_CONT_W);
    int clientW = editW + 2*S(DLG_PAD_H);
    int clientH = S(DLG_PAD_T) + S(REN_LBL_H) + S(REN_GAP_LE)
                + S(REN_EDIT_H) + S(DLG_GAP_TB) + S(DLG_BTN_H) + S(DLG_PAD_B);
    RECT wrc = { 0, 0, clientW, clientH };
    AdjustWindowRectEx(&wrc, WS_POPUP|WS_CAPTION|WS_SYSMENU, FALSE, WS_EX_DLGMODALFRAME);
    int w = wrc.right-wrc.left, h = wrc.bottom-wrc.top;

    RECT rcP; GetWindowRect(hwndParent,&rcP);
    int x=rcP.left+(rcP.right-rcP.left-w)/2;
    int y=rcP.top+(rcP.bottom-rcP.top-h)/2;
    RECT rcW; SystemParametersInfoW(SPI_GETWORKAREA,0,&rcW,0);
    if(x<rcW.left) x=rcW.left; if(y<rcW.top) y=rcW.top;
    if(x+w>rcW.right) x=rcW.right-w; if(y+h>rcW.bottom) y=rcW.bottom-h;

    auto itTitle = locale.find(L"rename_proj_title");
    std::wstring title = (itTitle != locale.end()) ? itTitle->second : L"Rename Project";

    // Pass locale copy with initial name to WM_CREATE
    std::map<std::wstring,std::wstring>* pLocCopy =
        new std::map<std::wstring,std::wstring>(locale);
    (*pLocCopy)[L"__dlg_initName"] = inOutName;

    g_renameConfirmed=false;
    g_renameResult=inOutName;
    HWND hDlg=CreateWindowExW(WS_EX_DLGMODALFRAME|WS_EX_TOPMOST,
        L"RenameProjDialogClass", title.c_str(),
        WS_POPUP|WS_CAPTION|WS_SYSMENU,
        x,y,w,h,hwndParent,NULL,GetModuleHandle(NULL),pLocCopy);
    if(hDlg){
        EnableWindow(hwndParent,FALSE);
        ShowWindow(hDlg,SW_SHOW);
        MSG msg={};
        while(IsWindow(hDlg)){
            BOOL bRet=GetMessageW(&msg,NULL,0,0);
            if(bRet==0){PostQuitMessage((int)msg.wParam);break;}
            if(bRet==-1||!IsWindow(hDlg)) break;
            TranslateMessage(&msg); DispatchMessageW(&msg);
        }
        EnableWindow(hwndParent,TRUE);
        SetForegroundWindow(hwndParent);
    }
    delete pLocCopy;
    if(g_renameConfirmed && !g_renameResult.empty())
        inOutName=g_renameResult;
    return g_renameConfirmed && !g_renameResult.empty();
}

bool IsCtrlWPressed(UINT msg, WPARAM wParam) {
    if (msg == WM_KEYDOWN && wParam == 'W') {
        return (GetKeyState(VK_CONTROL) & 0x8000) != 0;
    }
    return false;
}

// UnsavedChanges button widths (design px)
static const int UNS_BTN_W0 = 140; // Save
static const int UNS_BTN_W1 = 175; // Don't Save
static const int UNS_BTN_W2 = 120; // Cancel

// Global for unsaved changes dialog result
static int g_unsavedChangesDialogResult = 0;

// Dialog procedure for unsaved changes confirmation
LRESULT CALLBACK UnsavedChangesDialogProc(HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_CREATE: {
        CREATESTRUCTW* cs = (CREATESTRUCTW*)lParam;
        std::map<std::wstring, std::wstring>* pLocale =
            (std::map<std::wstring, std::wstring>*)cs->lpCreateParams;
        if (!pLocale) return 0;
        SetWindowLongPtr(hDlg, GWLP_USERDATA, (LONG_PTR)pLocale);
        HINSTANCE hInst = cs->hInstance;

        NONCLIENTMETRICSW ncm = {}; ncm.cbSize = sizeof(ncm);
        SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(ncm), &ncm, 0);
        if (ncm.lfMessageFont.lfHeight < 0)
            ncm.lfMessageFont.lfHeight = (LONG)(ncm.lfMessageFont.lfHeight * 1.2f);
        ncm.lfMessageFont.lfQuality = CLEARTYPE_QUALITY;
        HFONT hFont = CreateFontIndirectW(&ncm.lfMessageFont);

        auto itTH  = pLocale->find(L"__dlg_textH");
        auto itMsg = pLocale->find(L"close_unsaved_message");
        int textH  = (itTH  != pLocale->end()) ? std::stoi(itTH->second) : S(50);
        std::wstring message = (itMsg != pLocale->end()) ? itMsg->second
            : L"You have unsaved changes. Do you want to save before closing?";

        RECT rcC; GetClientRect(hDlg, &rcC);
        int cW = rcC.right, cH = rcC.bottom;

        // Text
        HWND hText = CreateWindowExW(0, L"STATIC", message.c_str(),
            WS_CHILD | WS_VISIBLE | SS_CENTER,
            S(DLG_PAD_H), S(DLG_PAD_T), cW - 2*S(DLG_PAD_H), textH,
            hDlg, NULL, hInst, NULL);
        if (hFont) SendMessageW(hText, WM_SETFONT, (WPARAM)hFont, TRUE);

        // 3 buttons centred, pinned to bottom
        auto itSave  = pLocale->find(L"save");
        auto itDont  = pLocale->find(L"dont_save");
        auto itCnl   = pLocale->find(L"cancel");
        std::wstring saveTxt  = (itSave != pLocale->end()) ? itSave->second  : L"Save";
        std::wstring dontTxt  = (itDont != pLocale->end()) ? itDont->second  : L"Don't Save";
        std::wstring cnlTxt   = (itCnl  != pLocale->end()) ? itCnl->second   : L"Cancel";

        auto itUW0 = pLocale->find(L"__btn_w0");
        auto itUW1 = pLocale->find(L"__btn_w1");
        auto itUW2 = pLocale->find(L"__btn_w2");
        int uw0 = (itUW0 != pLocale->end()) ? std::stoi(itUW0->second) : MeasureButtonWidth(saveTxt, true);
        int uw1 = (itUW1 != pLocale->end()) ? std::stoi(itUW1->second) : MeasureButtonWidth(dontTxt, true);
        int uw2 = (itUW2 != pLocale->end()) ? std::stoi(itUW2->second) : MeasureButtonWidth(cnlTxt,  true);

        int totalBtnW = uw0 + uw1 + uw2 + 2*S(DLG_BTN_GAP);
        int startX    = (cW - totalBtnW) / 2;
        int btnY      = cH - S(DLG_PAD_B) - S(DLG_BTN_H);

        CreateCustomButtonWithIcon(hDlg, 1, saveTxt, ButtonColor::Green,
            L"shell32.dll", 258, startX, btnY, uw0, S(DLG_BTN_H), hInst);
        CreateCustomButtonWithIcon(hDlg, 3, dontTxt, ButtonColor::Blue,
            L"shell32.dll", 240,
            startX+uw0+S(DLG_BTN_GAP), btnY,
            uw1, S(DLG_BTN_H), hInst);
        CreateCustomButtonWithIcon(hDlg, IDCANCEL, cnlTxt, ButtonColor::Red,
            L"shell32.dll", 131,
            startX+uw0+S(DLG_BTN_GAP)+uw1+S(DLG_BTN_GAP), btnY,
            uw2, S(DLG_BTN_H), hInst);
        return 0;
    }
    case WM_COMMAND:
        if (LOWORD(wParam) == 1)        { g_unsavedChangesDialogResult = 1; DestroyWindow(hDlg); return 0; }
        if (LOWORD(wParam) == 3)        { g_unsavedChangesDialogResult = 2; DestroyWindow(hDlg); return 0; }
        if (LOWORD(wParam) == IDCANCEL) { g_unsavedChangesDialogResult = 0; DestroyWindow(hDlg); return 0; }
        break;
    case WM_CLOSE:
        g_unsavedChangesDialogResult = 0;
        DestroyWindow(hDlg);
        return 0;
    case WM_CTLCOLORSTATIC: {
        SetBkMode((HDC)wParam, TRANSPARENT);
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

    // Measure message text for dynamic sizing
    auto itMsg = locale.find(L"close_unsaved_message");
    std::wstring msgRaw = (itMsg != locale.end()) ? itMsg->second
        : L"You have unsaved changes. Do you want to save before closing?";
    std::wstring msgExpanded = ExpandEscapes(msgRaw);

    // Content width driven by the 3-button row (auto-measured for multilanguage)
    auto itSave2 = locale.find(L"save");
    auto itDont2 = locale.find(L"dont_save");
    auto itCnl3  = locale.find(L"cancel");
    std::wstring saveTxt2 = (itSave2 != locale.end()) ? itSave2->second : L"Save";
    std::wstring dontTxt2 = (itDont2 != locale.end()) ? itDont2->second : L"Don't Save";
    std::wstring cnlTxt3  = (itCnl3  != locale.end()) ? itCnl3->second  : L"Cancel";
    int uw0s = MeasureButtonWidth(saveTxt2, true);
    int uw1s = MeasureButtonWidth(dontTxt2, true);
    int uw2s = MeasureButtonWidth(cnlTxt3,  true);
    int contW  = uw0s + uw1s + uw2s
               + 2*S(DLG_BTN_GAP) + 2*S(DLG_PAD_H);
    int textW  = contW - 2*S(DLG_PAD_H);
    int textH  = MeasureDialogTextHeight(msgExpanded, textW);
    if (textH < S(20)) textH = S(20);

    int clientW = contW;
    int clientH = S(DLG_PAD_T) + textH + S(DLG_GAP_TB) + S(DLG_BTN_H) + S(DLG_PAD_B);
    RECT wrc = { 0, 0, clientW, clientH };
    AdjustWindowRectEx(&wrc, WS_POPUP|WS_CAPTION|WS_SYSMENU, FALSE, WS_EX_DLGMODALFRAME);
    int width  = wrc.right  - wrc.left;
    int height = wrc.bottom - wrc.top;

    // Get title from locale
    auto itTitle = locale.find(L"close_unsaved_title");
    std::wstring title = (itTitle != locale.end()) ? itTitle->second : L"Unsaved Changes";

    // Centre on parent, clamp to work area
    RECT rcParent;
    if (!hwndParent || !GetWindowRect(hwndParent, &rcParent))
        SystemParametersInfoW(SPI_GETWORKAREA, 0, &rcParent, 0);
    int x = rcParent.left + (rcParent.right  - rcParent.left - width)  / 2;
    int y = rcParent.top  + (rcParent.bottom - rcParent.top  - height) / 2;
    RECT rcWork; SystemParametersInfoW(SPI_GETWORKAREA, 0, &rcWork, 0);
    if (x < rcWork.left) x = rcWork.left;
    if (y < rcWork.top)  y = rcWork.top;
    if (x + width  > rcWork.right)  x = rcWork.right  - width;
    if (y + height > rcWork.bottom) y = rcWork.bottom - height;

    // Inject pre-computed values and expanded message
    std::map<std::wstring, std::wstring>* pLocaleCopy =
        new std::map<std::wstring, std::wstring>(locale);
    (*pLocaleCopy)[L"close_unsaved_message"] = msgExpanded;
    (*pLocaleCopy)[L"__dlg_textH"]           = std::to_wstring(textH);
    (*pLocaleCopy)[L"__btn_w0"]              = std::to_wstring(uw0s);
    (*pLocaleCopy)[L"__btn_w1"]              = std::to_wstring(uw1s);
    (*pLocaleCopy)[L"__btn_w2"]              = std::to_wstring(uw2s);

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

// ── Generic yes/no confirm dialog (reuses ShowQuitDialog infrastructure) ──────
bool ShowConfirmDeleteDialog(HWND hwndParent, const std::wstring& title,
                             const std::wstring& message,
                             const std::map<std::wstring, std::wstring>& locale)
{
    std::map<std::wstring, std::wstring> patched = locale;
    patched[L"quit_title"]   = title;
    patched[L"quit_message"] = message;
    return ShowQuitDialog(hwndParent, patched);
}

// ── Single-button OK dialog (validation errors, informational notices) ─────────
struct ValidationDlgData {
    std::wstring message;
    std::wstring okText;
    int          textH;
    int          iconSize;  // scaled icon size (32 design-px)
    HINSTANCE    hInst;
};

static LRESULT CALLBACK ValidationDialogProc(HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg) {
    case WM_CREATE: {
        CREATESTRUCTW* cs   = (CREATESTRUCTW*)lParam;
        ValidationDlgData* pData = (ValidationDlgData*)cs->lpCreateParams;
        if (!pData) return 0;
        SetWindowLongPtr(hDlg, GWLP_USERDATA, (LONG_PTR)pData);

        NONCLIENTMETRICSW ncm = {}; ncm.cbSize = sizeof(ncm);
        SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(ncm), &ncm, 0);
        if (ncm.lfMessageFont.lfHeight < 0)
            ncm.lfMessageFont.lfHeight = (LONG)(ncm.lfMessageFont.lfHeight * 1.2f);
        ncm.lfMessageFont.lfQuality = CLEARTYPE_QUALITY;
        HFONT hFont = CreateFontIndirectW(&ncm.lfMessageFont);

        RECT rcC; GetClientRect(hDlg, &rcC);
        int cW = rcC.right;
        int cH = rcC.bottom;

        // ── Info icon (shell32.dll #221 — white-on-blue round "i") ──────────
        int iconSz = pData->iconSize;
        {
            HWND hIconCtrl = CreateWindowExW(0, L"STATIC", NULL,
                WS_CHILD | WS_VISIBLE | SS_ICON | SS_CENTERIMAGE,
                (cW - iconSz) / 2, S(DLG_PAD_T),
                iconSz, iconSz,
                hDlg, NULL, pData->hInst, NULL);
            wchar_t dllPath[MAX_PATH];
            GetSystemDirectoryW(dllPath, MAX_PATH);
            PathAppendW(dllPath, L"shell32.dll");
            HICON hIco = NULL;
            PrivateExtractIconsW(dllPath, 221, iconSz, iconSz, &hIco, NULL, 1, 0);
            if (hIco) SendMessageW(hIconCtrl, STM_SETICON, (WPARAM)hIco, 0);
        }

        int textY = S(DLG_PAD_T) + iconSz + S(8);
        HWND hText = CreateWindowExW(0, L"STATIC", pData->message.c_str(),
            WS_CHILD | WS_VISIBLE | SS_CENTER,
            S(DLG_PAD_H), textY,
            cW - 2 * S(DLG_PAD_H), pData->textH,
            hDlg, NULL, pData->hInst, NULL);
        if (hFont) SendMessageW(hText, WM_SETFONT, (WPARAM)hFont, TRUE);

        int btnW = MeasureButtonWidth(pData->okText, true);
        int btnX = (cW - btnW) / 2;
        int btnY = cH - S(DLG_PAD_B) - S(DLG_BTN_H);
        CreateCustomButtonWithIcon(hDlg, IDOK, pData->okText, ButtonColor::Blue,
            L"shell32.dll", 112,
            btnX, btnY, btnW, S(DLG_BTN_H), pData->hInst);
        return 0;
    }
    case WM_COMMAND:
        if (LOWORD(wParam) == IDOK) { DestroyWindow(hDlg); return 0; }
        break;
    case WM_DRAWITEM: {
        LPDRAWITEMSTRUCT dis = (LPDRAWITEMSTRUCT)lParam;
        if (dis->CtlID == IDOK) {
            ButtonColor color = (ButtonColor)GetWindowLongPtr(dis->hwndItem, GWLP_USERDATA);
            NONCLIENTMETRICSW ncm = {}; ncm.cbSize = sizeof(ncm);
            SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(ncm), &ncm, 0);
            if (ncm.lfMessageFont.lfHeight < 0)
                ncm.lfMessageFont.lfHeight = (LONG)(ncm.lfMessageFont.lfHeight * 1.2f);
            ncm.lfMessageFont.lfWeight = FW_BOLD;
            ncm.lfMessageFont.lfQuality = CLEARTYPE_QUALITY;
            HFONT hFont = CreateFontIndirectW(&ncm.lfMessageFont);
            LRESULT r = DrawCustomButton(dis, color, hFont);
            if (hFont) DeleteObject(hFont);
            return r;
        }
        break;
    }
    case WM_CTLCOLORSTATIC: {
        HDC hdcStatic = (HDC)wParam;
        SetBkMode(hdcStatic, TRANSPARENT);
        SetBkColor(hdcStatic, RGB(255, 255, 255));
        return (LRESULT)GetStockObject(WHITE_BRUSH);
    }
    case WM_CLOSE:
        DestroyWindow(hDlg);
        return 0;
    case WM_DESTROY:
        return 0;
    case WM_KEYDOWN:
        if (wParam == VK_ESCAPE || wParam == VK_RETURN) {
            DestroyWindow(hDlg);
            return 0;
        }
        break;
    }
    return DefWindowProcW(hDlg, msg, wParam, lParam);
}

void ShowValidationDialog(HWND hwndParent, const std::wstring& title,
                          const std::wstring& message,
                          const std::map<std::wstring, std::wstring>& locale)
{
    static bool s_classRegistered = false;
    if (!s_classRegistered) {
        WNDCLASSEXW wc = {};
        wc.cbSize        = sizeof(wc);
        wc.style         = CS_HREDRAW | CS_VREDRAW;
        wc.lpfnWndProc   = ValidationDialogProc;
        wc.hInstance     = GetModuleHandle(NULL);
        wc.hCursor       = LoadCursor(NULL, IDC_ARROW);
        wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
        wc.lpszClassName = L"ValidationDialogClass";
        RegisterClassExW(&wc);
        s_classRegistered = true;
    }

    std::wstring msgExpanded = ExpandEscapes(message);
    int textH = MeasureDialogTextHeight(msgExpanded, S(VAL_CONT_W));
    if (textH < S(20)) textH = S(20);

    auto itOk = locale.find(L"ok");
    std::wstring okText = (itOk != locale.end()) ? itOk->second : L"OK";

    int iconSize = S(32);
    int clientW = S(VAL_CONT_W) + 2 * S(DLG_PAD_H);
    int clientH = S(DLG_PAD_T) + iconSize + S(8) + textH + S(DLG_GAP_TB) + S(DLG_BTN_H) + S(DLG_PAD_B);

    RECT wrc = { 0, 0, clientW, clientH };
    AdjustWindowRectEx(&wrc, WS_POPUP | WS_CAPTION | WS_SYSMENU, FALSE, WS_EX_DLGMODALFRAME);
    int width  = wrc.right  - wrc.left;
    int height = wrc.bottom - wrc.top;

    RECT rcParent; GetWindowRect(hwndParent, &rcParent);
    int x = rcParent.left + (rcParent.right  - rcParent.left - width)  / 2;
    int y = rcParent.top  + (rcParent.bottom - rcParent.top  - height) / 2;

    RECT rcWork; SystemParametersInfoW(SPI_GETWORKAREA, 0, &rcWork, 0);
    if (x < rcWork.left)            x = rcWork.left;
    if (y < rcWork.top)             y = rcWork.top;
    if (x + width  > rcWork.right)  x = rcWork.right  - width;
    if (y + height > rcWork.bottom) y = rcWork.bottom - height;

    ValidationDlgData* pData = new ValidationDlgData();
    pData->message  = msgExpanded;
    pData->okText   = okText;
    pData->textH    = textH;
    pData->iconSize = iconSize;
    pData->hInst    = GetModuleHandle(NULL);

    HWND hDlg = CreateWindowExW(
        WS_EX_DLGMODALFRAME | WS_EX_TOPMOST,
        L"ValidationDialogClass",
        title.c_str(),
        WS_POPUP | WS_CAPTION | WS_SYSMENU,
        x, y, width, height,
        hwndParent, NULL, GetModuleHandle(NULL), pData);

    if (hDlg) {
        EnableWindow(hwndParent, FALSE);
        ShowWindow(hDlg, SW_SHOW);

        MSG msg = {};
        while (IsWindow(hDlg)) {
            BOOL bRet = GetMessageW(&msg, NULL, 0, 0);
            if (bRet == 0) { PostQuitMessage((int)msg.wParam); break; }
            if (bRet == -1) break;
            if (!IsWindow(hDlg)) break;
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }

        EnableWindow(hwndParent, TRUE);
        SetForegroundWindow(hwndParent);
    }

    delete pData;
}
