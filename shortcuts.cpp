/*
 * shortcuts.cpp — Shortcuts page implementation for SetupCraft.
 *
 * All Shortcuts-page state lives here as file-scope statics.
 * mainwindow.cpp routes WM_NOTIFY, WM_COMMAND, and WM_CONTEXTMENU here via
 * the public functions declared in shortcuts.h.
 *
 * Layout rules:  all pixel values through S(),  all strings through locale.
 * Persistence:   in-memory only; written to DB on IDM_FILE_SAVE.
 */

#include "shortcuts.h"
#include <windowsx.h>        // GET_X_LPARAM / GET_Y_LPARAM
#include "db.h"              // DB::InsertScShortcut etc.
#include "sc_shortcut_dialog.h"  // SC_EditShortcutDialog()
#include "mainwindow.h"    // MainWindow::MarkAsModified(), GetLocale()
#include "dpi.h"           // S() DPI-scale macro
#include "button.h"        // CreateCustomButtonWithIcon, SetButtonTooltip
#include "checkbox.h"      // CreateCustomCheckbox
#include "tooltip.h"       // ShowMultilingualTooltip, HideTooltip, IsTooltipVisible
#include <functional>      // std::function (recursive lambda)
#include <algorithm>       // std::remove_if

// PrivateExtractIconsW — undocumented but reliable for requesting arbitrary icon
// pixel sizes from system DLLs.  Same declaration as in button.cpp.
extern "C" __declspec(dllimport) UINT WINAPI PrivateExtractIconsW(
    LPCWSTR szFileName, int nIconIndex, int cxIcon, int cyIcon,
    HICON* phicon, UINT* piconid, UINT nIcons, UINT flags);

// ── Module-private state ──────────────────────────────────────────────────────

// "Allow user to opt out of the desktop shortcut at install time" toggle.
static bool s_scDesktopOptOut = false;
static bool s_scSmPinOptOut   = false;   // SM pin opt-out developer toggle
static bool s_scTbPinOptOut   = false;   // Taskbar pin opt-out developer toggle

// Live handle to the Start Menu / Programs folder TreeView.
// Set by SC_BuildPage; cleared (window destroyed) by SC_TearDown.
static HWND  s_hScStartMenuTree = NULL;

// Desktop icon displayed on the page at 64×64.  Loaded in SC_BuildPage,
// destroyed in SC_TearDown.
static HICON s_hDesktopIcon = NULL;

// Start Menu pin icon (imageres.dll #228) and Taskbar pin icon (#175), 64×64.
// Loaded in SC_BuildPage, destroyed in SC_TearDown.
static HICON s_hSmPinIcon = NULL;
static HICON s_hTbPinIcon = NULL;

// 9pt bold font for the Desktop opt-out checkbox label.
// Created in SC_BuildPage, destroyed in SC_TearDown.
static HFONT s_hScCbFont = NULL;

// In-memory folder tree.  Persists across page switches; cleared by SC_Reset().
static std::vector<ScMenuNode> s_scMenuNodes;
static int                     s_scNextMenuId = 2;  // 0 = Start Menu root, 1 = Programs

// In-memory shortcut definitions.  Populated by the shortcut-config dialog
// (implemented in a later session).  Cleared by SC_Reset().
static std::vector<ShortcutDef> s_scShortcuts;
static int                      s_scNextShortcutId = 1;

// Hover-tracking state for the Start Menu TreeView custom tooltip.
static int  s_scSmTreeHoveredId = -1;
static bool s_scSmTreeTracking  = false;

// Layout coordinates for the Desktop shortcut strip — stored so
// SC_RefreshDesktopStrip() can be called after the main window is laid out.
static int  s_scDskStripY  = 0;  // top-y of the mini-icon strip row
static int  s_scDskCol0X   = 0;  // left edge of Desktop column
static int  s_scDskColW    = 0;  // width of Desktop column

// Layout coordinates for the Pin to Start and Pin to Taskbar strips.
// Stored so SC_RefreshPinStrips() can recreate them without knowing the full layout.
static int  s_scPinStripY  = 0;  // top-y of both pin checkbox strips (same row)
static int  s_scSmPinColX  = 0;  // left edge of "Pin to Start" column
static int  s_scSmPinColW  = 0;  // width of "Pin to Start" column
static int  s_scTbPinColX  = 0;  // left edge of "Pin to Taskbar" column
static int  s_scTbPinColW  = 0;  // width of "Pin to Taskbar" column
static HFONT s_hScPinCbFont = NULL;  // font reused by pin strip checkboxes (created per BuildPage)

// Pixels the page content has been scrolled upward from its natural position.
// Reset to 0 on teardown and project reset.  Used by SC_RefreshDesktopStrip
// and SC_RefreshPinStrips to place newly created controls at the correct Y.
static int s_scScrollOffset = 0;

// HINSTANCE stored at SC_BuildPage time so SC_OnResize can call SC_Refresh*.
static HINSTANCE s_scHInst = NULL;

// ── SC_Reset ──────────────────────────────────────────────────────────────────

void SC_Reset()
{
    s_scDesktopOptOut  = false;
    s_scSmPinOptOut    = false;
    s_scTbPinOptOut    = false;
    s_scMenuNodes.clear();
    s_scNextMenuId     = 2;
    s_scShortcuts.clear();
    s_scNextShortcutId = 1;
    s_scScrollOffset   = 0;
}

bool SC_HasOptOut()
{
    return s_scDesktopOptOut || s_scSmPinOptOut || s_scTbPinOptOut;
}

// Forward declaration — SC_RefreshDesktopStrip is defined after SC_DskMiniSubclassProc.
void SC_RefreshDesktopStrip(HWND hwnd, HINSTANCE hInst);

// Forward declaration — SC_RefreshPinStrips is defined after SC_RefreshDesktopStrip.
// Destroys and recreates both pin-checkbox strips from the current s_scShortcuts list,
// and updates the pin-status labels.
void SC_RefreshPinStrips(HWND hwnd, HINSTANCE hInst);

// ── SC_DskMiniSubclassProc ────────────────────────────────────────────────────────
// Subclass for each 16×16 Desktop shortcut mini-icon.
// GWLP_USERDATA holds the ShortcutDef::id of the represented shortcut.
// Tooltip = shortcut name.  Double-click or right-click → edit dialog.
static bool  s_dskMiniTracking = false;   // mouse-leave tracking active
static HWND  s_dskMiniHovered  = NULL;    // which mini-icon is hovered

static LRESULT CALLBACK SC_DskMiniSubclassProc(
    HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam,
    UINT_PTR /*uid*/, DWORD_PTR /*ref*/)
{
    switch (msg) {
    case WM_ERASEBKGND: {
        RECT rc; GetClientRect(hwnd, &rc);
        FillRect((HDC)wParam, &rc, GetSysColorBrush(COLOR_WINDOW));
        return 1;
    }
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);
        HICON hIco = (HICON)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
        if (hIco) {
            RECT rc; GetClientRect(hwnd, &rc);
            DrawIconEx(hdc, 0, 0, hIco, rc.right, rc.bottom, 0, NULL, DI_NORMAL);
        }
        EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_MOUSEMOVE: {
        if (s_dskMiniHovered != hwnd) {
            HideTooltip();
            s_dskMiniHovered = hwnd;
            // ScId property holds the ShortcutDef::id (NOT the strip slot index).
            int scId = (int)(INT_PTR)GetPropW(hwnd, L"ScId");
            for (const auto& sc : s_scShortcuts) {
                if (sc.id == scId && sc.type == SCT_DESKTOP && !sc.name.empty()) {
                    POINT pt; GetCursorPos(&pt);
                    ShowMultilingualTooltip({{L"", sc.name}},
                        pt.x + 16, pt.y + 16, GetParent(hwnd));
                    break;
                }
            }
        }
        if (!s_dskMiniTracking) {
            TRACKMOUSEEVENT tme = { sizeof(tme), TME_LEAVE, hwnd, 0 };
            TrackMouseEvent(&tme);
            s_dskMiniTracking = true;
        }
        break;
    }
    case WM_MOUSELEAVE:
        HideTooltip();
        s_dskMiniHovered  = NULL;
        s_dskMiniTracking = false;
        break;
    case WM_LBUTTONDBLCLK:
    case WM_RBUTTONUP: {
        int ctrlId = GetDlgCtrlID(hwnd);
        if (msg == WM_RBUTTONUP) {
            // Right-click: show context menu (Edit / Remove).
            const auto& locMap = MainWindow::GetLocale();
            auto locS = [&](const wchar_t* k, const wchar_t* fb) -> std::wstring {
                auto it = locMap.find(k); return (it != locMap.end()) ? it->second : fb;
            };
            HMENU hMenu = CreatePopupMenu();
            AppendMenuW(hMenu, MF_STRING, IDM_SC_CTX_EDIT_DSK,
                locS(L"sc_ctx_edit", L"Edit shortcut…").c_str());
            AppendMenuW(hMenu, MF_STRING, IDM_SC_CTX_REMOVE_DSK,
                locS(L"sc_ctx_remove_sc", L"Remove shortcut").c_str());
            POINT pt; GetCursorPos(&pt);
            int cmd = (int)TrackPopupMenu(hMenu, TPM_RIGHTBUTTON | TPM_RETURNCMD,
                pt.x, pt.y, 0, GetParent(hwnd), NULL);
            DestroyMenu(hMenu);
            if (cmd == IDM_SC_CTX_EDIT_DSK)
                SendMessageW(GetParent(hwnd), WM_COMMAND,
                    MAKEWPARAM(ctrlId, 0), 0);
            else if (cmd == IDM_SC_CTX_REMOVE_DSK) {
                int scId = (int)(INT_PTR)GetPropW(hwnd, L"ScId");
                s_scShortcuts.erase(
                    std::remove_if(s_scShortcuts.begin(), s_scShortcuts.end(),
                        [scId](const ShortcutDef& s){ return s.id == scId; }),
                    s_scShortcuts.end());
                MainWindow::MarkAsModified();
                // Rebuild the strip immediately.
                HINSTANCE hInst = (HINSTANCE)GetWindowLongPtrW(
                    GetParent(hwnd), GWLP_HINSTANCE);
                SC_RefreshDesktopStrip(GetParent(hwnd), hInst);
                SC_RefreshPinStrips(GetParent(hwnd), hInst);
            }
        } else {
            // Double-click: open edit dialog directly.
            SendMessageW(GetParent(hwnd), WM_COMMAND,
                MAKEWPARAM(ctrlId, 0), 0);
        }
        return 0;
    }
    case WM_NCDESTROY:
        RemoveWindowSubclass(hwnd, SC_DskMiniSubclassProc, 0);
        return DefSubclassProc(hwnd, msg, wParam, lParam);
    }
    return DefSubclassProc(hwnd, msg, wParam, lParam);
}

// ── SC_RefreshDesktopStrip ────────────────────────────────────────────────────────
// Destroys all existing Desktop mini-icon strip controls and recreates them
// for every SCT_DESKTOP shortcut in s_scShortcuts.  Icons are 16×16, placed
// side-by-side and horizontally centred within the Desktop column.
void SC_RefreshDesktopStrip(HWND hwnd, HINSTANCE hInst)
{
    if (s_scDskColW == 0) return;  // BuildPage has not run yet

    // Destroy any existing strip controls.
    for (int i = 0; i < 50; ++i) {
        HWND hOld = GetDlgItem(hwnd, IDC_SC_DSK_STRIP_BASE + i);
        if (hOld) {
            // Free the HICON stored in GWLP_USERDATA.
            HICON hOldIco = (HICON)GetWindowLongPtrW(hOld, GWLP_USERDATA);
            if (hOldIco) DestroyIcon(hOldIco);
            DestroyWindow(hOld);
        } else break;  // IDs are allocated sequentially; first missing = done
    }
    HideTooltip();
    s_dskMiniHovered  = NULL;
    s_dskMiniTracking = false;

    // Collect Desktop shortcuts.
    std::vector<ShortcutDef*> dsk;
    for (auto& sc : s_scShortcuts)
        if (sc.type == SCT_DESKTOP) dsk.push_back(&sc);

    if (dsk.empty()) return;

    const int icoSz  = S(16);
    const int icoGap = S(6);
    int totalW = (int)dsk.size() * icoSz + ((int)dsk.size() - 1) * icoGap;
    int startX = s_scDskCol0X + (s_scDskColW - totalW) / 2;
    int stripY  = s_scDskStripY - s_scScrollOffset;

    for (int i = 0; i < (int)dsk.size() && i < 50; ++i) {
        ShortcutDef* pSc = dsk[i];
        int ctrlId = IDC_SC_DSK_STRIP_BASE + i;  // index, NOT sc.id
        // Load 16×16 icon from the exe or the configured icon file.
        HICON hIco = NULL;
        const std::wstring& src = pSc->iconPath.empty() ? pSc->exePath : pSc->iconPath;
        if (!src.empty())
            PrivateExtractIconsW(src.c_str(), pSc->iconIndex, icoSz, icoSz,
                &hIco, NULL, 1, 0);
        // If extraction fails fall back to a generic shortcut icon (shell32 #17).
        if (!hIco) {
            wchar_t shell32[MAX_PATH];
            GetSystemDirectoryW(shell32, MAX_PATH);
            wcscat_s(shell32, L"\\shell32.dll");
            PrivateExtractIconsW(shell32, 17, icoSz, icoSz, &hIco, NULL, 1, 0);
        }
        int x = startX + i * (icoSz + icoGap);
        HWND hMini = CreateWindowExW(0, L"STATIC", NULL,
            WS_CHILD | WS_VISIBLE | SS_NOTIFY,
            x, stripY, icoSz, icoSz,
            hwnd, (HMENU)(UINT_PTR)ctrlId, hInst, NULL);
        // Store the HICON in GWLP_USERDATA for painting; store sc.id in the
        // control's extra data via a window property so the subclass can look
        // up the correct ShortcutDef even after reordering.
        SetWindowLongPtrW(hMini, GWLP_USERDATA, (LONG_PTR)hIco);
        SetPropW(hMini, L"ScId", (HANDLE)(INT_PTR)pSc->id);
        SetWindowSubclass(hMini, SC_DskMiniSubclassProc, 0, 0);
    }
}

