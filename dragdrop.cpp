// dragdrop.cpp  ──  Generic Win32 TreeView / ListView drag-and-drop module
// See dragdrop.h and dragdrop_API.txt for usage.
#include "dragdrop.h"
#include <windowsx.h>  // GET_X_LPARAM / GET_Y_LPARAM
#include <commctrl.h>
#include <string>

// ── Internal state ────────────────────────────────────────────────────────────
struct DragDropState {
    DragDropConfig cfg = {};

    // Saved WNDPROCs (SetWindowLongPtrW chain, same pattern as mainwindow.cpp)
    WNDPROC prevTVProc = NULL;
    WNDPROC prevLVProc = NULL;

    // Active drag
    bool           active       = false;
    DragSourceKind source       = DragSourceKind::None;
    HTREEITEM      dragTreeItem = NULL;
    int            dragListIdx  = -1;
    HTREEITEM      dropHighlight = NULL;

    // Potential drag (detected in child proc, activated on threshold)
    DragSourceKind potential    = DragSourceKind::None;
    HTREEITEM      potTreeItem  = NULL;
    int            potListIdx   = -1;
    POINT          potStartPt   = {0, 0};

    // Cursors
    HCURSOR cursorNoDrop   = NULL;
    HCURSOR cursorCanDrop  = NULL;

    // Drag image: semi-transparent topmost popup that follows the cursor.
    // NOT ImageList_BeginDrag/DragMove — those render behind DWM-composited windows.
    HWND  hwndDragImage = NULL;
    HFONT hDragFont     = NULL;
};

static DragDropState g_dd;

// ── Cursor loading ────────────────────────────────────────────────────────────
static void EnsureCursors() {
    if (g_dd.cursorNoDrop && g_dd.cursorCanDrop) return;

    // Prefer caller-supplied cursors
    if (g_dd.cfg.hCursorNoDrop)  { g_dd.cursorNoDrop  = g_dd.cfg.hCursorNoDrop; }
    if (g_dd.cfg.hCursorCanDrop) { g_dd.cursorCanDrop = g_dd.cfg.hCursorCanDrop; }

    // Load from shell32.dll via PrivateExtractIconsW (gives true transparency)
    // icon 109 = "forbidden" / no-drop  •  icon 300 = copy/move arrow = can-drop
    if (!g_dd.cursorNoDrop || !g_dd.cursorCanDrop) {
        HMODULE hShell = GetModuleHandleW(L"shell32.dll");
        if (!hShell) hShell = LoadLibraryW(L"shell32.dll");
        typedef UINT (WINAPI *PFN)(LPCWSTR, int, int, int, HICON*, UINT*, UINT, UINT);
        PFN fn = hShell ? (PFN)GetProcAddress(hShell, "PrivateExtractIconsW") : nullptr;
        if (!g_dd.cursorNoDrop) {
            HICON h = NULL; UINT id = 0;
            if (fn) fn(L"shell32.dll", 109, 32, 32, &h, &id, 1, LR_DEFAULTCOLOR);
            g_dd.cursorNoDrop = h ? (HCURSOR)h : LoadCursorW(NULL, IDC_NO);
        }
        if (!g_dd.cursorCanDrop) {
            HICON h = NULL; UINT id = 0;
            if (fn) fn(L"shell32.dll", 300, 32, 32, &h, &id, 1, LR_DEFAULTCOLOR);
            if (h) {
                // Use CopyImage to get a proper cursor from the icon handle
                HCURSOR hc = (HCURSOR)CopyImage(h, IMAGE_CURSOR, 0, 0, LR_DEFAULTSIZE);
                g_dd.cursorCanDrop = hc ? hc : (HCURSOR)h;
            } else {
                g_dd.cursorCanDrop = LoadCursorW(NULL, IDC_HAND);
            }
        }
    }
}

