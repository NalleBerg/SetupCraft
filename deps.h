#pragma once
/*
 * deps.h — Public interface for SetupCraft's Dependencies page (page index 3).
 *
 * ── Calling sequence ──────────────────────────────────────────────────────────
 *   On project open/close : call DEP_Reset() to clear all dependency state.
 *   On SwitchPage(3)      : call DEP_BuildPage() to create all page controls.
 *   On SwitchPage away    : call DEP_TearDown() (called from SwitchPage teardown).
 *   In WM_NOTIFY          : call DEP_OnNotify(); return its result when handled.
 *   In WM_COMMAND         : call DEP_OnCommand(); return 0 when it returns true.
 *   On IDM_FILE_SAVE      : call DEP_SaveToDb(projectId) then DEP_LoadFromDb(projectId).
 *   On project load       : call DEP_LoadFromDb(projectId) after SC_Reset().
 *
 * ── Persistence ───────────────────────────────────────────────────────────────
 *   State lives in memory until IDM_FILE_SAVE is processed by mainwindow.
 *   s_deps is the canonical list; it survives page switches and is persisted
 *   on explicit Save only.  DEP_Reset() is the only thing that clears it.
 *
 * ── Modularity ────────────────────────────────────────────────────────────────
 *   All implementation is in deps.cpp.  The edit dialog is in
 *   dep_edit_dialog.h / dep_edit_dialog.cpp.  Architecture notes are in
 *   deps_INTERNALS.txt.
 */

#include <windows.h>
#include <commctrl.h>
#include <string>
#include <map>
#include <vector>

// ── Delivery mode ─────────────────────────────────────────────────────────────
// How the dependency reaches the user's machine during runtime install.
enum DepDelivery {
    DD_BUNDLED          = 0,  // included in the installer package itself
    DD_AUTO_DOWNLOAD    = 1,  // installer downloads and runs silently
    DD_REDIRECT_URL     = 2,  // installer opens a download page in the browser
    DD_INSTRUCTIONS_ONLY = 3, // shows a manual-install text to the user
};

// ── Target architecture ───────────────────────────────────────────────────────
enum DepArch {
    DA_ANY   = 0,
    DA_X64   = 1,
    DA_ARM64 = 2,   // x86 (32-bit) is not supported
};

// ── Offline behaviour ─────────────────────────────────────────────────────────
// What the generated installer does when it cannot reach the download URL.
enum DepOffline {
    DO_ABORT         = 0,  // abort install with an error
    DO_WARN_CONTINUE = 1,  // warn the user, then continue without the dep
    DO_SKIP_OPTIONAL = 2,  // silently skip if the dep is marked optional
};

// ── Install order ─────────────────────────────────────────────────────────────
// Named stage in the installer wizard at which this dependency is installed.
// DIO_UNSPECIFIED means the developer left the step unset; "nothing chosen" is
// a valid state — the generated installer will apply a sensible default.
enum DepInstallOrder {
    DIO_UNSPECIFIED     = -1, // not chosen; generated installer decides
    DIO_BEFORE_WELCOME  =  0, // before the Welcome screen appears (silent)
    DIO_AFTER_WELCOME   =  1, // after the Welcome dialog, before the License page
    DIO_BEFORE_INSTALL  =  2, // after License / settings, just before file transfer
    DIO_AFTER_INSTALL   =  3, // after the main program has been installed
    DIO_CUSTOM_DIALOG   =  4, // at a developer-defined custom dialog step
};

// ── The dependency record ─────────────────────────────────────────────────────
struct ExternalDep {
    int          id             = 0;            // DB primary key (0 = not yet saved)
    int          project_id     = 0;            // FK to projects.id
    std::wstring display_name;                  // shown in the ListView and install UI
    bool         is_required    = true;         // false = optional / enhancing
    DepDelivery  delivery       = DD_BUNDLED;
    int          install_order  = (int)DIO_BEFORE_INSTALL; // named install stage (DepInstallOrder)
    std::wstring detect_reg_key;                // HKLM path; empty = no registry check
    std::wstring detect_file_path;              // path check; empty = no file check
    std::wstring min_version;                   // minimum acceptable version string
    DepArch      architecture   = DA_X64;  // app is 64-bit only
    std::wstring url;                           // download or redirect URL
    std::wstring silent_args;                   // e.g. "/quiet /norestart"
    std::wstring sha256;                        // hex SHA-256 of the download
    std::wstring license_path;                  // real-disk path to .rtf or .txt file
    std::wstring license_text;                  // inline license text (stored in DB)
    std::wstring credits_text;                  // short attribution line
    std::vector<std::wstring> instructions_list; // manual-install guidance pages (RTF), in order
    DepOffline   offline_behavior = DO_ABORT;
};

// ── Control IDs (range 6000–6099) ────────────────────────────────────────────
// Listed here so mainwindow.cpp can reference them in WM_DRAWITEM.
#define IDC_DEP_LIST    6000   // report-view ListView of all dependencies
#define IDC_DEP_ADD     6001   // "Add" button  (Green, shell32 257+29 composite)
#define IDC_DEP_EDIT    6002   // "Edit" button (Blue, shell32 87 magnifier)
#define IDC_DEP_REMOVE  6003   // "Remove" button (Red, shell32 131 red X)
#define IDC_DEP_PAGE_TITLE 6100   // STATIC page heading — used by WM_CTLCOLORSTATIC

// ── Context-menu command IDs ──────────────────────────────────────────────────
#define IDM_EXT_DEP_CTX_EDIT   6300
#define IDM_EXT_DEP_CTX_REMOVE 6301

// ── Public functions ──────────────────────────────────────────────────────────

// Clear all in-memory dependency state (call on project open/close).
void DEP_Reset();

// Return true when the in-memory dependency list is non-empty.
// Used by IDLG_BuildPage to decide whether to show the Dependencies dialog row.
bool DEP_HasAny();

// Return a bitmask of every DepDelivery mode present in the current project's
// dependency list.  Bit N is set when at least one dep has delivery == N.
// Used by IDLG_ApplyDefaults to pick the most appropriate default body text
// for the Dependencies installer dialog.
int DEP_GetDeliveryModeMask();

// Build all page controls as children of hwnd.
// pageY        — top of the available page area in hwnd's client coords.
// clientWidth  — current client width of hwnd.
// hPageTitleFont — bold large font for the page heading.
// hGuiFont     — normal body font for all other controls.
// locale       — current locale map (MainWindow::GetLocale()).
// hInst        — application HINSTANCE.
void DEP_BuildPage(HWND hwnd, HINSTANCE hInst,
                   int pageY, int clientWidth,
                   HFONT hPageTitleFont, HFONT hGuiFont,
                   const std::map<std::wstring, std::wstring>& locale);

// Destroy any module-owned resources not cleaned up by the page container.
// Called from SwitchPage teardown.
void DEP_TearDown(HWND hwnd);

// Route WM_COMMAND messages for the Dependencies page.
// Returns true if the message was handled (caller should return 0).
bool DEP_OnCommand(HWND hwnd, int id, int event, HWND hCtrl);

// Route WM_NOTIFY messages for the Dependencies page.
// Sets *handled = true and returns the correct LRESULT when handled.
LRESULT DEP_OnNotify(HWND hwnd, LPNMHDR nmhdr, bool* handled);

// Persist the in-memory list to the database.
void DEP_SaveToDb(int projectId);

// Load the dependency list from the database; replaces in-memory state.
void DEP_LoadFromDb(int projectId);

// Call from mainwindow WM_SIZE after EndDeferWindowPos for the dep list.
void DEP_RepositionScrollbars();
