#include "tooltip.h"
#include <fstream>
#include <sstream>
#include <algorithm>
#include <map>

// Internal state
static HWND g_tooltipWindow = NULL;
static HFONT g_tooltipFont = NULL;
static std::vector<TooltipEntry> g_currentEntries;
static const wchar_t TOOLTIP_CLASS_NAME[] = L"CustomTooltipClass";

// Forward declaration
static LRESULT CALLBACK TooltipWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

// Trim whitespace from a string
static void TrimW(std::wstring &s) {
    size_t a = s.find_first_not_of(L" \t\r\n");
    if (a == std::wstring::npos) { s.clear(); return; }
    size_t b = s.find_last_not_of(L" \t\r\n");
    s = s.substr(a, b - a + 1);
}

// Get exe directory
static std::wstring GetExeDir() {
    wchar_t exePath[MAX_PATH];
    if (!GetModuleFileNameW(NULL, exePath, _countof(exePath))) return L".";
    wchar_t *p = wcsrchr(exePath, L'\\');
    if (!p) return L".";
    *p = 0;
    return std::wstring(exePath);
}

// Load a locale file
static bool LoadLocaleFile(const std::wstring &code, const std::wstring& localeDir, std::map<std::wstring, std::wstring> &out) {
    std::wstring dir = GetExeDir();
    std::wstring path = dir + L"\\" + localeDir + L"\\" + code + L".txt";
    
    std::wifstream f(path.c_str());
    if (!f.is_open()) return false;
    
    out.clear();
    std::wstring line;
    while (std::getline(f, line)) {
        // remove BOM if present
        if (!line.empty() && line[0] == 0xFEFF) line.erase(0, 1);
        TrimW(line);
        if (line.empty() || line[0] == L'#') continue;
        size_t eq = line.find(L'=');
        if (eq == std::wstring::npos) continue;
        std::wstring key = line.substr(0, eq);
        std::wstring val = (eq + 1 < line.size()) ? line.substr(eq + 1) : L"";
        TrimW(key);
        TrimW(val);
        if (!key.empty()) out[key] = val;
    }
    return true;
}

// Extract country code from locale code (e.g., "en_GB" -> "[GB] ")
static std::wstring GetCountryCode(const std::wstring &code) {
    if (code.size() < 2) return L"";
    size_t pos = code.find(L'_');
    if (pos == std::wstring::npos) return L"";
    std::wstring country = code.substr(pos + 1);
    if (country.size() == 2) {
        return L"[" + country + L"] ";
    }
    return L"";
}

// Tooltip window procedure
static LRESULT CALLBACK TooltipWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);
        
        // Get client area
        RECT rc;
        GetClientRect(hwnd, &rc);
        
        // Fill background with light yellow
        HBRUSH hBrush = CreateSolidBrush(RGB(255, 255, 225));
        FillRect(hdc, &rc, hBrush);
        DeleteObject(hBrush);
        
        // Draw border
        HPEN hPen = CreatePen(PS_SOLID, 1, RGB(0, 0, 0));
        HPEN hOldPen = (HPEN)SelectObject(hdc, hPen);
        MoveToEx(hdc, rc.left, rc.top, NULL);
        LineTo(hdc, rc.right - 1, rc.top);
        LineTo(hdc, rc.right - 1, rc.bottom - 1);
        LineTo(hdc, rc.left, rc.bottom - 1);
        LineTo(hdc, rc.left, rc.top);
        SelectObject(hdc, hOldPen);
        DeleteObject(hPen);
        
        // Draw table with translations
        SetBkMode(hdc, TRANSPARENT);
        if (g_tooltipFont) SelectObject(hdc, g_tooltipFont);
        
        // Check if this is a simple single-entry tooltip (empty country code)
        bool isSimpleTooltip = (g_currentEntries.size() == 1 && g_currentEntries[0].first.empty());
        
        if (isSimpleTooltip) {
            // Simple tooltip - just draw centered text
            const int startX = 10;
            const int startY = 10;
            SetTextColor(hdc, RGB(0, 0, 0));
            TextOutW(hdc, startX, startY, g_currentEntries[0].second.c_str(), (int)g_currentEntries[0].second.length());
        } else {
            // Multilingual tooltip - 4 columns: Code | Text | Code | Text
            const int startX = 10;
            const int startY = 10;
            const int rowHeight = 22;
            const int textCol1X = 65;      // First text column (codes right-aligned before this)
            const int textCol2X = 410;     // Second text column (codes right-aligned before this)
            
            // Process 2 entries per row
            for (size_t i = 0; i < g_currentEntries.size(); i += 2) {
                int row = (int)(i / 2);
                int y = startY + row * rowHeight;
                
                // Draw first entry (left side)
                // Country code in royal blue, right-aligned
                SetTextColor(hdc, RGB(65, 105, 225));
                SIZE sz1;
                GetTextExtentPoint32W(hdc, g_currentEntries[i].first.c_str(), (int)g_currentEntries[i].first.length(), &sz1);
                TextOutW(hdc, textCol1X - sz1.cx - 5, y, g_currentEntries[i].first.c_str(), (int)g_currentEntries[i].first.length());
                // Translation text in black
                SetTextColor(hdc, RGB(0, 0, 0));
                TextOutW(hdc, textCol1X, y, g_currentEntries[i].second.c_str(), (int)g_currentEntries[i].second.length());
                
                // Draw second entry (right side) if it exists
                if (i + 1 < g_currentEntries.size()) {
                    // Country code in royal blue, right-aligned
                    SetTextColor(hdc, RGB(65, 105, 225));
                    SIZE sz2;
                    GetTextExtentPoint32W(hdc, g_currentEntries[i + 1].first.c_str(), (int)g_currentEntries[i + 1].first.length(), &sz2);
                    TextOutW(hdc, textCol2X - sz2.cx - 5, y, g_currentEntries[i + 1].first.c_str(), (int)g_currentEntries[i + 1].first.length());
                    // Translation text in black
                    SetTextColor(hdc, RGB(0, 0, 0));
                    TextOutW(hdc, textCol2X, y, g_currentEntries[i + 1].second.c_str(), (int)g_currentEntries[i + 1].second.length());
                }
            }
        }
        
        EndPaint(hwnd, &ps);
        return 0;
    }
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

