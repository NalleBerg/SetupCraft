#pragma once
/*
 * shortcuts.h — Public interface for SetupCraft's Shortcuts page (page index 2).
 *
 * ── Calling sequence ──────────────────────────────────────────────────────────
 *   On project open/close : call SC_Reset() to clear all shortcuts state.
 *   On SwitchPage(2)      : call SC_BuildPage() to create all page controls.
 *   On SwitchPage away    : call SC_TearDown() to destroy the TreeView + image list.
 *   In WM_NOTIFY          : call SC_OnNotify() and return its result when handled.
 *   In WM_COMMAND         : call SC_OnCommand(); return 0 when it returns true.
 *   In WM_CONTEXTMENU     : call SC_OnContextMenu(); return 0 when it returns true.
 *
 * ── i18n ──────────────────────────────────────────────────────────────────────
 *   All user-visible strings come from the locale map passed to SC_BuildPage()
 *   or fetched at runtime via MainWindow::GetLocale().  No hardcoded English
 *   strings in the final display paths.
 *
 * ── Persistence ───────────────────────────────────────────────────────────────
 *   State lives in memory until IDM_FILE_SAVE is processed by mainwindow.
 *   s_scMenuNodes and s_scShortcuts survive SwitchPage teardown; SC_Reset() is
 *   the only thing that clears them (called when a project is opened or closed).
 */

#include <windows.h>
#include <commctrl.h>
#include <string>
#include <map>
#include <vector>

// ── Embedded icon resource IDs ──────────────────────────────────────────────
#define IDI_TRASHCAN  2   // icons/trashcan_empty.ico — multi-size 16‥128 px

// ── Control IDs ───────────────────────────────────────────────────────────────
// Range 5200–5229 is reserved for the Shortcuts page.
// imageres.dll index 105 = Desktop/monitor icon (verified with IconViewer.exe).
#define IDC_SC_DESKTOP_BTN      5200   // "Desktop" row button
#define IDC_SC_DESKTOP_OPT      5201   // "Allow user to opt out" checkbox
#define IDC_SC_STARTMENU_BTN    5202   // (reserved — no longer a standalone button)
#define IDC_SC_PROGRAMS_BTN     5203   // (reserved — no longer a standalone button)
#define IDC_SC_PINSTART_BTN     5204   // "Pin to Start" row button
#define IDC_SC_PINTASKBAR_BTN   5205   // "Pin to Taskbar" row button
#define IDC_SC_SM_TREE          5206   // Start Menu / Programs folder TreeView
#define IDC_SC_SM_ADD           5207   // "Add Subfolder" button
#define IDC_SC_SM_REMOVE        5208   // "Remove Subfolder" button
#define IDC_SC_SM_ADDSC         5213   // "Add Shortcut Here" button (below SM tree)
#define IDC_SC_DSK_STRIP_BASE    5400   // Desktop shortcut mini-icons 5400–5449 (max 50)
#define IDC_SC_SMPIN_STRIP_BASE  5450   // "Pin to Start" checkboxes 5450–5499 (max 50)
#define IDC_SC_TBPIN_STRIP_BASE  5500   // "Pin to Taskbar" checkboxes 5500–5549 (max 50)
#define IDC_SC_SM_PIN_LABEL     5209   // "Not Pinned / Pinned / Multi Pinned" label under SM pin icon
#define IDC_SC_TB_PIN_LABEL     5210   // "Not Pinned / Pinned / Multi Pinned" label under Taskbar pin icon
#define IDC_SC_SM_PIN_OPT       5211   // "Allow opt-out" checkbox under Start Menu pin icon
#define IDC_SC_TB_PIN_OPT       5212   // "Allow opt-out" checkbox under Taskbar pin icon

