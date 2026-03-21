# Changelog

All notable changes to SetupCraft will be documented in this file.

## [2026.03.21.11] - 2026-03-21

### Added
- **New RTF editor component (`edit_rtf.h` / `edit_rtf.cpp`)** — general-purpose resizable rich-text editor modal (`WS_OVERLAPPEDWINDOW`, default S(660)×S(520)). Two-row toolbar: Bold/Italic/Underline/Strikethrough/Subscript/Superscript/FontFace/FontSize (row 1); Align L/C/R/J / Bullet / Numbered / Text colour / Highlight / Insert Image (row 2). `BS_AUTOCHECKBOX|BS_PUSHLIKE` toggle buttons synced from caret on `EN_SELCHANGE`. RTF streamed in/out via `EM_STREAMIN`/`EM_STREAMOUT`. All `notes_editor` bug-fixes carried forward. Control IDs 4500–4530.
- **Image insertion (PNG / JPEG)** — 🖼 toolbar button: file picked via `GetOpenFileNameW`, classified by header bytes (PNG 8-byte signature / JPEG SOF marker scan), hex-encoded, streamed into caret as `\pict\pngblip`/`\jpegblip` via `SF_RTF|SFF_SELECTION`. Fully embedded in the RTF string. Zero extra library dependencies.
- **Toolbar tooltips** — all 17 toolbar controls subclassed via `RtfEd_SetToolTip` / `RtfEd_ToolbarBtnProc`; shows the project's `ShowMultilingualTooltip` on hover. `WM_MOUSELEAVE` / `TrackMouseEvent` for reliable hide.
- **`rtf_editor_test.cpp`** — standalone WinMain test harness; CMake target `RtfEditorTest`, output `build\rtf_editor_test.exe`.
- **`edit_rtf_API.txt`** / **`API_list.txt`** — full component reference and index entry.
- **NSBEdit — standalone RTF notepad** — self-contained, statically-linked RTF notepad built on the `edit_rtf` component patterns: File menu (New / Open / Save / Save As / Exit with unsaved-changes prompt), same two-row formatting toolbar, PNG/JPEG image insertion, English-only hover tooltips, shell32.dll index 70 icon on title bar/taskbar/status bar, command-line file opening. No MinGW runtime DLL dependencies (`-static -static-libgcc -static-libstdc++`). Extracted to author's toolbox for reuse; source not included in the SetupCraft distribution.

### Fixed
- **`makeit.bat` packaging** — searches `build\SetupCraft.exe` specifically instead of the first `*.exe` found (alphabetically `rtf_editor_test.exe` was always matched first, leaving `SetupCraft.exe` out of the package).

## [2026.03.21.09] - 2026-03-21

### Fixed
- **Files page button mutual width reconciliation** — All three Files-page buttons (Add Folder, Add Files, Remove) are now measured before any button is created. `topRowW = wFDir + fBtnGap + wFFiles` is computed; if `wRemove > topRowW`, the extra pixels are added to `wFDir` so Add Folder grows and the visual block stays flush-left. Remove then uses `topRowW` as its width. Previously Remove's width was derived one-way from the top row, which would clip Remove's text in any locale where "Remove" translates wider than the two buttons combined.

## [2026.03.20.11] - 2026-03-20

### Added
- **Dependencies page** — new modular page (`deps.h` / `deps.cpp` / `deps_INTERNALS.txt`, page index 3). ListView with Name / Delivery / Required / Detection columns, three action buttons (Add / Edit / Remove), right-click context menu, double-click to edit. State held in `s_deps` (in-memory vector); persisted to new `external_deps` DB table on Save. DB API: `InsertExternalDep`, `GetExternalDepsForProject`, `DeleteExternalDepsForProject`.
- **Edit Dependency dialog** (`dep_edit_dialog.h` / `dep_edit_dialog.cpp`) — modal measure-then-create dialog with all 18 `ExternalDep` fields: delivery type (Bundled / Auto-download / Redirect URL / Instructions only), Required checkbox, architecture (Any / x64 / ARM64), install order, Detection section (registry key, file path, min version), conditional Network section (URL, SHA-256, silent args, offline behaviour), License section (path + Browse… picker), credits, multi-line instructions.
- **Scrollable dialog** — dialog uses `WS_VSCROLL`, height-clamped to work area. `WM_VSCROLL` and `WM_MOUSEWHEEL` handlers move all child controls. Centers over parent; clamped to work area on all four sides.

### Changed
- **`DepArch` enum** — `DA_X86` removed; `DA_ARM64` renumbered 3 → 2. App is 64-bit only. Arch combo uses `CB_SETITEMDATA`/`CB_GETITEMDATA`. Default architecture changed `DA_ANY` → `DA_X64`.

### Fixed
- **Dependencies page title font** — title STATIC now carries `IDC_DEP_PAGE_TITLE (6100)`; `WM_CTLCOLORSTATIC` selects `s_hPageTitleFont`, matching Files and Shortcuts headings.
- **`DepDlgProc` signature** — corrected from `INT_PTR CALLBACK` to `LRESULT CALLBACK`. Added `WM_CTLCOLORSTATIC`, `WM_KEYDOWN` Escape, `DefWindowProcW` fallthrough. Modal loop aligned with `sc_shortcut_dialog.cpp` pattern.

## [2026.03.20.09] - 2026-03-20

### Changed
- **Pin status labels ("Not Pinned / Pinned / Multi Pinned") removed** — the checkboxes make pin state self-evident; removing them gives the page a cleaner layout. `IDC_SC_SM_PIN_LABEL` and `IDC_SC_TB_PIN_LABEL` controls removed; `SC_RefreshPinLabels` now only updates pin-button enable state.
- **Select-all removed from 64×64 pin icons** — clicking the Pin to Start / Pin to Taskbar icon no longer bulk-toggles checkboxes. Each shortcut must be selected individually.

### Added
- **64×64 pin icon hover tooltip** — when a pin icon is enabled, hovering shows a multiline tooltip: "Use the checkboxes below to select which shortcuts to pin to Start" / "…to pin to the Taskbar". Added `WM_MOUSEMOVE`/`WM_MOUSELEAVE` to `SC_DesktopIconSubclassProc`. The disabled-state "Add shortcuts to pin first" tooltip (mainwindow.cpp timer callback) is unchanged.
- **`SC_TooltipSubclassProc` / `AttachTooltip` helper** — generic reusable hover-tooltip subclass in `shortcuts.cpp`. Text stored as heap `std::wstring` in property `L"TtText"`; `WM_NCDESTROY` frees it. Subclass id 2 (coexists with `CheckboxSubclassProc` id 1).

### Fixed
- **Pin-strip write-back fix** — `SC_OnCommand` now receives the real `HWND` from `lParam` and the `wmEvent` code. Range handlers use `hCtrl` directly with a `wmEvent != 0` guard that blocks phantom writes during page rebuilds. Pin state now survives page switches.
- **DB persistence for `pinToStart`/`pinToTaskbar`** — `SC_SaveToDb`/`SC_LoadFromDb` now map the `pin_to_start`/`pin_to_taskbar` columns. Existing databases are migrated via `ALTER TABLE` in `DB::InitDb`.