// ── Drag image (WS_EX_LAYERED popup) ─────────────────────────────────────────
//
// We paint a small label with the item name semi-transparently so the user sees
// what they are dragging.  The window is WS_EX_TRANSPARENT (mouse pass-through)
// and WS_EX_LAYERED (per-window alpha).
//
// ── DO NOT REPLACE WITH ImageList_BeginDrag / DragMove ───────────────────────
//  On Windows 10/11 with DWM compositing the ImageList drag surface is rendered
//  BEHIND other windows, making the image invisible.  The layered-window approach
//  always paints on top and cooperates correctly with DWM.
// ─────────────────────────────────────────────────────────────────────────────

static const wchar_t DRAGIMAGE_CLASS[] = L"SetupCraft_DragImage";
static bool s_dragImageClassRegistered = false;

static LRESULT CALLBACK DragImageWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);
        RECT rc; GetClientRect(hwnd, &rc);

        HBRUSH hBr = CreateSolidBrush(GetSysColor(COLOR_HIGHLIGHT));
        FillRect(hdc, &rc, hBr);
        DeleteObject(hBr);

        // Thin border
        HPEN hPen = CreatePen(PS_SOLID, 1, GetSysColor(COLOR_WINDOWTEXT));
        HPEN hOld = (HPEN)SelectObject(hdc, hPen);
        RECT rb = rc; --rb.right; --rb.bottom;
        MoveToEx(hdc, rb.left,  rb.top,    NULL); LineTo(hdc, rb.right, rb.top);
        LineTo  (hdc, rb.right, rb.bottom);       LineTo(hdc, rb.left,  rb.bottom);
        LineTo  (hdc, rb.left,  rb.top);
        SelectObject(hdc, hOld); DeleteObject(hPen);

        SetTextColor(hdc, GetSysColor(COLOR_HIGHLIGHTTEXT));
        SetBkMode(hdc, TRANSPARENT);
        if (g_dd.hDragFont) SelectObject(hdc, g_dd.hDragFont);
        wchar_t buf[260] = {};
        GetWindowTextW(hwnd, buf, _countof(buf));
        RECT rt = { 6, 0, rc.right - 4, rc.bottom };
        DrawTextW(hdc, buf, -1, &rt,
            DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);
        EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_ERASEBKGND: return 1;
    case WM_NCHITTEST:   return HTTRANSPARENT;  // let all mouse events fall through to windows behind
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

static void EnsureDragImageClass() {
    if (s_dragImageClassRegistered) return;
    WNDCLASSEXW wc = {};
    wc.cbSize        = sizeof(wc);
    wc.lpfnWndProc   = DragImageWndProc;
    wc.hInstance     = (HINSTANCE)GetModuleHandleW(NULL);
    wc.lpszClassName = DRAGIMAGE_CLASS;
    RegisterClassExW(&wc);
    s_dragImageClassRegistered = true;
}

// Return label text for the item being dragged.
static std::wstring GetDragLabel() {
    if (g_dd.source == DragSourceKind::TreeView && g_dd.dragTreeItem && g_dd.cfg.hwndTreeView) {
        wchar_t buf[260] = {};
        TVITEMW tvi = {};
        tvi.hItem      = g_dd.dragTreeItem;
        tvi.mask       = TVIF_TEXT;
        tvi.pszText    = buf;
        tvi.cchTextMax = _countof(buf);
        TreeView_GetItem(g_dd.cfg.hwndTreeView, &tvi);
        return std::wstring(L">> ") + buf;  // plain prefix — emoji cause GDI fallback slowness
    }
    if (g_dd.source == DragSourceKind::ListView && g_dd.dragListIdx >= 0 && g_dd.cfg.hwndListView) {
        wchar_t buf[260] = {};
        ListView_GetItemText(g_dd.cfg.hwndListView, g_dd.dragListIdx, 0, buf, _countof(buf));
        return std::wstring(L"> ") + buf;
    }
    return L"";
}

