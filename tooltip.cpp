#include "tooltip.h"
#include "dpi.h"
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
            const std::wstring& s = g_currentEntries[0].second;
            bool isMultiline = (s.find(L'\n') != std::wstring::npos);
            SetTextColor(hdc, RGB(0, 0, 0));
            if (isMultiline) {
                RECT textRc = { 10, 10, rc.right - 10, rc.bottom - 10 };
                DrawTextW(hdc, s.c_str(), -1, &textRc, DT_LEFT | DT_TOP | DT_WORDBREAK | DT_NOPREFIX);
            } else {
                RECT textRc = { S(10), 0, rc.right - S(10), rc.bottom };
                DrawTextW(hdc, s.c_str(), -1, &textRc, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
            }
        } else {
            // Multilingual tooltip - two columns, up to 10 rows per column
            const int startX = S(10);
            const int startY = S(10);
            const int rowHeight = S(22);
            const int leftTextColX = S(80);   // left column text start
            const int rightTextColX = S(430); // right column text start

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
    
    // ── DO NOT CHANGE THE FONT SETUP BELOW ────────────────────────────────────
    // Font rules for the multilingual tooltip:
    //   • Face name MUST be "Segoe UI" (not "Segoe UI Variable").
    //     Windows 11 returns "Segoe UI Variable" from NONCLIENTMETRICS; that is
    //     a GDI variable font that cannot render Greek or Cyrillic — they show
    //     as |||||||.  "Segoe UI" (the classic version) has full Unicode coverage
    //     for Latin, Greek, Cyrillic, Arabic, Hebrew, etc.
    //   • Font HEIGHT is derived from NONCLIENTMETRICS so it scales with DPI.
    //   • FW_BOLD keeps the country codes and text legible at small sizes.
    //   • DEFAULT_CHARSET lets GDI substitute a glyph from another installed
    //     font as a last resort if a codepoint is absent from Segoe UI.
    // ──────────────────────────────────────────────────────────────────────────
    if (!g_tooltipFont) {
        NONCLIENTMETRICSW ncm = {};
        ncm.cbSize = sizeof(ncm);
        SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(ncm), &ncm, 0);
        LOGFONTW lf       = ncm.lfMessageFont;   // start from system metrics (gets DPI-correct height)
        if (lf.lfHeight < 0)
            lf.lfHeight   = (LONG)(lf.lfHeight * 1.2f); // 120 % — match project-wide body font scale
        lf.lfWeight       = FW_BOLD;
        lf.lfQuality      = CLEARTYPE_QUALITY;
        lf.lfCharSet      = DEFAULT_CHARSET;      // allow per-glyph GDI substitution
        wcscpy_s(lf.lfFaceName, L"Segoe UI");     // override face: full Unicode coverage, not variable
        g_tooltipFont = CreateFontIndirectW(&lf);
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
        std::wstring s = g_currentEntries[0].second;
        // Detect whether the text is multiline (contains \n)
        bool isMultiline = (s.find(L'\n') != std::wstring::npos);

        HDC hdc = GetDC(NULL);
        HGDIOBJ hOldFont = NULL;
        if (g_tooltipFont) hOldFont = SelectObject(hdc, g_tooltipFont);

        TEXTMETRIC tm = {0};
        GetTextMetricsW(hdc, &tm);
        int lineHeight = tm.tmHeight + tm.tmExternalLeading;
        if (lineHeight <= 0) lineHeight = 20;

        if (isMultiline) {
            // Measure each line individually to find the natural width, then cap.
            int maxAllowed = S(480);
            int maxLineWidth = 0;
            std::wstring rem = s;
            size_t lineStart = 0;
            while (true) {
                size_t nl = rem.find(L'\n', lineStart);
                std::wstring line = (nl == std::wstring::npos)
                    ? rem.substr(lineStart)
                    : rem.substr(lineStart, nl - lineStart);
                if (!line.empty()) {
                    SIZE lsz = {};
                    GetTextExtentPoint32W(hdc, line.c_str(), (int)line.size(), &lsz);
                    if (lsz.cx > maxLineWidth) maxLineWidth = lsz.cx;
                }
                if (nl == std::wstring::npos) break;
                lineStart = nl + 1;
            }
            int naturalWidth = maxLineWidth + S(20);
            tooltipWidth = (naturalWidth < maxAllowed) ? naturalWidth : maxAllowed;
            if (tooltipWidth < S(80)) tooltipWidth = S(80);
            // Re-measure height using the actual chosen width
            RECT calcRc = { 0, 0, tooltipWidth - S(20), 0 };
            DrawTextW(hdc, s.c_str(), -1, &calcRc, DT_CALCRECT | DT_WORDBREAK | DT_NOPREFIX);
            tooltipHeight = (calcRc.bottom - calcRc.top) + S(20);
        } else {
            // Single line: measure exact width, add padding. No hard cap here —
            // the monitor-clamping block below constrains to screen width.
            SIZE sz = {0};
            GetTextExtentPoint32W(hdc, s.c_str(), (int)s.size(), &sz);
            tooltipWidth  = sz.cx + S(32);  // 16px each side
            if (tooltipWidth < S(80)) tooltipWidth = S(80);
            tooltipHeight = lineHeight + S(20);
        }

        if (hOldFont) SelectObject(hdc, hOldFont);
        ReleaseDC(NULL, hdc);
    } else {
        // Multilingual tooltip - two columns, up to 10 rows per column
        int totalEntries = (int)g_currentEntries.size();
        if (totalEntries > 20) totalEntries = 20; // cap display
        int rows = totalEntries <= 10 ? totalEntries : 10;
        // 760px logical: each column gets ~330px for text (code ~50px + gap + text ~330px)
        tooltipWidth = S(760);
        tooltipHeight = rows * S(22) + S(30);
    }
    
    // For simple tooltips, also clamp X to parent window right edge so the
    // tooltip never overflows the app (e.g. registry warning near the right edge).
    int finalX = x;
    int finalY = y;
    if (isSimpleTooltip && parentHwnd) {
        RECT rcParent;
        GetClientRect(parentHwnd, &rcParent);
        POINT pBR = { rcParent.right, rcParent.bottom };
        ClientToScreen(parentHwnd, &pBR);
        if (finalX + tooltipWidth > pBR.x - 8)
            finalX = pBR.x - tooltipWidth - 8;
        POINT pTL = { rcParent.left, rcParent.top };
        ClientToScreen(parentHwnd, &pTL);
        if (finalX < pTL.x + 8)
            finalX = pTL.x + 8;
    }

    // Clamp to monitor bounds (not parent bounds — the parent entry window is
    // only ~180px tall and cannot contain the multilingual table at all).
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
        LONG_PTR exStyles = WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE;
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
}

HWND GetTooltipWindow() {
    return g_tooltipWindow;
}

void HideTooltip() {
    if (g_tooltipWindow && IsWindowVisible(g_tooltipWindow))
        ShowWindow(g_tooltipWindow, SW_HIDE);
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
