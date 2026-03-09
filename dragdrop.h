// dragdrop.h  ──  Generic Win32 TreeView / ListView drag-and-drop module
// ============================================================================
// Drop into any Win32 C++ project.  Only Windows.h + commctrl.h required.
//
// Usage in 5 lines:
//   DragDropConfig cfg = {};
//   cfg.hwndParent    = hwnd;
//   cfg.hwndTreeView  = s_hTreeView;
//   cfg.isValidDrop   = [](HTREEITEM h){ return h != myRoot; };
//   cfg.onDrop        = [](HTREEITEM h){ MoveItemTo(h); };
//   DragDrop_Register(cfg);
//
// Then in the parent WndProc:
//   case WM_MOUSEMOVE:    if (DragDrop_OnMouseMove())    return 0; break;
//   case WM_LBUTTONUP:    if (DragDrop_OnLButtonUp())    return 0; break;
//   case WM_SETCURSOR:    if (DragDrop_OnSetCursor())    return TRUE; break;
//   case WM_CAPTURECHANGED: DragDrop_OnCaptureChanged((HWND)lParam); break;
// ============================================================================
#pragma once
#include <windows.h>
#include <commctrl.h>
#include <functional>

// ── Source type of the current drag ─────────────────────────────────────────
enum class DragSourceKind { None, TreeView, ListView };

// ── Configuration struct ─────────────────────────────────────────────────────
struct DragDropConfig {
    // Required
    HWND hwndParent;        // Window that owns SetCapture during drag

    // Controls  (either or both may be non-NULL)
    HWND hwndTreeView;      // TreeView to enable folder drag from
    HWND hwndListView;      // ListView to enable item drag from

    // Cursors  (NULL = auto-load shell32.dll icon 109 / 300)
    HCURSOR hCursorNoDrop;
    HCURSOR hCursorCanDrop;

    // ── Callbacks ──────────────────────────────────────────────────────────
    // canStartTreeDrag  — return false to block a specific TV item from dragging.
    //   hItem: the TreeView item the user pressed LMB on.
    std::function<bool(HTREEITEM hItem)> canStartTreeDrag;

    // canStartListDrag  — return false to block a specific LV row from dragging.
    //   iItem: zero-based ListView row index.
    std::function<bool(int iItem)> canStartListDrag;

    // isValidDrop  — called every WM_MOUSEMOVE and WM_SETCURSOR while dragging.
    //   hDrop: the TreeView item currently under the cursor (NULL if none).
    //   Return true to allow the drop, false to show the no-drop cursor and
    //   suppress the drop-target highlight.
    std::function<bool(HTREEITEM hDrop)> isValidDrop;

    // onDrop  — called when the user releases LMB over a valid target.
    //   hDrop: the TreeView item that was hit-tested as the target.
    //   Access source details via DragDrop_GetSource() / DragDrop_GetTreeItem() /
    //   DragDrop_GetListIndex() inside this callback.
    std::function<void(HTREEITEM hDrop)> onDrop;

    // onCancel (optional) — called when the drag is aborted before a drop
    //   (Escape key, WM_CAPTURECHANGED by another window, RMB during drag, etc.).
    std::function<void()> onCancel;
};

// ── Lifecycle ─────────────────────────────────────────────────────────────────

// Register drag-and-drop.  Subclasses hwndTreeView and/or hwndListView.
// Call after the controls are created.  Safe to call multiple times — the
// previous registration is replaced.
void DragDrop_Register(const DragDropConfig& cfg);

// Unregister and remove all subclasses.
// Call before the controls or parent window are destroyed (e.g. WM_DESTROY).
void DragDrop_Unregister();

// ── Parent-window message hooks ───────────────────────────────────────────────
// Call these from the parent's WndProc.  Each returns true if the message was
// fully handled and the caller should return 0 (or TRUE for WM_SETCURSOR).

// WM_NOTIFY — call before any other notification handling.
// Returns true (and sets up drag state) when TVN_BEGINDRAG or LVN_BEGINDRAG
// is received from the registered controls.
bool DragDrop_OnBeginDrag(NMHDR* nmhdr);

// WM_MOUSEMOVE — updates drop-target highlight.
bool DragDrop_OnMouseMove();

// WM_LBUTTONUP — executes the drop (or cancels if no valid target).
bool DragDrop_OnLButtonUp();

// WM_SETCURSOR — overrides the cursor with can-drop / no-drop icon during drag.
// Call from WM_SETCURSOR before the DefWindowProc default handling.
bool DragDrop_OnSetCursor();

// WM_CAPTURECHANGED — cancels the drag if capture moved to an unrelated window.
// Pass (HWND)lParam as hwndNewCapture.
void DragDrop_OnCaptureChanged(HWND hwndNewCapture);

// ── State queries ─────────────────────────────────────────────────────────────
// These are valid inside onDrop, onCancel, isValidDrop, and between
// DragDrop_Register and DragDrop_Unregister.

bool           DragDrop_IsActive();
DragSourceKind DragDrop_GetSource();
HTREEITEM      DragDrop_GetTreeItem();    // valid when source == TreeView
int            DragDrop_GetListIndex();   // valid when source == ListView

// ── Utilities ─────────────────────────────────────────────────────────────────

// True if hTest is a descendant of hAncestor anywhere in hTree's hierarchy.
// Useful in isValidDrop to prevent dropping a node onto its own subtree.
bool DragDrop_IsDescendant(HWND hTree, HTREEITEM hAncestor, HTREEITEM hTest);

// Force-cancel any active drag (call on WM_KEYDOWN VK_ESCAPE, WM_RBUTTONDOWN, etc.).
// Calls onCancel if registered.
void DragDrop_Cancel();
