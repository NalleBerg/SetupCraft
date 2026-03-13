# Changelog

All notable changes to SetupCraft will be documented in this file.

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
