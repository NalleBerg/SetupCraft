#pragma once
/*
 * dialogs.h — Public interface for SetupCraft's Dialogs page (page index 4).
 *
 * ── Calling sequence ──────────────────────────────────────────────────────────
 *   On project open/close : call IDLG_Reset() to clear all dialog content state.
 *   On SwitchPage(4)      : call IDLG_BuildPage() to create all page controls.
 *   On SwitchPage away    : call IDLG_TearDown() (called from SwitchPage teardown).
 *   In WM_COMMAND         : call IDLG_OnCommand(); return 0 when it returns true.
 *   On IDM_FILE_SAVE      : call IDLG_SaveToDb(projectId) then IDLG_LoadFromDb(projectId).
 *   On project load       : call IDLG_LoadFromDb(projectId) after IDLG_Reset().
 *
 * ── Persistence ───────────────────────────────────────────────────────────────
 *   Edited RTF content lives in memory until IDM_FILE_SAVE is processed.
 *   IDLG_Reset() is the only thing that clears it.
 *
 * ── Conditionality ────────────────────────────────────────────────────────────
 *   Some dialog rows only appear when their feature is enabled elsewhere:
 *     IDLG_DEPENDENCIES — shown when the project has ≥1 external dependency
 *     IDLG_FOR_ME_ALL   — shown when AskAtInstall (install-scope choice) is enabled
 *     IDLG_COMPONENTS   — shown when the project uses component-based install
 *     IDLG_SHORTCUTS    — shown when any shortcut opt-out checkbox is enabled
 *   These rows appear/disappear instantly: IDLG_BuildPage reads live state.
 *
 * ── Architecture notes ────────────────────────────────────────────────────────
 *   Full implementation in dialogs.cpp, including the inline preview dialog.
 *   Control IDs range: 7000–7109.
 */

#include <windows.h>
#include <commctrl.h>
#include <string>
#include <map>
#include <vector>

// ── Dialog-type enum ──────────────────────────────────────────────────────────
// The order matches the wizard sequence the end user sees at install time.
enum InstallerDialogType {
    IDLG_WELCOME      = 0,
    IDLG_LICENSE      = 1,
    IDLG_DEPENDENCIES = 2,
    IDLG_FOR_ME_ALL   = 3,
    IDLG_COMPONENTS   = 4,
    IDLG_SHORTCUTS    = 5,
    IDLG_READY        = 6,
    IDLG_INSTALL      = 7,
    IDLG_FINISH       = 8,
    IDLG_COUNT        = 9
};

// ── Dialog content record ─────────────────────────────────────────────────────
struct InstallerDialog {
    InstallerDialogType type;
    std::wstring        content_rtf;  // RTF-encoded content; empty = not yet edited
};

// ── Control IDs (range 7000–7109) ─────────────────────────────────────────────
#define IDC_IDLG_PAGE_TITLE  7000    // Page heading STATIC

// Per-row controls: IDC_IDLG_ROW_BASE + (type * 4) + offset
//   offset 0 → 32×32 icon STATIC (owner-drawn)
//   offset 1 → name label STATIC
//   offset 2 → "Edit Content…" button
//   offset 3 → "Preview…" button
// Maximum ID used: 7010 + 8*4 + 3 = 7045
#define IDC_IDLG_ROW_BASE    7010

// Preview dialog internal controls (range 7100–7109)
#define IDC_IDLG_PRV_CONTENT 7100   // RichEdit showing dialog content
#define IDC_IDLG_PRV_BACK    7101   // "◀ Back" button (disabled on first visible dialog)
#define IDC_IDLG_PRV_NEXT    7102   // "Next ▶" / "Finish ✔" navigation button
#define IDC_IDLG_PRV_CANCEL  7103   // "Cancel" — closes preview

// Preview size panel ("sizer") controls (range 7120–7129)
// A small always-on-top floating panel to the left of the preview that lets
// the developer set the installer dialog dimensions (stored to DB on project
// Save; applied as S(value) so the preview always reflects the current DPI).
#define IDC_IDLG_SZR_W_EDIT  7120   // width buddy edit
#define IDC_IDLG_SZR_W_SPIN  7121   // width up-down spinner
#define IDC_IDLG_SZR_H_EDIT  7122   // height buddy edit
#define IDC_IDLG_SZR_H_SPIN  7123   // height up-down spinner

// Installer-title section controls (range 7110–7119)
// Displayed at the top of the Dialogs page, above the dialog-type rows.
// The icon and title here set what appears in the installer's own title bar.
#define IDC_IDLG_INST_ICON_PREVIEW  7110  // 48x48 SS_ICON static – installer icon preview
#define IDC_IDLG_INST_CHANGE_ICON   7111  // "Change Icon…" button
#define IDC_IDLG_INST_TITLE_LABEL   7112  // "Installer title:" label
#define IDC_IDLG_INST_TITLE_EDIT    7113  // edit field for installer title text

// ── Public API ────────────────────────────────────────────────────────────────

// Clear all in-memory dialog content for a new/closed project session.
// Call from MainWindow::Create() before loading any project data.
void IDLG_Reset();

// Build the entire Dialogs page inside hwnd.
//   hwnd            — the main window (all page controls become its direct children).
//   hInst           — application HINSTANCE.
//   pageY           — y-coordinate where the page content area begins.
//   clientWidth     — full client width of hwnd (rc.right from GetClientRect).
//   hPageTitleFont  — semi-bold headline font passed from SwitchPage.
//   hGuiFont        — scaled body font passed from SwitchPage.
//   locale          — current locale string map.
void IDLG_BuildPage(HWND hwnd, HINSTANCE hInst,
                    int pageY, int clientWidth,
                    HFONT hPageTitleFont, HFONT hGuiFont,
                    const std::map<std::wstring,std::wstring>& locale);

// Tear down Dialogs page controls (called from SwitchPage generic teardown).
// Restores icon subclass procs and destroys loaded HICONs.
// Does NOT clear s_dialogs — content survives page switches.
void IDLG_TearDown(HWND hwnd);

// Persist all dialog RTF content to the database.  Called from IDM_FILE_SAVE.
void IDLG_SaveToDb(int projectId);

// Load dialog RTF content from the database into memory.
// Call after IDLG_Reset() when opening an existing project (project.id > 0).
void IDLG_LoadFromDb(int projectId);

// Route WM_COMMAND messages from WndProc.
// Returns true when the command was fully handled (caller should return 0).
bool IDLG_OnCommand(HWND hwnd, int wmId, int wmEvent, HWND hCtrl);

// Initialise in-memory installer title and icon path from project data.
// Call once after IDLG_Reset() and before IDLG_BuildPage() when opening a project.
// title    — default title shown in the installer title bar; typically project name.
// iconPath — path to a .ico file, or empty string to use the default (shell32 #2).
void IDLG_SetInstallerInfo(const std::wstring& title, const std::wstring& iconPath);

// Retrieve current in-memory values for the save handler.
std::wstring IDLG_GetInstallerTitle();
std::wstring IDLG_GetInstallerIconPath();
