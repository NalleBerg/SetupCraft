#pragma once
/*
 * script_edit_dialog.h — "Add/Edit Script" modal dialog for SetupCraft.
 *
 * Control IDs: 7350–7399   (page itself uses 7300–7349)
 */
#include "db.h"
#include <windows.h>
#include <string>
#include <map>
#include <vector>

// Returns true when the user confirmed (OK), false on cancel.
// 'scr' is read for initial values and written back on OK.
// 'existing' is the current list of scripts so duplicate names can be caught.
bool SCR_EditDialog(HWND hwndParent, HINSTANCE hInst,
                    const std::map<std::wstring, std::wstring>& locale,
                    const std::vector<DB::ScriptRow>& existing,
                    DB::ScriptRow& scr);

// ── Control IDs ───────────────────────────────────────────────────────────────
#define IDC_SCRDLG_NAME             7350   // Name edit
#define IDC_SCRDLG_TYPE_BAT         7351   // "Batch (.bat / .cmd)" radio
#define IDC_SCRDLG_TYPE_PS1         7352   // "PowerShell (.ps1)" radio
#define IDC_SCRDLG_WHEN_COMBO       7353   // "When to run" dropdown
#define IDC_SCRDLG_RUN_HIDDEN       7354   // "Run hidden (no console window)" checkbox
#define IDC_SCRDLG_WAIT             7355   // "Wait for completion" checkbox
#define IDC_SCRDLG_ALSO_UNINSTALL   7356   // "Also run at uninstall" checkbox
#define IDC_SCRDLG_FINISH_LABEL_LBL 7357   // Static label for finish-page text field
#define IDC_SCRDLG_FINISH_LABEL     7358   // Edit: label shown on Finish page checkbox
#define IDC_SCRDLG_CONTENT_LBL      7359   // Static "Script content:" label
#define IDC_SCRDLG_CONTENT          7360   // Multiline edit: script body
#define IDC_SCRDLG_LOAD             7361   // "Load from file…" button
#define IDC_SCRDLG_TEST             7362   // "Test in terminal" button
#define IDC_SCRDLG_OK               7363   // OK / Save
#define IDC_SCRDLG_CANCEL           7364   // Cancel
// ── Rename-on-duplicate sub-dialog ───────────────────────────────────────────
#define IDC_SCRDLG_RENAME_MSG       7365   // Static: conflict message
#define IDC_SCRDLG_RENAME_EDIT      7366   // Edit: new name input
#define IDC_SCRDLG_RENAME_OK        7367   // "Rename" button
#define IDC_SCRDLG_RENAME_CANCEL    7368   // Cancel button
