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
 *   The always-present rows (WELCOME, LICENSE, READY, FINISH) each have an
 *   enable checkbox (IDC_IDLG_ROW_ENABLE_BASE + type).  When unchecked, the
 *   dialog is omitted from the installer script and skipped during preview
 *   navigation.  INSTALL cannot be disabled.
 *
 * ── Architecture notes ────────────────────────────────────────────────────────
 *   Full implementation in dialogs.cpp, including the inline preview dialog.
 *   Control IDs range: 7000–7136; enable checkboxes: 7060–7068;
 *   finish launch section: 7070–7073; ready summary: 7074–7075;
 *   sizer font section: 7128–7133; sizer color section: 7134–7136.
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

// ── Header font / color descriptors ──────────────────────────────────────────
// Returned by IDLG_GetHeaderFont() for [Code] section script generation.
// An empty name or size==0 means "use Inno's built-in default".
struct IdlgHeaderFont {
    std::wstring name;       // font family (empty = Inno default)
    int          size = 0;   // point size  (0 = Inno default)
    bool         bold   = false;
    bool         italic = false;
};
// Sentinel returned by IDLG_GetHeaderFgColor / IDLG_GetHeaderBgColor when no
// color override is set (meaning: use Inno defaults, no [Code] entry emitted).
constexpr COLORREF IDLG_NOCOLOR = (COLORREF)-1;

// ── Control IDs (range 7000–7109) ─────────────────────────────────────────────
#define IDC_IDLG_PAGE_TITLE  7000    // Page heading STATIC

// Per-row controls: IDC_IDLG_ROW_BASE + (type * 4) + offset
//   offset 0 → 32×32 icon STATIC (owner-drawn)
//   offset 1 → name label STATIC  (or absent when an enable checkbox is used)
//   offset 2 → "Edit Content…" button
//   offset 3 → "Preview…" button
// Maximum ID used: 7010 + 8*4 + 3 = 7045; license sub-controls: 7046–7053
#define IDC_IDLG_ROW_BASE    7010

// Per-row enable checkboxes (WELCOME/LICENSE/READY/FINISH only):
//   IDC_IDLG_ROW_ENABLE_BASE + InstallerDialogType  (range 7060–7068)
// When unchecked the dialog is excluded from the installer and skipped during
// preview Back/Next navigation.  The row itself always remains visible so the
// developer can re-enable it at any time.
#define IDC_IDLG_ROW_ENABLE_BASE  7060

// Finish-row sub-controls — "launch app when done" section
// When IDC_IDLG_FINISH_LAUNCH is checked, a [Run] entry is emitted in the Inno
// script with Flags: nowait postinstall shellexec skipifsilent (and optionally
// unchecked if IDC_IDLG_FINISH_LAUNCH_DEFCHK is unchecked).  The description
// text (IDC_IDLG_FINISH_LAUNCH_DESC) becomes the Description parameter.
#define IDC_IDLG_FINISH_LAUNCH          7070  // checkbox: launch the app when installer finishes
#define IDC_IDLG_FINISH_LAUNCH_DESC_LBL 7071  // static label: "Description text:"
#define IDC_IDLG_FINISH_LAUNCH_DESC     7072  // edit field: text shown next to launch checkbox
#define IDC_IDLG_FINISH_LAUNCH_DEFCHK   7073  // checkbox: launch checkbox is checked by default

// Ready-row sub-controls — "show summary of choices" section
// These map directly to Inno [Setup] keys AlwaysShowDirOnReadyPage and
// AlwaysShowGroupOnReadyPage (both default to yes/true in Inno).
#define IDC_IDLG_READY_SHOW_DIR   7074  // checkbox: AlwaysShowDirOnReadyPage
#define IDC_IDLG_READY_SHOW_GROUP 7075  // checkbox: AlwaysShowGroupOnReadyPage