**Shortcuts page is now feature-complete.**

## [2026.03.19.15] - 2026-03-19

### Fixed
- **Pin-strip checkboxes now persist state across page switches** — clicking an individual "Pin to Start" or "Pin to Taskbar" strip checkbox now immediately writes `pinToStart`/`pinToTaskbar` back to the matching `ShortcutDef` in `s_scShortcuts`. Previously no write-back handler existed: the custom checkbox subclass toggled the visual state but never updated the data model, so state was discarded every time the page was torn down and rebuilt.
- **Pin-strip cross-talk fix (UB → `SC_RefreshPinLabels`)** — `SC_RefreshPinStrips` destroys and recreates every pin checkbox. Calling it from `WM_COMMAND` while the triggering checkbox's `WM_LBUTTONUP` is still on the call stack is UB: the button's post-`WM_LBUTTONUP` cleanup ran against an already-destroyed HWND, causing spurious check/uncheck on adjacent strip entries. Added `SC_RefreshPinLabels()` — a lightweight helper that updates only the "Not Pinned / Pinned / Multi Pinned" status labels and bulk-pin button enable state, without touching checkbox HWNDs. Individual-click handlers now call this instead of `SC_RefreshPinStrips`.
- **Entry screen font scaling corrected** — `g_guiFont` was created from the raw `NONCLIENTMETRICS` `lfHeight` without the `× 1.2f` scale applied to `s_scaledFont` in the main window, making entry-screen labels and the new-project dialog visually smaller. Fixed by adding `if (lf.lfHeight < 0) lf.lfHeight = (LONG)(lf.lfHeight * 1.2f)` before `CreateFontIndirectW`. Same fix applied to `g_tooltipFont` in `tooltip.cpp`.
- **New-project dialog title and label fonts corrected** — dialog title used hardcoded `CreateFontW(-18, …)` instead of `NONCLIENTMETRICS × 1.5 + FW_SEMIBOLD`. All five static labels (Name, Directory, Description, Language, Version) were never sent `WM_SETFONT` and fell back to the system default font. Both now use the correct scaled body/title fonts.

## [2026.03.19.14] - 2026-03-19

### Added
- **Shortcuts page vertical scrollbar** — `SC_BuildPage` now returns `int` (absolute Y of last content row). `SwitchPage` case 2 uses this to set up `SCROLLINFO` (`nMax = contentH-1`, `nPage = viewH`) and add `WS_VSCROLL` when content exceeds the view. `WM_VSCROLL` and `WM_MOUSEWHEEL` handlers added to the main window. `SC_SetScrollOffset`/`SC_GetScrollOffset` in `shortcuts.cpp`; `SC_RefreshDesktopStrip` and `SC_RefreshPinStrips` subtract the offset when positioning controls. `SC_TearDown` removes `WS_VSCROLL` and resets the offset on page switch. Status-bar height always measured via `GetWindowRect(s_hStatus)`, not hardcoded.
- **`make_ico.ps1`** — PowerShell script wrapping ImageMagick `magick` to repack any source `.ico` into a multi-frame icon (16, 24, 32, 40, 48, 64, 96, 128 px). `icons/trashcan_empty.ico` generated from a 128 px source and embedded in `SetupCraft.rc` as resource ID 2 (`IDI_TRASHCAN = 2` in `shortcuts.h`).
- **`scrollbar_INTERNALS.txt`** — architectural doc covering SCROLLINFO setup, why `SW_SCROLLCHILDREN` fails for below-viewport controls, the correct manual child-enumeration pattern, controls to exclude, `SC_SetScrollOffset` integration, teardown protocol, and status-bar `HWND_TOP` anti-overlap technique. Entry added to `API_list.txt`.

### Fixed
- **Scrollbar mechanism — `SW_SCROLLCHILDREN` replaced with manual child enumeration** — `ScrollWindowEx(..., SW_SCROLLCHILDREN)` only moves children intersecting the scroll rect at call time; controls created below the viewport are never moved and stay permanently unreachable. Both `WM_MOUSEWHEEL` and `WM_VSCROLL` handlers now enumerate all direct children of `hwnd` via `GetWindow(hwnd, GW_CHILD)` and call `SetWindowPos` on each page control (skipping toolbar buttons, the About button, and the status bar). Status bar is pinned to `HWND_TOP` after each scroll step.
- **Page controls painting over status bar** — added `WS_CLIPSIBLINGS` to all page controls after `SC_BuildPage`. Each control now excludes sibling-covered pixels from its own paint region, so the status bar (always at `HWND_TOP`) is never overdrawn by a scrolled page control.
- **Instant pin-strip refresh on all mutation paths** — `SC_RefreshPinStrips` now called from all 8 mutation sites, including `IDC_SC_SM_REMOVE` (folder Remove button and context-menu equivalent) and `IDM_SC_CTX_EDIT_SM` (SM shortcut Edit, where an exe-path change may alter pin eligibility). Previously those two paths refreshed the SM tree but left pin-strip checkboxes stale.

## [2026.03.19.12] - 2026-03-19

### Added
- **VFS picker double-click to select** — `NM_DBLCLK` on `VFSP_LIST` now sends `WM_COMMAND(VFSP_OK)` when a row is selected, identical to clicking OK.
- **SM tree per-shortcut program icons** — `SC_RebuildSmTree()` now rebuilds the image list on every call. Indices 0/1 = folder icons; index 2 = fallback link; indices 3+ = per-shortcut icon loaded via `PrivateExtractIconsW` from `iconPath` or `exePath`. Image list creation removed from `SC_BuildPage`.

### Changed
- **SM tree width = button row width** — `treeW = addW + btnGap + scW + btnGap + remW`; `bRowX = treeX`. Tree and buttons now share the same horizontal extent and are centred together. Button constants moved before tree creation.



