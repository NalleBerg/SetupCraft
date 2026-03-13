#pragma once
#include <windows.h>
#include <commctrl.h>
#include <string>

// ============================================================================
//  Custom theme-aware checkbox
// ============================================================================
//
//  Creates an owner-drawn BUTTON that looks like a native checkbox but paints
//  its own box + ✓ glyph (U+2713, Segoe UI Symbol) with per-theme colours:
//
//    Light mode   — white box, forest-green tick  (RGB 34,139,34)
//    Dark mode    — dark box, bright-green tick   (RGB 102,204,102)
//    HC Black     — system colours, bright glyph  (COLOR_HOTLIGHT)
//    HC White     — system colours, dark-green    (RGB 0,100,0)
//
//  Hover highlights the border in Windows blue (or system hot-light in HC).
//  A focus rectangle is drawn when the control has keyboard focus.
//
// ----------------------------------------------------------------------------
//  USAGE
// ----------------------------------------------------------------------------
//
//  1. Create the control:
//
//       HWND hChk = CreateCustomCheckbox(hwnd, MY_ID, L"My label",
//                       /*initiallyChecked=*/false,
//                       S(10), S(20), S(300), S(22), hInst);
//       SendMessageW(hChk, WM_SETFONT, (WPARAM)myFont, TRUE);
//
//  2. In the parent's WM_DRAWITEM:
//
//       case WM_DRAWITEM: {
//           LPDRAWITEMSTRUCT dis = (LPDRAWITEMSTRUCT)lParam;
//           if (DrawCustomCheckbox(dis)) return TRUE;
//           // ... other drawing ...
//       }
//
//  3. In the parent's WM_SETTINGCHANGE:
//
//       case WM_SETTINGCHANGE:
//           OnCheckboxSettingChange(hwnd);
//           break;
//
//  4. Read / write checked state anywhere (drop-in for BS_AUTOCHECKBOX):
//
//       bool checked = (SendMessageW(hChk, BM_GETCHECK, 0, 0) == BST_CHECKED);
//       SendMessageW(hChk, BM_SETCHECK, BST_CHECKED, 0);
//
//  5. The BN_CLICKED notification reaches the parent AFTER the internal state
//     has already toggled, so BM_GETCHECK inside the BN_CLICKED handler returns
//     the NEW (post-click) state — identical to BS_AUTOCHECKBOX behaviour.
//
// ----------------------------------------------------------------------------

// Create a theme-aware owner-drawn checkbox.
//
//   hwndParent      — parent window
//   id              — control ID used in WM_COMMAND BN_CLICKED notification
//   label           — text displayed to the right of the box
//   initiallyChecked — tick the box at creation time if true
//   x, y, w, h      — position/size in DPI-scaled pixels (pass S(value))
//   hInst           — module instance
//
// Returns the HWND of the new control, or NULL on failure.
HWND CreateCustomCheckbox(HWND hwndParent, int id, const std::wstring& label,
                           bool initiallyChecked,
                           int x, int y, int width, int height,
                           HINSTANCE hInst);

// Call from the parent's WM_DRAWITEM handler.
// Returns TRUE if dis->hwndItem is a custom checkbox and has been fully drawn.
// Returns FALSE if it is not a custom checkbox (caller should handle normally).
BOOL DrawCustomCheckbox(LPDRAWITEMSTRUCT dis);

// Call from the parent's WM_SETTINGCHANGE handler.
// Enumerates all direct children of hwndParent that are custom checkboxes and
// invalidates them so they repaint with the updated theme colours.
void OnCheckboxSettingChange(HWND hwndParent);

// Build a 3-entry HIMAGELIST ([0] blank / [1] unchecked / [2] checked) drawn
// with GDI using the current theme colours.  Each bitmap is sizePx × sizePx.
// The caller owns the returned handle; destroy it with ImageList_Destroy().
// Designed for use as a TreeView TVSIL_STATE image list.
HIMAGELIST CreateCheckboxStateImageList(int sizePx);

// Convenience wrapper: (re)creates the state image list and assigns it to
// hTreeView via TreeView_SetImageList(... TVSIL_STATE), destroying the old one.
// Call once after TreeView creation and again from WM_SETTINGCHANGE.
void UpdateTreeViewCheckboxImages(HWND hTreeView, int sizePx);