// ── SC_RefreshPinStrips ────────────────────────────────────────────────────────
// Destroys and recreates both Pin-to-Start and Pin-to-Taskbar checkbox strips
// from the current s_scShortcuts.  Also updates the two status labels.
//
// ── SC_TooltipSubclassProc ────────────────────────────────────────────────────
// Generic hover-tooltip subclass.  Before installing, store the tooltip text
// (heap-allocated std::wstring) as property L"TtText" on the control.
// Install with subclassId = 2 (checkbox controls use id = 1).
static LRESULT CALLBACK SC_TooltipSubclassProc(
    HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam,
    UINT_PTR uIdSubclass, DWORD_PTR /*dwRefData*/)
{
    switch (msg) {
    case WM_MOUSEMOVE: {
        if (!IsTooltipVisible()) {
            auto* pText = reinterpret_cast<std::wstring*>(GetPropW(hwnd, L"TtText"));
            if (pText && !pText->empty()) {
                POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
                ClientToScreen(hwnd, &pt);
                ShowMultilingualTooltip({{L"", *pText}},
                    pt.x + 16, pt.y + 16, GetParent(hwnd));
                TRACKMOUSEEVENT tme = { sizeof(tme), TME_LEAVE, hwnd, 0 };
                TrackMouseEvent(&tme);
            }
        }
        break;
    }
    case WM_MOUSELEAVE:
        HideTooltip();
        break;
    case WM_NCDESTROY:
        delete reinterpret_cast<std::wstring*>(GetPropW(hwnd, L"TtText"));
        RemovePropW(hwnd, L"TtText");
        RemoveWindowSubclass(hwnd, SC_TooltipSubclassProc, uIdSubclass);
        break;
    }
    return DefSubclassProc(hwnd, msg, wParam, lParam);
}

// Attach a hover tooltip to a control (installs SC_TooltipSubclassProc).
static void AttachTooltip(HWND hwnd, std::wstring text, UINT_PTR subclassId = 2)
{
    SetPropW(hwnd, L"TtText", reinterpret_cast<HANDLE>(new std::wstring(std::move(text))));
    SetWindowSubclass(hwnd, SC_TooltipSubclassProc, subclassId, 0);
}

// Eligible shortcuts: any SCT_DESKTOP or SCT_STARTMENU entry.  If multiple
// shortcuts share the same exePath they are deduplicated — only the first one
// (by id) appears.  The shortcut's sc.id is stored in the L"ScId" window
// property of each checkbox so the WM_COMMAND handler can look it up.
void SC_RefreshPinStrips(HWND hwnd, HINSTANCE hInst)
{
    if (s_scPinStripY == 0) return;  // BuildPage has not run yet

    // Destroy existing strip checkboxes for both columns.
    for (int i = 0; i < 50; ++i) {
        HWND hOld = GetDlgItem(hwnd, IDC_SC_SMPIN_STRIP_BASE + i);
        if (hOld) DestroyWindow(hOld); else break;
    }
    for (int i = 0; i < 50; ++i) {
        HWND hOld = GetDlgItem(hwnd, IDC_SC_TBPIN_STRIP_BASE + i);
        if (hOld) DestroyWindow(hOld); else break;
    }

    // Build the deduplicated eligible list (Desktop + SM shortcuts, unique exePath).
    std::vector<ShortcutDef*> eligible;
    std::vector<std::wstring> seenPaths;
    for (auto& sc : s_scShortcuts) {
        if (sc.type != SCT_DESKTOP && sc.type != SCT_STARTMENU) continue;
        if (sc.exePath.empty()) continue;
        // Case-insensitive deduplication by exePath.
        std::wstring lower = sc.exePath;
        for (wchar_t& c : lower) c = towlower(c);
        bool dup = false;
        for (const auto& seen : seenPaths)
            if (seen == lower) { dup = true; break; }
        if (dup) continue;
        seenPaths.push_back(lower);
        eligible.push_back(&sc);
    }

    // Enable/disable the big pin icon buttons (grey = no eligible shortcuts).
    HWND hPinStartBtn = GetDlgItem(hwnd, IDC_SC_PINSTART_BTN);
    HWND hPinTbBtn    = GetDlgItem(hwnd, IDC_SC_PINTASKBAR_BTN);
    if (hPinStartBtn) EnableWindow(hPinStartBtn, !eligible.empty());
    if (hPinTbBtn)    EnableWindow(hPinTbBtn,    !eligible.empty());

    if (eligible.empty()) return;  // nothing to show in the strips

    // Layout: one checkbox row per eligible shortcut, left-aligned in each column.
    const int cbH   = S(20);  // single-line height for pin strip checkboxes
    const int cbGap = S(3);
    const int cbMargin = S(6);

    for (int i = 0; i < (int)eligible.size() && i < 50; ++i) {
        ShortcutDef* pSc = eligible[i];
        int y = s_scPinStripY - s_scScrollOffset + i * (cbH + cbGap);

        // -- Pin to Start checkbox --
        int smCbW = s_scSmPinColW - 2 * cbMargin;
        HWND hSmCb = CreateCustomCheckbox(
            hwnd, IDC_SC_SMPIN_STRIP_BASE + i, pSc->name,
            pSc->pinToStart,
            s_scSmPinColX + cbMargin, y, smCbW, cbH, hInst);
        if (s_hScPinCbFont) SendMessageW(hSmCb, WM_SETFONT, (WPARAM)s_hScPinCbFont, TRUE);
        SetPropW(hSmCb, L"ScId", (HANDLE)(INT_PTR)pSc->id);

        // -- Pin to Taskbar checkbox --
        int tbCbW = s_scTbPinColW - 2 * cbMargin;
        HWND hTbCb = CreateCustomCheckbox(
            hwnd, IDC_SC_TBPIN_STRIP_BASE + i, pSc->name,
            pSc->pinToTaskbar,
            s_scTbPinColX + cbMargin, y, tbCbW, cbH, hInst);
        if (s_hScPinCbFont) SendMessageW(hTbCb, WM_SETFONT, (WPARAM)s_hScPinCbFont, TRUE);
        SetPropW(hTbCb, L"ScId", (HANDLE)(INT_PTR)pSc->id);
    }
}

// ── SC_SmTreeSubclassProc ────────────────────────────────────────────────────────
// Shows the project's custom tooltip when hovering over a tree node.
// Suppresses the system truncation tooltip (TVS_NOTOOLTIPS is set at creation).
static LRESULT CALLBACK SC_SmTreeSubclassProc(
    HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam,
    UINT_PTR /*uid*/, DWORD_PTR /*ref*/)
{
    switch (msg) {
    case WM_MOUSEMOVE: {
        int mx = (short)LOWORD(lParam);
        int my = (short)HIWORD(lParam);
        TVHITTESTINFO ht = {};
        ht.pt = { mx, my };
        HTREEITEM hItem = TreeView_HitTest(hwnd, &ht);
        int hovId = -1;
        std::wstring nodeName;
        if (hItem && (ht.flags & TVHT_ONITEM)) {
            wchar_t buf[256] = {};
            TVITEMW tvi    = {};
            tvi.mask       = TVIF_TEXT | TVIF_PARAM;
            tvi.hItem      = hItem;
            tvi.pszText    = buf;
            tvi.cchTextMax = 256;
            TreeView_GetItem(hwnd, &tvi);
            hovId    = (int)tvi.lParam;
            nodeName = buf;
        }
        if (hovId != s_scSmTreeHoveredId) {
            HideTooltip();
            s_scSmTreeHoveredId = hovId;
            if (!nodeName.empty()) {
                POINT pt = { mx, my };
                ClientToScreen(hwnd, &pt);
                ShowMultilingualTooltip({{L"", nodeName}},
                    pt.x + 16, pt.y + 16, GetParent(hwnd));
            }
        }
        if (!s_scSmTreeTracking) {
            TRACKMOUSEEVENT tme = { sizeof(tme), TME_LEAVE, hwnd, 0 };
            TrackMouseEvent(&tme);
            s_scSmTreeTracking = true;
        }
        break;
    }
    case WM_MOUSELEAVE:
        HideTooltip();
        s_scSmTreeHoveredId = -1;
        s_scSmTreeTracking  = false;
        break;
    case WM_NCDESTROY:
        RemoveWindowSubclass(hwnd, SC_SmTreeSubclassProc, 0);
        return DefSubclassProc(hwnd, msg, wParam, lParam);
    }
    return DefSubclassProc(hwnd, msg, wParam, lParam);
}

// ── BuildSmPath ──────────────────────────────────────────────────────────────
// Builds a breadcrumb path string from a Start Menu node id to the root,
// e.g. "Start Menu › Programs › MyApp".
static std::wstring BuildSmPath(int nodeId)
{
    std::vector<std::wstring> parts;
    int id = nodeId;
    while (id >= 0) {
        bool found = false;
        for (const auto& n : s_scMenuNodes) {
            if (n.id == id) {
                parts.push_back(n.name);
                id = n.parentId;
                found = true;
                break;
            }
        }
        if (!found) break;
    }
    std::reverse(parts.begin(), parts.end());
    std::wstring result;
    for (size_t i = 0; i < parts.size(); ++i) {
        if (i > 0) result += L" › ";
        result += parts[i];
    }
    return result;
}

// ── SC_BuildPage ──────────────────────────────────────────────────────────────

// Returns s with L'\n' inserted at the word boundary nearest the string midpoint
// so long checkbox labels split into two roughly equal lines.
// DrawTextW with DT_WORDBREAK treats L'\n' as a hard line break.
static std::wstring MidBreak(const std::wstring& s)
{
    if (s.empty()) return s;
    size_t mid = s.size() / 2;
    for (size_t d = 0; d <= mid; ++d) {
        if (mid + d < s.size() && s[mid + d] == L' ')
            return s.substr(0, mid + d) + L'\n' + s.substr(mid + d + 1);
        if (d > 0 && mid >= d && s[mid - d] == L' ')
            return s.substr(0, mid - d) + L'\n' + s.substr(mid - d + 1);
    }
    return s;  // no space found — return unchanged
}

