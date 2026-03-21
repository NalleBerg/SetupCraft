# SetupCraft

An installer creation tool for making your developed packages distributable. Designed to be simple to use with a clean, native Windows interface.

**Current Release:** Version 2026.03.21.11 (Published: 21.03.2026 11:03)

> Note: This project is in active development. Entry screen, Files management, Shortcuts, and Dependencies pages are complete.

## Features

- **RTF Editor Component (`edit_rtf.h` / `edit_rtf.cpp`)**: General-purpose reusable rich-text editor modal — two-row toolbar (Bold / Italic / Underline / Strikethrough / Subscript / Superscript / Font Face / Font Size / Align L/C/R/J / Bullet / Numbered / Text colour / Highlight / Insert Image). `BS_AUTOCHECKBOX|BS_PUSHLIKE` toggle buttons synced from caret on `EN_SELCHANGE`. PNG and JPEG images embedded inline as hex-encoded `\pict\pngblip`/`\jpegblip` RTF blocks — no external files. Hover tooltips on all 17 controls via the project tooltip system. RTF streamed in/out; safe to round-trip through the DB. Test harness: `build\rtf_editor_test.exe` (CMake target `RtfEditorTest`)
- **DPI-Aware Scaling**: Full per-monitor DPI support via `S()` helper — all pixel values, fonts, tooltip dimensions, and custom dialog sizes scale correctly at any DPI (100%, 125%, 150%, 175%, 200%+). All custom dialogs use named layout constants + `S()` with `AdjustWindowRectEx` outer-window sizing — no hardcoded pixel values anywhere in the dialog system
- **Consistent Body Font**: All labels, edits, checkboxes, TreeViews, and ListViews — including the entry screen, new-project dialog, and tooltips — use a system-derived `NONCLIENTMETRICS` font at 120% scale for clear, legible text on every screen. Entry-screen labels and the new-project dialog title use the same scaling recipe as the main window (`× 1.2` body, `× 1.5 + FW_SEMIBOLD` title)
- **Dependencies Page**: New modular page for declaring external dependencies required by the installer — ListView with Name / Delivery / Required / Detection columns, Add / Edit / Remove buttons with right-click context menu. Full edit dialog with 18 fields: delivery type (Bundled / Auto-download / Redirect URL / Instructions only), Required checkbox, architecture (Any / x64 / ARM64 — x86 removed, app is 64-bit only), install order, registry-key and file-path detection, minimum version, conditional Network fields (URL, SHA-256, silent args, offline behaviour), license file picker, credits, and manual-install instructions. Scrollable dialog (`WS_VSCROLL`) clamped to work area. All state persisted to `external_deps` DB table on Save. Module in `deps.h/.cpp`; documented in `deps_INTERNALS.txt`
- **Shortcuts Page**: Dedicated page for configuring installer shortcuts — Desktop shortcut (with opt-out checkbox, multiple shortcuts, 16×16 mini-icon strip below the large icon), Start Menu & Programs folder hierarchy (editable TreeView with add/remove subfolders, inline rename, custom hover tooltip, "Add Shortcut Here" button and context menu item), Pin to Start, Pin to Taskbar. Full module in `shortcuts.h/.cpp`. Right-click context menus on tree and row buttons. All shortcuts and opt-out flags persisted to the `sc_shortcuts` and `sc_menu_nodes` DB tables on Save; restored on project open via `SC_LoadFromDb`. Automatic vertical scrollbar (native `WS_VSCROLL`) appears when content exceeds the window height; scroll position tracked via `SC_SetScrollOffset`/`SC_GetScrollOffset` so strip controls stay correctly positioned at any scroll offset; `WS_CLIPSIBLINGS` added to all page controls to prevent overdrawing the status bar when scrolled. All pin-strip checkboxes refresh instantly on every shortcut mutation (all 8 paths wired). Individual pin-strip checkboxes write back to `s_scShortcuts` immediately on click so pin state survives page switches; a lightweight `SC_RefreshPinLabels()` helper updates only the pin-button enable states without destroying/recreating checkbox HWNDs (avoids UB from `DestroyWindow` while `WM_LBUTTONUP` is still on the call stack). Enabled pin icons show a hover tooltip explaining how to use the checkboxes below; clicking the icon does nothing (reserved for a future shortcut-config dialog). **The Shortcuts page is feature-complete.**
- **Bold Page Titles**: Each page has a prominent semi-bold heading rendered with a dedicated `s_hPageTitleFont` (150% NONCLIENTMETRICS) — correctly applied via `WM_CTLCOLORSTATIC` ID check so the body-font override no longer clobbers it
- **Two-Row Toolbar**: 12 buttons in two compact rows — Row 1: Files, Components, Registry, Shortcuts, Dependencies, Dialogs; Row 2: Settings, Scripts, Test, Build, Save, Close Project, Exit. About «i» icon centered vertically at the right end
- **Components Page**: Full component-based installation page — enable/disable toggle, split-pane TreeView + ListView layout (mirrors Files page), VFS folder tree on the left, file list on the right, Edit actions, modal edit dialog with auto-fill and dependency selection. All component mutations work purely in memory; DB is written only on explicit Save (`IDM_FILE_SAVE`)
- **Required-Folder Icon**: Folders in the Components tree where every file (recursively) carries the Required flag are shown with `shell32.dll` sequential index 110 — the classic yellow folder with blue checkmark badge, loaded via `ExtractIconExW`. Assigned by `UpdateCompTreeRequiredIcons()` after each tree build or Edit Folder operation; reverts automatically when any file inside loses the Required flag. Subfolders with no registered component entries inherit the badge from their parent via `parentIsRequired` propagation. Two-phase matching: Phase 1 checks for a folder-type component row (`source_path == snap->fullPath`) as the authoritative answer; Phase 2 scans per-file rows (unregistered files clear `allRequired` to prevent false positives on container folders). Right-clicking any folder in the comp tree auto-upserts a folder-type row so the flag persists across page switches without a manual Save. Required state also survives Files page deletions — `PurgeComponentRowsByPaths` now works in-memory only
- **Required-Folder Hover Tooltip**: Hovering over a required-folder icon in the Components tree shows a "Required" tooltip via `CompTree_TooltipSubclassProc` — uses `TreeView_HitTest` to detect the hovered item and `ShowMultilingualTooltip` for display
- **Native Blue Multi-Select (Files page)**: Checkboxes removed (`TVS_CHECKBOXES`). Multi-selection is tracked in `s_filesTreeMultiSel` (`std::set<HTREEITEM>`). Ctrl+Click toggles items, Shift+Click selects a contiguous range, plain click clears the selection. `NM_CUSTOMDRAW` paints selected items with the system highlight colour — identical to Windows Explorer. Remove iterates the entire selection set
- **Components Folder TreeView**: Left pane of the Components page shows the virtual folder tree (VFS snapshots) from the Files page; selecting a folder instantly shows its files with component metadata in the right pane
- **Components Enable Auto-Populate**: Toggling "Enable components" auto-creates `ComponentRow` entries for every file in the current VFS snapshot — no manual Add clicks required; disabling clears all components from memory (persisted to DB on Save)
- **VFS Picker Dialog**: "Add Files / Folders" opens a split-pane VFS browser instead of the native file dialog — pick files or real-path folders from the tree already built on the Files page
- **Component Dependencies**: Component Edit dialog shows a multi-select "Requires:" listbox to declare which other components a component depends on; stored in the new `component_dependencies` DB table
- **Components Disabled Tooltip**: Hovering the grayed-out Components button shows an i18n tooltip ("Components are not available yet. Go to the Files page and add at least one file or folder first.") — implemented with a 60 ms timer-callback poll, no subclass or TrackMouseEvent (disabled windows cause blink loop with those approaches)
- **Auto-Measured Toolbar Buttons**: All 12 toolbar buttons measure their label width at runtime via `GetTextExtentPoint32W` with the bold NONCLIENTMETRICS font — correct widths for every language, no hardcoded pixel values
- **Tight Multiline Tooltip Sizing**: Multiline tooltips measure each line individually and use only as much width as the widest line requires — no more excess whitespace around short messages
- **Components Button Auto-Enable**: Toolbar Components button is grayed out until at least one file/folder exists on the Files page
- **Files Page Tree Persistence**: Full `TreeNodeSnapshot` recursive approach — folder hierarchy, virtual folders, and their associated files all survive page switches intact. All four tree roots (ProgramFiles, ProgramData, AppData, AskAtInstall) preserved
- **Files Management**: Split-pane interface with TreeView (32×32 folder icons, 34 px row height) and ListView for visual file selection. Remove confirms before deleting — leaf nodes and file selections both prompt
- **Selection-Based Remove**: TreeView uses plain blue selection highlight (no checkboxes). Remove deletes the selected item with a per-item or recursive confirmation dialog
- **Saved/Unsaved Indicator**: Owner-drawn right section of the status bar shows green ✔ Saved or red ● Unsaved, updating instantly on every change and on save
- **Persistent Ask-at-Install**: "Ask end user at install" checkbox state stored per-project via `DB::SetSetting` and restored on project open
- **Registry Management**: Complete Windows Registry integration page for Add/Remove Programs registration with icon preview and registry path navigation
- **Registry Backup System**: "Create Restore Point" button creates Windows System Restore Point before registry edits with animated spinner dialog
- **Registry Warning Tooltip**: Custom tooltip with light yellow background matching globe icon style, explains risks and recommends restore point creation
- **Registry Tree Structure**: Clean registry hierarchy showing only keys with actual values - Uninstall key displays DisplayName, DisplayVersion, Publisher, InstallLocation, DisplayIcon, UninstallString
- **Registry Templates**: All 5 HKEY hives (HKCR, HKCU, HKLM, HKU, HKCC) with common paths pre-expanded for easy navigation
- **Show Regkey Dialog**: Professional dialog displaying full uninstall registry path with Copy button, Ctrl+C support, and right-click context menu
- **About Dialog System**: Professional About dialog with SetupCraft logo (PNG with transparency), formatted RichEdit content, and colored sections matching WinUpdate template
- **Icon-Only About Button**: Compact About button (40px) with shell32.dll icon #221 and tooltip support matching globe icon pattern
- **License Dialog**: Enhanced GPL v2 license display with GnuLogo.bmp, formatted sections, colored headers (blue/red), and proper parsing
- **Multi-Size ICO Generation**: `make_ico.ps1` PowerShell script uses ImageMagick to repack any source `.ico` into a multi-frame icon (16, 24, 32, 40, 48, 64, 96, 128 px). `icons/trashcan_empty.ico` embedded as resource ID 2 (`IDI_TRASHCAN`)  
- **Version Management**: Centralized version control via curver.txt file and NewVersion.ps1 PowerShell script for automated version updates
- **Dynamic Version Loading**: About dialog reads Published timestamp and Version from curver.txt at runtime (no recompile needed for version changes)
- **Take Me There Navigation**: Navigate to registry key in TreeView and automatically populate ListView with uninstall values (DisplayName, DisplayVersion, Publisher, etc.)
- **Spinner Dialog System**: Modal loading dialog with animated multi-line text display for long-running operations
- **Virtual Folder Support**: Create custom folder structures without physical disk paths - files and full hierarchy persist when navigating between pages
- **Context Menus on Both Panes**: Right-click in the TreeView shows Add Folder, Add Files, Create Folder, and (for non-system nodes) Remove. Right-click in the ListView shows Add Folder, Add Files, and (when a file is selected) Remove. System roots (Program Files, ProgramData, AppData, Ask at Install) never offer Remove
- **Duplicate Project Name Guard**: Saving a new project whose name already exists in the database raises a modal dialog — Overwrite (adopt existing record), Rename this one (inline rename dialog), or Cancel — preventing silent data loss
- **Smart Project Naming**: Automatically derives project name from first folder added under Program Files, and keeps updating it on every rename/replace until the user manually edits the name field (no longer stops after first save)
- **Save Always Works**: New projects with no database ID are created via `DB::InsertProject` on first save — no more "No project selected" error
- **Close Project Button**: Red toolbar button between Save and Exit — prompts to save unsaved changes then returns to the entry screen. i18n-ready via `close_project` locale key
- **Per-Button Hover Tooltips**: `SetButtonTooltip(hBtn, text)` registers a plain-text tooltip on any enabled toolbar button; `ButtonSubclassProc` shows it on first hover and hides on mouse-leave with no extra tracking state
- **Picker Last-Folder Memory**: Add Folder and Add Files both remember the last used directory per project in the DB (`last_picker_folder_<id>` / `last_picker_files_<id>`). File picker first-use falls back to `%USERPROFILE%` to prevent cross-picker shell state sharing
- **Drag-and-Drop (Files Page)**: Fully working tree node drag-and-drop — move a folder anywhere in the tree, or drop onto a same-named folder to Merge (files carried across, recursively), Overwrite, or Cancel. Drop-target highlight, cursor feedback (no-drop / can-drop cursors), and freeze-free selection (nodes are always virtualized after a drop so `TVN_SELCHANGED` never blocks on a disk scan). Capture is deferred until the drag threshold is exceeded so toolbar hover is never blocked
- **Components Page Info Icon**: Floppy-disk icon (shell32.dll #258) to the right of the Components hint label shows a tooltip warning that files and folders will not appear in the dependency picker until the project has been saved at least once. "FYI!" is painted directly onto the diskette's white label strip. Implemented with the correct `SS_NOTIFY`-static + `GWLP_USERDATA` + `WM_PAINT`/`DrawIconEx` technique (not `SS_ICON`, which auto-resizes the control on `STM_SETICON`).
- **Dep Picker Fully Populated**: Dependency picker now shows complete VFS tree including AskAtInstall subfolders and files, real-path folder contents (locale files, img/ directories etc.), and files without an existing ComponentRow (auto-file nodes with synthetic lParam). `EnsureTreeSnapshotsFromDb()` was rewritten with a stable `std::map` + three-pass bottom-up linking; `PopulateSnapshotFilesFromDisk()` fills any real-path node left empty by the DB pass via a one-time `FindFirstFileW` scan per dep-picker open.
- **Folder-Level Pre-selected Flag**: Each folder component can be marked "Pre-selected" — ticked by default in the installer so the end-user sees it enabled but can still uncheck it if it is not required. Checking the Required flag automatically force-ticks and locks Pre-selected (a required component is always pre-selected); unchecking Required releases the lock. Stored in the new `is_preselected` column in the components table, persisted on Save, and cascaded to all files in the section-scoped subtree alongside the Required flag
- **Folder-Level Required Flag**: Right-click any folder in the Components tree to cascade the Required flag to all files inside it and all subfolders via a single compact dialog. Cascade is section-scoped (`dest_path` in `ComponentRow`) so AskAtInstall entries are never touched when cascading a Program Files folder and vice-versa. Legacy rows with empty `dest_path` are repaired automatically on page load
- **Entry Page Button Tooltips**: All four entry-screen buttons (New Project, Open Project, Delete Project, Exit) show hover tooltips via `SetButtonTooltip()`, i18n-ready via locale keys `new_project_hint` etc., refreshed on language switch
- **UTF-8 BOM Fix**: `LoadLocaleFile` now strips the 3-byte BOM (`EF BB BF`) from the first line of every locale file — all 20 bundled locales were BOM-encoded, causing the first key in each file to silently fail lookup. Cyrillic, Greek, emoji and all other non-ASCII characters now display correctly across every language
- **Full Tree Persistence Across Restarts**: `SaveTreeToDb` walks all four tree roots on every Save and writes every folder node and file to the DB `files` table. On project open a DB-rebuild path reconstructs the exact tree from those rows — no dependency on a live disk path. Three bugs fixed: tree not saved, tree not loaded from DB, `directory` field not synced
- **`DB::GetFilesForProject()`**: New DB function returns all file/folder rows for a project; used by the load path to rebuild the Files-page tree from scratch on restart
- **Context-Aware Operations**: Add Folder/Add Files buttons respect currently selected folder as parent/target
- **Install Path Display**: Read-only dark blue install path display that reflects actual folder structure
- **Add Folder/Files**: Buttons to add existing folders or individual files with automatic folder structure creation
- **File Type Icons**: System icons for all file types (exe, dll, txt, md, png, etc.) with transparent backgrounds
- **Multilingual Support**: 20 languages with native translations (Norwegian, English, Greek, Spanish, German, French, Italian, Dutch, and more)
- **SQLite Database**: Project configurations stored in `%APPDATA%\SetupCraft\SetupCraft.db`
- **Native Windows UI**: Clean interface using Windows system icons and native styling
- **Project Management**: Create, open, and delete installation projects
- **Intuitive Design**: Globe icon with multilingual tooltip showing all available languages
- **Professional Quit System**: Confirmation dialog with Yes/No buttons - triggered by Exit button, X button, or Ctrl+W keyboard shortcut. Close Project shows a dedicated "Do you want to close this project?" dialog (i18n-ready)
- **Unsaved Changes Warning**: Custom 3-button dialog (Save/Don't Save/Cancel) warns before closing projects with unsaved changes — Save branch now actually saves
- **Entry Screen Restored Correctly**: Returning to the entry screen after closing a project re-enables it (was frozen/non-interactive)
- **About Icon Functionality**: Clickable About (i) icons on both entry screen and main window with tooltip support - WM_LBUTTONDOWN handler for reliable click detection
- **Keyboard Shortcuts**: Ctrl+W for quick exit with confirmation, F7 for Build, F5 for Test

## Current Status

✅ Entry screen complete with language selection
✅ Project database and management system
✅ Create new project functionality
✅ Open existing project dialog
✅ Delete project with confirmation
✅ Native Windows button styling with system icons
✅ Full internationalization (i18n) support
✅ Quit confirmation dialog with Ctrl+W support
✅ Main window with 8-button toolbar
✅ Files page with TreeView/ListView split-pane
✅ TreeView proper indentation and folder hierarchy display
✅ Project name and install path separation
✅ Add Folder button (adds to Program Files structure)
✅ Add Files button (auto-creates folder structure)
✅ Context menu with Create/Remove Folder options
✅ File/folder multi-select and removal
✅ Install path auto-updates with folder operations
✅ Read-only install path display (dark blue)
✅ TreeView selection updates ListView automatically
✅ System file type icons in ListView with transparency
✅ Tooltips showing full file paths
✅ Scripts page for before/after install hooks
✅ Registry page with Windows Installed Apps integration
✅ Registry TreeView with all 5 HKEY hives
✅ Show Regkey dialog with copy/navigation functionality
✅ Registry value population in ListView
✅ Icon preview with default generic Windows icon
✅ Registry backup system with System Restore Point creation
✅ Spinner dialog with animated loading and multi-line text
✅ Warning tooltip with custom styling matching globe tooltip
✅ About dialog with SetupCraft logo and formatted content
✅ Icon-only About button with tooltip support
✅ Enhanced license dialog with GnuLogo and formatted GPL text
✅ Version management system (curver.txt + NewVersion.ps1)
✅ Dynamic version loading in About dialog
✅ About icon click functionality on entry screen and main window
✅ Unsaved changes warning dialog (Save/Don't Save/Cancel)
✅ Close Project confirmation with unsaved changes detection

🔄 In Progress: Registry edit functionality for keys and values, Build/Test implementation, Shortcuts page

## Building

Use the provided build script:
```cmd
makeit.bat
```

This will compile the project using MinGW and create the executable in the `SetupCraft` directory.

## Requirements

- Windows OS
- MinGW (for building)
- CMake
- SQLite3 (included)

## License

This project is licensed under GPLv2. See [GPLv2.md](GPLv2.md) for details.

## Prebuilt Binaries

Prebuilt binaries are included in the `SetupCraft` directory for quick testing without compilation.
