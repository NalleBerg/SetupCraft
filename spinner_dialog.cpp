#include "spinner_dialog.h"
#include <map>

// Static map to associate HWND with SpinnerDialog instance
static std::map<HWND, SpinnerDialog*> g_spinnerInstances;

SpinnerDialog::SpinnerDialog(HWND hParent)
    : m_hParent(hParent)
    , m_hDialog(NULL)
    , m_hSpinnerCtrl(NULL)
    , m_hTextCtrl(NULL)
    , m_spinnerFrame(0)
    , m_visible(false)
    , m_title(L"Please Wait")
    , m_hTextFont(NULL)
    , m_hSpinnerFont(NULL)
{
}

SpinnerDialog::~SpinnerDialog() {
    Hide();
}

void SpinnerDialog::CreateDialogWindow() {
    if (m_hDialog && IsWindow(m_hDialog)) {
        return; // Already created
    }
    
    HINSTANCE hInstance = GetModuleHandle(NULL);
    
    // Register window class
    static bool classRegistered = false;
    if (!classRegistered) {
        WNDCLASSW wc = {};
        wc.lpfnWndProc = DialogProc;
        wc.hInstance = hInstance;
        wc.lpszClassName = L"SpinnerDialogClass";
        wc.hbrBackground = (HBRUSH)GetStockObject(WHITE_BRUSH);
        wc.hCursor = LoadCursor(NULL, IDC_ARROW);
        RegisterClassW(&wc);
        classRegistered = true;
    }
    
    // Create dialog window
    m_hDialog = CreateWindowExW(
        WS_EX_DLGMODALFRAME,
        L"SpinnerDialogClass",
        m_title.c_str(),
        WS_POPUP | WS_CAPTION,
        0, 0, 400, 460,
        m_hParent, NULL, hInstance, NULL
    );
    
    if (!m_hDialog) return;
    
    // Store instance pointer for window procedure
    g_spinnerInstances[m_hDialog] = this;
    
    // Center dialog
    RECT rc;
    GetWindowRect(m_hDialog, &rc);
    int dialogWidth = rc.right - rc.left;
    int dialogHeight = rc.bottom - rc.top;
    
    int x, y;
    if (m_hParent && IsWindow(m_hParent)) {
        // Center on parent
        RECT parentRc;
        GetWindowRect(m_hParent, &parentRc);
        x = parentRc.left + (parentRc.right - parentRc.left - dialogWidth) / 2;
        y = parentRc.top + (parentRc.bottom - parentRc.top - dialogHeight) / 2;
    } else {
        // Center on screen
        x = (GetSystemMetrics(SM_CXSCREEN) - dialogWidth) / 2;
        y = (GetSystemMetrics(SM_CYSCREEN) - dialogHeight) / 2;
    }
    SetWindowPos(m_hDialog, HWND_TOP, x, y, 0, 0, SWP_NOSIZE);
    
    // Add icon
    HICON hIcon = LoadIcon(NULL, IDI_INFORMATION);
    if (hIcon) {
        HWND hIconCtrl = CreateWindowExW(0, L"STATIC", NULL,
            WS_CHILD | WS_VISIBLE | SS_ICON | SS_CENTERIMAGE,
            150, 20, 100, 80, m_hDialog, NULL, hInstance, NULL);
        SendMessageW(hIconCtrl, STM_SETICON, (WPARAM)hIcon, 0);
    }
    
    // Build system message font for text label
    NONCLIENTMETRICSW ncm = {};
    ncm.cbSize = sizeof(ncm);
    SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(ncm), &ncm, 0);
    if (ncm.lfMessageFont.lfHeight < 0)
        ncm.lfMessageFont.lfHeight = (LONG)(ncm.lfMessageFont.lfHeight * 1.2f);
    ncm.lfMessageFont.lfQuality = CLEARTYPE_QUALITY;
    m_hTextFont = CreateFontIndirectW(&ncm.lfMessageFont);

    // Add text label - tall enough for 5 lines
    m_hTextCtrl = CreateWindowExW(0, L"STATIC", L"Please wait...",
        WS_CHILD | WS_VISIBLE | SS_CENTER,
        20, 112, 360, 200, m_hDialog, NULL, hInstance, NULL);
    if (m_hTextFont)
        SendMessageW(m_hTextCtrl, WM_SETFONT, (WPARAM)m_hTextFont, TRUE);
    
    // spinner midway between text-bottom (312) and client-bottom (~430): y = (312+430)/2 - 34 = 337
    m_hSpinnerCtrl = CreateWindowExW(0, L"STATIC", L"\u25D0",
        WS_CHILD | WS_VISIBLE | SS_CENTER,
        150, 337, 100, 68, m_hDialog, NULL, hInstance, NULL);
    m_hSpinnerFont = CreateFontW(-52, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    if (m_hSpinnerFont)
        SendMessageW(m_hSpinnerCtrl, WM_SETFONT, (WPARAM)m_hSpinnerFont, TRUE);
    
    // Start timer
    SetTimer(m_hDialog, 1, 60, NULL);
}