// Subclass proc for the 64×64 Desktop icon static control.
// Paints the icon via DrawIconEx and fills the background with the window colour.
static LRESULT CALLBACK SC_DesktopIconSubclassProc(
    HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam,
    UINT_PTR /*uid*/, DWORD_PTR /*ref*/)
{
    switch (msg) {
    case WM_ERASEBKGND: {
        RECT rc; GetClientRect(hwnd, &rc);
        FillRect((HDC)wParam, &rc, GetSysColorBrush(COLOR_WINDOW));
        return 1;
    }
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);
        HICON hIco = (HICON)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
        if (hIco) {
            RECT rc; GetClientRect(hwnd, &rc);
            DrawIconEx(hdc, 0, 0, hIco, rc.right, rc.bottom, 0, NULL, DI_NORMAL);
        }
        EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_MOUSEMOVE: {
        // Show a tooltip on hover when the pin icon is enabled.
        // (Disabled-state tooltip is handled in mainwindow.cpp WM_MOUSEMOVE
        // because disabled windows forward mouse messages to their parent.)
        int ctrlId = GetDlgCtrlID(hwnd);
        if ((ctrlId == IDC_SC_PINSTART_BTN || ctrlId == IDC_SC_PINTASKBAR_BTN)
            && IsWindowEnabled(hwnd) && !IsTooltipVisible())
        {
            const wchar_t* key = (ctrlId == IDC_SC_PINSTART_BTN)
                ? L"sc_pin_start_tooltip" : L"sc_pin_taskbar_tooltip";
            const wchar_t* fb  = (ctrlId == IDC_SC_PINSTART_BTN)
                ? L"Use the checkboxes below to select which shortcuts to pin to Start."
                : L"Use the checkboxes below to select which shortcuts to pin to the Taskbar.";
            const auto& loc = MainWindow::GetLocale();
            auto it = loc.find(key);
            std::wstring txt = (it != loc.end()) ? it->second : fb;
            POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
            ClientToScreen(hwnd, &pt);
            ShowMultilingualTooltip({{L"", txt}}, pt.x + 16, pt.y + 16, GetParent(hwnd));
            TRACKMOUSEEVENT tme = { sizeof(tme), TME_LEAVE, hwnd, 0 };
            TrackMouseEvent(&tme);
        }
        break;
    }
    case WM_MOUSELEAVE: {
        int ctrlId = GetDlgCtrlID(hwnd);
        if (ctrlId == IDC_SC_PINSTART_BTN || ctrlId == IDC_SC_PINTASKBAR_BTN)
            HideTooltip();
        break;
    }
    case WM_NCDESTROY:
        RemoveWindowSubclass(hwnd, SC_DesktopIconSubclassProc, 0);
        return DefSubclassProc(hwnd, msg, wParam, lParam);
    }
    return DefSubclassProc(hwnd, msg, wParam, lParam);
}

// ── SC_RebuildSmTree ──────────────────────────────────────────────────────────
// Clears and rebuilds the Start Menu TreeView from s_scMenuNodes and
// s_scShortcuts.  Also rebuilds the TreeView image list from scratch:
//   index 0  — closed folder (shell32 #3)
//   index 1  — open folder   (shell32 #5)
//   index 2  — fallback link icon (shell32 #17, used when icon load fails)
//   index 3+ — one icon per SCT_STARTMENU shortcut, loaded from its exe/icon
// lParam encoding: folder nodes → node.id (≥ 0); shortcut items → -(sc.id) (< 0).
//
// selectLParam  — after rebuilding, attempt to reselect the item whose lParam
//                 matches this value.  0 (default) reselects the Start Menu root.
//                 Pass -(sc.id) to reselect a newly added or edited shortcut.
static void SC_RebuildSmTree(LPARAM selectLParam = 0)
{
    if (!s_hScStartMenuTree || !IsWindow(s_hScStartMenuTree)) return;

    HWND hwnd = GetParent(s_hScStartMenuTree);

    // Reset transient HTREEITEM handles — they become invalid after DeleteAllItems.
    for (auto& n  : s_scMenuNodes) n.hItem  = nullptr;
    for (auto& sc : s_scShortcuts) if (sc.type == SCT_STARTMENU) sc.hSmItem = nullptr;

    // ── Rebuild image list with per-shortcut icons ────────────────────────────
    // Destroy the previous list and create a fresh one so indices stay stable.
    std::map<int, int> scIdToImgIdx;  // sc.id → image list index
    {
        HIMAGELIST hOldIL = (HIMAGELIST)GetPropW(hwnd, L"hScSmTreeIL");
        if (hOldIL) { ImageList_Destroy(hOldIL); RemovePropW(hwnd, L"hScSmTreeIL"); }

        wchar_t shell32Path[MAX_PATH];
        GetSystemDirectoryW(shell32Path, MAX_PATH);
        wcscat_s(shell32Path, L"\\shell32.dll");

        HIMAGELIST hIL = ImageList_Create(32, 32, ILC_COLOR32 | ILC_MASK, 4, 4);
        if (hIL) {
            HICON hClose = NULL, hOpen = NULL;
            ExtractIconExW(shell32Path, 3, &hClose, NULL, 1);  // index 0: closed folder
            ExtractIconExW(shell32Path, 5, &hOpen,  NULL, 1);  // index 1: open folder
            if (hClose) { ImageList_AddIcon(hIL, hClose); DestroyIcon(hClose); }
            if (hOpen)  { ImageList_AddIcon(hIL, hOpen);  DestroyIcon(hOpen);  }
            HICON hFallback = NULL;
            ExtractIconExW(shell32Path, 17, &hFallback, NULL, 1);  // index 2: fallback link
            if (hFallback) { ImageList_AddIcon(hIL, hFallback); DestroyIcon(hFallback); }

            // Indices 3+: one per SCT_STARTMENU shortcut, loaded from exe or icon file.
            for (const auto& sc : s_scShortcuts) {
                if (sc.type != SCT_STARTMENU) continue;
                HICON hIco = NULL;
                const std::wstring& src = sc.iconPath.empty() ? sc.exePath : sc.iconPath;
                if (!src.empty())
                    PrivateExtractIconsW(src.c_str(), sc.iconIndex, 32, 32, &hIco, NULL, 1, 0);
                int idx;
                if (hIco) {
                    idx = ImageList_AddIcon(hIL, hIco);
                    DestroyIcon(hIco);
                    if (idx < 0) idx = 2;  // add failed — fall back
                } else {
                    idx = 2;  // icon load failed — use generic link
                }
                scIdToImgIdx[sc.id] = idx;
            }
            TreeView_SetImageList(s_hScStartMenuTree, hIL, TVSIL_NORMAL);
            SetPropW(hwnd, L"hScSmTreeIL", (HANDLE)hIL);
        }
    }

    TreeView_DeleteAllItems(s_hScStartMenuTree);

    std::function<void(HTREEITEM, int)> addChildren =
        [&](HTREEITEM hParent, int parentId) {
        // Folder children first.
        for (auto& node : s_scMenuNodes) {
            if (node.parentId != parentId) continue;
            TVINSERTSTRUCTW tvis     = {};
            tvis.hParent             = hParent;
            tvis.hInsertAfter        = TVI_LAST;
            tvis.item.mask           = TVIF_TEXT | TVIF_IMAGE | TVIF_SELECTEDIMAGE | TVIF_PARAM;
            tvis.item.pszText        = (LPWSTR)node.name.c_str();
            tvis.item.iImage         = 0;  // closed folder
            tvis.item.iSelectedImage = 1;  // open folder
            tvis.item.lParam         = (LPARAM)node.id;
            node.hItem = (HTREEITEM)SendMessageW(
                s_hScStartMenuTree, TVM_INSERTITEM, 0, (LPARAM)&tvis);
            if (node.hItem) addChildren(node.hItem, node.id);
        }
        // Shortcut leaf items after folders.
        for (auto& sc : s_scShortcuts) {
            if (sc.type != SCT_STARTMENU || sc.smNodeId != parentId) continue;
            auto it    = scIdToImgIdx.find(sc.id);
            int imgIdx = (it != scIdToImgIdx.end()) ? it->second : 2;
            TVINSERTSTRUCTW tvis     = {};
            tvis.hParent             = hParent;
            tvis.hInsertAfter        = TVI_LAST;
            tvis.item.mask           = TVIF_TEXT | TVIF_IMAGE | TVIF_SELECTEDIMAGE | TVIF_PARAM;
            tvis.item.pszText        = (LPWSTR)sc.name.c_str();
            tvis.item.iImage         = imgIdx;
            tvis.item.iSelectedImage = imgIdx;
            tvis.item.lParam         = -(LPARAM)sc.id;  // negative → shortcut item
            sc.hSmItem = (HTREEITEM)SendMessageW(
                s_hScStartMenuTree, TVM_INSERTITEM, 0, (LPARAM)&tvis);
        }
    };
    addChildren(TVI_ROOT, -1);

    // Always expand the root so Programs is immediately visible.
    HTREEITEM hRoot = TreeView_GetRoot(s_hScStartMenuTree);
    if (hRoot) TreeView_Expand(s_hScStartMenuTree, hRoot, TVE_EXPAND);

    // Restore selection: find the item whose lParam matches selectLParam.
    HTREEITEM hRestore = nullptr;
    if (selectLParam >= 0) {
        for (const auto& n : s_scMenuNodes)
            if ((LPARAM)n.id == selectLParam && n.hItem) { hRestore = n.hItem; break; }
    } else {
        int scId = (int)(-selectLParam);
        for (const auto& sc : s_scShortcuts)
            if (sc.type == SCT_STARTMENU && sc.id == scId && sc.hSmItem)
            { hRestore = sc.hSmItem; break; }
    }
    if (!hRestore) hRestore = hRoot;
    if (hRestore) {
        TreeView_EnsureVisible(s_hScStartMenuTree, hRestore);
        TreeView_SelectItem(s_hScStartMenuTree, hRestore);
    }
}