// License-row sub-controls
#define IDC_IDLG_LICENSE_SRC_LBL       7049   // static label: "License source:"
#define IDC_IDLG_LICENSE_SRC           7050   // combobox: Built-in RTF editor / External file
#define IDC_IDLG_LICENSE_FILE_LBL      7051   // static label: "File path:" (visible when external)
#define IDC_IDLG_LICENSE_FILE_EDIT     7052   // read-only edit: path to external .rtf/.txt file
#define IDC_IDLG_LICENSE_FILE_BROWSE   7053   // "Browse…" button to pick the external file
#define IDC_IDLG_LICENSE_ACCEPT        7046   // checkbox: require end-user acceptance before Next
#define IDC_IDLG_LICENSE_TEMPLATE      7047   // combobox: choose a license template to load
#define IDC_IDLG_LICENSE_TEMPLATE_LBL  7048   // static label: "License template:"

// Preview dialog internal controls (range 7100–7109)
#define IDC_IDLG_PRV_CONTENT      7100   // RichEdit showing dialog content
#define IDC_IDLG_PRV_BACK         7101   // "◀ Back" button (disabled on first visible dialog)
#define IDC_IDLG_PRV_NEXT         7102   // "Next ▶" / "Finish ✔" navigation button
#define IDC_IDLG_PRV_CANCEL       7103   // "Cancel" — closes preview
#define IDC_IDLG_PRV_EXTRAS_LABEL 7104   // descriptor label above extras panel
#define IDC_IDLG_PRV_RADIO_ME     7105   // For Me/All Users — "Install just for me"
#define IDC_IDLG_PRV_RADIO_ALL    7106   // For Me/All Users — "Install for all users"
#define IDC_IDLG_PRV_COMP_BASE    7200   // base ID for dynamic component checkboxes (7200+index)

// Preview size panel ("sizer") controls (range 7120–7129)
// A small always-on-top floating panel to the left of the preview that lets
// the developer set the installer dialog dimensions (stored to DB on project
// Save; applied as S(value) so the preview always reflects the current DPI).
#define IDC_IDLG_SZR_W_EDIT  7120   // width buddy edit
#define IDC_IDLG_SZR_W_SPIN  7121   // width up-down spinner
#define IDC_IDLG_SZR_H_EDIT  7122   // height buddy edit
#define IDC_IDLG_SZR_H_SPIN  7123   // height up-down spinner
#define IDC_IDLG_SZR_H_ALIGN 7124   // horizontal alignment combo (Left/Center/Right)
#define IDC_IDLG_SZR_V_ALIGN 7125   // vertical alignment combo (Top/Middle/Bottom)
#define IDC_IDLG_SZR_CLOSE   7126   // "Close" button — saves size and closes preview
#define IDC_IDLG_SZR_RESET   7127   // "Reset" button — clears user-sized flag and auto-fits

// Sizer panel — header font section (range 7128–7133)
// Appended below the size/alignment controls.  Controls the Inno WizardForm
// PageNameLabel font per-dialog or globally via CurPageChanged in [Code].
#define IDC_IDLG_SZR_FONT_GLOBAL  7128  // "Use on all dialogs" checkbox for header font
#define IDC_IDLG_SZR_FONT_NAME    7129  // font family name edit field
#define IDC_IDLG_SZR_FONT_SIZE_E  7130  // font size edit field
#define IDC_IDLG_SZR_FONT_SIZE_S  7131  // font size up/down spinner (0–72 pt)
#define IDC_IDLG_SZR_FONT_BOLD    7132  // Bold checkbox
#define IDC_IDLG_SZR_FONT_ITALIC  7133  // Italic checkbox

// Sizer panel — header color section (range 7134–7136)
// Controls WizardForm.PageNameLabel.Font.Color (fg) and the header panel
// background color (bg) per-dialog or globally via CurPageChanged in [Code].
#define IDC_IDLG_SZR_CLR_GLOBAL   7134  // "Use on all dialogs" checkbox for header colors
#define IDC_IDLG_SZR_CLR_FG       7135  // title text (fg) color swatch button
#define IDC_IDLG_SZR_CLR_BG       7136  // header background (bg) color swatch button
#define IDC_IDLG_SZR_FONT_BROWSE  7137  // "..." button — opens ChooseFontW picker

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