static void StartDragImage(POINT ptScreen) {
    if (g_dd.hwndDragImage) return;
    EnsureDragImageClass();
    std::wstring label = GetDragLabel();
    if (label.empty()) return;

    // Build font once: NONCLIENTMETRICS height + Segoe UI semi-bold.
    // (Same rule as tooltip.cpp — DO NOT use "Segoe UI Variable".)
    if (!g_dd.hDragFont) {
        NONCLIENTMETRICSW ncm = {};
        ncm.cbSize = sizeof(ncm);
        SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(ncm), &ncm, 0);
        LOGFONTW lf = ncm.lfMessageFont;
        lf.lfWeight  = FW_SEMIBOLD;
        lf.lfQuality = CLEARTYPE_QUALITY;
        lf.lfCharSet = DEFAULT_CHARSET;
        wcscpy_s(lf.lfFaceName, L"Segoe UI");
        g_dd.hDragFont = CreateFontIndirectW(&lf);
    }

    // Measure text width
    HDC hdcRef = GetDC(NULL);
    HGDIOBJ hOld = g_dd.hDragFont ? SelectObject(hdcRef, g_dd.hDragFont) : NULL;
    SIZE sz = {};
    GetTextExtentPoint32W(hdcRef, label.c_str(), (int)label.size(), &sz);
    if (hOld) SelectObject(hdcRef, hOld);
    ReleaseDC(NULL, hdcRef);

    int w = sz.cx + 18;
    int h = sz.cy + 10;
    if (w < 60) w = 60;

    // WS_EX_TRANSPARENT is intentionally omitted: it forces sibling windows to
    // repaint before this one, causing the drag image to appear frozen during
    // movement.  Mouse pass-through is handled via WM_NCHITTEST → HTTRANSPARENT.
    g_dd.hwndDragImage = CreateWindowExW(
        WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_LAYERED | WS_EX_NOACTIVATE,
        DRAGIMAGE_CLASS, label.c_str(), WS_POPUP,
        ptScreen.x + 16, ptScreen.y + 4, w, h,
        NULL, NULL, (HINSTANCE)GetModuleHandleW(NULL), NULL);

    if (g_dd.hwndDragImage) {
        SetLayeredWindowAttributes(g_dd.hwndDragImage, 0, 200, LWA_ALPHA); // ~78% opaque
        ShowWindow(g_dd.hwndDragImage, SW_SHOWNOACTIVATE);
    }
}

static void MoveDragImage(POINT ptScreen) {
    if (!g_dd.hwndDragImage) return;
    // Do NOT use SWP_NOZORDER — without it, HWND_TOPMOST is honoured on every
    // call so the image stays above all other windows while dragging.
    // UpdateWindow forces an immediate repaint at the new position instead of
    // waiting for the next WM_PAINT dispatch (prevents visual lag / freeze).
    SetWindowPos(g_dd.hwndDragImage, HWND_TOPMOST,
                 ptScreen.x + 16, ptScreen.y + 4, 0, 0,
                 SWP_NOSIZE | SWP_NOACTIVATE);
    UpdateWindow(g_dd.hwndDragImage);
}

static void EndDragImage() {
    if (!g_dd.hwndDragImage) return;
    DestroyWindow(g_dd.hwndDragImage);
    g_dd.hwndDragImage = NULL;
}

// ── Internal helpers ──────────────────────────────────────────────────────────

// Hit-test the TreeView at a screen-coordinate point.  Returns NULL if no item.
static HTREEITEM HitTestTV(POINT ptScreen) {
    HWND hTree = g_dd.cfg.hwndTreeView;
    if (!hTree || !IsWindow(hTree)) return NULL;
    POINT pt = ptScreen;
    ScreenToClient(hTree, &pt);
    TVHITTESTINFO ht = {};
    ht.pt = pt;
    return TreeView_HitTest(hTree, &ht);
}

// Clear the drop-target highlight on the TreeView.
static void ClearHighlight() {
    if (g_dd.cfg.hwndTreeView && g_dd.dropHighlight)
        TreeView_SelectDropTarget(g_dd.cfg.hwndTreeView, NULL);
    g_dd.dropHighlight = NULL;
}