int SC_BuildPage(HWND hwnd, HINSTANCE hInst, int pageY, int clientWidth,
                 HFONT hPageTitleFont, HFONT hGuiFont,
                 const std::map<std::wstring, std::wstring>& locale)
{
    s_scHInst = hInst;  // saved for SC_OnResize

    // Handy locale lookup with English fallback.
    auto loc = [&](const wchar_t* key, const wchar_t* fallback) -> std::wstring {
        auto it = locale.find(key);
        return (it != locale.end()) ? it->second : fallback;
    };

    // ── Page title ────────────────────────────────────────────────────────────
    std::wstring scTitle = loc(L"sc_page_title", L"Shortcuts");
    HWND hScTitle = CreateWindowExW(0, L"STATIC", scTitle.c_str(),
        WS_CHILD | WS_VISIBLE | SS_LEFT,
        S(20), pageY + S(15), clientWidth - S(40), S(38),
        hwnd, (HMENU)5300, hInst, NULL);
    if (hPageTitleFont) SendMessageW(hScTitle, WM_SETFONT, (WPARAM)hPageTitleFont, TRUE);

    // ── Hint label ────────────────────────────────────────────────────────────
    std::wstring scHint = loc(L"sc_page_hint",
        L"Click a shortcut type below to configure it.");
    HWND hScHint = CreateWindowExW(0, L"STATIC", scHint.c_str(),
        WS_CHILD | WS_VISIBLE | SS_LEFT,
        S(20), pageY + S(56), clientWidth - S(40), S(22),
        hwnd, NULL, hInst, NULL);
    if (hGuiFont) SendMessageW(hScHint, WM_SETFONT, (WPARAM)hGuiFont, TRUE);

    // ── Separator below hint ──────────────────────────────────────────────────
    CreateWindowExW(0, L"STATIC", L"",
        WS_CHILD | WS_VISIBLE | SS_ETCHEDHORZ,
        S(20), pageY + S(84), clientWidth - S(40), S(2),
        hwnd, NULL, hInst, NULL);

    // ── Layout constants ──────────────────────────────────────────────────────
    const int gap  = S(10);
    int rowY = pageY + S(96);

    // Build system-directory paths once.
    wchar_t shell32Path[MAX_PATH];
    GetSystemDirectoryW(shell32Path, MAX_PATH);
    wcscat_s(shell32Path, L"\\shell32.dll");

    wchar_t imageresPath[MAX_PATH];
    GetSystemDirectoryW(imageresPath, MAX_PATH);
    wcscat_s(imageresPath, L"\\imageres.dll");

    // ── 3-column shortcut section: Desktop | Start Menu pin | Taskbar pin ─────
    // Each column is clientWidth/3 wide.  Bold centred heading, 64×64 icon,
    // and beneath: opt-out checkbox for each column.
    // IDs 5302/5303/5304 → WM_CTLCOLORSTATIC selects hPageTitleFont for headings.
    {
        const int colW    = clientWidth / 3;
        const int col2W   = clientWidth - 2 * colW;  // last column takes remainder
        const int col0X   = 0;
        const int col1X   = colW;
        const int col2X   = colW * 2;
        const int iconSz  = S(64);
        const int headH   = S(22);

        // Row A — section headings
        {
            std::wstring s0 = loc(L"sc_desktop_section", L"Desktop");
            std::wstring s1 = loc(L"sc_sm_pin_section",  L"Pin to Start");
            std::wstring s2 = loc(L"sc_tb_pin_section",  L"Taskbar");
            HWND h0 = CreateWindowExW(0, L"STATIC", s0.c_str(),
                WS_CHILD | WS_VISIBLE | SS_CENTER | SS_NOPREFIX,
                col0X, rowY, colW, headH,
                hwnd, (HMENU)5302, hInst, NULL);
            if (hPageTitleFont) SendMessageW(h0, WM_SETFONT, (WPARAM)hPageTitleFont, TRUE);
            HWND h1 = CreateWindowExW(0, L"STATIC", s1.c_str(),
                WS_CHILD | WS_VISIBLE | SS_CENTER | SS_NOPREFIX,
                col1X, rowY, colW, headH,
                hwnd, (HMENU)5303, hInst, NULL);
            if (hPageTitleFont) SendMessageW(h1, WM_SETFONT, (WPARAM)hPageTitleFont, TRUE);
            HWND h2 = CreateWindowExW(0, L"STATIC", s2.c_str(),
                WS_CHILD | WS_VISIBLE | SS_CENTER | SS_NOPREFIX,
                col2X, rowY, col2W, headH,
                hwnd, (HMENU)5304, hInst, NULL);
            if (hPageTitleFont) SendMessageW(h2, WM_SETFONT, (WPARAM)hPageTitleFont, TRUE);
        }
        rowY += headH + S(6);

        // Row B — 64×64 icons, centred within each column
        {
            int iconY = rowY;
            // Column 0: Desktop (imageres.dll #104)
            {
                if (s_hDesktopIcon) { DestroyIcon(s_hDesktopIcon); }
                s_hDesktopIcon = NULL;
                PrivateExtractIconsW(imageresPath, 104, iconSz, iconSz, &s_hDesktopIcon, NULL, 1, 0);
                if (!s_hDesktopIcon) ExtractIconExW(imageresPath, 104, &s_hDesktopIcon, NULL, 1);
                int ix = col0X + (colW - iconSz) / 2;
                HWND hIco = CreateWindowExW(0, L"STATIC", NULL,
                    WS_CHILD | WS_VISIBLE | SS_NOTIFY,
                    ix, iconY, iconSz, iconSz,
                    hwnd, (HMENU)IDC_SC_DESKTOP_BTN, hInst, NULL);
                SetWindowLongPtrW(hIco, GWLP_USERDATA, (LONG_PTR)s_hDesktopIcon);
                SetWindowSubclass(hIco, SC_DesktopIconSubclassProc, 0, 0);
            }            // Column 1: Start Menu pin (imageres.dll #228)
            {
                if (s_hSmPinIcon) { DestroyIcon(s_hSmPinIcon); }
                s_hSmPinIcon = NULL;
                PrivateExtractIconsW(imageresPath, 228, iconSz, iconSz, &s_hSmPinIcon, NULL, 1, 0);
                if (!s_hSmPinIcon) ExtractIconExW(imageresPath, 228, NULL, &s_hSmPinIcon, 1);
                int ix = col1X + (colW - iconSz) / 2;
                HWND hIco = CreateWindowExW(0, L"STATIC", NULL,
                    WS_CHILD | WS_VISIBLE | SS_NOTIFY,
                    ix, iconY, iconSz, iconSz,
                    hwnd, (HMENU)IDC_SC_PINSTART_BTN, hInst, NULL);
                SetWindowLongPtrW(hIco, GWLP_USERDATA, (LONG_PTR)s_hSmPinIcon);
                SetWindowSubclass(hIco, SC_DesktopIconSubclassProc, 0, 0);
                // Disabled until SC_RefreshPinStrips finds eligible shortcuts.
                EnableWindow(hIco, FALSE);
            }
            // Column 2: Taskbar pin (imageres.dll #175)
            {
                if (s_hTbPinIcon) { DestroyIcon(s_hTbPinIcon); }
                s_hTbPinIcon = NULL;
                PrivateExtractIconsW(imageresPath, 175, iconSz, iconSz, &s_hTbPinIcon, NULL, 1, 0);
                if (!s_hTbPinIcon) ExtractIconExW(imageresPath, 175, NULL, &s_hTbPinIcon, 1);
                int ix = col2X + (col2W - iconSz) / 2;
                HWND hIco = CreateWindowExW(0, L"STATIC", NULL,
                    WS_CHILD | WS_VISIBLE | SS_NOTIFY,
                    ix, iconY, iconSz, iconSz,
                    hwnd, (HMENU)IDC_SC_PINTASKBAR_BTN, hInst, NULL);
                SetWindowLongPtrW(hIco, GWLP_USERDATA, (LONG_PTR)s_hTbPinIcon);
                SetWindowSubclass(hIco, SC_DesktopIconSubclassProc, 0, 0);
                // Disabled until SC_RefreshPinStrips finds eligible shortcuts.
                EnableWindow(hIco, FALSE);
            }
        }
        rowY += iconSz + S(8);

        // Desktop shortcut mini-icon strip — 16×16 icons for each added Desktop
        // shortcut, centred in column 0, just below the 64×64 icon.
        // The strip Y coordinate is saved so SC_RefreshDesktopStrip() can
        // recreate the controls after the dialog returns without knowing the
        // full layout.
        s_scDskCol0X  = col0X;
        s_scDskColW   = colW;
        s_scDskStripY = rowY;
        SC_RefreshDesktopStrip(hwnd, hInst);
        rowY += S(16) + S(6);  // reserve one row height for the desktop strip

        // Pin strip area (cols 1 & 2) — one checkbox per eligible shortcut.
        // Eligible = SCT_DESKTOP + SCT_STARTMENU shortcuts, deduplicated by exePath.
        // SC_RefreshPinStrips() creates and destroys these checkboxes dynamically.
        // We save the layout coordinates here so it can be called at any time.
        // Build the pin checkbox font (same size as the status label, not bold).
        if (s_hScPinCbFont) { DeleteObject(s_hScPinCbFont); s_hScPinCbFont = NULL; }
        {
            HDC hdc = GetDC(hwnd);
            int lfH = -MulDiv(8, GetDeviceCaps(hdc, LOGPIXELSY), 72);
            ReleaseDC(hwnd, hdc);
            LOGFONTW lf = {};
            lf.lfHeight  = lfH;
            lf.lfWeight  = FW_NORMAL;
            lf.lfCharSet = DEFAULT_CHARSET;
            lf.lfQuality = CLEARTYPE_QUALITY;
            wcscpy_s(lf.lfFaceName, L"Segoe UI");
            s_hScPinCbFont = CreateFontIndirectW(&lf);
        }
        s_scPinStripY  = rowY;
        s_scSmPinColX  = col1X;
        s_scSmPinColW  = colW;
        s_scTbPinColX  = col2X;
        s_scTbPinColW  = col2W;
        // Pre-count eligible shortcuts (same dedup logic as SC_RefreshPinStrips)
        // so we reserve exactly enough space — no more blank gap.
        {
            int eligCnt = 0;
            std::vector<std::wstring> seenLower;
            for (const auto& sc : s_scShortcuts) {
                if (sc.type != SCT_DESKTOP && sc.type != SCT_STARTMENU) continue;
                if (sc.exePath.empty()) continue;
                std::wstring low = sc.exePath;
                for (wchar_t& c : low) c = towlower(c);
                bool dup = false;
                for (const auto& s : seenLower) if (s == low) { dup = true; break; }
                if (dup) continue;
                seenLower.push_back(low);
                ++eligCnt;
            }
            // Each checkbox row is cbH(20) + cbGap(3) = 23px; last row has no gap.
            const int pinStripReservedH = (eligCnt > 0)
                ? eligCnt * (S(20) + S(3)) - S(3) + S(10)
                : 0;
            rowY += pinStripReservedH;
        }

        // Row C — opt-out checkboxes for all 3 columns, directly below the pin strip.
        // Cols 1 & 2 previously had a "Not Pinned / Pinned / Multi Pinned" status
        // label here — removed when persistent pin-strip checkboxes were added.
        {
            // Build 9pt bold font (recreated each page visit).
            if (s_hScCbFont) { DeleteObject(s_hScCbFont); s_hScCbFont = NULL; }
            {
                HDC hdc = GetDC(hwnd);
                int lfH = -MulDiv(9, GetDeviceCaps(hdc, LOGPIXELSY), 72);
                ReleaseDC(hwnd, hdc);
                LOGFONTW lf = {};
                lf.lfHeight  = lfH;
                lf.lfWeight  = FW_BOLD;
                lf.lfCharSet = DEFAULT_CHARSET;
                lf.lfQuality = CLEARTYPE_QUALITY;
                wcscpy_s(lf.lfFaceName, L"Segoe UI");
                s_hScCbFont = CreateFontIndirectW(&lf);
            }
            // All checkboxes are S(34) tall (two lines at 9pt bold).
            const int cbH = S(34);

            // Measure a MidBreak'd string with s_hScCbFont and return the pixel
            // width needed for the whole control (box + gap + longest line).
            // The caller uses this to centre the control in its column.
            auto CbWidth = [&](const std::wstring& brokenStr, int colMaxW) -> int {
                size_t nl = brokenStr.find(L'\n');
                HDC hdc   = GetDC(hwnd);
                HFONT hOF = s_hScCbFont ? (HFONT)SelectObject(hdc, s_hScCbFont) : NULL;
                SIZE sz1  = {}, sz2 = {};
                if (nl != std::wstring::npos) {
                    GetTextExtentPoint32W(hdc, brokenStr.c_str(), (int)nl, &sz1);
                    GetTextExtentPoint32W(hdc, brokenStr.c_str() + nl + 1,
                                         (int)(brokenStr.size() - nl - 1), &sz2);
                } else {
                    GetTextExtentPoint32W(hdc, brokenStr.c_str(), (int)brokenStr.size(), &sz1);
                }
                if (hOF) SelectObject(hdc, hOF);
                ReleaseDC(hwnd, hdc);
                int needed = S(15) + S(6) + std::max(sz1.cx, sz2.cx) + S(2);
                return std::min(needed, colMaxW);
            };

            // Column 0: Desktop opt-out, centred in column.
            {
                std::wstring scOptOut = MidBreak(loc(L"sc_desktop_opt_out",
                    L"Allow the end user to opt out of the desktop shortcut at install time"));
                int cbW = CbWidth(scOptOut, colW - S(8));
                int cbX = col0X + (colW - cbW) / 2;
                HWND hOptOut = CreateCustomCheckbox(
                    hwnd, IDC_SC_DESKTOP_OPT, scOptOut,
                    s_scDesktopOptOut,
                    cbX, rowY + S(4), cbW, cbH, hInst);
                if (s_hScCbFont) SendMessageW(hOptOut, WM_SETFONT, (WPARAM)s_hScCbFont, TRUE);
            }

            // Column 1: Start Menu pin opt-out checkbox centred.
            {
                std::wstring scSmPinOpt = MidBreak(loc(L"sc_sm_pin_opt_out",
                    L"Allow the end user to opt out of the Start Menu pin at install time"));
                int cbW = CbWidth(scSmPinOpt, colW - S(8));
                int cbX = col1X + (colW - cbW) / 2;
                HWND hSmOpt = CreateCustomCheckbox(
                    hwnd, IDC_SC_SM_PIN_OPT, scSmPinOpt,
                    s_scSmPinOptOut,
                    cbX, rowY + S(4), cbW, cbH, hInst);
                if (s_hScCbFont) SendMessageW(hSmOpt, WM_SETFONT, (WPARAM)s_hScCbFont, TRUE);
            }

            // Column 2: Taskbar pin opt-out checkbox centred.
            {
                std::wstring scTbPinOpt = MidBreak(loc(L"sc_tb_pin_opt_out",
                    L"Allow the end user to opt out of the Taskbar pin at install time"));
                int cb2W = CbWidth(scTbPinOpt, col2W - S(8));
                int cb2X = col2X + (col2W - cb2W) / 2;
                HWND hTbOpt = CreateCustomCheckbox(
                    hwnd, IDC_SC_TB_PIN_OPT, scTbPinOpt,
                    s_scTbPinOptOut,
                    cb2X, rowY + S(4), cb2W, cbH, hInst);
                if (s_hScCbFont) SendMessageW(hTbOpt, WM_SETFONT, (WPARAM)s_hScCbFont, TRUE);
            }
        }
        rowY += S(4) + S(34) + S(10);
    }

    // ── Section divider ───────────────────────────────────────────────────────
    CreateWindowExW(0, L"STATIC", L"",
        WS_CHILD | WS_VISIBLE | SS_ETCHEDHORZ,
        S(20), rowY, clientWidth - S(40), S(2),
        hwnd, NULL, hInst, NULL);
    rowY += S(2) + gap;

    // ── Start Menu & Programs — TreeView ─────────────────────────────────────
    // The developer builds the folder hierarchy here.  Clicking a node will open
    // the shortcut-config dialog (implemented in a later session).  User-added
    // subfolders can be renamed by double-clicking (TVS_EDITLABELS) and removed
    // with the Remove button.  The root and Programs nodes are fixed.

    // Tree width matches the 3 action buttons beneath it — both are centred
    // together so the tree left/right edges align exactly with the button row.
    // Widths are measured from locale text so any language fits without truncation.
    const int btnGap = S(6);
    const int addW  = MeasureButtonWidth(loc(L"sc_sm_add",    L"Add Subfolder"), true);
    const int scW   = MeasureButtonWidth(loc(L"sc_sm_addsc",  L"Add Shortcut"),  true);
    const int remW  = MeasureButtonWidth(loc(L"sc_sm_remove", L"Remove"),        true);
    const int treeW = addW + btnGap + scW + btnGap + remW;
    const int treeX = (clientWidth - treeW) / 2;

    // Section header — centred + bold (hPageTitleFont).
    // SS_NOPREFIX prevents the '&' in "Start Menu & Programs" being consumed
    // as an accelerator-key prefix and rendered as a missing character.
    // ID 5301 lets WM_CTLCOLORSTATIC apply the bold page-title font to the DC.
    std::wstring scSmLabel = loc(L"sc_sm_section_label", L"Start Menu");
    HWND hSmLabel = CreateWindowExW(0, L"STATIC", scSmLabel.c_str(),
        WS_CHILD | WS_VISIBLE | SS_CENTER | SS_NOPREFIX,
        treeX, rowY, treeW, S(22),
        hwnd, (HMENU)5301, hInst, NULL);
    if (hPageTitleFont) SendMessageW(hSmLabel, WM_SETFONT, (WPARAM)hPageTitleFont, TRUE);
    rowY += S(22) + S(6);

    // TreeView — folder icons: index 0 = closed (shell32 #3), 1 = open (shell32 #5).
    // No TVS_FULLROWSELECT — highlight stays tight around icon+text, like the Files page.
    // Item height set to S(34) so 32×32 icons have a little breathing room.
    int smTreeH = S(160);
    s_hScStartMenuTree = CreateWindowExW(WS_EX_CLIENTEDGE, WC_TREEVIEWW, L"",
        WS_CHILD | WS_VISIBLE | WS_VSCROLL | WS_HSCROLL |
        TVS_HASLINES | TVS_LINESATROOT |
        TVS_HASBUTTONS | TVS_SHOWSELALWAYS | TVS_EDITLABELS | TVS_NOTOOLTIPS,
        treeX, rowY, treeW, smTreeH,
        hwnd, (HMENU)IDC_SC_SM_TREE, hInst, NULL);
    // Image list and per-shortcut icons are created by SC_RebuildSmTree() below.
    // SC_TearDown() destroys the list via the "hScSmTreeIL" window property.
    // Apply the same scaled GUI font used on the Files page so text weight and
    // size are consistent across all TreeViews in the app.
    if (hGuiFont) SendMessageW(s_hScStartMenuTree, WM_SETFONT, (WPARAM)hGuiFont, TRUE);

    // Install the custom-tooltip subclass (suppresses system truncation tooltip).
    SetWindowSubclass(s_hScStartMenuTree, SC_SmTreeSubclassProc, 0, 0);

    // Seed default nodes on the very first visit for this project.
    if (s_scMenuNodes.empty()) {
        std::wstring rootName = loc(L"sc_startmenu", L"Start Menu");
        std::wstring progName = loc(L"sc_programs",  L"Programs");
        s_scMenuNodes.push_back({0, -1, rootName, nullptr});
        s_scMenuNodes.push_back({1,  0, progName, nullptr});
    }

    // Re-populate the TreeView from the in-memory node list each page visit.
    // SC_RebuildSmTree also inserts SCT_STARTMENU shortcuts as leaf items (Phase 2).
    SC_RebuildSmTree();
    // Now that both the pin-strip layout coords and the SM tree are ready,
    // populate pin checkboxes and enable/disable the big pin icon buttons.
    SC_RefreshPinStrips(hwnd, hInst);
    rowY += smTreeH + S(6);

    // Action buttons: Add Subfolder (blue) | Add Shortcut Here (green) | Remove (red).
    // Centred under the tree.  Remove and Add Shortcut start disabled;
    // TVN_SELCHANGED enables them appropriately.
    // addW, scW, remW, btnGap are defined above with treeW.
    const int bRowX = treeX;  // tree and button row share the same left edge
    std::wstring scSmAdd = loc(L"sc_sm_add", L"Add Subfolder");
    HWND hSmAddBtn = CreateCustomButtonWithIcon(
        hwnd, IDC_SC_SM_ADD, scSmAdd.c_str(), ButtonColor::Blue,
        L"shell32.dll", 296,
        bRowX, rowY, addW, S(34), hInst);
    {
        std::wstring tt = loc(L"sc_sm_add_tooltip",
            L"Add a subfolder under the selected folder");
        SetButtonTooltip(hSmAddBtn, tt.c_str());
    }
    std::wstring scSmAddSc = loc(L"sc_sm_addsc", L"Add Shortcut");
    HWND hSmAddScBtn = CreateCustomButtonWithCompositeIcon(
        hwnd, IDC_SC_SM_ADDSC, scSmAddSc.c_str(), ButtonColor::Green,
        L"shell32.dll", 257, L"shell32.dll", 29,
        bRowX + addW + btnGap, rowY, scW, S(34), hInst);
    {
        std::wstring tt = loc(L"sc_sm_addsc_tooltip",
            L"Create a shortcut in the selected Start Menu folder");
        SetButtonTooltip(hSmAddScBtn, tt.c_str());
    }
    EnableWindow(hSmAddScBtn, FALSE);  // TVN_SELCHANGED enables when a node is selected
    std::wstring scSmRem = loc(L"sc_sm_remove", L"Remove");
    HWND hSmRemBtn = CreateCustomButtonWithIcon(
        hwnd, IDC_SC_SM_REMOVE, scSmRem.c_str(), ButtonColor::Red,
        L"shell32.dll", 234,
        bRowX + addW + btnGap + scW + btnGap, rowY, remW, S(34), hInst);
    {
        std::wstring tt = loc(L"sc_sm_remove_tooltip",
            L"Remove the selected subfolder from the Start Menu structure");
        SetButtonTooltip(hSmRemBtn, tt.c_str());
    }
    EnableWindow(hSmRemBtn, FALSE);  // TVN_SELCHANGED re-enables for removable nodes
    rowY += S(34) + S(10);
    return rowY;  // absolute Y of first pixel below the page content
}

