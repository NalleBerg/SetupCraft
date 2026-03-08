# Changelog

All notable changes to SetupCraft will be documented in this file.

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