// Shortcut Edit dialog control IDs (used by the shortcut-config dialog).
#define IDC_SCDLG_NAME          5220   // Editable shortcut name (pre-filled from filename)
#define IDC_SCDLG_RUN_AS_ADMIN  5221   // "Run as administrator" checkbox
#define IDC_SCDLG_ICON_PREVIEW  5222   // Static owner-drawn icon preview area
#define IDC_SCDLG_ICON_ADD      5223   // "Change Icon…" button
#define IDC_SCDLG_OK            5224   // OK button
#define IDC_SCDLG_CANCEL        5225   // Cancel button
#define IDC_SCDLG_EXE           5226   // Executable path edit
#define IDC_SCDLG_EXE_BROWSE    5227   // Browse executable button
#define IDC_SCDLG_WORKDIR       5228   // Working directory path edit
#define IDC_SCDLG_WORKDIR_BROWSE 5229  // Browse working directory button

// Context menu command IDs for Shortcuts page right-click menus.
#define IDM_SC_CTX_ADD_SUBFOLDER 6300  // SM tree: "Add Subfolder"
#define IDM_SC_CTX_REMOVE_FOLDER 6301  // SM tree: "Remove Subfolder"
#define IDM_SC_CTX_EDIT_SC       6302  // Row button: "Configure shortcut…"
#define IDM_SC_CTX_REMOVE_SC     6303  // Row button: "Remove shortcut"
#define IDM_SC_CTX_ADD_SC        6304  // SM tree: "Add shortcut here…"
#define IDM_SC_CTX_EDIT_DSK      6305  // Desktop mini-icon: "Edit shortcut…"
#define IDM_SC_CTX_REMOVE_DSK    6306  // Desktop mini-icon: "Remove shortcut"
#define IDM_SC_CTX_EDIT_SM       6307  // SM tree shortcut item: "Edit shortcut…"
#define IDM_SC_CTX_REMOVE_SM     6308  // SM tree shortcut item: "Remove shortcut"

// ── Shortcut type constants ────────────────────────────────────────────────────
#define SCT_DESKTOP     0   // Desktop shortcut
#define SCT_STARTMENU   1   // Start Menu folder shortcut (links to an ScMenuNode)
#define SCT_PIN_START   2   // Pin to Start screen
#define SCT_PIN_TASKBAR 3   // Pin to Taskbar

// ── Structs ───────────────────────────────────────────────────────────────────

// One node in the Start Menu / Programs folder tree on the Shortcuts page.
// hItem is transient — non-null only while the Shortcuts page is displayed.
// The in-memory list (s_scMenuNodes) persists for the full project session.
struct ScMenuNode {
    int          id;        // unique node id; 0 = Start Menu root, 1 = Programs
    int          parentId;  // parent node id; -1 means root of the TreeView
    std::wstring name;      // folder display name (editable inline)
    HTREEITEM    hItem;     // live TreeView item handle; nullptr when page is hidden
};

// One shortcut to be created by the installer.
// Persisted to DB on IDM_FILE_SAVE; lives in memory until then.
struct ShortcutDef {
    int          id;          // unique shortcut id (assigned by the shortcut dialog)
    int          type;        // SCT_* constant above
    int          smNodeId;    // ScMenuNode::id for the target folder (-1 if not SM)
    std::wstring name;        // shortcut display name (default: exe name without ext)
    std::wstring exePath;     // target executable (.exe) path
    std::wstring workingDir;  // working directory; defaults to exe's directory
    std::wstring iconPath;    // .ico / .exe / .dll to extract icon from; empty = exe
    int          iconIndex;   // icon index within iconPath (0 = first icon)
    bool         runAsAdmin;  // create shortcut with "Run as administrator" elevation
    bool         pinToStart;   // also pin this shortcut to the Start Menu tile area
    bool         pinToTaskbar; // also pin this shortcut to the Taskbar
    HTREEITEM    hSmItem;     // live TreeView item for SCT_STARTMENU; nullptr when page hidden
};

// ── Public API ────────────────────────────────────────────────────────────────