// Internal cancel: clean up state and optionally fire onCancel callback.
static void DoCancel(bool fireCallback) {
    bool wasActive = g_dd.active;
    EndDragImage();
    ClearHighlight();
    g_dd.active       = false;
    g_dd.source       = DragSourceKind::None;
    g_dd.dragTreeItem = NULL;
    g_dd.dragListIdx  = -1;
    g_dd.potential    = DragSourceKind::None;
    SetCursor(LoadCursorW(NULL, IDC_ARROW));
    if (wasActive && fireCallback && g_dd.cfg.onCancel)
        g_dd.cfg.onCancel();
}

// ── TreeView subclass proc ─────────────────────────────────────────────────────
// WM_MOUSEMOVE flows through the subclass chain even when the native proc
// holds capture (TVS_EDITLABELS grabs it in WM_LBUTTONDOWN).  We use that
// to do a plain threshold check — no SetCapture needed here at all.
static LRESULT CALLBACK TVDragProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg) {
    case WM_LBUTTONDOWN: {
        // Set potential BEFORE calling native so any messages dispatched
        // inside its proc already see the correct state.
        POINT ptC = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
        TVHITTESTINFO ht = {}; ht.pt = ptC;
        HTREEITEM h = TreeView_HitTest(hwnd, &ht);
        if (h && !(ht.flags & TVHT_ONITEMBUTTON)) {
            bool allow = !g_dd.cfg.canStartTreeDrag || g_dd.cfg.canStartTreeDrag(h);
            if (allow) {
                POINT ptS = ptC; ClientToScreen(hwnd, &ptS);
                g_dd.potential   = DragSourceKind::TreeView;
                g_dd.potTreeItem = h;
                g_dd.potListIdx  = -1;
                g_dd.potStartPt  = ptS;
            } else { g_dd.potential = DragSourceKind::None; }
        } else { g_dd.potential = DragSourceKind::None; }

        // Do NOT SetCapture here — grabbing capture on every click prevents
        // toolbar buttons from receiving WM_MOUSEMOVE (hover) until the mouse
        // button is released.  Capture is grabbed only when the drag threshold
        // is actually exceeded (in WM_MOUSEMOVE below).
        return CallWindowProcW(g_dd.prevTVProc, hwnd, msg, wParam, lParam);
    }
    case WM_MOUSEMOVE: {
        // While drag is active: update drag visuals directly here, then swallow.
        // Reason: SetCapture(hwndParent) triggers WM_CAPTURECHANGED on the TV;
        // the native TV proc responds by calling SetCapture(hwndTV) to reclaim
        // capture, so WM_MOUSEMOVE keeps arriving here rather than hwndParent.
        // Calling DragDrop_OnMouseMove() here ensures the image + cursor always
        // update regardless of which window currently holds capture.
        if (g_dd.active) {
            DragDrop_OnMouseMove();
            return 0;
        }
        if ((wParam & MK_LBUTTON) &&
            g_dd.potential == DragSourceKind::TreeView && !g_dd.active) {
            POINT ptS = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
            ClientToScreen(hwnd, &ptS);
            int dx = abs(ptS.x - g_dd.potStartPt.x);
            int dy = abs(ptS.y - g_dd.potStartPt.y);
            if (dx > GetSystemMetrics(SM_CXDRAG) || dy > GetSystemMetrics(SM_CYDRAG)) {
                // Threshold exceeded — NOW grab capture and start drag.
                TreeView_EndEditLabelNow(hwnd, TRUE);
                EnsureCursors();
                g_dd.active       = true;
                g_dd.source       = DragSourceKind::TreeView;
                g_dd.dragTreeItem = g_dd.potTreeItem;
                g_dd.dragListIdx  = -1;
                g_dd.potential    = DragSourceKind::None;
                SetCapture(g_dd.cfg.hwndParent);
                StartDragImage(ptS);
                return 0;  // don't forward — native would steal capture
            }
        }
        break;
    }
    case WM_LBUTTONUP:
        // When drag is active, capture is on hwndParent so WM_LBUTTONUP
        // should go there.  If it somehow arrives here instead, forward it
        // manually so DragDrop_OnLButtonUp runs.
        if (g_dd.active) {
            SendMessageW(g_dd.cfg.hwndParent, WM_LBUTTONUP, wParam, lParam);
            return 0;
        }
        // Clear potential drag state; capture is only held when a real drag
        // was started (active==true above), so no ReleaseCapture needed here.
        g_dd.potential = DragSourceKind::None;
        break;
    }
    return CallWindowProcW(g_dd.prevTVProc, hwnd, msg, wParam, lParam);
}

