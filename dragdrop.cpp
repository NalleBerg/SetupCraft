// dragdrop.cpp  ──  Generic Win32 TreeView / ListView drag-and-drop module
// See dragdrop.h and dragdrop_API.txt for usage.
#include "dragdrop.h"
#include <windowsx.h>  // GET_X_LPARAM / GET_Y_LPARAM

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
            g_dd.cursorCanDrop = h ? (HCURSOR)h : LoadCursorW(NULL, IDC_HAND);
        }
    }
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

        LRESULT r = CallWindowProcW(g_dd.prevTVProc, hwnd, msg, wParam, lParam);

        // Grab capture AFTER native returns so WM_MOUSEMOVE always routes to
        // us while the button is held, regardless of what native did.
        if (g_dd.potential == DragSourceKind::TreeView)
            SetCapture(hwnd);

        return r;
    }
    case WM_MOUSEMOVE: {
        // While drag is active, swallow the message — do NOT forward to
        // the native TV proc, because it will call SetCapture(hwndTV) and
        // steal capture back from hwndParent, breaking WM_LBUTTONUP routing.
        if (g_dd.active) return 0;
        if ((wParam & MK_LBUTTON) &&
            g_dd.potential == DragSourceKind::TreeView && !g_dd.active) {
            POINT ptS = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
            ClientToScreen(hwnd, &ptS);
            int dx = abs(ptS.x - g_dd.potStartPt.x);
            int dy = abs(ptS.y - g_dd.potStartPt.y);
            if (dx > GetSystemMetrics(SM_CXDRAG) || dy > GetSystemMetrics(SM_CYDRAG)) {
                TreeView_EndEditLabelNow(hwnd, TRUE);
                EnsureCursors();
                g_dd.active       = true;
                g_dd.source       = DragSourceKind::TreeView;
                g_dd.dragTreeItem = g_dd.potTreeItem;
                g_dd.dragListIdx  = -1;
                g_dd.potential    = DragSourceKind::None;
                SetCapture(g_dd.cfg.hwndParent);
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
        if (g_dd.potential != DragSourceKind::None) {
            g_dd.potential = DragSourceKind::None;
            ReleaseCapture();
        }
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
        if (g_dd.active) return 0;
        if ((wParam & MK_LBUTTON) &&
            g_dd.potential == DragSourceKind::ListView && !g_dd.active) {
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

    POINT ptScreen; GetCursorPos(&ptScreen);
    HTREEITEM hDrop = HitTestTV(ptScreen);
    bool valid = g_dd.cfg.isValidDrop ? g_dd.cfg.isValidDrop(hDrop) : (hDrop != NULL);

    // Update drop-target highlight only when the hovered item changes.
    if (hDrop != g_dd.dropHighlight) {
        if (g_dd.cfg.hwndTreeView)
            TreeView_SelectDropTarget(g_dd.cfg.hwndTreeView, valid ? hDrop : NULL);
        g_dd.dropHighlight = valid ? hDrop : NULL;
    }
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

    // Clear active BEFORE ReleaseCapture so the WM_CAPTURECHANGED that fires
    // synchronously inside ReleaseCapture doesn't trigger DoCancel a second time.
    g_dd.active = false;

    if (valid && g_dd.cfg.onDrop)
        g_dd.cfg.onDrop(hDrop);
    else if (!valid && g_dd.cfg.onCancel)
        g_dd.cfg.onCancel();

    ReleaseCapture();
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
    // Only cancel when capture moves to a window that is not one of our controls.
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
