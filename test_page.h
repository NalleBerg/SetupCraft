#pragma once
/*
 * test_page.h — Public interface for SetupCraft's Test Installer page (page index 7).
 *
 * ── Calling sequence ──────────────────────────────────────────────────────────
 *   On SwitchPage(7)  : call TEST_BuildPage() to create all page controls.
 *   On SwitchPage away: call TEST_TearDown() to clear HWND statics.
 *   In WM_COMMAND     : call TEST_OnCommand(); return 0 when it returns true.
 *   IDM_BUILD_TEST    : call TEST_RunTest(hwnd) directly (F5 shortcut, any page).
 *
 * ── Control IDs ───────────────────────────────────────────────────────────────
 *   Page controls: 8300–8315
 */

#include <windows.h>
#include <string>
#include <map>

// ── Control IDs ───────────────────────────────────────────────────────────────
#define IDC_TEST_PAGE_TITLE        8300   // Static: page title (hPageTitleFont)
#define IDC_TEST_OUTPUT_FOLDER     8301   // Edit: test output folder path
#define IDC_TEST_OUTPUT_FOLDER_BTN 8302   // Button: browse for test output folder
#define IDC_TEST_OUTPUT_FILE       8303   // Edit: test output filename (no extension)
#define IDC_TEST_RUN_BTN           8304   // Button: Run Test (F5)  —  Green
#define IDC_TEST_STATUS            8305   // Static: status / output path display
#define IDC_TEST_OPEN_FOLDER_BTN   8306   // Button: Open test folder  —  Blue
#define IDC_TEST_PROGRESS          8307   // PROGRESS_CLASS: build progress (marquee)
#define IDC_TEST_STEP_LBL          8308   // Static: current build step description
#define IDC_TEST_DETAILS_BTN       8309   // Button: show ISCC output log  —  Blue

// ── Public API ────────────────────────────────────────────────────────────────

// Build the Test page.  Controls are direct children of hwnd (the main window).
// pageY is the absolute Y where the page area starts (below the toolbar).
void TEST_BuildPage(HWND hwnd, HINSTANCE hInst,
                    int pageY, int clientWidth,
                    HFONT hPageTitleFont, HFONT hGuiFont,
                    const std::map<std::wstring, std::wstring>& locale);

// Null HWND statics.  Called from SwitchPage teardown before child-window loop.
void TEST_TearDown();

// Route WM_COMMAND for test-page controls.  Returns true when handled.
bool TEST_OnCommand(HWND hwnd, int wmId, int wmEvent);

// Validate state and prepare the test build.
// Called by the Run Test button AND by IDM_BUILD_TEST (F5) from any page.
// Inno Setup integration (compile + launch) is implemented in the next round.
void TEST_RunTest(HWND hwnd);