// ── ListView subclass proc ─────────────────────────────────────────────────────
static LRESULT CALLBACK LVDragProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg) {
    case WM_LBUTTONDOWN: {
        POINT ptC = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
        LVHITTESTINFO ht = {}; ht.pt = ptC;
        int idx = ListView_HitTest(hwnd, &ht);
        if (idx >= 0) {
            bool allow = !g_dd.cfg.canStartListDrag || g_dd.cfg.canStartListDrag(idx);
            if (allow) {
                POINT ptS = ptC; ClientToScreen(hwnd, &ptS);
                g_dd.potential   = DragSourceKind::ListView;
                g_dd.potTreeItem = NULL;
                g_dd.potListIdx  = idx;
                g_dd.potStartPt  = ptS;
            } else { g_dd.potential = DragSourceKind::None; }
        } else { g_dd.potential = DragSourceKind::None; }

        LRESULT r = CallWindowProcW(g_dd.prevLVProc, hwnd, msg, wParam, lParam);

        if (g_dd.potential == DragSourceKind::ListView)
            SetCapture(hwnd);

        return r;
    }
    case WM_MOUSEMOVE: {
        if (g_dd.active) {
            DragDrop_OnMouseMove();
            return 0;
        }
        if ((wParam & MK_LBUTTON) &&
            g_dd.potential == DragSourceKind::ListView && !g_dd.active) {
            if (GetCapture() != hwnd) SetCapture(hwnd);
            POINT ptS = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
            ClientToScreen(hwnd, &ptS);
            int dx = abs(ptS.x - g_dd.potStartPt.x);
            int dy = abs(ptS.y - g_dd.potStartPt.y);
            if (dx > GetSystemMetrics(SM_CXDRAG) || dy > GetSystemMetrics(SM_CYDRAG)) {
                EnsureCursors();
                g_dd.active       = true;
                g_dd.source       = DragSourceKind::ListView;
                g_dd.dragListIdx  = g_dd.potListIdx;
                g_dd.dragTreeItem = NULL;
                g_dd.potential    = DragSourceKind::None;
                SetCapture(g_dd.cfg.hwndParent);
                StartDragImage(ptS);
                return 0;
            }
        }
        break;
    }
    case WM_LBUTTONUP:
        if (g_dd.active) {
            SendMessageW(g_dd.cfg.hwndParent, WM_LBUTTONUP, wParam, lParam);
            return 0;
        }
        if (g_dd.potential != DragSourceKind::None) {
            g_dd.potential = DragSourceKind::None;
            ReleaseCapture();
        }
        break;
    }
    return CallWindowProcW(g_dd.prevLVProc, hwnd, msg, wParam, lParam);
}

// ── Public: WM_NOTIFY hook ────────────────────────────────────────────────────

