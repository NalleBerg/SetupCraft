#pragma once
/*
 * dep_edit_dialog.h — "Edit Dependency" modal dialog for SetupCraft.
 *
 * DEP_EditDialog() opens a fixed-width modal dialog where the developer
 * fills in all fields of an ExternalDep record: display name, delivery type,
 * detection paths (optional), download URL, SHA-256, silent args, offline
 * behaviour, license file, and credits.
 *
 * ── Caller protocol ──────────────────────────────────────────────────────────
 *   1. Create an ExternalDep (either blank for Add, or a copy for Edit).
 *   2. Call DEP_EditDialog(), passing the record by reference.
 *   3. If the function returns true, the record has been updated with the
 *      user's input; persist it to s_deps and mark as modified.
 *
 * ── Modularity ───────────────────────────────────────────────────────────────
 *   Implementation is in dep_edit_dialog.cpp.
 */

#include "deps.h"
#include <windows.h>
#include <string>
#include <map>
#include <vector>

// Open the "Edit Dependency" modal dialog centred over hwndParent.
// dep        — in/out: caller fills with initial values; written back on OK.
// compNames  — project component display names (sorted). Empty = no component
//              linkage section shown (good default when components aren't used).
// Returns true when the user clicked OK; false when cancelled/closed.
bool DEP_EditDialog(HWND hwndParent, HINSTANCE hInst,
                    const std::map<std::wstring, std::wstring>& locale,
                    ExternalDep& dep,
                    const std::vector<std::wstring>& compNames);

// ── Dialog control IDs ────────────────────────────────────────────────────────
// Scoped to the dep-edit dialog window; no conflict with page control IDs.
#define IDC_DEPDLG_NAME         401
#define IDC_DEPDLG_DELIVERY     402   // combo box
#define IDC_DEPDLG_REQUIRED     403   // custom checkbox
#define IDC_DEPDLG_ARCH         404   // combo box
#define IDC_DEPDLG_OFFLINE      405   // combo box
#define IDC_DEPDLG_INSTALL_ORDER 406  // combo: install-order stage (DepInstallOrder)
#define IDC_DEPDLG_URL          407   // edit
#define IDC_DEPDLG_SILENT_ARGS  408   // edit
#define IDC_DEPDLG_SHA256       409   // edit
#define IDC_DEPDLG_DETECT_REG   410   // edit
#define IDC_DEPDLG_DETECT_FILE  411   // edit
#define IDC_DEPDLG_MIN_VER      412   // edit
#define IDC_DEPDLG_INSTRUCTIONS 413   // multi-line edit
#define IDC_DEPDLG_CREDITS      414   // edit
#define IDC_DEPDLG_LIC_PATH     415   // read-only edit showing the file path
#define IDC_DEPDLG_LIC_BROWSE   416   // "Browse…" button
#define IDC_DEPDLG_OK           417
#define IDC_DEPDLG_CANCEL       418
#define IDC_DEPDLG_EDIT_INSTR   419   // "Add Instructions…" button
#define IDC_DEPDLG_EDIT_LIC     420   // "Edit License…" button
#define IDC_DEPDLG_INSTR_ICON   421   // SS_NOTIFY icon (shell32 #70) shown when instructions exist
#define IDC_DEPDLG_TIMEOUT     422   // download timeout edit (seconds; 0 = no timeout)
#define IDC_DEPDLG_EXIT_CODES       423   // acceptable exit codes edit (space-separated; DD_BUNDLED + DD_AUTO_DOWNLOAD)
#define IDC_DEPDLG_EXIT_CODES_HELP  425   // icon-only help button: opens the common exit codes reference list
#define IDC_DEPDLG_MAX_VER     424   // maximum allowed version edit (optional; empty = no upper bound)
#define IDC_DEPDLG_COMP_EDIT   426   // read-only edit: space-separated linked component names
#define IDC_DEPDLG_COMP_PICK   427   // "…" picker button: opens the component selector
#define IDC_DEPDLG_VER_SOURCE  428   // combo: where the version string is read from (DepVersionSource)
