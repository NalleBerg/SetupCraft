#pragma once
/*
 * vfs_picker.h — Reusable VFS-backed file/folder picker dialog for SetupCraft.
 *
 * See vfs_picker_API.txt for full documentation, design notes, and usage examples.
 *
 * The picker shows the same virtual file-system tree that the Files page builds
 * (s_treeSnapshot_ProgramFiles / ProgramData / AppData / AskAtInstall) so that
 * callers can let users choose files or folders from the installer's VFS without
 * touching the real filesystem.
 *
 * Public surface:
 *   VfsPickerResult  — output record (one per selected item)
 *   VfsPickerParams  — all configuration: UI strings, behaviour flags, filter
 *   VfsPicker_IsExecutable() — filter predicate for executable file types
 *   ShowVfsPicker()  — create, show, and run the modal picker; returns true on OK
 */

#include <windows.h>
#include <string>
#include <map>
#include <vector>
#include <functional>

// ── Output record ─────────────────────────────────────────────────────────────
struct VfsPickerResult {
    std::wstring sourcePath;   // real disk path of the selected file or folder
    std::wstring displayName;  // leaf name only (e.g. "setup.exe")
};

// ── Configuration ─────────────────────────────────────────────────────────────
struct VfsPickerParams {
    // ── UI strings (populate from locale at call site) ────────────────────────
    std::wstring title;
    std::wstring okText      = L"OK";
    std::wstring cancelText  = L"Cancel";
    std::wstring foldersLabel = L"Folders";
    std::wstring filesLabel  = L"Files in selected folder";
    std::wstring colFileName = L"Name";
    std::wstring colFilePath = L"Path";
    std::wstring noSelMessage;  // shown when user clicks OK with nothing selected

    // ── Root section labels (empty → English defaults used) ───────────────────
    std::wstring rootLabel_ProgramFiles;    // default L"Program Files"
    std::wstring rootLabel_ProgramData;     // default L"ProgramData"
    std::wstring rootLabel_AppData;         // default L"AppData (Roaming)"
    std::wstring rootLabel_AskAtInstall;    // default L"Ask at install"

    // ── Behaviour ─────────────────────────────────────────────────────────────
    bool singleSelect    = false; // LVS_SINGLESEL on the file listview
    bool showFilePane    = true;  // false → folder-only; no listview, tree is full-width
    bool allowFolderPick = false; // true → tree-selected folder usable as result
                                  //        (used when showFilePane=false)

    // Optional filter: return true to show the file in the listview.
    // fileName is the leaf name only (e.g. "setup.exe").
    // Null filter = show all files.
    std::function<bool(const std::wstring& fileName)> fileFilter;
};

// ── Helpers ───────────────────────────────────────────────────────────────────

// Returns true when fileName has an executable extension.
// Checked extensions: .exe .com .bat .cmd .msi .ps1 .vbs .js .wsf .pif .scr
bool VfsPicker_IsExecutable(const std::wstring& fileName);

// ── Entry point ───────────────────────────────────────────────────────────────

// Opens a VFS-backed picker dialog, modal to hwndParent.
//   hwndParent — parent window; disabled while the picker is open.
//   hInst      — HINSTANCE for CreateWindowExW.
//   params     — all configuration and UI strings.
//   locale     — project locale map (used for root labels if set in params).
//   results    — cleared then populated on OK click.
// Returns true if the user clicked OK, false on cancel/close.
bool ShowVfsPicker(
    HWND                                        hwndParent,
    HINSTANCE                                   hInst,
    const VfsPickerParams&                      params,
    const std::map<std::wstring, std::wstring>& locale,
    std::vector<VfsPickerResult>&               results);