bool DragDrop_OnBeginDrag(NMHDR* nmhdr) {
    if (!nmhdr) return false;

    // TreeView sends TVN_BEGINDRAG when its internal threshold is exceeded.
    if (nmhdr->hwndFrom == g_dd.cfg.hwndTreeView && nmhdr->code == TVN_BEGINDRAG) {
        LPNMTREEVIEWW pnm = (LPNMTREEVIEWW)nmhdr;
        HTREEITEM h = pnm->itemNew.hItem;
        bool allow = !g_dd.cfg.canStartTreeDrag || g_dd.cfg.canStartTreeDrag(h);
        if (allow) {
            EnsureCursors();
            g_dd.active       = true;
            g_dd.source       = DragSourceKind::TreeView;
            g_dd.dragTreeItem = h;
            g_dd.dragListIdx  = -1;
            SetCapture(g_dd.cfg.hwndParent);
            POINT pt; GetCursorPos(&pt);
            StartDragImage(pt);
        }
        return true;  // handled — return 0 from WM_NOTIFY
    }

    // ListView sends LVN_BEGINDRAG when its internal threshold is exceeded.
    if (nmhdr->hwndFrom == g_dd.cfg.hwndListView && nmhdr->code == LVN_BEGINDRAG) {
        LPNMLISTVIEW pnm = (LPNMLISTVIEW)nmhdr;
        int idx = pnm->iItem;
        bool allow = !g_dd.cfg.canStartListDrag || g_dd.cfg.canStartListDrag(idx);
        if (allow) {
            EnsureCursors();
            g_dd.active       = true;
            g_dd.source       = DragSourceKind::ListView;
            g_dd.dragListIdx  = idx;
            g_dd.dragTreeItem = NULL;
            SetCapture(g_dd.cfg.hwndParent);
            POINT pt; GetCursorPos(&pt);
            StartDragImage(pt);
        }
        return true;
    }

    return false;
}

// ── Public: Lifecycle ─────────────────────────────────────────────────────────

void DragDrop_Register(const DragDropConfig& cfg) {
    // Tear down any previous registration first.
    DragDrop_Unregister();

    g_dd.cfg           = cfg;
    g_dd.cursorNoDrop  = cfg.hCursorNoDrop;
    g_dd.cursorCanDrop = cfg.hCursorCanDrop;

    // Use SetWindowLongPtrW (same pattern as mainwindow.cpp) — no comctl32 v6
    // required.  The new proc is installed on TOP of whatever proc is current,
    // so the chain is: TVDragProc → prevTVProc (e.g. TreeView_SubclassProc) → native.
    if (cfg.hwndTreeView && IsWindow(cfg.hwndTreeView))
        g_dd.prevTVProc = (WNDPROC)SetWindowLongPtrW(
            cfg.hwndTreeView, GWLP_WNDPROC, (LONG_PTR)TVDragProc);
    if (cfg.hwndListView && IsWindow(cfg.hwndListView))
        g_dd.prevLVProc = (WNDPROC)SetWindowLongPtrW(
            cfg.hwndListView, GWLP_WNDPROC, (LONG_PTR)LVDragProc);
}

void DragDrop_Unregister() {
    if (g_dd.active) DoCancel(false);  // cancel without firing callback
    EndDragImage();
    if (g_dd.hDragFont) { DeleteObject(g_dd.hDragFont); g_dd.hDragFont = NULL; }
    // Restore saved WNDPROCs only if the window is still alive.
    if (g_dd.prevTVProc && g_dd.cfg.hwndTreeView && IsWindow(g_dd.cfg.hwndTreeView))
        SetWindowLongPtrW(g_dd.cfg.hwndTreeView, GWLP_WNDPROC, (LONG_PTR)g_dd.prevTVProc);
    if (g_dd.prevLVProc && g_dd.cfg.hwndListView && IsWindow(g_dd.cfg.hwndListView))
        SetWindowLongPtrW(g_dd.cfg.hwndListView, GWLP_WNDPROC, (LONG_PTR)g_dd.prevLVProc);
    g_dd = DragDropState{};  // reset everything
}

// ── Public: Parent-window message hooks ───────────────────────────────────────