// ── SC_TearDown ───────────────────────────────────────────────────────────────

void SC_TearDown(HWND hwnd)
{
    // Hide any tooltip that may be showing from the tree hover.
    HideTooltip();
    s_scSmTreeHoveredId = -1;
    s_scSmTreeTracking  = false;

    // Destroy the live TreeView window.
    if (s_hScStartMenuTree && IsWindow(s_hScStartMenuTree)) {
        DestroyWindow(s_hScStartMenuTree);
    }
    s_hScStartMenuTree = NULL;

    // Destroy the Desktop icon.
    if (s_hDesktopIcon) { DestroyIcon(s_hDesktopIcon); s_hDesktopIcon = NULL; }

    // Destroy the Start Menu pin and Taskbar pin icons.
    if (s_hSmPinIcon) { DestroyIcon(s_hSmPinIcon); s_hSmPinIcon = NULL; }
    if (s_hTbPinIcon) { DestroyIcon(s_hTbPinIcon); s_hTbPinIcon = NULL; }

    // Destroy the checkbox bold font.
    if (s_hScCbFont) { DeleteObject(s_hScCbFont); s_hScCbFont = NULL; }

    // Destroy the pin strip checkbox font and reset layout coords.
    if (s_hScPinCbFont) { DeleteObject(s_hScPinCbFont); s_hScPinCbFont = NULL; }
    s_scPinStripY = 0;
    s_scSmPinColX = s_scSmPinColW = 0;
    s_scTbPinColX = s_scTbPinColW = 0;

    // Clear transient HTREEITEM handles; node names and structure persist.
    for (auto& n  : s_scMenuNodes) n.hItem  = nullptr;
    for (auto& sc : s_scShortcuts) if (sc.type == SCT_STARTMENU) sc.hSmItem = nullptr;

    // Destroy the image list stored as a window property on the main window.
    HIMAGELIST hOldIL = (HIMAGELIST)GetPropW(hwnd, L"hScSmTreeIL");
    if (hOldIL) { ImageList_Destroy(hOldIL); RemovePropW(hwnd, L"hScSmTreeIL"); }

    // Explicitly destroy dynamic strip controls (they may be outside the
    // position-based enumeration rect if the page was scrolled).
    for (int i = 0; i < 50; ++i) {
        HWND h = GetDlgItem(hwnd, IDC_SC_DSK_STRIP_BASE + i);
        if (!h) break;
        HICON ico = (HICON)GetWindowLongPtrW(h, GWLP_USERDATA);
        if (ico) DestroyIcon(ico);
        DestroyWindow(h);
    }
    for (int i = 0; i < 50; ++i) {
        HWND h = GetDlgItem(hwnd, IDC_SC_SMPIN_STRIP_BASE + i);
        if (!h) break;
        DestroyWindow(h);
    }
    for (int i = 0; i < 50; ++i) {
        HWND h = GetDlgItem(hwnd, IDC_SC_TBPIN_STRIP_BASE + i);
        if (!h) break;
        DestroyWindow(h);
    }

    // Reset the scroll offset (MSB is detached by SwitchPage teardown in mainwindow.cpp).
    s_scScrollOffset = 0;
}

// ── SC_SetScrollOffset / SC_GetScrollOffset ───────────────────────────────────
// Called by mainwindow.cpp WM_VSCROLL and WM_MOUSEWHEEL handlers to keep the
// offset in sync so SC_RefreshDesktopStrip / SC_RefreshPinStrips can place
// dynamically created controls at the correct physical Y position.
void SC_SetScrollOffset(int off) { s_scScrollOffset = off; }
int  SC_GetScrollOffset()        { return s_scScrollOffset; }
HWND SC_GetStartMenuTree()       { return s_hScStartMenuTree; }

// ── SC_OnResize ───────────────────────────────────────────────────────────────