// Reset all shortcuts state for a new project session.
// Clears s_scMenuNodes, s_scShortcuts, and resets all counters.
// Call from MainWindow::Create() before loading any project data.
void SC_Reset();

// Return true when at least one shortcut opt-out checkbox is enabled.
// Used by IDLG_BuildPage to decide whether to show the Shortcuts dialog row.
bool SC_HasOptOut();

// Build the entire Shortcuts page inside hwnd.
//   hwnd           — the main window (parent of all page controls).
//   hInst          — application HINSTANCE.
//   pageY          — y-coordinate where the page content area begins.
//   clientWidth    — full client width of hwnd (rc.right from GetClientRect).
//   hPageTitleFont — semi-bold headline font passed from SwitchPage.
//   hGuiFont       — scaled body font passed from SwitchPage.
//   locale         — current locale string map (MainWindow::s_locale).
// Returns the absolute Y of the first pixel below the last laid-out row,
// so the caller can compute content height for a vertical scrollbar.
int SC_BuildPage(HWND hwnd, HINSTANCE hInst, int pageY, int clientWidth,
                 HFONT hPageTitleFont, HFONT hGuiFont,
                 const std::map<std::wstring, std::wstring>& locale);

// Scroll-offset accessors — called by mainwindow.cpp WM_VSCROLL / WM_MOUSEWHEEL.
// The offset is the number of pixels the page content has been scrolled upward.
void SC_SetScrollOffset(int off);
int  SC_GetScrollOffset();

// Returns the live HWND of the Start Menu / Programs folder TreeView, or NULL
// if the Shortcuts page is not currently shown.  Used by mainwindow.cpp to
// attach / detach the MSB scrollbars for the tree (s_hMsbScSmTreeV/H).
HWND SC_GetStartMenuTree();

// Tear down the Shortcuts page.
// Destroys the Start Menu TreeView window and its associated image list property.
// s_scMenuNodes and s_scShortcuts are NOT cleared — they survive page switches.
// Call from SwitchPage's generic teardown block.
void SC_TearDown(HWND hwnd);

// Reposition all Shortcuts page controls to match a new client width.
// Call from WM_SIZE when s_currentPageIndex == 2.
void SC_OnResize(HWND hwnd, int newClientWidth);

// Route WM_NOTIFY messages from WndProc.
//   handled (out) — set to true when the notification was consumed.
//   returns       — the LRESULT the WndProc should return when *handled is true.
// When *handled is false the return value is undefined; continue WM_NOTIFY
// processing normally.
LRESULT SC_OnNotify(HWND hwnd, LPNMHDR nmhdr, bool* handled);

// Route WM_COMMAND messages from WndProc.  Returns true when the command was
// fully handled (caller should immediately return 0 from WM_COMMAND).
//   wmEvent — HIWORD(wParam): the notification code (BN_CLICKED = 0, etc.).
//   hCtrl   — (HWND)lParam: the control that sent the notification.
bool SC_OnCommand(HWND hwnd, int id, int wmEvent, HWND hCtrl);

// Route WM_CONTEXTMENU messages from WndProc.  Returns true when handled.
//   hCtrl — (HWND)wParam from WM_CONTEXTMENU (the control that was right-clicked).
//   x, y  — screen coordinates (GET_X_LPARAM / GET_Y_LPARAM from lParam).
bool SC_OnContextMenu(HWND hwnd, HWND hCtrl, int x, int y);

// Persist all shortcuts state (s_scMenuNodes, s_scShortcuts, opt-out flags)
// for the given project to the database.  Called from IDM_FILE_SAVE.
void SC_SaveToDb(int projectId);

// Load shortcuts state from the database into memory (s_scMenuNodes,
// s_scShortcuts, opt-out flags).  Called from MainWindow::Create() after
// SC_Reset() when opening an existing project (project.id > 0).
void SC_LoadFromDb(int projectId);
