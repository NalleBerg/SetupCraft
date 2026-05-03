#pragma once
/*
 * sc_shortcut_dialog.h — "Configure Shortcut" modal dialog for SetupCraft.
 *
 * SC_EditShortcutDialog() opens a fixed-width modal dialog where the developer
 * sets shortcut properties: display name, icon, and run-as-administrator flag.
 * For SCT_STARTMENU shortcuts a read-only path label shows the target folder.
 *
 * The dialog is triggered by:
 *   - Left-clicking a shortcut type icon on the Shortcuts page
 *   - Right-clicking a Start Menu tree node → "Configure shortcut…"
 *   - Right-clicking a shortcut type icon    → "Configure shortcut…"
 *
 * ── Caller protocol ──────────────────────────────────────────────────────────
 *   1. Look up (or create) the matching ShortcutDef in s_scShortcuts.
 *   2. Call SC_EditShortcutDialog(), passing current values as init* parameters.
 *   3. If the function returns true, copy 'out' back into the ShortcutDef and
 *      call MainWindow::MarkAsModified().
 *
 * ── Modularity ───────────────────────────────────────────────────────────────
 *   This file declares the public API only.  All implementation lives in
 *   sc_shortcut_dialog.cpp; see sc_shortcut_dialog_INTERNALS.txt for design
 *   notes, layout constants, and the message-loop pattern.
 */

#include <windows.h>
#include <string>
#include <map>

// Result written by the dialog when the user clicks OK.
// Unchanged if the user cancels or closes the dialog.
struct ScDlgResult {
    std::wstring name;               // shortcut display name
    std::wstring exePath;            // target executable path
    std::wstring workingDir;         // working directory (defaults to exe directory)
    std::wstring arguments;          // command-line arguments passed to the shortcut target
    std::wstring comment;            // tooltip shown when hovering the shortcut (Inno Comment:)
    std::wstring hotkey;             // global keyboard shortcut, e.g. "ctrl+alt+h" (Inno HotKey:)
    std::wstring iconPath;           // path of .ico / .exe / .dll; empty = use app exe
    int          iconIndex = 0;      // icon index within iconPath
    bool         runAsAdmin = false; // launch shortcut with administrator elevation
};

// Open the "Configure Shortcut" modal dialog.
//
//   hwndParent      — main window; disabled while the dialog is open
//   hInst           — application HINSTANCE (used to register the dialog class)
//   type            — SCT_* constant from shortcuts.h; controls which rows appear
//   smPath          — Start Menu location string shown as a read-only label,
//                     e.g. "Start Menu › Programs › MyApp".
//                     Pass L"" for non-SCT_STARTMENU types.
//   initName        — pre-filled shortcut name (edit field will contain this)
//   initExePath     — pre-filled executable path (may be empty)
//   initWorkingDir  — pre-filled working directory (may be empty; auto = exe dir)
//   initArguments   — pre-filled command-line arguments (may be empty)
//   initComment     — pre-filled shortcut comment / tooltip text (may be empty)
//   initHotKey      — pre-filled Inno HotKey string, e.g. "ctrl+alt+h" (may be empty)
//   initIconPath    — pre-filled icon file path (may be empty)
//   initIconIndex   — pre-filled icon index within initIconPath
//   initRunAsAdmin  — pre-filled Run-as-administrator checkbox state
//   locale          — current locale map (MainWindow::GetLocale())
//   out             — populated with edited values on OK; unchanged on Cancel
//
// Returns true when the user clicked OK; false when closed/cancelled.
bool SC_EditShortcutDialog(
    HWND hwndParent,
    HINSTANCE hInst,
    int type,
    const std::wstring& smPath,
    const std::wstring& initName,
    const std::wstring& initExePath,
    const std::wstring& initWorkingDir,
    const std::wstring& initArguments,
    const std::wstring& initComment,
    const std::wstring& initHotKey,
    const std::wstring& initIconPath,
    int initIconIndex,
    bool initRunAsAdmin,
    const std::map<std::wstring, std::wstring>& locale,
    ScDlgResult& out);
