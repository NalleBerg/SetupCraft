#pragma once
/*
 * scripts.h — Public interface for SetupCraft's Scripts page (page index 8).
 *
 * ── Calling sequence ──────────────────────────────────────────────────────────
 *   On project open/close : call SCR_Reset() to clear all scripts state.
 *   On SwitchPage(8)      : call SCR_BuildPage() to create all page controls.
 *   On SwitchPage away    : call SCR_TearDown() to destroy page controls.
 *   In WM_COMMAND         : call SCR_OnCommand(); return 0 when it returns true.
 *   In WM_NOTIFY          : call SCR_OnNotify(); return its result when handled.
 *   On IDM_FILE_SAVE      : call SCR_SaveToDb(projectId).
 *   On project load       : call SCR_LoadFromDb(projectId) after SCR_Reset().
 *
 * ── Persistence ───────────────────────────────────────────────────────────────
 *   Script records live in s_scripts (vector<ScriptRow>) until IDM_FILE_SAVE.
 *   Stored in the `scripts` DB table (see db.h ScriptRow / DB::InsertScript).
 *   SCR_Reset() is the only thing that clears in-memory state.
 *
 * ── Architecture notes ────────────────────────────────────────────────────────
 *   Page:    scripts.cpp             — ListView tile grid, toolbar, checkbox.
 *   Editor:  script_edit_dialog.cpp  — modal popup for add/edit.
 *   DB:      db.cpp ScriptRow CRUD   — InsertScript / DeleteScriptsForProject /
 *                                      GetScriptsForProject.
 *   Page control IDs:   7300–7349.
 *   Editor control IDs: 7350–7399 (see script_edit_dialog.h).
 */

#include <windows.h>
#include <commctrl.h>
#include <string>
#include <map>

// ── Script type constants ─────────────────────────────────────────────────────
#define SCR_TYPE_BAT  0   // .bat / cmd
#define SCR_TYPE_PS1  1   // .ps1 / PowerShell

// ── When-to-run (maps to Inno Setup sections/flags) ───────────────────────────
//   SWR_BEFORE_FILES  → [Code] CurStepChanged(ssInstall) before file copy
//   SWR_AFTER_FILES   → [Run] (unconditional, no postinstall flag)
//   SWR_FINISH_OPTOUT → [Run] Flags: postinstall skipifsilent (Finish checkbox)
//   SWR_UNINSTALL     → [UninstallRun]
enum ScrWhenToRun {
    SWR_BEFORE_FILES  = 0,
    SWR_AFTER_FILES   = 1,
    SWR_FINISH_OPTOUT = 2,
    SWR_UNINSTALL     = 3,
};

// ── Page control IDs (7300–7349) ──────────────────────────────────────────────
#define IDC_SCR_PAGE_TITLE      7300
#define IDC_SCR_MASTER_ENABLE   7301   // master enable custom checkbox
#define IDC_SCR_ENABLE_HINT     7302   // hint label shown when master is off
#define IDC_SCR_TOOLBAR_ADD     7303   // [+ Add Script] button
#define IDC_SCR_TOOLBAR_LOAD    7304   // [Load from file…] button
#define IDC_SCR_TOOLBAR_EDIT    7305   // [Edit] button
#define IDC_SCR_TOOLBAR_DELETE  7306   // [Delete] button
#define IDC_SCR_LIST            7307   // ListView (LVS_ICON / large-icon mode)
#define IDC_SCR_EMPTY_HINT      7308   // "No scripts yet" label

// ── Public API ────────────────────────────────────────────────────────────────

// Clear all in-memory state.  Call before loading any project data.
void SCR_Reset();

// Build the Scripts page.  All controls are direct children of hwnd.
void SCR_BuildPage(HWND hwnd, HINSTANCE hInst,
                   int pageY, int clientWidth,
                   HFONT hPageTitleFont, HFONT hGuiFont,
                   const std::map<std::wstring, std::wstring>& locale);

// Destroy page controls and free the icon ImageList.
void SCR_TearDown(HWND hwnd);

// Route WM_COMMAND.  Returns true when handled.
bool SCR_OnCommand(HWND hwnd, int wmId, int wmEvent, HWND hCtrl);

// Route WM_NOTIFY.  Sets *handled = true and returns the LRESULT when handled.
LRESULT SCR_OnNotify(HWND hwnd, LPNMHDR nmhdr, bool* handled);

// Persist all scripts to the `scripts` DB table.
void SCR_SaveToDb(int projectId);

// Load scripts from the DB into memory and refresh the ListView.
// Call after SCR_Reset() on project open.
void SCR_LoadFromDb(int projectId);