bool DragDrop_OnMouseMove() {
    if (!g_dd.active) return false;

    EnsureCursors();
    POINT ptScreen; GetCursorPos(&ptScreen);
    HTREEITEM hDrop = HitTestTV(ptScreen);
    bool valid = g_dd.cfg.isValidDrop ? g_dd.cfg.isValidDrop(hDrop) : (hDrop != NULL);

    // ── Cursor ───────────────────────────────────────────────────────────────
    // Set cursor directly here — WM_SETCURSOR is unreliable when the parent
    // window holds mouse capture (OS may skip cursor queries during capture).
    SetCursor(valid ? g_dd.cursorCanDrop : g_dd.cursorNoDrop);

    // ── Drop-target highlight ────────────────────────────────────────────────
    HTREEITEM newHi = valid ? hDrop : NULL;
    if (newHi != g_dd.dropHighlight) {
        if (g_dd.cfg.hwndTreeView)
            TreeView_SelectDropTarget(g_dd.cfg.hwndTreeView, newHi);
        g_dd.dropHighlight = newHi;
    }

    // ── Drag image ───────────────────────────────────────────────────────────
    MoveDragImage(ptScreen);

    return true;
}

bool DragDrop_OnLButtonUp() {
    if (!g_dd.active) {
        g_dd.potential = DragSourceKind::None;  // clear any stale potential
        return false;
    }

    POINT ptScreen; GetCursorPos(&ptScreen);
    HTREEITEM hDrop = HitTestTV(ptScreen);
    bool valid = g_dd.cfg.isValidDrop ? g_dd.cfg.isValidDrop(hDrop) : (hDrop != NULL);

    ClearHighlight();
    EndDragImage();

    // Clear active and release capture BEFORE calling onDrop.
    // onDrop may show a modal dialog (TaskDialogIndirect); if capture is still
    // held during the modal loop all mouse messages go to hwndParent instead of
    // the dialog, causing the app to appear frozen.
    g_dd.active = false;
    ReleaseCapture();

    if (valid && g_dd.cfg.onDrop)
        g_dd.cfg.onDrop(hDrop);
    else if (!valid && g_dd.cfg.onCancel)
        g_dd.cfg.onCancel();

    DoCancel(false);   // full state reset — active already false, won't re-fire callback

    // Force the cursor to restore immediately without waiting for next WM_SETCURSOR.
    SetCursor(LoadCursorW(NULL, IDC_ARROW));
    return true;
}

bool DragDrop_OnSetCursor() {
    if (!g_dd.active) return false;
    EnsureCursors();
    POINT ptScreen; GetCursorPos(&ptScreen);
    HTREEITEM hDrop  = HitTestTV(ptScreen);
    bool valid = g_dd.cfg.isValidDrop ? g_dd.cfg.isValidDrop(hDrop) : (hDrop != NULL);
    SetCursor(valid ? g_dd.cursorCanDrop : g_dd.cursorNoDrop);
    return true;
}

void DragDrop_OnCaptureChanged(HWND hwndNewCapture) {
    if (!g_dd.active) return;
    // NULL = transient — someone is mid-SetCapture, don't cancel.
    if (hwndNewCapture == NULL) return;
    // Our own controls re-capturing is fine too.
    if (hwndNewCapture == g_dd.cfg.hwndTreeView ||
        hwndNewCapture == g_dd.cfg.hwndListView) return;
    DoCancel(true);
}

// ── Public: State queries ─────────────────────────────────────────────────────

bool           DragDrop_IsActive()     { return g_dd.active; }
DragSourceKind DragDrop_GetSource()    { return g_dd.source; }
HTREEITEM      DragDrop_GetTreeItem()  { return g_dd.dragTreeItem; }
int            DragDrop_GetListIndex() { return g_dd.dragListIdx; }

// ── Public: Utilities ─────────────────────────────────────────────────────────

bool DragDrop_IsDescendant(HWND hTree, HTREEITEM hAncestor, HTREEITEM hTest) {
    HTREEITEM h = hTest;
    while (h) {
        h = TreeView_GetParent(hTree, h);
        if (h == hAncestor) return true;
    }
    return false;
}

void DragDrop_Cancel() {
    if (g_dd.active) {
        ReleaseCapture();
        DoCancel(true);
    }
}