void SC_OnResize(HWND hwnd, int newWidth)
{
    if (!s_hScStartMenuTree || !IsWindow(s_hScStartMenuTree)) return;

    // Same column formulas as SC_BuildPage.
    int colW  = newWidth / 3;
    int col2W = newWidth - 2 * colW;
    int col0X = 0;
    int col1X = colW;
    int col2X = colW * 2;
    const int iconSz = S(64);
    const int btnGap = S(6);

    // Tree width is fixed (driven by button text widths); just re-center it.
    RECT rcTr; GetWindowRect(s_hScStartMenuTree, &rcTr);
    int treeW = rcTr.right - rcTr.left;
    int treeX = (newWidth - treeW) / 2;

    // Helper: reposition a control to a new x/width keeping its current y/h.
    HDWP hdwp = BeginDeferWindowPos(20);

    auto DeferX = [&](HWND h, int x, int w) {
        if (!h || !IsWindow(h)) return;
        RECT r; GetWindowRect(h, &r);
        MapWindowPoints(HWND_DESKTOP, hwnd, (POINT*)&r, 2);
        hdwp = DeferWindowPos(hdwp, h, NULL, x, r.top, w, r.bottom - r.top,
                               SWP_NOZORDER | SWP_NOACTIVATE);
    };

    // Page title (5300)
    DeferX(GetDlgItem(hwnd, 5300), S(20), newWidth - S(40));

    // 3-column headings
    DeferX(GetDlgItem(hwnd, 5302), col0X, colW);
    DeferX(GetDlgItem(hwnd, 5303), col1X, colW);
    DeferX(GetDlgItem(hwnd, 5304), col2X, col2W);

    // 64×64 icon buttons — re-centre within each column
    DeferX(GetDlgItem(hwnd, IDC_SC_DESKTOP_BTN),    col0X + (colW  - iconSz) / 2, iconSz);
    DeferX(GetDlgItem(hwnd, IDC_SC_PINSTART_BTN),   col1X + (colW  - iconSz) / 2, iconSz);
    DeferX(GetDlgItem(hwnd, IDC_SC_PINTASKBAR_BTN), col2X + (col2W - iconSz) / 2, iconSz);

    // Opt-out checkboxes — keep current width, re-centre in column
    auto DeferCbCenter = [&](HWND h, int cx, int cw) {
        if (!h || !IsWindow(h)) return;
        RECT r; GetWindowRect(h, &r);
        MapWindowPoints(HWND_DESKTOP, hwnd, (POINT*)&r, 2);
        int cbW = r.right - r.left;
        hdwp = DeferWindowPos(hdwp, h, NULL, cx + (cw - cbW) / 2, r.top,
                               cbW, r.bottom - r.top, SWP_NOZORDER | SWP_NOACTIVATE);
    };
    DeferCbCenter(GetDlgItem(hwnd, IDC_SC_DESKTOP_OPT), col0X, colW);
    DeferCbCenter(GetDlgItem(hwnd, IDC_SC_SM_PIN_OPT),  col1X, colW);
    DeferCbCenter(GetDlgItem(hwnd, IDC_SC_TB_PIN_OPT),  col2X, col2W);

    // "Start Menu" section label (5301) — centred over tree
    DeferX(GetDlgItem(hwnd, 5301), treeX, treeW);

    // Start Menu TreeView
    {
        RECT r; GetWindowRect(s_hScStartMenuTree, &r);
        MapWindowPoints(HWND_DESKTOP, hwnd, (POINT*)&r, 2);
        hdwp = DeferWindowPos(hdwp, s_hScStartMenuTree, NULL, treeX, r.top,
                               treeW, r.bottom - r.top, SWP_NOZORDER | SWP_NOACTIVATE);
    }

    // Action buttons under the tree — keep their heights, re-anchor to treeX
    {
        HWND hAdd  = GetDlgItem(hwnd, IDC_SC_SM_ADD);
        HWND hAddSc = GetDlgItem(hwnd, IDC_SC_SM_ADDSC);
        HWND hRem  = GetDlgItem(hwnd, IDC_SC_SM_REMOVE);
        if (hAdd && IsWindow(hAdd) && hAddSc && hRem) {
            RECT rA; GetWindowRect(hAdd,   &rA);
            RECT rS; GetWindowRect(hAddSc, &rS);
            RECT rR; GetWindowRect(hRem,   &rR);
            int addW2  = rA.right - rA.left;
            int scBtnW = rS.right - rS.left;
            int remW2  = rR.right - rR.left;
            MapWindowPoints(HWND_DESKTOP, hwnd, (POINT*)&rA, 2);
            int btnY = rA.top;
            int btnH = rA.bottom - rA.top;
            hdwp = DeferWindowPos(hdwp, hAdd,   NULL,
                treeX, btnY, addW2, btnH, SWP_NOZORDER | SWP_NOACTIVATE);
            hdwp = DeferWindowPos(hdwp, hAddSc, NULL,
                treeX + addW2 + btnGap, btnY, scBtnW, btnH, SWP_NOZORDER | SWP_NOACTIVATE);
            hdwp = DeferWindowPos(hdwp, hRem,   NULL,
                treeX + addW2 + btnGap + scBtnW + btnGap, btnY,
                remW2, btnH, SWP_NOZORDER | SWP_NOACTIVATE);
        }
    }

    EndDeferWindowPos(hdwp);

    // Update stored column layout coords so SC_Refresh* creates controls in the
    // right columns the next time they're called.
    s_scDskCol0X  = col0X;
    s_scDskColW   = colW;
    s_scSmPinColX = col1X;
    s_scSmPinColW = colW;
    s_scTbPinColX = col2X;
    s_scTbPinColW = col2W;

    // Re-create the dynamic strip controls (desktop mini-icons, pin checkboxes)
    // at the new column positions.
    if (s_scHInst) {
        SC_RefreshDesktopStrip(hwnd, s_scHInst);
        SC_RefreshPinStrips(hwnd, s_scHInst);
    }
}

// ── SC_OnNotify ───────────────────────────────────────────────────────────────

LRESULT SC_OnNotify(HWND hwnd, LPNMHDR nmhdr, bool* handled)
{
    *handled = false;

    // Only handle notifications from the Start Menu TreeView.
    if (nmhdr->idFrom != IDC_SC_SM_TREE)
        return 0;

    // Block label editing on the fixed "Start Menu" root node (id == 0).
    // Returning TRUE from TVN_BEGINLABELEDIT cancels the in-place label edit.
    if (nmhdr->code == TVN_BEGINLABELEDIT) {
        LPNMTVDISPINFO ptvdi = (LPNMTVDISPINFO)nmhdr;
        TVITEMW tvi = {}; tvi.mask = TVIF_PARAM; tvi.hItem = ptvdi->item.hItem;
        if (s_hScStartMenuTree) TreeView_GetItem(s_hScStartMenuTree, &tvi);
        *handled = true;
        // Cancel editing for the fixed root (id 0) and for shortcut leaf items (lParam < 0).
        return (tvi.lParam == 0 || tvi.lParam < 0) ? TRUE : FALSE;
    }

    // Persist the renamed label into s_scMenuNodes on label-edit commit.
    // Returning TRUE accepts the new label; FALSE rejects it.
    if (nmhdr->code == TVN_ENDLABELEDIT) {
        LPNMTVDISPINFO ptvdi = (LPNMTVDISPINFO)nmhdr;
        if (!ptvdi->item.pszText || ptvdi->item.pszText[0] == L'\0') {
            *handled = true;
            return FALSE;  // reject empty names
        }
        int nodeId = (int)ptvdi->item.lParam;
        for (auto& node : s_scMenuNodes) {
            if (node.id == nodeId) {
                node.name = ptvdi->item.pszText;
                MainWindow::MarkAsModified();
                break;
            }
        }
        *handled = true;
        return TRUE;  // accept the new label
    }

    // Enable/disable the Remove and Add Shortcut buttons based on selection.
    // Nodes 0 and 1 are fixed and cannot be removed.  Any selected node can
    // receive a shortcut.
    if (nmhdr->code == TVN_SELCHANGED) {
        HWND hRemBtn   = GetDlgItem(hwnd, IDC_SC_SM_REMOVE);
        HWND hAddScBtn = GetDlgItem(hwnd, IDC_SC_SM_ADDSC);
        HWND hAddBtn   = GetDlgItem(hwnd, IDC_SC_SM_ADD);
        if (s_hScStartMenuTree) {
            HTREEITEM hSel = TreeView_GetSelection(s_hScStartMenuTree);
            BOOL canRemove = FALSE;
            BOOL canAddSc  = FALSE;
            BOOL canAddSub = TRUE;  // enabled unless a shortcut item is selected
            if (hSel) {
                TVITEMW tvi = {}; tvi.mask = TVIF_PARAM; tvi.hItem = hSel;
                TreeView_GetItem(s_hScStartMenuTree, &tvi);
                if (tvi.lParam < 0) {
                    // Shortcut leaf item: folder-management buttons don't apply.
                    canRemove = FALSE;
                    canAddSc  = FALSE;
                    canAddSub = FALSE;
                } else {
                    // Folder node: existing rules.
                    canRemove = (tvi.lParam > 1) ? TRUE : FALSE;
                    canAddSc  = TRUE;
                    canAddSub = TRUE;
                }
            }
            if (hRemBtn)   EnableWindow(hRemBtn,   canRemove);
            if (hAddScBtn) EnableWindow(hAddScBtn, canAddSc);
            if (hAddBtn)   EnableWindow(hAddBtn,   canAddSub);
        }
        *handled = true;
        return 0;
    }

    // Double-click on a shortcut leaf item opens the edit dialog.
    // For folder items the default expand/collapse action is preserved.
    if (nmhdr->code == NM_DBLCLK) {
        if (s_hScStartMenuTree) {
            HTREEITEM hSel = TreeView_GetSelection(s_hScStartMenuTree);
            if (hSel) {
                TVITEMW tvi = {}; tvi.mask = TVIF_PARAM; tvi.hItem = hSel;
                TreeView_GetItem(s_hScStartMenuTree, &tvi);
                if (tvi.lParam < 0) {
                    // Shortcut item — dispatch to SC_OnCommand(IDM_SC_CTX_EDIT_SM).
                    SendMessageW(hwnd, WM_COMMAND,
                        MAKEWPARAM(IDM_SC_CTX_EDIT_SM, 0), 0);
                    *handled = true;
                    return TRUE;  // TRUE suppresses default expand/label-edit action
                }
            }
        }
        *handled = false;
        return 0;
    }

    return 0;
}

// ── SC_RefreshPinLabels ────────────────────────────────────────────────────────
// Lightweight helper: updates only the pin status labels and the enable state
// of the two bulk-pin buttons.  Does NOT touch the strip checkboxes themselves,
// so it is safe to call while a checkbox WM_LBUTTONUP is still on the stack.
static void SC_RefreshPinLabels(HWND hwnd)
{
    // Determine whether any eligible shortcuts exist (dedup by exePath).
    // Used only to enable/disable the bulk pin icon buttons.
    bool anyEligible = false;
    std::vector<std::wstring> seenPaths;
    for (const auto& sc : s_scShortcuts) {
        if (sc.type != SCT_DESKTOP && sc.type != SCT_STARTMENU) continue;
        if (sc.exePath.empty()) continue;
        std::wstring lower = sc.exePath;
        for (wchar_t& c : lower) c = towlower(c);
        bool dup = false;
        for (const auto& seen : seenPaths) if (seen == lower) { dup = true; break; }
        if (dup) continue;
        seenPaths.push_back(lower);
        anyEligible = true;
    }
    if (HWND h = GetDlgItem(hwnd, IDC_SC_PINSTART_BTN))   EnableWindow(h, anyEligible);
    if (HWND h = GetDlgItem(hwnd, IDC_SC_PINTASKBAR_BTN)) EnableWindow(h, anyEligible);
}

// ── SC_OnCommand ──────────────────────────────────────────────────────────────

