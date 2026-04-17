#pragma once
/*
 * settings.h — Public interface for SetupCraft's Settings page (page index 5).
 *
 * ── Calling sequence ──────────────────────────────────────────────────────────
 *   On project open/close  : call SETT_Reset() to clear all settings state.
 *   On SwitchPage(5)       : call SETT_BuildPage() to create all page controls.
 *   On SwitchPage away     : call SETT_TearDown() to clear HWND statics.
 *   In WM_COMMAND          : call SETT_OnCommand(); return 0 when it returns true.
 *                            NOTE: shared-field IDs (IDC_SETT_APP_NAME etc.)
 *                            are handled directly by mainwindow.cpp, not here.
 *   On IDM_FILE_SAVE       : call SETT_SaveToDb(projectId).
 *   On project load        : call SETT_LoadFromDb(projectId) after SETT_Reset().
 *   On WM_MOUSEWHEEL / WM_VSCROLL (page 5):
 *                            use SETT_GetScrollOffset() / SETT_SetScrollOffset().
 *
 * ── Shared fields ─────────────────────────────────────────────────────────────
 *   App name, version, publisher, and icon path are owned by mainwindow.cpp
 *   statics (s_currentProject.name/.version, s_appPublisher, s_appIconPath).
 *   SETT_BuildPage receives them as read-only parameters to pre-fill the edits.
 *   Changes to those controls are handled by mainwindow.cpp EN_CHANGE handlers
 *   (IDC_SETT_APP_NAME, IDC_SETT_APP_VERSION, IDC_SETT_PUBLISHER) and the
 *   IDC_SETT_CHANGE_ICON button handler — exactly like the Registry-page
 *   equivalents (IDC_REG_VERSION, IDC_REG_PUBLISHER, IDC_REG_ADD_ICON).
 *
 * ── New settings ──────────────────────────────────────────────────────────────
 *   Publisher URL, support URL, output filename, compression type/level, solid
 *   compression, UAC privilege level, minimum OS version, allow uninstall, and
 *   close-apps-before-install flag are stored as file-scope statics here and
 *   persisted via DB::GetSetting / DB::SetSetting with project-scoped keys
 *   ("sett_<projectId>_<key>").
 *
 * ── Control ID ranges ─────────────────────────────────────────────────────────
 *   Page controls: 8000–8099
 */

#include <windows.h>
#include <string>
#include <map>

// ── Shared-data section (application identity) ────────────────────────────────
#define IDC_SETT_APP_NAME        8000   // Edit: app name (mirrors s_currentProject.name)
#define IDC_SETT_APP_VERSION     8001   // Edit: version (mirrors s_currentProject.version)
#define IDC_SETT_PUBLISHER       8002   // Edit: publisher (mirrors s_appPublisher)
#define IDC_SETT_PUBLISHER_URL   8003   // Edit: publisher URL (new)
#define IDC_SETT_SUPPORT_URL     8004   // Edit: support URL (new)
#define IDC_SETT_ICON_PREVIEW    8005   // Static SS_ICON: app icon preview
#define IDC_SETT_CHANGE_ICON     8006   // Button: Change Icon…

// ── Build output section ──────────────────────────────────────────────────────
#define IDC_SETT_OUTPUT_FOLDER   8009   // Edit: output folder path
#define IDC_SETT_OUTPUT_FOLDER_BTN 8014 // Button: browse output folder
#define IDC_SETT_OUTPUT_FILE     8010   // Edit: output .exe filename
#define IDC_SETT_COMPRESSION     8011   // Combo: compression type
#define IDC_SETT_COMP_LEVEL      8012   // Combo: compression level 0-9
#define IDC_SETT_SOLID           8013   // Custom checkbox: solid compression

// ── Installation section ──────────────────────────────────────────────────────
#define IDC_SETT_UAC_REQADMIN    8020   // Radio: requireAdministrator
#define IDC_SETT_UAC_INVOKER     8021   // Radio: asInvoker
#define IDC_SETT_UAC_HIGHEST     8022   // Radio: highestAvailable
#define IDC_SETT_MIN_OS          8023   // Combo: minimum OS version

// ── Uninstall section ─────────────────────────────────────────────────────────
#define IDC_SETT_ALLOW_UNINSTALL 8030   // Custom checkbox: allow uninstall
#define IDC_SETT_CLOSE_APPS      8031   // Custom checkbox: close apps before install

// ── Page title (no interaction; just kept in controlIds[]) ────────────────────
#define IDC_SETT_PAGE_TITLE      8099

// ── Public API ────────────────────────────────────────────────────────────────

// Clear new-settings statics to defaults.  Call on project open/close.
void SETT_Reset();

// Build the Settings page.  Controls are direct children of hwnd.
// Returns the absolute Y of the first pixel below the last control (used for
// the scrollbar range calculation — same contract as SC_BuildPage).
// Shared values are used to pre-fill the corresponding edits.
int  SETT_BuildPage(HWND hwnd, HINSTANCE hInst,
                    int pageY, int clientWidth,
                    HFONT hPageTitleFont, HFONT hGuiFont,
                    const std::map<std::wstring, std::wstring>& locale,
                    const std::wstring& appName,
                    const std::wstring& appVersion,
                    const std::wstring& appPublisher,
                    const std::wstring& appIconPath);

// Null HWND statics.  Call from SwitchPage teardown.
void SETT_TearDown(HWND hwnd);

// Route WM_COMMAND for new-settings-only controls.  Returns true when handled.
// Does NOT consume IDC_SETT_APP_NAME / IDC_SETT_APP_VERSION / IDC_SETT_PUBLISHER
// / IDC_SETT_CHANGE_ICON — those are handled by mainwindow.cpp.
bool SETT_OnCommand(HWND hwnd, int wmId, int wmEvent, HWND hCtrl);

// Scroll offset accessors (used by mainwindow.cpp WM_MOUSEWHEEL / WM_VSCROLL).
int  SETT_GetScrollOffset();
void SETT_SetScrollOffset(int offset);

// Persist new settings.
void SETT_SaveToDb(int projectId);

// Load new settings.  Call after SETT_Reset() when opening a project.
void SETT_LoadFromDb(int projectId);