// Build the entire Dialogs page inside hwnd.  Returns the absolute Y coordinate
// (in hwnd client coords) of the first pixel below the last content row — used
// by SwitchPage to size the vertical scrollbar.
//   hwnd            — the main window (all page controls become its direct children).
//   hInst           — application HINSTANCE.
//   pageY           — y-coordinate where the page content area begins.
//   clientWidth     — full client width of hwnd (rc.right from GetClientRect).
//   hPageTitleFont  — semi-bold headline font passed from SwitchPage.
//   hGuiFont        — scaled body font passed from SwitchPage.
//   locale          — current locale string map.
int IDLG_BuildPage(HWND hwnd, HINSTANCE hInst,
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

// Fill any empty in-memory dialog slots with default RTF, substituting
// <<AppName>>, <<AppVersion>>, and <<AppNameAndVersion>> with the given values.
// Call once in Create() after IDLG_LoadFromDb() (or after IDLG_Reset() for new
// projects) so both new and existing projects get sensible starter content.
void IDLG_ApplyDefaults(const std::wstring& appName, const std::wstring& appVersion);

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

// Scroll-offset accessors — used by the main-window WM_VSCROLL / WM_MOUSEWHEEL
// handlers to track the current vertical scroll position while the Dialogs page
// is visible.  IDLG_TearDown resets the offset to 0 on page switch.
void IDLG_SetScrollOffset(int off);
int  IDLG_GetScrollOffset();

// Returns true when the developer has enabled required license acceptance on
// this project (License preview shows the I accept / I do not accept radio pair).
bool IDLG_GetLicenseMustAccept();

// Returns 0 when using the built-in RTF editor, 1 when pointing at an external file.
int IDLG_GetLicenseSource();

// Returns the absolute path to the external license file (only meaningful when
// IDLG_GetLicenseSource() returns 1). Empty string when source is built-in.
std::wstring IDLG_GetLicenseFilePath();

// Returns true when the developer has enabled this dialog page for the installer.
// Always returns true for IDLG_INSTALL (the progress dialog cannot be disabled).
// Only WELCOME, LICENSE, READY, and FINISH have user-facing enable toggles.
bool IDLG_IsDialogEnabled(InstallerDialogType t);

// Returns true when the "Launch app when done" option is enabled for the Finish page.
bool IDLG_GetFinishLaunchEnabled();

// Returns the description text for the launch-app checkbox in the installer.
// Used as the Description parameter of the [Run] entry.  Defaults to
// "Launch {#MyAppName}" when not customised.
std::wstring IDLG_GetFinishLaunchDesc();

// Returns true when the launch-app checkbox should be pre-checked in the installer
// (omit Flags: unchecked).  Returns false when the end user should have to opt in.
bool IDLG_GetFinishLaunchDefaultChecked();

// Returns true when AlwaysShowDirOnReadyPage=yes should be emitted in [Setup].
// Defaults to true (matching Inno's built-in default).
bool IDLG_GetReadyShowDir();

// Returns true when AlwaysShowGroupOnReadyPage=yes should be emitted in [Setup].
// Defaults to true (matching Inno's built-in default).
bool IDLG_GetReadyShowGroup();

// ── Header font / color accessors (for [Code] section generation) ─────────────

// Returns the effective header font spec for dialog type t.
// When IDLG_IsHeaderFontGlobal() is true all types return the same value.
// An empty name / size==0 means the developer has not set that attribute.
IdlgHeaderFont IDLG_GetHeaderFont(InstallerDialogType t);

// Returns the effective header foreground (title text) color for dialog type t.
// Returns IDLG_NOCOLOR when no override is set.
COLORREF IDLG_GetHeaderFgColor(InstallerDialogType t);

// Returns the effective header background color for dialog type t.
// Returns IDLG_NOCOLOR when no override is set.
COLORREF IDLG_GetHeaderBgColor(InstallerDialogType t);

// Returns true when all dialogs share a single header font (vs per-dialog).
bool IDLG_IsHeaderFontGlobal();

// Returns true when all dialogs share a single header color pair (vs per-dialog).
bool IDLG_IsHeaderColorGlobal();