### Added
- **SM tree shortcut child items (Phase 2)** — `SCT_STARTMENU` shortcuts now appear as leaf items in their parent folder node in the Start Menu TreeView. Image-list index 2 (`shell32.dll` #17 link icon) distinguishes them from folder nodes (indices 0/1). `lParam` scheme: positive = folder `node.id`, negative = `-(sc.id)`. New `SC_RebuildSmTree(selectLParam)` replaces the inline population block and is called on every page visit, shortcut add/edit/remove, and folder remove. Double-click opens Edit dialog; right-click shows Edit/Remove shortcut menu (`IDM_SC_CTX_EDIT_SM` 6307, `IDM_SC_CTX_REMOVE_SM` 6308). TVN_BEGINLABELEDIT cancelled for shortcut items. TVN_SELCHANGED disables Add Subfolder and Add Shortcut Here for shortcut items. Folder remove also cleans up shortcuts in the removed subtree. "Add Shortcut Here" always adds new — multiple shortcuts per SM folder now supported.
- **Multiple Desktop shortcuts** — clicking the 64×64 Desktop icon always adds a new shortcut. Existing shortcuts appear as 16×16 mini-icons (centred strip below the big icon). `SC_DskMiniSubclassProc` subclass handles paint, tooltip, double-click edit, and right-click Edit/Remove. `SC_RefreshDesktopStrip()` rebuilds the strip from `s_scShortcuts`. ID range `IDC_SC_DSK_STRIP_BASE` 5400–5449; `IDM_SC_CTX_EDIT_DSK` 6305 / `IDM_SC_CTX_REMOVE_DSK` 6306.
- **SM tree custom hover tooltip** — `TVS_NOTOOLTIPS` added to suppress system truncation tooltip; `SC_SmTreeSubclassProc` shows the project's custom tooltip on `WM_MOUSEMOVE` via `TreeView_HitTest` + `ShowMultilingualTooltip`.
- **"Add Shortcut Here" button** (`IDC_SC_SM_ADDSC` / 5213) — green button in the SM section; composite icon (shell32.dll 257 + 29); tooltip via `sc_sm_addsc_tooltip`.
- **"Add shortcut here…" context menu item** — first item in the SM tree right-click menu, dispatches to `IDC_SC_SM_ADDSC`.
- **`BuildSmPath()` helper** — builds breadcrumb `Start Menu › Programs › Folder` from a node id.
- **DB persistence for shortcuts** — two new tables: `sc_menu_nodes (id, project_id, parent_id, name)` and `sc_shortcuts (id, project_id, type, sm_node_id, name, exe_path, working_dir, icon_path, icon_index, run_as_admin)`. New DB functions: `InsertScMenuNode`, `DeleteScMenuNodesForProject`, `GetScMenuNodesForProject`, `InsertScShortcut`, `DeleteScShortcutsForProject`, `GetScShortcutsForProject`. `SC_SaveToDb(projectId)` called on `IDM_FILE_SAVE`; `SC_LoadFromDb(projectId)` called on project open. Covers all shortcut types and opt-out flags.
- **New locale keys**: `sc_ctx_edit`, `sc_ctx_remove_sc` added to `locale/en_GB.txt`.

### Fixed
- **Desktop mini-icon tooltip off-by-one** — hover and right-click Remove used `ctrlId − IDC_SC_DSK_STRIP_BASE` to look up the shortcut but `ShortcutDef::id` starts at 1 (not 0). Fixed to read the `L"ScId"` window property set at creation time.
- **Tooltip single-line width** — removed hardcoded `S(500)` cap; width is now `sz.cx + S(32)`, clamped to monitor width. Updated `tooltip_API.txt` §3 and §7.

## [2026.03.19.08] - 2026-03-19

### Changed
- **Balanced two-line opt-out labels** — new `MidBreak()` helper in `shortcuts.cpp` inserts a hard `L'\n'` at the word boundary nearest the string midpoint so each sentence splits into two roughly equal lines. Adapts to translated strings automatically.
- **Opt-out checkboxes centred in columns** — each control is measured with `GetTextExtentPoint32W` (per line, taking the wider), sized to fit exactly, then centred within its column via `cbX = colX + (colW - cbW) / 2`, matching the headings and status labels above.

## [2026.03.18.10] - 2026-03-18

### Added
- **"Pin to Start" column heading** — renamed from "Start Menu" (`sc_sm_pin_section`) to eliminate the duplicate label also used by the Start Menu tree section below.
- **SM pin opt-out checkbox** (`IDC_SC_SM_PIN_OPT` / 5211) — developer toggle allowing end-users to opt out of the Start Menu pin at install time. State stored in `s_scSmPinOptOut`, reset by `SC_Reset()`.
- **Taskbar pin opt-out checkbox** (`IDC_SC_TB_PIN_OPT` / 5212) — same pattern for the Taskbar pin column.
- **New locale keys**: `sc_sm_pin_opt_out`, `sc_tb_pin_opt_out` in `locale/en_GB.txt`.

### Changed
- **Vertical alignment of all opt-out checkboxes** — Desktop, Start Menu pin, and Taskbar pin checkboxes all now start at `rowY + statusH + S(4)`, one row below the status labels.
- **`DrawCustomCheckbox` word-wrap** — label rendering switched from `DT_SINGLELINE | DT_VCENTER` to `DT_WORDBREAK`; box is now top-aligned at `+S(2)`. Checkbox height is S(34) to accommodate two lines of text at 9pt bold.

## [2026.03.18.09] - 2026-03-18

### Fixed
- **Start Menu tree centred** — box is 17% of dialog width, horizontally centred using content-area maths.
- **Section label ampersand** — `SS_NOPREFIX` added; "Start Menu & Programs" now shows the `&` literally.
- **Row height** — removed `TreeView_SetItemHeight(S(34))`; Windows auto-sizes rows from image list + font, giving the same compact highlight as the Files page.
- **TreeView font** — `WM_SETFONT` with `hGuiFont` applied, matching Files/Components trees.
- **Icon extraction** — large 32×32 icons extracted via `ExtractIconExW` large-icon param, consistent with project convention.
- **Bug A** — tree X position uses content-area maths (not raw `rc.right`).
- **Bug B** — Add Subfolder/Remove buttons pass bare `L"shell32.dll"` so `DrawCustomButton` builds the correct full path.
- **Bug C** — section label assigned ID 5301 and added to `WM_CTLCOLORSTATIC` so bold page-title font is applied.

## [2026.03.17.11] - 2026-03-17

### Added
- **Shortcuts page module** — all Shortcuts-page code extracted from `mainwindow.cpp` into `shortcuts.h`, `shortcuts.cpp`, and `shortcuts_INTERNALS.txt`. `mainwindow.cpp` routes `WM_NOTIFY` / `WM_COMMAND` / `WM_CONTEXTMENU` via `SC_OnNotify` / `SC_OnCommand` / `SC_OnContextMenu`.
- **Right-click context menu on Start Menu tree** — Add Subfolder and Remove (grayed for fixed root/Programs nodes). Uses `TPM_RETURNCMD` + `SendMessageW(WM_COMMAND)` to reuse the existing button handlers.
- **Right-click stub on shortcut row buttons** — grayed «Configure shortcut…» item in place; handler infrastructure ready for the config dialog next session.
- **Shortcut config dialog spec** — full specification added to `ToDo.txt` Item 1 (name field, run-as-admin checkbox, icon picker, OK/Cancel).
- **APPDATA/home install location spec** — added to `ToDo.txt` Item 5.

### Changed
- **Shortcuts page title uses `WM_CTLCOLORSTATIC`** — control ID 5300 added alongside 5100 so the page headline renders in `s_hPageTitleFont` (bold, large) matching the Files page headline.
- **“Start Menu & Programs” section label** — `SS_CENTER` + `hPageTitleFont`, horizontally centred above the tree.
- **TreeView and buttons centred at 40% width** — folder tree and its action buttons are horizontally centred and limited to 40% of the content area.
- **Button icons** — Add Subfolder: `shell32.dll` index 296; Remove: `shell32.dll` index 234 (matching the Files page).

### Fixed
- **Components Required-icon state** — marked complete in `ToDo.txt`.

## [2026.03.16.11] - 2026-03-16

### Fixed
- **Dep picker: excluded folder's children now visible** — `addVFS` previously used a bare `continue` when it hit the excluded node, which skipped the node's entire subtree. Changed to `addVFS(hParent, snap.children, secLabel); continue;` so the excluded folder itself is not selectable (correct) but all its subfolders and files remain visible and checkable as dependencies.
- **Dep init always reads from in-memory state** — `IDM_COMP_TREE_CTX_EDIT` previously used a DB fallback (`DB::GetDependenciesForComponent`) when `cmp.dependencies` was empty but `cmp.id > 0`. This discarded any in-memory edits made since the last save. Changed to always use `cmp.dependencies` directly.
- **"Choose" after auto-save opens picker immediately** — the `anyUnsaved` path in `IDC_FOLDER_DLG_CHOOSE_DEPS` previously returned 0 after saving and rebuilding `otherComponents`, requiring the user to press "Choose" a second time. Now falls through to open the dep picker in the same gesture. `outDependencyIds` is cleared before opening so stale pre-save IDs (which would be invalid after the Save ID-remap) are not carried over.

## [2026.03.16.09] - 2026-03-16

### Added
- **Component deps list: two columns (Name | Type)** — the dependency summary in the folder-edit dialog is now a proper `WC_LISTVIEW` with two columns (locale keys `comp_deps_col_name` / `comp_deps_col_type`). Type shows "Folder" or "File". An empty list shows a `(none)` placeholder row. The list supports multiselect.
- **Dep list: custom hover tooltip** — `DepListSubclassProc` subclasses the ListView and uses the project's `ShowMultilingualTooltip` / `HideTooltip` system (`TME_LEAVE` tracking via window properties). Hovering a file row shows its full virtual path; hovering a folder row shows the locale string `comp_deps_folder_dblclick` ("Double-click to see files").
- **Dep list: double-click opens file-tree popup** — double-clicking a folder dep row opens `ShowDepsFileListPopup`, a blocking popup with a `WC_TREEVIEW` showing the VFS contents of that folder dependency. The tree uses an ImageList built from shell32.dll (folder/document icons). Closes via the "Close" button or the title-bar X.
- **Remove button beside Choose** — new `IDC_FOLDER_DLG_REMOVE_DEPS` button (shell32.dll icon index 131, `ButtonColor::Red`) removes all selected rows in the dep list; also cascade-removes covered file deps. Enabled only when one or more rows are selected (`LVN_ITEMCHANGED`).
- **Choose button now custom-styled** — `CreateCustomButtonWithIcon` with shell32.dll icon index 87 (`ButtonColor::Blue`), consistent with all other action buttons in the app.
- **Right-click context menu on dep list** — shows "Remove" (grayed if nothing selected) and, when a single folder row is selected, "Show files…". Commands delegate to the Remove handler and `ShowDepsFileListPopup` respectively.
- **Folder-coverage filter** — if a folder component is in the dep list, file deps whose `source_path` begins with the folder's `source_path` are omitted from the list (they remain stored internally). Prevents visual duplication.
- **`comp_deps_INTERNALS.txt`** — full internals reference: layout constants, control IDs, ListView columns, folder-coverage filter, `DepListSubclassProc` message handling, `ShowDepsFileListPopup` design, Remove handler cascade logic, and locale key table.
- **`API_list.txt`** updated with `comp_deps_INTERNALS.txt` entry.

### Fixed
- **Dep picker cascade-up: structural folders no longer skipped** — the WM_TIMER ancestor-check block used `if (tp.lParam > 0)` to decide whether to auto-tick a parent node; this excluded nodes with `lParam == 0` (structural folder nodes without their own `ComponentRow`). Changed to `if (tp.lParam != -1)` so all ancestor nodes up to the section header are ticked, regardless of whether they have a component row.
- **New locale keys added to `locale/en_GB.txt`**: `comp_deps_col_name`, `comp_deps_col_type`, `comp_type_folder`, `comp_type_file`, `comp_deps_none`, `comp_deps_remove`, `comp_deps_folder_dblclick`, `comp_deps_ctx_remove`, `comp_deps_ctx_showfiles`, `comp_deps_files_popup_title`, `comp_deps_files_popup_close`.

## [2026.03.16.08] - 2026-03-16

### Fixed
- **Components enable checkbox now uses the custom themed control** — the last native `BS_AUTOCHECKBOX` on the Components page replaced with `CreateCustomCheckbox`. Now theme-aware (Light/Dark/HC Black/HC White), owner-drawn with U+2714 tick and disabled visual via `ODS_DISABLED`, consistent with all other checkboxes in the app.
- **Enable-components toggle works on unsaved projects** — `IDC_COMP_ENABLE` handler had `if (s_currentProject.id <= 0) return 0` blocking the toggle before first save. Guard removed; component tree and list now un-grey immediately after ticking, even with `id == 0`.
- **"Save First" prompt replaced with custom i18n dialog** — `MessageBoxW` (native, no i18n buttons) replaced with `ShowConfirmDeleteDialog` using locale keys `comp_deps_unsaved_title` / `comp_deps_unsaved_msg`, proper themed Yes/No buttons, and `S()`-scaled layout.
- **Missing locale keys added to `en_GB.txt`** — `comp_info_tooltip` and `comp_info_icon_label` were present as C++ fallback strings only. Both are now defined in `locale/en_GB.txt`. The floppy-disk WM_PAINT label is now read from locale at paint time.

## [2026.03.15.11] - 2026-03-15

### Fixed
- **Dep picker: AskAtInstall subfolders and files now visible** — `EnsureTreeSnapshotsFromDb()` completely rewritten. Old code had `if (!snapVecs[si]->empty()) continue` inside the folder loop, bailing after the first folder per section. New version uses a stable `std::map<wstring, TreeNodeSnapshot>` for node storage (no iterator invalidation). Three passes: (1) folder rows → nodeMap entries, (2) file rows → `nodeMap[parentPath].virtualFiles.push_back(vf)`, (3) sort deepest-first, link children into parents bottom-up, move section roots into snapshot vectors.
- **Dep picker: real-path folder files visible (locale/, img/, WinUpdate etc.)** — new `PopulateSnapshotFilesFromDisk(std::vector<TreeNodeSnapshot>&)` walks the snapshot tree recursively; for any real-path node with empty `virtualFiles` it scans the disk directory with `FindFirstFileW` and populates `snap.virtualFiles`. Called once per dep-picker open for all four sections before `addVFS` traversal.
- **Components info icon hidden by overlapping controls** — replaced `SS_ICON` style (which calls `STM_SETICON` and auto-resizes the control to the icon's natural size, overlapping adjacent controls) with a plain `SS_NOTIFY` static. `HICON` stored in `GWLP_USERDATA`; subclass proc's `WM_PAINT` calls `DrawIconEx` at the exact control size. `WM_ERASEBKGND` clears with `COLOR_BTNFACE` to prevent bleed-through.

### Added
- **Dep picker: auto-file nodes for files without a ComponentRow** — `addVFS` now inserts a synthetic auto-file node for every `snap.virtualFiles` entry that has no matching `ComponentRow` (lParam ≥ `kAutoFileBase = 1,000,000`). On picker OK the handler finds the component by source path or auto-creates one via `DB::InsertComponent`. No files silently skipped.
- **Dep picker: DB-first unified `snap.virtualFiles` path in `addVFS`** — single code path iterating `snap.virtualFiles` only; no per-node disk I/O inside the render walk. DB rows take priority; `PopulateSnapshotFilesFromDisk` fills only nodes that remain empty after the DB pass.
- **Components page info icon (shell32.dll #258)** — floppy-disk icon to the right of the hint label with tooltip "Files and folders will not appear in the dependency picker until the project has been saved at least once." (`comp_info_tooltip` locale key). Subclassed with `CompInfoIcon_SubclassProc`.
- **"FYI!" text on diskette label** — `CompInfoIcon_SubclassProc` `WM_PAINT` overlays "FYI!" in dark navy bold Arial on the white label strip of the diskette graphic so the icon's purpose is readable at a glance.

## [2026.03.15.08] - 2026-03-15

### Fixed
- **Edit button now opens folder-edit dialog for tree-selected folders** — `IDC_COMP_EDIT` previously only inspected the ListView for a selected row and showed "Please select a component first" even when a folder was selected in the Components tree. The button now falls back to the tree selection and calls `IDM_COMP_TREE_CTX_EDIT`. The entire folder-edit flow is consolidated into a single `WM_COMMAND` case; the right-click context menu delegates to it via `SendMessageW` (no more duplicated inline logic).

### Added
- **Pre-selected checkbox in folder-edit dialog** — new `is_preselected` field on `ComponentRow` and `components` table (migration: `ALTER TABLE components ADD COLUMN is_preselected INTEGER DEFAULT 0`). The folder-edit dialog shows a "Pre-selected (ticked by default at install)" checkbox directly below Required. Checking Required force-ticks and disables Pre-selected; unchecking Required re-enables it. Cascades to all files in the folder (section-scoped) and persists to DB on Save.
- **Custom checkbox disabled visual** — `DrawCustomCheckbox` now checks `ODS_DISABLED` in `dis->itemState`; border, tick, and label all grey out when `EnableWindow(hCtrl, FALSE)` is called. Hover highlight is suppressed. Works across all four themes.

### Fixed
- **Dep picker: folders no longer auto-expand** — `TreeView_Expand` call removed from `addVFS`; sub-folders start collapsed so AskAtInstall is always visible without scrolling.
- **Dep picker: virtual folder files reliably placed** — replaced `handledFilePaths`/`virtualFiles`-in-`addVFS` with a `virtualFilePaths` map (`sourcePath → HTREEITEM`); second pass checks exact virtual match first, then deepest-real-path-prefix, then section header. Files placed correctly even when `snap.virtualFiles` is empty (DB-loaded projects).
- **Dep picker: auto-check ancestor folders** — ticking a file/folder fires a 1 ms timer that walks parents and auto-checks any ancestor with a `ComponentRow` (`lParam > 0`).
- **Dep picker: otherComponents rebuilt after save-first** — after save, `pData->otherComponents` is rebuilt from `s_components` immediately and the picker opens without the "press Choose again" message.
- **`CompFolderDlgData`: `sectionName` field added** — forwards VFS section name to save-first rebuild for correct section-aware exclusion.

## [2026.03.14.08] - 2026-03-14

### Fixed
- **Required state no longer lost after Files page deletion** — All four deletion paths (`IDC_FILES_REMOVE` ListView, multi-select tree, single-item tree, `IDM_TREEVIEW_REMOVE_FOLDER`) called `PurgeComponentRowsByPaths` then immediately reloaded `s_components` from DB via `DB::GetComponentsForProject`, wiping all unsaved `is_required=1` flags. `PurgeComponentRowsByPaths` is now memory-only (`std::remove_if` on `s_components`, no DB access, parameter `int projectId` removed). The four `s_components = DB::GetComponentsForProject(...)` reload lines are removed. Required state survives any number of Files page edits.
- **Any folder can be marked Required, not just the first** — Right-click "Edit Folder" previously did nothing for folders with no existing component rows. Two fixes: (1) Guard `if (paths.empty()) return 0` changed to `if (paths.empty() && snap->fullPath.empty()) return 0` so whole-folder (folder-type) components open the dialog. (2) A folder-type row (`source_type="folder"`, `source_path=snap->fullPath`) is now always upserted into `s_components` on OK — created if absent, `is_required` updated if present.
- **`UpdateCompTreeRequiredIcons`: two-phase matching, icon survives page switch** — Old logic scanned per-file paths and treated unregistered files as "skip", causing container folders with mixed registered+unregistered files to appear all-required and cascade incorrectly. New Phase 1: if a folder-type component row exists for `snap->fullPath`, use it as the authoritative answer (no file scan). Phase 2 (file-type only): an unregistered file now explicitly sets `allRequired=false`, preventing false positives on container folders while still allowing component-less subfolders to inherit parent state via `anyFound=false`.

## [2026.03.13.10] - 2026-03-13

### Changed
- **Components page: full in-memory architecture** — `s_components` is now loaded once in `MainWindow::Create` and never reloaded on a page switch. `SwitchPage` teardown no longer clears it; `SwitchPage(9)` only reads from DB when the vector is empty (first visit after project open). All four mutation handlers (`IDC_COMP_ENABLE`, `IDC_COMP_ADD`, `IDC_COMP_EDIT`, `IDC_COMP_REMOVE`) mutate `s_components` directly — no DB writes. Components are written to DB exclusively in `IDM_FILE_SAVE` (`DB::DeleteComponentsForProject` + full re-insert). Implements the project design rule "work in memory, save only on explicit Save" for the entire Components subsystem.
- **`DB::InsertComponent` return type changed from `bool` to `int`** — now returns the new row's DB id (via `sqlite3_last_insert_rowid`) or 0 on failure. The Save path updates the in-memory `id` field immediately after insert.

### Fixed
- **Required flag: folder-type components now matched** — `UpdateCompTreeRequiredIcons` and the Required cascade only matched `source_path` against per-file paths from `CollectSnapshotPaths`. Folder-type components (`source_type="folder"`) store the folder path in `source_path`, so they were never found and the shell32.dll #110 icon never appeared. Both code paths now additionally check `snap->fullPath` directly, fixing the match for folder nodes.
- **Required cascade: `DB::UpdateComponent` no longer called per row** — cascade changes stay in `s_components` until Save (consistent with in-memory model).
- **Legacy `dest_path` repair: no longer writes to DB** — the one-time repair block that infers sections for pre-tagging rows now updates `s_components` only; the fix is persisted on the next explicit Save.

## [2026.03.13.09] - 2026-03-13

### Changed
- **Files TreeView: ticking a parent auto-ticks all sub-folders** — `FilesTree_CtrlClickProc` now recursively applies the same check state to every descendant, both the native `TVIS_STATEIMAGEMASK` and `s_filesTreeMultiSel`. Unticking a parent unticks all children.
- **Remove confirm dialog is context-aware** — when the deduplicated delete list is exactly 1 item that has sub-folders, the message reads "Remove 1 folder and all its sub-folders?" (new locale key `confirm_remove_folder_subtree`). Single leaf-folder and multi-folder messages are unchanged.

## [2026.03.13.08] - 2026-03-13

### Added
- **Custom checkbox component (`checkbox.h/.cpp/_API.txt`)** — reusable owner-draw checkbox with U+2714 heavy tick glyph (Segoe UI Symbol, 1.4× box height, right stroke crosses the top border for a hand-written look). Colour palettes for Light, Dark, HC Black, and HC White themes are detected at paint time via `DetectCbTheme()`; live theme switches take effect without a restart. BM_GETCHECK/BM_SETCHECK subclass makes it a drop-in replacement for `BS_AUTOCHECKBOX`.
- **`CreateCheckboxStateImageList(int sizePx)`** — builds a 3-entry HIMAGELIST (blank / unchecked / checked) with GDI-drawn bitmaps matching the custom checkbox style. No external image files.
- **`UpdateTreeViewCheckboxImages(HWND, int sizePx)`** — replaces native `TVS_CHECKBOXES` bitmaps with the custom ones; call once at creation and again from `WM_SETTINGCHANGE`.
- **Files TreeView multi-select** — folders can be ticked for batch removal. Subclass proc syncs `s_filesTreeMultiSel` with the checkbox state; hint label explains the feature; right-click routes to multi-delete when the item is ticked.
- **4 px gap between TreeView checkbox and folder icon** — normal image list is 36 px wide (32 px icon + 4 px transparent left padding).

## [2026.03.13.07] - 2026-03-13

### Fixed
- **Registry Key Path dialog: gray label background eliminated** — the static label inside `RegKeyDialogProc` was painted with the default gray `COLOR_BTNFACE` brush returned by `DefWindowProc` for `WM_CTLCOLORSTATIC`, causing a visible gray band against the white dialog background. A `WM_CTLCOLORSTATIC` handler now returns `GetSysColorBrush(COLOR_WINDOW)` with `COLOR_WINDOW` / `COLOR_WINDOWTEXT` set on the DC so the label blends seamlessly with the dialog background.
- **Registry Key Path dialog: horizontal scrollbar replaces vertical** — the edit control was created with `WS_VSCROLL`, which showed a useless up/down scrollbar on the single-line registry path field. Replaced with `WS_HSCROLL`; `ES_AUTOHSCROLL` (already present) continues to drive the caret. Long paths can now be scrolled left/right.

## [2026.03.12.11] - 2026-03-12

### Changed
- **All custom dialogs: fully DPI-aware, no hardcoded pixel values** — every custom dialog in `ctrlw.cpp` (Quit, Duplicate Project, Rename Project, Unsaved Changes) and `mainwindow.cpp` (Registry Key, Add Key, Add Value, Edit Folder) now follows the measure-then-create pattern: named layout constants for every dimension, all wrapped in `S()`, outer window size computed via `AdjustWindowRectEx`. No hardcoded pixel sizes remain anywhere in the dialog system. Dialogs display correctly at all DPI settings (100%–200%+) without clipping or oversizing.
- **Button widths generous for i18n** — dialog buttons use layout constants of 150–200 design-px so translated labels are never clipped at any DPI.
- **`CompFolderEditDlgProc`: cascade hint measured at creation site** — hint text height is measured inline with `GetDC(NULL)` + `DrawTextW(DT_CALCRECT | DT_WORDBREAK)` and stored in `CompFolderDlgData::hintH`. The dialog is sized exactly to the measured text — hint never clips regardless of locale or DPI.

## [2026.03.12.08] - 2026-03-12

### Changed
- **Components page: Required-folder icon source switched to `shell32.dll` #110** — previously loaded from `imageres.dll` sequential index 110 (folder with blue circular badge). Changed to `shell32.dll` sequential index 110 (classic yellow folder with blue checkmark badge) — subtle, recognisable, and visually consistent with Windows Explorer. Path built via `GetSystemDirectoryW` + `\shell32.dll`, extracted with `ExtractIconExW`.

## [2026.03.11.10] - 2026-03-11

### Fixed
- **Components page: Required-folder icon (`ExtractIconExW`)** — `PrivateExtractIconsW` is undocumented and silently returned 0 on some Windows configurations, leaving image-list slot 3 empty. Switched to the documented `ExtractIconExW(imgresPath, 110, &hReq, NULL, 1)` which reliably loads `imageres.dll` sequential index 110 (folder with blue checkmark badge).
- **Components page: cascade scope — AskAtInstall no longer affected** — Legacy `ComponentRow` rows had `dest_path = ""` which made the section-filter a no-op. A repair block runs once on Components page load: it infers each legacy row's section by matching source paths against the four VFS snapshots (in original insertion order) and persists the fix to the DB. The filter `if (!cmp.dest_path.empty() && cmp.dest_path != section)` then correctly isolates each section.
- **Components page: `UpdateCompTreeRequiredIcons` inherits parent Required state** — Subfolders with no registered component files (e.g. `img/`, `locale/`) always showed the plain folder icon because `anyFound == false` unconditionally mapped to icon 0. A `parentIsRequired` parameter now propagates each node's resolved icon state into its children so component-less subfolders of a fully-required parent inherit the blue-checkmark badge and pass it further down the tree.

## [2026.03.11.08] - 2026-03-11

### Added
- **Components page: Required-folder icon** — folders where every file (recursively) is flagged Required are shown with `imageres.dll` icon #110 (folder with a blue checkmark badge) at image-list index 3. `UpdateCompTreeRequiredIcons(hTree, hItem)` walks the comp tree after every build and after each Edit Folder OK; reverts to the normal folder icon as soon as any file inside loses the Required flag.
- **Components page: "Required" hover tooltip** — `CompTree_TooltipSubclassProc` subclasses `s_hCompTreeView`. On `WM_MOUSEMOVE` it uses `TreeView_HitTest` to find the hovered item; shows `ShowMultilingualTooltip({L"", L"Required"})` when the item carries image index 3; hides on `WM_MOUSELEAVE` or when the cursor is over a non-required item.
- **Files page: native blue multi-select** — `TVS_CHECKBOXES` removed. Multi-selection tracked in `static std::set<HTREEITEM> s_filesTreeMultiSel`. `FilesTree_CtrlClickProc` handles Ctrl+Click (toggle), Shift+Click (range), and plain click (clear). `NM_CUSTOMDRAW / CDDS_ITEMPREPAINT` in `WM_NOTIFY` (idFrom 102) colours selected items with `COLOR_HIGHLIGHT`/`COLOR_HIGHLIGHTTEXT` — identical to Windows Explorer. Remove iterates the set; set is cleared on page teardown.

### Fixed
- **Components page: Edit Folder dialog clipping** — dialog height raised from 190 px to 240 px; cascade-hint label height raised from 22 px to 42 px so the two-line hint text is fully visible.
- **Components page: Required icon not loading** — `LoadLibraryExW(..., LOAD_LIBRARY_AS_DATAFILE)` prevents MUI/icon resolution on modern Windows and returned the wrong icon. Fixed to `LoadLibraryW`.

## [2026.03.10.13] - 2026-03-10

### Added
- **Folder expand/collapse memory — Files page**: `TreeNodeSnapshot` now stores `expanded` (Files page) and `compExpanded` (Components page) flags. `SaveTreeSnapshot` records each node's expanded state; `RestoreTreeSnapshot` restores it per node. On first visit all folders are fully expanded via `ExpandAllSubnodes`. Within a session, collapsing a folder on the Files page is remembered when revisiting.
- **Folder expand/collapse memory — Components page**: Independent of the Files page state. `SaveCompTreeExpansion` traverses the comp tree on leaving page 9 and writes each node's state back into its snapshot (`mutable compExpanded`). `VFSPicker_AddSubtree` expands per `compExpanded` when rebuilding the tree.

### Fixed
- **Components page title font**: The title static control was created with `NULL` as its menu-ID, so `WM_CTLCOLORSTATIC` never matched it and `s_hPageTitleFont` was not applied. Control now receives `(HMENU)5100` — consistent with every other page title.
- **Components page `AskAtInstall` root: wrong label and missing badge icon**: The root was inserted as `"Ask At Install"` (spaces, no badge). Label corrected to `"AskAtInstall"` to match the Files page. The `addRoot` helper now accepts an icon-index parameter; the badge icon (blue circle, index 2) is added to the comp-page image list exactly as on the Files page.
- **Context menu `Add Files` shown on root nodes**: Right-clicking Program Files, ProgramData, AppData (Roaming), or AskAtInstall showed an `Add Files` entry that would have been blocked anyway. The item is now omitted from the menu when `isSystemRoot` is true — menu only contains `Add Folder` and `Create Folder…` for these four roots.

## [2026.03.10.10] - 2026-03-10

### Added
- **Components page: folder TreeView** — split-pane layout mirroring the Files page. Left pane shows the virtual-folder tree built from VFS snapshots; selecting a folder populates the right pane with its files and their component metadata (name, description, required, type, source path).
- **Components enable auto-populate** — toggling the "Enable components" checkbox now automatically creates a `ComponentRow` for every file found in the current VFS snapshot; disabling clears all components from the DB.
- **VFS Picker dialog** — "Add Files / Folders" now opens a split-pane VFS browser instead of a native `IFileOpenDialog`. Users pick files from the right pane or a real-path folder from the left pane — no filesystem access required.
- **Component dependency selection** — the Component Edit dialog now shows a multi-select "Requires:" listbox so dependencies between components can be declared. Stored in the new `component_dependencies` DB table via `InsertComponentDependency` / `GetDependenciesForComponent` / `DeleteDependenciesForComponent`.
- **Folder-level Required flag (`CompFolderEditDlg`)** — right-click any folder in the Components tree to cascade the Required flag to all files in that folder and its subfolders in one step, with a descriptive cascade-hint label.
- **Context menus on Components panes** — right-click in the Components TreeView or ListView shows an Edit context menu item.
- `component_dependencies` DB table — schema + migration for existing DBs.

### Fixed
- **Drag-and-drop: toolbar button hover blocked** — `SetCapture` was called on every `WM_LBUTTONDOWN` in the TreeView subclass, preventing toolbar buttons from receiving `WM_MOUSEMOVE` (hover) until the mouse button was released. Capture is now deferred until the drag threshold is actually exceeded.
- **Files page `TVN_SELCHANGED` icon lookup** — `SHGetFileInfoW` now uses `SHGFI_USEFILEATTRIBUTES` so icons are resolved by extension only, with no filesystem access and no UI-thread blocking.
- **Tooltip window no longer steals keyboard focus** — `WS_EX_NOACTIVATE` added to the tooltip window extended styles.
- **Context menu alt-tab freeze** — `SetForegroundWindow` + `PostMessage(WM_NULL)` added around the Files-page TreeView `TrackPopupMenu` call; fixes the Windows issue where the popup blocks until the owning window is re-activated after an alt-tab.

### Changed
- **Removed tooltip debug log** — `tooltip_debug.log` written by `ShowMultilingualTooltip` / `HideTooltip` was hitting the disk on the UI thread. Logging removed; use `OutputDebugStringW` or a debugger instead.
- `tooltip_API.txt` §13 updated to document the removed log and recommend `OutputDebugStringW`.

## [2026.03.09.16] - 2026-03-09

### Fixed
- **Files page drag-and-drop: Merge brings all files across**: When merging a real-disk-path folder into an existing same-named folder, the files inside the dragged folder were silently dropped — they lived on disk, not in `s_virtualFolderFiles`, so the merge code never found them. `IngestRealPathFiles` now reads the folder's direct children from disk and appends them as `VirtualFolderFile` entries into the target before the merge proceeds.
- **Files page drag-and-drop: erratic UI freeze eliminated**: After a move or merge, clicking the resulting tree node triggered `TVN_SELCHANGED → PopulateListView`, which called `SHGetFileInfoW` per file on the UI thread and caused intermittent hangs. `CloneTreeSubtree` now always creates the clone with an empty lParam (virtual node) and calls `IngestRealPathFiles` to pre-populate `s_virtualFolderFiles`. `TVN_SELCHANGED` therefore always reads from the in-memory map and never blocks on a disk scan.

## [2026.03.09.12] - 2026-03-09

### Fixed
- **Tooltip Greek/Cyrillic/Ukrainian rendering**: Windows 11 returns `"Segoe UI Variable"` from `NONCLIENTMETRICS`, which is a GDI variable font that cannot render non-Latin scripts — they displayed as `|||||||`. `InitTooltipSystem` in `tooltip.cpp` now derives the font **height** from `NONCLIENTMETRICS` (keeps DPI-correctness) but overrides the face name to `"Segoe UI"` (the classic version with full Unicode coverage for Latin, Greek, Cyrillic, Arabic, Hebrew, CJK). A `DO NOT MODIFY` comment block documents why the face name must not be reverted to the system default.
- **Entry screen `g_guiFont` uses same font rule**: `CreateFontW` with hardcoded `"Segoe UI"` and a manually-scaled pixel size replaced by `NONCLIENTMETRICS` height + `"Segoe UI"` face name override — consistent with the tooltip font and DPI-correct.
- **Removed dead `g_tooltipFont` / `g_tooltipText` in `main.cpp`**: Two variables that were created but never read by the tooltip system have been removed. The tooltip system owns its font entirely inside `tooltip.cpp`.

### Added
- `entry_screen.h`: New header file declaring `EntryScreen_Run()` and `ENTRY_SCREEN_CLASS` — groundwork for moving the entry screen into its own translation unit to prevent future font/tooltip regressions.

## [2026.03.09.09] - 2026-03-09

### Added
- Entry page button tooltips: all four buttons (New Project, Open Project, Delete Project, Exit) now show hover tooltips via `SetButtonTooltip()`. Locale keys: `new_project_hint`, `open_project_hint`, `delete_project_hint`, `exit_hint` — added to `en_GB.txt`. Falls back to hardcoded English strings for locales missing the keys. Tooltips refresh on `CBN_SELCHANGE`.
- `tooltip_API.txt` §15 “Unicode and encoding”: documents UTF-8 BOM auto-stripping, Segoe UI Unicode coverage, ANSI-not-supported rule, and `Utf8ToW()` / `MultiByteToWideChar` usage

### Fixed
- **UTF-8 BOM in `LoadLocaleFile`**: all 20 locale files begin with `EF BB BF`. Any file whose first line is a key–value pair (no `#` comment) had its first key stored as `U+FEFF+keyname`, silently breaking lookups. `LoadLocaleFile` now strips the BOM from the first read line. Files without BOM are unaffected.

## [2026.03.08.10] - 2026-03-08

### Added
- Per-project **Add Folder** last-location memory: DB key `last_picker_folder_<projectId>` — `SHBrowseForFolderW` opens at last used parent directory via `PickerFolderCallback` / `BFFM_SETSELECTION`
- Per-project **Add Files** last-location memory: DB key `last_picker_files_<projectId>` — `GetOpenFileNameW` `lpstrInitialDir` set from DB; first-use fallback to `%USERPROFILE%` to prevent shell state bleed from the folder picker
- Drag-and-drop infrastructure: `DragSource` enum, drag state statics, `EnsureDragCursors()` (shell32 icon 109 = no-drop / icon 300 = can-drop), `IsDragDropValid()`, `CloneTreeSubtree()`, `CancelDrag()`, `HitTestTreeView()`, drop-target highlight via `TreeView_SelectDropTarget`, `WM_LBUTTONUP` drop handler, `WM_SETCURSOR` cursor override, `WM_CAPTURECHANGED` guard
- Public accessors on `MainWindow`: `GetFilesTreeView()`, `GetProgramFilesRoot()`, `GetProgramDataRoot()`, `GetAppDataRoot()`, `GetAskAtInstallRoot()`; `AddTreeNode()` moved to public

### Changed
- Removed `ImageList_DragEnter` / `ImageList_DragMove` ghost image — DWM compositing renders it behind all windows causing visual artifacts
- Drag activation refactored to `WM_PARENTNOTIFY` + threshold check (temporary; will be replaced by TreeView/ListView subclassing next session)

## [2026.03.08.09] - 2026-03-08

### Added
- `Close Project` toolbar button (`IDC_TB_CLOSE_PROJECT = 5083`) between Save and Exit — red, shell32.dll icon 131, forwards to `IDM_FILE_CLOSE`, i18n-ready via `close_project` locale key
- `SetButtonTooltip(HWND, const wchar_t*)` in `button.h`/`button.cpp` — stores tooltip text as `"TooltipText"` window property; `ButtonSubclassProc` shows on hover and hides on mouse-leave automatically
- `button_INTERNALS.txt` — full internal reference for button system, toolbar layout, icon indices, and step-by-step guide for new toolbar buttons

### Changed
- `ButtonSubclassProc` `WM_MOUSEMOVE` now shows tooltip if `"TooltipText"` property set; `WM_MOUSELEAVE` now calls `HideTooltip()` unconditionally
- `API_list.txt` updated with `button_INTERNALS.txt` entry

## [2026.03.08.08] - 2026-03-08

### Fixed
- Full tree (folders + files) now saved to DB on every Save via new `SaveTreeToDb` recursive helper — previously only virtual-node files were written, physical-path folder nodes produced zero DB rows
- Files-page tree now rebuilt from DB rows on project open (new Priority-2 DB-rebuild load path) — previously the `files` table was never queried on load, so projects with no session snapshot and no `directory` opened to a blank tree
- `s_currentProject.directory` now synced from `IDC_INSTALL_FOLDER` before `DB::UpdateProject` on every Save — was always empty for projects that never opened the Settings page

### Added
- `FileRow` struct in `db.h` mirroring the `files` table
- `DB::GetFilesForProject(int projectId)` in `db.cpp` — returns all file/folder rows for a project; `install_scope="__folder__"` marks folder nodes
- `SaveTreeToDb` static helper in `mainwindow.cpp` — recursively writes complete tree snapshot to DB
- `files_save_load_INTERNALS.txt` — internal architecture reference for the save/load subsystem
- `API_list.txt` — documentation index with `_API` vs `_INTERNALS` naming convention

## [Unreleased] - 2026-02-23

### Added
- TreeView indentation increased to 19 pixels for better visual hierarchy
- Project name field now separate from install folder path
- Automatic folder creation when adding files (uses filename without extension)
- Context menu "Create Folder" and "Remove Folder" options with confirmation dialogs
- Install path now displays as read-only dark blue text (RGB 0,51,153)

### Fixed
- Install path now properly updates when first folder under Program Files is renamed
- "Add Folder" button now correctly adds folders under Program Files hierarchy
- "Add Files" button creates folder structure automatically if none exists
- Install path updates correctly after folder deletion
- Project name protection - stops auto-updating once manually edited by user
- Folder hierarchy display with proper indentation under Program Files

### Changed
- All "directory" terminology changed to "folder" throughout UI
- Install folder field changed from editable to read-only display
- Quit dialog YES button now uses shell32.dll icon #112 with transparent background
- Button icon rendering uses PrivateExtractIconsW for proper transparency

### Technical
- Added UpdateInstallPathFromTree() helper function for centralized path updates
- Improved TreeView label edit handling to check first child position
- Enhanced folder add/remove operations to trigger install path updates
- Fixed EN_CHANGE event handling during programmatic control updates

## [Initial Release]

### Added
- Entry screen with language selection (20 languages supported)
- Project database management (SQLite)
- Create, open, and delete project functionality
- Main window with 8-button toolbar (Files, Registry, Shortcuts, Dependencies, Settings, Build, Test, Scripts)
- Files Management page with TreeView/ListView split-pane interface
- Multi-select support (Ctrl/Shift for files, checkboxes for folders)
- System file type icons in ListView
- Install directory configuration with folder picker
- Professional quit dialog with confirmation (Ctrl+W shortcut)
- Native Windows UI with system icons
- Full internationalization support
- Keyboard shortcuts (Ctrl+W for exit, F7 for Build, F5 for Test)