bool SC_OnCommand(HWND hwnd, int id, int wmEvent, HWND hCtrl)
{
    // ── Individual "Pin to Start" strip checkbox toggled ──────────────────────
    // These are dynamic controls (IDC_SC_SMPIN_STRIP_BASE+i) — can't use switch.
    // IMPORTANT: Do NOT call SC_RefreshPinStrips here.  The checkbox's own
    // WM_LBUTTONUP is still on the call stack (WM_COMMAND is sent synchronously
    // by DefSubclassProc).  Destroying the checkbox HWND while processing its
    // WM_LBUTTONUP is UB and causes cross-talk between strip entries.
    // Instead we update s_scShortcuts and refresh only the pin-button states.
    // Use hCtrl (the exact sending HWND from lParam) — more reliable than
    // GetDlgItem which might return a different window if IDs are temporarily
    // ambiguous.  Guard on wmEvent==0 (BN_CLICKED) to ignore focus/hilite
    // notifications that Windows sends for BS_OWNERDRAW buttons.
    if (id >= IDC_SC_SMPIN_STRIP_BASE && id < IDC_SC_SMPIN_STRIP_BASE + 50) {
        if (wmEvent != 0) return true;  // only process BN_CLICKED
        if (hCtrl) {
            int scId    = (int)(INT_PTR)GetPropW(hCtrl, L"ScId");
            bool ticked = (SendMessageW(hCtrl, BM_GETCHECK, 0, 0) == BST_CHECKED);
            for (auto& sc : s_scShortcuts)
                if (sc.id == scId) { sc.pinToStart = ticked; break; }
            SC_RefreshPinLabels(hwnd);
            MainWindow::MarkAsModified();
        }
        return true;
    }

    // ── Individual "Pin to Taskbar" strip checkbox toggled ────────────────────
    if (id >= IDC_SC_TBPIN_STRIP_BASE && id < IDC_SC_TBPIN_STRIP_BASE + 50) {
        if (wmEvent != 0) return true;  // only process BN_CLICKED
        if (hCtrl) {
            int scId    = (int)(INT_PTR)GetPropW(hCtrl, L"ScId");
            bool ticked = (SendMessageW(hCtrl, BM_GETCHECK, 0, 0) == BST_CHECKED);
            for (auto& sc : s_scShortcuts)
                if (sc.id == scId) { sc.pinToTaskbar = ticked; break; }
            SC_RefreshPinLabels(hwnd);
            MainWindow::MarkAsModified();
        }
        return true;
    }

    switch (id) {

    // ── Desktop shortcut button — ALWAYS adds a new shortcut ─────────────────
    case IDC_SC_DESKTOP_BTN: {
        const auto& locMap = MainWindow::GetLocale();
        // Always create a new Desktop ShortcutDef (big icon = add).
        ShortcutDef tmpSc{};
        tmpSc.id   = s_scNextShortcutId++;
        tmpSc.type = SCT_DESKTOP;

        ScDlgResult result;
        HINSTANCE hInst = (HINSTANCE)GetWindowLongPtrW(hwnd, GWLP_HINSTANCE);
        if (SC_EditShortcutDialog(hwnd, hInst, SCT_DESKTOP, L"",
                tmpSc.name, tmpSc.exePath, tmpSc.workingDir,
                tmpSc.iconPath, tmpSc.iconIndex, tmpSc.runAsAdmin,
                locMap, result))
        {
            tmpSc.name       = result.name;
            tmpSc.exePath    = result.exePath;
            tmpSc.workingDir = result.workingDir;
            tmpSc.iconPath   = result.iconPath;
            tmpSc.iconIndex  = result.iconIndex;
            tmpSc.runAsAdmin = result.runAsAdmin;
            s_scShortcuts.push_back(tmpSc);
            MainWindow::MarkAsModified();
            SC_RefreshDesktopStrip(hwnd, hInst);
            SC_RefreshPinStrips(hwnd, hInst);
        } else {
            // Dialog cancelled — reclaim the id.
            --s_scNextShortcutId;
        }
        return true;
    }

    // ── Start Menu / Programs buttons (reserved, not yet standalone) ──────────
    case IDC_SC_STARTMENU_BTN:
        return true;

    case IDC_SC_PROGRAMS_BTN:
        return true;

    // ── Pin-to-Start icon — no action on click (tooltip shown on hover via subclass) ─
    case IDC_SC_PINSTART_BTN:
        return true;

    // ── Pin-to-Taskbar icon — no action on click ──────────────────────────────
    case IDC_SC_PINTASKBAR_BTN:
        return true;

    // ── Desktop opt-out checkbox ──────────────────────────────────────────────
    case IDC_SC_DESKTOP_OPT: {
        HWND hCb = GetDlgItem(hwnd, IDC_SC_DESKTOP_OPT);
        if (hCb) {
            s_scDesktopOptOut = (SendMessageW(hCb, BM_GETCHECK, 0, 0) == BST_CHECKED);
            MainWindow::MarkAsModified();
        }
        return true;
    }

    // ── SM pin opt-out checkbox ───────────────────────────────────────────────
    case IDC_SC_SM_PIN_OPT: {
        HWND hCb = GetDlgItem(hwnd, IDC_SC_SM_PIN_OPT);
        if (hCb) {
            s_scSmPinOptOut = (SendMessageW(hCb, BM_GETCHECK, 0, 0) == BST_CHECKED);
            MainWindow::MarkAsModified();
        }
        return true;
    }

    // ── Taskbar pin opt-out checkbox ──────────────────────────────────────────
    case IDC_SC_TB_PIN_OPT: {
        HWND hCb = GetDlgItem(hwnd, IDC_SC_TB_PIN_OPT);
        if (hCb) {
            s_scTbPinOptOut = (SendMessageW(hCb, BM_GETCHECK, 0, 0) == BST_CHECKED);
            MainWindow::MarkAsModified();
        }
        return true;
    }

    // ── Add Subfolder button (also invoked from right-click context menu) ─────
    case IDC_SC_SM_ADD: {
        if (!s_hScStartMenuTree || !IsWindow(s_hScStartMenuTree)) return true;

        HTREEITEM hSel = TreeView_GetSelection(s_hScStartMenuTree);
        if (!hSel) hSel = TreeView_GetRoot(s_hScStartMenuTree);

        // Determine the parent node id from the selected HTREEITEM.
        int parentId = 0;  // default: child of Start Menu root
        for (const auto& n : s_scMenuNodes)
            if (n.hItem == hSel) { parentId = n.id; break; }

        // Resolve the default "New Subfolder" name from the current locale.
        const auto& locMap = MainWindow::GetLocale();
        auto itNF = locMap.find(L"sc_sm_new_folder");
        std::wstring newName = (itNF != locMap.end()) ? itNF->second : L"New Subfolder";

        int newId = s_scNextMenuId++;
        s_scMenuNodes.push_back({newId, parentId, newName, nullptr});
        ScMenuNode& newNode = s_scMenuNodes.back();

        TVINSERTSTRUCTW tvis     = {};
        tvis.hParent             = hSel;
        tvis.hInsertAfter        = TVI_LAST;
        tvis.item.mask           = TVIF_TEXT | TVIF_IMAGE | TVIF_SELECTEDIMAGE | TVIF_PARAM;
        tvis.item.pszText        = (LPWSTR)newName.c_str();
        tvis.item.iImage         = 0;
        tvis.item.iSelectedImage = 1;
        tvis.item.lParam         = (LPARAM)newId;
        newNode.hItem = (HTREEITEM)SendMessageW(
            s_hScStartMenuTree, TVM_INSERTITEM, 0, (LPARAM)&tvis);
        if (newNode.hItem) {
            TreeView_Expand(s_hScStartMenuTree, hSel, TVE_EXPAND);
            TreeView_SelectItem(s_hScStartMenuTree, newNode.hItem);
            TreeView_EditLabel(s_hScStartMenuTree, newNode.hItem);
            MainWindow::MarkAsModified();
        }
        return true;
    }

    // ── Add Shortcut Here button (also invoked from right-click context menu) ──
    // Always creates a new shortcut in the selected folder.  (Existing shortcuts
    // in the folder are edited by double-clicking their tree item.)
    case IDC_SC_SM_ADDSC: {
        if (!s_hScStartMenuTree || !IsWindow(s_hScStartMenuTree)) return true;

        HTREEITEM hSel = TreeView_GetSelection(s_hScStartMenuTree);
        if (!hSel) return true;

        TVITEMW tvi = {}; tvi.mask = TVIF_PARAM; tvi.hItem = hSel;
        TreeView_GetItem(s_hScStartMenuTree, &tvi);
        if (tvi.lParam < 0) return true;   // shortcut item selected — button is disabled

        int selNodeId = (int)tvi.lParam;
        std::wstring smPath = BuildSmPath(selNodeId);

        // Always add a new shortcut — multiple SC_STARTMENU shortcuts per folder allowed.
        ShortcutDef tmpSc{};
        tmpSc.id       = s_scNextShortcutId++;
        tmpSc.type     = SCT_STARTMENU;
        tmpSc.smNodeId = selNodeId;

        ScDlgResult result;
        HINSTANCE hInst = (HINSTANCE)GetWindowLongPtrW(hwnd, GWLP_HINSTANCE);
        if (SC_EditShortcutDialog(hwnd, hInst, SCT_STARTMENU, smPath,
                tmpSc.name, tmpSc.exePath, tmpSc.workingDir,
                tmpSc.iconPath, tmpSc.iconIndex, tmpSc.runAsAdmin,
                MainWindow::GetLocale(), result))
        {
            tmpSc.name       = result.name;
            tmpSc.exePath    = result.exePath;
            tmpSc.workingDir = result.workingDir;
            tmpSc.iconPath   = result.iconPath;
            tmpSc.iconIndex  = result.iconIndex;
            tmpSc.runAsAdmin = result.runAsAdmin;
            s_scShortcuts.push_back(tmpSc);
            MainWindow::MarkAsModified();
            SC_RebuildSmTree(-(LPARAM)tmpSc.id);  // select the newly added shortcut
            SC_RefreshPinStrips(hwnd, (HINSTANCE)GetWindowLongPtrW(hwnd, GWLP_HINSTANCE));
        } else {
            --s_scNextShortcutId;  // reclaim the unused id
        }
        return true;
    }

    // ── Remove Subfolder button (also invoked from right-click context menu) ──
    case IDC_SC_SM_REMOVE: {
        if (!s_hScStartMenuTree || !IsWindow(s_hScStartMenuTree)) return true;

        HTREEITEM hSel = TreeView_GetSelection(s_hScStartMenuTree);
        if (!hSel) return true;

        TVITEMW tvi = {}; tvi.mask = TVIF_PARAM; tvi.hItem = hSel;
        TreeView_GetItem(s_hScStartMenuTree, &tvi);
        int selId = (int)tvi.lParam;
        if (selId <= 1) return true;  // Start Menu root and Programs are fixed

        // Recursively erase the selected subtree from s_scMenuNodes.
        std::function<void(int)> removeSubtree = [&](int nodeId) {
            for (int i = (int)s_scMenuNodes.size() - 1; i >= 0; --i)
                if (s_scMenuNodes[i].parentId == nodeId)
                    removeSubtree(s_scMenuNodes[i].id);
            // Also erase any shortcuts whose folder was this node.
            s_scShortcuts.erase(
                std::remove_if(s_scShortcuts.begin(), s_scShortcuts.end(),
                    [nodeId](const ShortcutDef& sc){
                        return sc.type == SCT_STARTMENU && sc.smNodeId == nodeId; }),
                s_scShortcuts.end());
            s_scMenuNodes.erase(
                std::remove_if(s_scMenuNodes.begin(), s_scMenuNodes.end(),
                    [nodeId](const ScMenuNode& n){ return n.id == nodeId; }),
                s_scMenuNodes.end());
        };
        removeSubtree(selId);

        // Removing the HTREEITEM also removes all its visual children.
        TreeView_DeleteItem(s_hScStartMenuTree, hSel);
        MainWindow::MarkAsModified();

        // Nothing is selected after the deletion — disable the Remove button.
        HWND hRemBtn = GetDlgItem(hwnd, IDC_SC_SM_REMOVE);
        if (hRemBtn) EnableWindow(hRemBtn, FALSE);
        SC_RefreshPinStrips(hwnd, (HINSTANCE)GetWindowLongPtrW(hwnd, GWLP_HINSTANCE));
        return true;
    }

    // ── SM tree shortcut item: edit ───────────────────────────────────────────
    // Invoked via NM_DBLCLK or the right-click context menu on a shortcut leaf.
    case IDM_SC_CTX_EDIT_SM: {
        if (!s_hScStartMenuTree || !IsWindow(s_hScStartMenuTree)) return true;
        HTREEITEM hSel = TreeView_GetSelection(s_hScStartMenuTree);
        if (!hSel) return true;
        TVITEMW tvi = {}; tvi.mask = TVIF_PARAM; tvi.hItem = hSel;
        TreeView_GetItem(s_hScStartMenuTree, &tvi);
        if (tvi.lParam >= 0) return true;   // not a shortcut item
        int scId = -(int)tvi.lParam;
        ShortcutDef* pSc = nullptr;
        for (auto& sc : s_scShortcuts)
            if (sc.id == scId) { pSc = &sc; break; }
        if (!pSc) return true;

        std::wstring smPath = BuildSmPath(pSc->smNodeId);
        ScDlgResult result;
        HINSTANCE hInst = (HINSTANCE)GetWindowLongPtrW(hwnd, GWLP_HINSTANCE);
        if (SC_EditShortcutDialog(hwnd, hInst, SCT_STARTMENU, smPath,
                pSc->name, pSc->exePath, pSc->workingDir,
                pSc->iconPath, pSc->iconIndex, pSc->runAsAdmin,
                MainWindow::GetLocale(), result))
        {
            pSc->name       = result.name;
            pSc->exePath    = result.exePath;
            pSc->workingDir = result.workingDir;
            pSc->iconPath   = result.iconPath;
            pSc->iconIndex  = result.iconIndex;
            pSc->runAsAdmin = result.runAsAdmin;
            MainWindow::MarkAsModified();
            SC_RebuildSmTree(-(LPARAM)pSc->id);  // reselect the edited shortcut
            SC_RefreshPinStrips(hwnd, (HINSTANCE)GetWindowLongPtrW(hwnd, GWLP_HINSTANCE));
        }
        return true;
    }

    // ── SM tree shortcut item: remove ─────────────────────────────────────────
    case IDM_SC_CTX_REMOVE_SM: {
        if (!s_hScStartMenuTree || !IsWindow(s_hScStartMenuTree)) return true;
        HTREEITEM hSel = TreeView_GetSelection(s_hScStartMenuTree);
        if (!hSel) return true;
        TVITEMW tvi = {}; tvi.mask = TVIF_PARAM; tvi.hItem = hSel;
        TreeView_GetItem(s_hScStartMenuTree, &tvi);
        if (tvi.lParam >= 0) return true;   // not a shortcut item
        int scId = -(int)tvi.lParam;
        // Save parent folder id so we can reselect it after the rebuild.
        LPARAM parentNodeLParam = 0;
        for (const auto& sc : s_scShortcuts)
            if (sc.id == scId) { parentNodeLParam = (LPARAM)sc.smNodeId; break; }
        s_scShortcuts.erase(
            std::remove_if(s_scShortcuts.begin(), s_scShortcuts.end(),
                [scId](const ShortcutDef& sc){ return sc.id == scId; }),
            s_scShortcuts.end());
        MainWindow::MarkAsModified();
        SC_RebuildSmTree(parentNodeLParam);  // reselect parent folder
        SC_RefreshPinStrips(hwnd, (HINSTANCE)GetWindowLongPtrW(hwnd, GWLP_HINSTANCE));
        return true;
    }

    // ── Desktop mini-icon strip: edit existing shortcut ───────────────────────
    // IDs IDC_SC_DSK_STRIP_BASE .. +49. The ScId window property holds sc.id.
    default:
        if (id >= IDC_SC_DSK_STRIP_BASE && id < IDC_SC_DSK_STRIP_BASE + 50) {
            HWND hMini = GetDlgItem(hwnd, id);
            if (!hMini) return false;
            int scId = (int)(INT_PTR)GetPropW(hMini, L"ScId");
            ShortcutDef* pSc = nullptr;
            for (auto& sc : s_scShortcuts)
                if (sc.id == scId) { pSc = &sc; break; }
            if (!pSc) return false;

            const auto& locMap = MainWindow::GetLocale();
            ScDlgResult result;
            HINSTANCE hInst = (HINSTANCE)GetWindowLongPtrW(hwnd, GWLP_HINSTANCE);
            if (SC_EditShortcutDialog(hwnd, hInst, SCT_DESKTOP, L"",
                    pSc->name, pSc->exePath, pSc->workingDir,
                    pSc->iconPath, pSc->iconIndex, pSc->runAsAdmin,
                    locMap, result))
            {
                pSc->name       = result.name;
                pSc->exePath    = result.exePath;
                pSc->workingDir = result.workingDir;
                pSc->iconPath   = result.iconPath;
                pSc->iconIndex  = result.iconIndex;
                pSc->runAsAdmin = result.runAsAdmin;
                MainWindow::MarkAsModified();
                SC_RefreshDesktopStrip(hwnd, hInst);
                SC_RefreshPinStrips(hwnd, hInst);
            }
            return true;
        }
        return false;
    }
}

