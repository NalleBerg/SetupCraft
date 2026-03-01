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
static std::wstring GetExeDir();
// Helper to find available locale codes under <exeDir>\locale\*.txt
static std::vector<std::wstring> FindAvailableLocales() {
    std::vector<std::wstring> codes;
    std::wstring exeDir = GetExeDir();
    std::wstring search = exeDir + L"\\locale\\*.txt";
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW(search.c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return codes;
    do {
        if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
            std::wstring name = fd.cFileName;
            size_t pos = name.rfind(L".txt");
            if (pos != std::wstring::npos) codes.push_back(name.substr(0, pos));
        }
    } while (FindNextFileW(h, &fd));
    FindClose(h);
    std::sort(codes.begin(), codes.end());
    codes.erase(std::unique(codes.begin(), codes.end()), codes.end());
    return codes;
}

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

// Load a locale file (UTF-8 aware — uses narrow ifstream + MultiByteToWideChar)
static bool LoadLocaleFile(const std::wstring &code, const std::wstring& localeDir, std::map<std::wstring, std::wstring> &out) {
    std::wstring dir = GetExeDir();
    std::wstring wpath = dir + L"\\" + localeDir + L"\\" + code + L".txt";

    // Convert wide path to UTF-8 narrow string for ifstream
    int pathLen = WideCharToMultiByte(CP_UTF8, 0, wpath.c_str(), -1, NULL, 0, NULL, NULL);
    if (pathLen <= 0) return false;
    std::string narrowPath(pathLen - 1, 0);
    WideCharToMultiByte(CP_UTF8, 0, wpath.c_str(), -1, &narrowPath[0], pathLen, NULL, NULL);

    std::ifstream f(narrowPath);
    if (!f.is_open()) return false;

    out.clear();
    std::string line;
    while (std::getline(f, line)) {
        if (line.empty()) continue;
        // Strip UTF-8 BOM (EF BB BF)
        if (line.size() >= 3 &&
            (unsigned char)line[0] == 0xEF &&
            (unsigned char)line[1] == 0xBB &&
            (unsigned char)line[2] == 0xBF)
            line.erase(0, 3);
        if (line.empty() || line[0] == '#') continue;
        size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        std::string key = line.substr(0, eq);
        std::string val = (eq + 1 < line.size()) ? line.substr(eq + 1) : "";
        // Trim narrow strings
        auto trimN = [](std::string &s) {
            size_t a = s.find_first_not_of(" \t\r\n");
            if (a == std::string::npos) { s.clear(); return; }
            size_t b = s.find_last_not_of(" \t\r\n");
            s = s.substr(a, b - a + 1);
        };
        trimN(key); trimN(val);
        if (key.empty()) continue;
        // Convert UTF-8 key and value to wide strings
        auto toW = [](const std::string &s) -> std::wstring {
            if (s.empty()) return {};
            int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), NULL, 0);
            if (n <= 0) return {};
            std::wstring w(n, 0);
            MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), &w[0], n);
            return w;
        };
        out[toW(key)] = toW(val);
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
        
        // Fill background using system info background (matches other info tooltips)
        HBRUSH hBrush = CreateSolidBrush(GetSysColor(COLOR_INFOBK));
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
            // Simple tooltip - draw wrapped text (up to 3 lines)
            RECT textRc = { 10, 10, rc.right - 10, rc.bottom - 10 };
            SetTextColor(hdc, RGB(0, 0, 0));
            DrawTextW(hdc, g_currentEntries[0].second.c_str(), -1, &textRc, DT_LEFT | DT_TOP | DT_WORDBREAK);
        } else {
            // Multilingual tooltip - two columns, up to 10 rows per column
            const int startX = 10;
            const int startY = 10;
            const int rowHeight = 22;
            const int leftTextColX = 80;   // left column text start
            const int rightTextColX = 430; // right column text start (760px total / 2 = 380, +50 for code)

            // Cap entries shown to 20 (10 per column)
            int totalEntries = (int)g_currentEntries.size();
            if (totalEntries > 20) totalEntries = 20;

            int rows = totalEntries <= 10 ? totalEntries : 10;

            // Draw rows: left column indices [0..rows-1], right column indices [rows..rows+rows-1]
            for (int r = 0; r < rows; ++r) {
                int y = startY + r * rowHeight;

                int leftIdx = r;
                if (leftIdx < totalEntries) {
                    // country code, blue, right-aligned just before text
                    SetTextColor(hdc, RGB(65, 105, 225));
                    SIZE szc;
                    GetTextExtentPoint32W(hdc, g_currentEntries[leftIdx].first.c_str(), (int)g_currentEntries[leftIdx].first.length(), &szc);
                    TextOutW(hdc, leftTextColX - szc.cx - 5, y, g_currentEntries[leftIdx].first.c_str(), (int)g_currentEntries[leftIdx].first.length());
                    // text
                    SetTextColor(hdc, RGB(0, 0, 0));
                    TextOutW(hdc, leftTextColX, y, g_currentEntries[leftIdx].second.c_str(), (int)g_currentEntries[leftIdx].second.length());
                }

                int rightIdx = r + rows;
                if (rightIdx < totalEntries) {
                    SetTextColor(hdc, RGB(65, 105, 225));
                    SIZE szc2;
                    GetTextExtentPoint32W(hdc, g_currentEntries[rightIdx].first.c_str(), (int)g_currentEntries[rightIdx].first.length(), &szc2);
                    TextOutW(hdc, rightTextColX - szc2.cx - 5, y, g_currentEntries[rightIdx].first.c_str(), (int)g_currentEntries[rightIdx].first.length());
                    SetTextColor(hdc, RGB(0, 0, 0));
                    TextOutW(hdc, rightTextColX, y, g_currentEntries[rightIdx].second.c_str(), (int)g_currentEntries[rightIdx].second.length());
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
    wc.hbrBackground = (HBRUSH)(COLOR_INFOBK + 1);
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

    // If caller passed an empty multilingual list, attempt to build a multilingual
    // list from available locale files so the globe/entry-screen tooltip still
    // shows translations.
    if (g_currentEntries.empty()) {
        auto codes = FindAvailableLocales();
        if (!codes.empty()) {
            auto fallback = BuildMultilingualEntries(L"select_language", L"locale", codes);
            if (!fallback.empty()) g_currentEntries = fallback;
        }
    }

    // Check if this is a simple single-entry tooltip (empty country code)
    bool isSimpleTooltip = (g_currentEntries.size() == 1 && g_currentEntries[0].first.empty());
    
    // Calculate dimensions
    int tooltipWidth, tooltipHeight;
    if (isSimpleTooltip) {
        // Simple tooltip - calculate width/height for multiline content with word-wrap
        std::wstring s = g_currentEntries[0].second;
        HDC hdc = GetDC(NULL);
        if (g_tooltipFont) SelectObject(hdc, g_tooltipFont);

        // Choose max width per API (700px) so text wraps into multiple lines
        int desiredMaxWidth = 700;

        // Measure wrapped rect using DrawText with DT_CALCRECT | DT_WORDBREAK
        RECT calcRc = { 0, 0, desiredMaxWidth - 20, 0 };
        DrawTextW(hdc, s.c_str(), -1, &calcRc, DT_CALCRECT | DT_WORDBREAK);

        // Determine line height from font metrics
        TEXTMETRIC tm = {0};
        GetTextMetricsW(hdc, &tm);
        int lineHeight = tm.tmHeight + tm.tmExternalLeading;
        if (lineHeight <= 0) lineHeight = 20;

        // Limit height to at most 3 lines
        int maxHeight = lineHeight * 3;
        int contentHeight = calcRc.bottom - calcRc.top;
        if (contentHeight > maxHeight) contentHeight = maxHeight;

        tooltipWidth = desiredMaxWidth;
        tooltipHeight = contentHeight + 20; // add padding
    } else {
        // Multilingual tooltip - two columns, up to 10 rows per column
        int totalEntries = (int)g_currentEntries.size();
        if (totalEntries > 20) totalEntries = 20; // cap display
        int rows = totalEntries <= 10 ? totalEntries : 10;
        // 760px: each column gets ~330px for text (code ~50px + gap + text ~330px)
        tooltipWidth = 760;
        tooltipHeight = rows * 22 + 30;
    }
    
    // Clamp to monitor bounds (not parent bounds — the parent entry window is
    // only ~180px tall and cannot contain the multilingual table at all).
    int finalX = x;
    int finalY = y;
    {
        POINT pt = { x, y };
        HMONITOR hMon = MonitorFromPoint(pt, MONITOR_DEFAULTTONEAREST);
        MONITORINFO mi = {};
        mi.cbSize = sizeof(mi);
        if (GetMonitorInfo(hMon, &mi)) {
            int monLeft   = mi.rcWork.left;
            int monTop    = mi.rcWork.top;
            int monRight  = mi.rcWork.right;
            int monBottom = mi.rcWork.bottom;

            // Clamp width
            int maxW = monRight - monLeft - 20;
            if (tooltipWidth > maxW) tooltipWidth = maxW;

            // Keep within horizontal bounds
            if (finalX + tooltipWidth > monRight - 10)
                finalX = monRight - tooltipWidth - 10;
            if (finalX < monLeft + 10)
                finalX = monLeft + 10;

            // Prefer below requested y; if it overflows, try above
            if (finalY + tooltipHeight > monBottom - 10) {
                int aboveY = y - tooltipHeight - 10;
                if (aboveY >= monTop + 10)
                    finalY = aboveY;
                else
                    finalY = monBottom - tooltipHeight - 10;
            }
            if (finalY < monTop + 10)
                finalY = monTop + 10;
        }
    }

    if (!g_tooltipWindow) {
        HINSTANCE hInst = (HINSTANCE)GetWindowLongPtr(parentHwnd, GWLP_HINSTANCE);
        // For simple single-entry tooltips we keep them mouse-transparent so they
        // don't steal events; for multilingual table tooltips we must allow the
        // tooltip to receive mouse events so we can track mouse enter/leave on
        // the tooltip window itself (prevents flashing when moving from icon
        // into the tooltip).
        LONG_PTR exStyles = WS_EX_TOPMOST | WS_EX_TOOLWINDOW;
        if (isSimpleTooltip) exStyles |= WS_EX_TRANSPARENT;
        g_tooltipWindow = CreateWindowExW((DWORD)exStyles,
            TOOLTIP_CLASS_NAME, L"",
            WS_POPUP | WS_BORDER,
            finalX, finalY, tooltipWidth, tooltipHeight,
            parentHwnd, NULL, hInst, NULL);
    }
    
    if (g_tooltipWindow) {
        SetWindowPos(g_tooltipWindow, HWND_TOPMOST, finalX, finalY, tooltipWidth, tooltipHeight,
                     SWP_SHOWWINDOW | SWP_NOACTIVATE);
        InvalidateRect(g_tooltipWindow, NULL, TRUE);
    }

    // Lightweight debug log to help diagnose tooltip visibility issues
    try {
        std::wstring logPath = GetExeDir() + L"\\tooltip_debug.log";
        std::wofstream lf;
        lf.open(logPath.c_str(), std::ios::app);
        if (lf.is_open()) {
            lf << L"ShowMultilingualTooltip called: entries=" << g_currentEntries.size() << L" x=" << finalX << L" y=" << finalY << L"\n";
            // list first few entries codes
            for (size_t i = 0; i < g_currentEntries.size() && i < 6; ++i) {
                lf << L"  [" << g_currentEntries[i].first << L"] " << g_currentEntries[i].second << L"\n";
            }
            lf.flush();
            lf.close();
        }
    } catch (...) { }
}

HWND GetTooltipWindow() {
    return g_tooltipWindow;
}

void HideTooltip() {
    try {
        if (g_tooltipWindow && IsWindowVisible(g_tooltipWindow)) {
            ShowWindow(g_tooltipWindow, SW_HIDE);
            std::wstring logPath = GetExeDir() + L"\\tooltip_debug.log";
            std::wofstream lf(logPath.c_str(), std::ios::app);
            if (lf.is_open()) {
                lf << L"HideTooltip called - Hid window\n";
                lf.close();
            }
        } else {
            std::wstring logPath = GetExeDir() + L"\\tooltip_debug.log";
            std::wofstream lf(logPath.c_str(), std::ios::app);
            if (lf.is_open()) {
                lf << L"HideTooltip called - no visible window\n";
                lf.close();
            }
        }
    } catch(...) {}
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