bool InitTooltipSystem(HINSTANCE hInstance) {
    // Register tooltip window class
    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(WNDCLASSEXW);
    wc.lpfnWndProc = TooltipWndProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = TOOLTIP_CLASS_NAME;
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    
    if (!RegisterClassExW(&wc)) {
        // Check if already registered
        if (GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
            return false;
        }
    }
    
    // Create tooltip font
    if (!g_tooltipFont) {
        g_tooltipFont = CreateFontW(-14, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                                    DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                    CLEARTYPE_QUALITY, VARIABLE_PITCH | FF_SWISS, L"Segoe UI");
    }
    
    return true;
}

void CleanupTooltipSystem() {
    if (g_tooltipWindow) {
        DestroyWindow(g_tooltipWindow);
        g_tooltipWindow = NULL;
    }
    if (g_tooltipFont) {
        DeleteObject(g_tooltipFont);
        g_tooltipFont = NULL;
    }
    g_currentEntries.clear();
}

void ShowMultilingualTooltip(const std::vector<TooltipEntry>& entries, int x, int y, HWND parentHwnd) {
    g_currentEntries = entries;
    
    // Check if this is a simple single-entry tooltip (empty country code)
    bool isSimpleTooltip = (entries.size() == 1 && entries[0].first.empty());
    
    // Calculate dimensions
    int tooltipWidth, tooltipHeight;
    if (isSimpleTooltip) {
        // Simple tooltip - calculate width based on text length
        HDC hdc = GetDC(NULL);
        if (g_tooltipFont) SelectObject(hdc, g_tooltipFont);
        SIZE sz;
        GetTextExtentPoint32W(hdc, entries[0].second.c_str(), (int)entries[0].second.length(), &sz);
        ReleaseDC(NULL, hdc);
        tooltipWidth = sz.cx + 25;  // Add padding
        tooltipHeight = 40;
    } else {
        // Multilingual tooltip - use full width
        tooltipWidth = 700;
        int numRows = ((int)entries.size() + 1) / 2;
        tooltipHeight = numRows * 22 + 30;
    }
    
    if (!g_tooltipWindow) {
        HINSTANCE hInst = (HINSTANCE)GetWindowLongPtr(parentHwnd, GWLP_HINSTANCE);
        g_tooltipWindow = CreateWindowExW(WS_EX_TOPMOST | WS_EX_TOOLWINDOW,
            TOOLTIP_CLASS_NAME, L"",
            WS_POPUP | WS_BORDER,
            x, y, tooltipWidth, tooltipHeight,
            parentHwnd, NULL, hInst, NULL);
    }
    
    if (g_tooltipWindow) {
        SetWindowPos(g_tooltipWindow, HWND_TOPMOST, x, y, tooltipWidth, tooltipHeight, 
                     SWP_SHOWWINDOW | SWP_NOACTIVATE);
        InvalidateRect(g_tooltipWindow, NULL, TRUE);
    }
}

void HideTooltip() {
    if (g_tooltipWindow && IsWindowVisible(g_tooltipWindow)) {
        ShowWindow(g_tooltipWindow, SW_HIDE);
    }
}

bool IsTooltipVisible() {
    return g_tooltipWindow && IsWindowVisible(g_tooltipWindow);
}

std::vector<TooltipEntry> BuildMultilingualEntries(
    const std::wstring& localeKey,
    const std::wstring& localeDir,
    const std::vector<std::wstring>& availableLocales) {
    
    std::vector<TooltipEntry> entries;
    
    // Load localeKey from all available locale files with country codes
    for (const auto &code : availableLocales) {
        std::map<std::wstring, std::wstring> tempLocale;
        if (LoadLocaleFile(code, localeDir, tempLocale)) {
            auto it = tempLocale.find(localeKey);
            if (it != tempLocale.end() && !it->second.empty()) {
                std::wstring cc = GetCountryCode(code);
                entries.push_back({cc, it->second});
            }
        }
    }
    
    // Sort by country code ascending
    std::sort(entries.begin(), entries.end(),
        [](const TooltipEntry &a, const TooltipEntry &b) {
            return a.first < b.first;
        });
    
    return entries;
}