// ── SC_OnContextMenu ──────────────────────────────────────────────────────────

bool SC_OnContextMenu(HWND hwnd, HWND hCtrl, int x, int y)
{
    // Right-click on the Start Menu folder TreeView.
    if (hCtrl == s_hScStartMenuTree && s_hScStartMenuTree) {
        // Hit-test to find the item under the cursor and select it.
        TVHITTESTINFO ht = {};
        ht.pt.x = x; ht.pt.y = y;
        ScreenToClient(s_hScStartMenuTree, &ht.pt);
        HTREEITEM hItem = TreeView_HitTest(s_hScStartMenuTree, &ht);
        if (hItem)
            TreeView_SelectItem(s_hScStartMenuTree, hItem);
        else
            hItem = TreeView_GetSelection(s_hScStartMenuTree);

        // Determine selected item type (folder node vs. shortcut leaf).
        LPARAM itemLParam = 0;
        BOOL   canRemove  = FALSE;
        if (hItem) {
            TVITEMW tvi = {}; tvi.mask = TVIF_PARAM; tvi.hItem = hItem;
            TreeView_GetItem(s_hScStartMenuTree, &tvi);
            itemLParam = tvi.lParam;
            canRemove  = (itemLParam > 1) ? TRUE : FALSE;
        }

        const auto& locMap = MainWindow::GetLocale();
        auto locS = [&](const wchar_t* k, const wchar_t* fb) -> std::wstring {
            auto it = locMap.find(k); return (it != locMap.end()) ? it->second : fb;
        };

        if (itemLParam < 0) {
            // Right-clicked a shortcut leaf — show Edit / Remove shortcut menu.
            HMENU hMenu = CreatePopupMenu();
            AppendMenuW(hMenu, MF_STRING, IDM_SC_CTX_EDIT_SM,
                locS(L"sc_ctx_edit", L"Edit shortcut\u2026").c_str());
            AppendMenuW(hMenu, MF_STRING, IDM_SC_CTX_REMOVE_SM,
                locS(L"sc_ctx_remove_sc", L"Remove shortcut").c_str());
            int cmd = (int)TrackPopupMenu(hMenu, TPM_RIGHTBUTTON | TPM_RETURNCMD,
                x, y, 0, hwnd, NULL);
            DestroyMenu(hMenu);
            if (cmd == IDM_SC_CTX_EDIT_SM)
                SendMessageW(hwnd, WM_COMMAND, MAKEWPARAM(IDM_SC_CTX_EDIT_SM, 0), 0);
            else if (cmd == IDM_SC_CTX_REMOVE_SM)
                SendMessageW(hwnd, WM_COMMAND, MAKEWPARAM(IDM_SC_CTX_REMOVE_SM, 0), 0);
        } else {
            // Right-clicked a folder node — show folder management menu.
            HMENU hMenu = CreatePopupMenu();
            AppendMenuW(hMenu, MF_STRING, IDM_SC_CTX_ADD_SC,
                locS(L"sc_ctx_add_sc", L"Add shortcut here\u2026").c_str());
            AppendMenuW(hMenu, MF_SEPARATOR, 0, NULL);
            AppendMenuW(hMenu, MF_STRING, IDM_SC_CTX_ADD_SUBFOLDER,
                locS(L"sc_sm_add", L"Add Subfolder").c_str());
            AppendMenuW(hMenu, MF_SEPARATOR, 0, NULL);
            AppendMenuW(hMenu,
                canRemove ? MF_STRING : (MF_STRING | MF_GRAYED),
                IDM_SC_CTX_REMOVE_FOLDER,
                locS(L"sc_sm_remove", L"Remove").c_str());
            int cmd = (int)TrackPopupMenu(hMenu, TPM_RIGHTBUTTON | TPM_RETURNCMD,
                x, y, 0, hwnd, NULL);
            DestroyMenu(hMenu);
            if (cmd == IDM_SC_CTX_ADD_SC)
                SendMessageW(hwnd, WM_COMMAND, MAKEWPARAM(IDC_SC_SM_ADDSC, 0), 0);
            else if (cmd == IDM_SC_CTX_ADD_SUBFOLDER)
                SendMessageW(hwnd, WM_COMMAND, MAKEWPARAM(IDC_SC_SM_ADD, 0), 0);
            else if (cmd == IDM_SC_CTX_REMOVE_FOLDER)
                SendMessageW(hwnd, WM_COMMAND, MAKEWPARAM(IDC_SC_SM_REMOVE, 0), 0);
        }
        return true;
    }

    // Right-click on any of the shortcut row buttons (Desktop, Pin to Start/Taskbar).
    // The shortcut config dialog is not yet implemented; show a greyed-out stub so
    // the right-click infrastructure is in place for the next development session.
    HWND hDesktopBtn    = GetDlgItem(hwnd, IDC_SC_DESKTOP_BTN);
    HWND hPinStartBtn   = GetDlgItem(hwnd, IDC_SC_PINSTART_BTN);
    HWND hPinTaskbarBtn = GetDlgItem(hwnd, IDC_SC_PINTASKBAR_BTN);

    if ((hDesktopBtn    && hCtrl == hDesktopBtn)    ||
        (hPinStartBtn   && hCtrl == hPinStartBtn)   ||
        (hPinTaskbarBtn && hCtrl == hPinTaskbarBtn))
    {
        const auto& locMap = MainWindow::GetLocale();
        auto locS = [&](const wchar_t* k, const wchar_t* fb) -> std::wstring {
            auto it = locMap.find(k); return (it != locMap.end()) ? it->second : fb;
        };
        // Determine which type this control corresponds to.
        int ctxType = SCT_DESKTOP;
        if (hCtrl == hPinStartBtn)   ctxType = SCT_PIN_START;
        if (hCtrl == hPinTaskbarBtn) ctxType = SCT_PIN_TASKBAR;

        HMENU hMenu = CreatePopupMenu();
        AppendMenuW(hMenu, MF_STRING, IDM_SC_CTX_EDIT_SC,
            locS(L"sc_ctx_configure", L"Configure shortcut\u2026").c_str());

        int cmd = (int)TrackPopupMenu(hMenu, TPM_RIGHTBUTTON | TPM_RETURNCMD,
            x, y, 0, hwnd, NULL);
        DestroyMenu(hMenu);

        if (cmd == IDM_SC_CTX_EDIT_SC) {
            // Dispatch through the same SC_OnCommand handler so logic is shared.
            int ctrlId = IDC_SC_DESKTOP_BTN;
            if (ctxType == SCT_PIN_START)   ctrlId = IDC_SC_PINSTART_BTN;
            if (ctxType == SCT_PIN_TASKBAR) ctrlId = IDC_SC_PINTASKBAR_BTN;
            SendMessageW(hwnd, WM_COMMAND, MAKEWPARAM(ctrlId, 0), 0);
        }
        return true;
    }

    return false;
}

// ── SC_SaveToDb ───────────────────────────────────────────────────────────
void SC_SaveToDb(int projectId)
{
    if (projectId <= 0) return;

    // — Opt-out flags (stored in the settings table, keyed by project id) —
    DB::SetSetting(L"sc_desktop_opt_out_"  + std::to_wstring(projectId),
                   s_scDesktopOptOut ? L"1" : L"0");
    DB::SetSetting(L"sc_sm_pin_opt_out_"   + std::to_wstring(projectId),
                   s_scSmPinOptOut   ? L"1" : L"0");
    DB::SetSetting(L"sc_tb_pin_opt_out_"   + std::to_wstring(projectId),
                   s_scTbPinOptOut   ? L"1" : L"0");

    // — Start Menu folder nodes —
    DB::DeleteScMenuNodesForProject(projectId);
    for (const auto& node : s_scMenuNodes) {
        DB::ScMenuNodeRow r;
        r.id         = node.id;
        r.project_id = projectId;
        r.parent_id  = node.parentId;
        r.name       = node.name;
        DB::InsertScMenuNode(projectId, r);
    }

    // — Shortcut definitions (Desktop, Start Menu, Pin to Start, Pin to Taskbar) —
    DB::DeleteScShortcutsForProject(projectId);
    for (const auto& sc : s_scShortcuts) {
        DB::ScShortcutRow r;
        r.id          = sc.id;
        r.project_id  = projectId;
        r.type        = sc.type;
        r.sm_node_id  = sc.smNodeId;
        r.name        = sc.name;
        r.exe_path    = sc.exePath;
        r.working_dir = sc.workingDir;
        r.icon_path   = sc.iconPath;
        r.icon_index  = sc.iconIndex;
        r.run_as_admin  = sc.runAsAdmin  ? 1 : 0;
        r.pin_to_start  = sc.pinToStart  ? 1 : 0;
        r.pin_to_taskbar= sc.pinToTaskbar? 1 : 0;
        DB::InsertScShortcut(projectId, r);
    }
}

// ── SC_LoadFromDb ──────────────────────────────────────────────────────────
void SC_LoadFromDb(int projectId)
{
    if (projectId <= 0) return;
    SC_Reset();  // ensure clean slate before populating

    // — Opt-out flags —
    std::wstring val;
    if (DB::GetSetting(L"sc_desktop_opt_out_" + std::to_wstring(projectId), val))
        s_scDesktopOptOut = (val == L"1");
    if (DB::GetSetting(L"sc_sm_pin_opt_out_"  + std::to_wstring(projectId), val))
        s_scSmPinOptOut   = (val == L"1");
    if (DB::GetSetting(L"sc_tb_pin_opt_out_"  + std::to_wstring(projectId), val))
        s_scTbPinOptOut   = (val == L"1");

    // — Start Menu folder nodes —
    auto nodes = DB::GetScMenuNodesForProject(projectId);
    for (const auto& r : nodes) {
        ScMenuNode node{};
        node.id       = r.id;
        node.parentId = r.parent_id;
        node.name     = r.name;
        node.hItem    = nullptr;
        s_scMenuNodes.push_back(node);
        if (r.id >= s_scNextMenuId) s_scNextMenuId = r.id + 1;
    }

    // — Shortcut definitions —
    auto shortcuts = DB::GetScShortcutsForProject(projectId);
    for (const auto& r : shortcuts) {
        ShortcutDef sc{};
        sc.id          = r.id;
        sc.type        = r.type;
        sc.smNodeId    = r.sm_node_id;
        sc.name        = r.name;
        sc.exePath     = r.exe_path;
        sc.workingDir  = r.working_dir;
        sc.iconPath    = r.icon_path;
        sc.iconIndex   = r.icon_index;
        sc.runAsAdmin  = (r.run_as_admin  != 0);
        sc.pinToStart  = (r.pin_to_start  != 0);
        sc.pinToTaskbar= (r.pin_to_taskbar != 0);
        s_scShortcuts.push_back(sc);
        if (r.id >= s_scNextShortcutId) s_scNextShortcutId = r.id + 1;
    }
}