void SpinnerDialog::Show(const std::wstring& text, const std::wstring& title) {
    m_title = title;
    CreateDialogWindow();
    if (!m_hDialog) return;
    
    SetText(text);
    ShowWindow(m_hDialog, SW_SHOW);
    UpdateWindow(m_hDialog);
    m_visible = true;
    
    // Process a few messages to let dialog initialize
    MSG msg;
    for (int i = 0; i < 5; i++) {
        while (PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        Sleep(16);
    }
}

void SpinnerDialog::Hide() {
    if (m_hDialog && IsWindow(m_hDialog)) {
        KillTimer(m_hDialog, 1);
        g_spinnerInstances.erase(m_hDialog);
        DestroyWindow(m_hDialog);
        m_hDialog = NULL;
        m_hSpinnerCtrl = NULL;
        m_hTextCtrl = NULL;
    }
    if (m_hTextFont)    { DeleteObject(m_hTextFont);    m_hTextFont    = NULL; }
    if (m_hSpinnerFont) { DeleteObject(m_hSpinnerFont); m_hSpinnerFont = NULL; }
    m_visible = false;
}

void SpinnerDialog::SetText(const std::wstring& text) {
    if (m_hTextCtrl && IsWindow(m_hTextCtrl)) {
        // Convert escape sequences to actual characters
        std::wstring processedText = text;
        size_t pos = 0;
        while ((pos = processedText.find(L"\\r\\n", pos)) != std::wstring::npos) {
            processedText.replace(pos, 4, L"\r\n");
            pos += 2;
        }
        pos = 0;
        while ((pos = processedText.find(L"\\n", pos)) != std::wstring::npos) {
            processedText.replace(pos, 2, L"\n");
            pos += 1;
        }
        SetWindowTextW(m_hTextCtrl, processedText.c_str());
        UpdateWindow(m_hTextCtrl);
    }
}

bool SpinnerDialog::IsVisible() const {
    return m_visible && m_hDialog && IsWindow(m_hDialog);
}

LRESULT CALLBACK SpinnerDialog::DialogProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    static HBRUSH hWhiteBrush = (HBRUSH)GetStockObject(WHITE_BRUSH);
    
    // Get instance from map
    auto it = g_spinnerInstances.find(hwnd);
    SpinnerDialog* pThis = (it != g_spinnerInstances.end()) ? it->second : nullptr;
    
    switch (uMsg) {
        case WM_TIMER:
            if (wParam == 1 && pThis) {
                const wchar_t* frames[] = { L"◐", L"◓", L"◑", L"◒" };
                pThis->m_spinnerFrame = (pThis->m_spinnerFrame + 1) % 4;
                if (pThis->m_hSpinnerCtrl && IsWindow(pThis->m_hSpinnerCtrl)) {
                    SetWindowTextW(pThis->m_hSpinnerCtrl, frames[pThis->m_spinnerFrame]);
                }
            }
            return 0;
            
        case WM_CTLCOLORSTATIC: {
            HDC hdcStatic = (HDC)wParam;
            HWND hStatic = (HWND)lParam;
            
            SetBkMode(hdcStatic, OPAQUE);
            SetBkColor(hdcStatic, RGB(255, 255, 255));
            
            // Make spinner blue
            if (pThis && hStatic == pThis->m_hSpinnerCtrl) {
                SetTextColor(hdcStatic, RGB(0, 120, 215));
            } else {
                SetTextColor(hdcStatic, RGB(0, 0, 0));
            }
            
            return (LRESULT)hWhiteBrush;
        }
        
        case WM_CLOSE:
            // Don't allow user to close
            return 0;
    }
    
    return DefWindowProcW(hwnd, uMsg, wParam, lParam);
}
