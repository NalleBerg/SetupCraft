# SetupCraft

An installer creation tool for making your developed packages distributable. Designed to be simple to use with a clean, native Windows interface.

**Current Release:** Version 2026.05.02.13 (Published: 02.05.2026 13:50)

> Note: This project is in active development. Entry screen, Files management, Shortcuts, Dependencies, Dialogs, and Scripts pages are complete.

## Bundled Freeware Utilities

Two small standalone tools are included in the repository under their own subfolders. Both are public-domain freeware, statically linked single-exe Win32 applications built with MinGW.

### GlyphPicker (`glyphpicker/`)

A Unicode glyph and emoji picker. Opens to a scrollable grid showing every renderable codepoint across a curated set of Unicode blocks — Smileys, People & Body, Animals, Food, Travel, Activities, Objects, Symbols, Currency, Greek, Cyrillic, Arrows, Math, and more. Only codepoints that the installed fonts can actually render are shown; empty squares for missing glyphs are filtered out at startup using `IDWriteFontFace::GetGlyphIndices`. Emoji are drawn in full color via Direct2D + DirectWrite (`D2D1_DRAW_TEXT_OPTIONS_ENABLE_COLOR_FONT`); non-emoji glyphs fall back to Segoe UI. Click a glyph once to select it, click again (or press Enter / Space) to copy it to the clipboard. Search by U+hex prefix to filter across all blocks. A live `HH:MM:SS` clock is displayed in the status bar. DPI-aware, Common Controls v6.

### StopWatch (`stopwatch/`)

An app-launching stopwatch. Screen 1 lets you optionally pick an executable to launch; pressing Start fires the app and immediately switches to Screen 2, which shows a large elapsed-time display (`H:MM:SS.cc`) with four buttons: Reset (zero the elapsed time, keep running), Pause (freeze the display — the clock keeps ticking; un-pausing jumps to live time), Stop/Start (actually halt and resume the clock), and Always-on-top toggle. Settings (app path, always-on-top state) are saved to an INI file next to the exe, or under `%APPDATA%\Stopwatch\` if the exe folder is read-only. Icon from `shell32.dll` index 249. Released to the public domain under The Unlicense.

## Features

- **RTF Editor Component (`edit_rtf.h` / `edit_rtf.cpp`)**: General-purpose reusable rich-text editor modal — responsive toolbar (one or two rows, `RtfEd_LayoutToolbar`) with 19 controls: Bold / Italic / Underline / Strikethrough / Subscript / Superscript / Font Face / Font Size / Align L/C/R/J / Bullet / Numbered / Text colour / Highlight / Insert Image / Open file / Insert table. `BS_AUTOCHECKBOX|BS_PUSHLIKE` toggle buttons synced from caret on `EN_SELCHANGE`. Ctrl+B/I/U keyboard shortcuts. PNG and JPEG images embedded inline as hex-encoded `\pict\pngblip`/`\jpegblip` RTF blocks — no external files. File Open button streams RTF, TXT, and Markdown (`.md`) files into the editor. Table insertion via ⊞ toolbar button: configurable rows × cols, table width %, row height, border style/colour/width, table alignment (row position on page), cell V alignment, **unit picker button** (px ▼ / % ▼, `IDC_RTFE_TD_COLWUNIT`) for column width — clicking converts the current value between px and % modes; new tables default to **50 %** so a 2-column table fills 100 % of the editor by default. Cell widths are computed as `physPx × 15 / g_dpiScale` (DPI-correct at any scaling) using `EM_GETRECT` to get the exact formatting-rectangle width. Pct-mode tables are rescaled on window resize via `RtfEd_RescalePctTables()` (80 ms debounced timer). Cell H alignment applied via `EM_SETPARAFORMAT PFM_ALIGNMENT`. Right-click inside a table shows "Table properties…" + "Cell alignment ▶" submenu (Align left / Centre / Right); right-click outside falls through to the standard cut/copy/paste menu. Hover tooltips on all controls via the project tooltip system; all UI strings look up keys in `RtfEditorData::pLocale`. RTF streamed in/out; safe to round-trip through the DB.
- **DPI-Aware Scaling**: Full per-monitor DPI support via `S()` helper — all pixel values, fonts, tooltip dimensions, and custom dialog sizes scale correctly at any DPI (100%, 125%, 150%, 175%, 200%+). All custom dialogs use named layout constants + `S()` with `AdjustWindowRectEx` outer-window sizing — no hardcoded pixel values anywhere in the dialog system
- **Consistent Body Font**: All labels, edits, checkboxes, TreeViews, and ListViews — including the entry screen, new-project dialog, and tooltips — use a system-derived `NONCLIENTMETRICS` font at 120% scale for clear, legible text on every screen. Entry-screen labels and the new-project dialog title use the same scaling recipe as the main window (`× 1.2` body, `× 1.5 + FW_SEMIBOLD` title)
- **Dialogs Page**: Installer-dialogs configuration page (toolbar index 4) — one row per dialog type (Welcome, License, Dependencies, For Me/All Users, Components, Shortcuts, Ready to Install, Install, Finish) with 32×32 shell32.dll icon, type name, "Edit Content…" and "Preview…" buttons per row. Conditional rows appear only when the corresponding feature is enabled. Installer-title section at the top: 48×48 icon preview (default shell32 #2), "Change Icon…" OFN_EXPLORER picker (*.ico only), and title edit field auto-filled from the project name. Preview popup (`WS_POPUP | WS_CAPTION`, no × button) shows a styled facsimile with a read-only RichEdit and Back/Next/Cancel buttons; Back ◀ / Next ▶ navigate through all visible dialog types, Next becomes "Finish ✔" at the last page; clicking Finish shows an "End of Preview" modal (disabled checked "Open \<AppName\>" checkbox + OK) before closing the preview. A floating sizer panel (Width/Height spinners, 200–1400/150–1000 logical px) to the left of the preview resizes it live; dimensions are DPI-scaled and persisted to the DB. The Components page auto-fits its height on open using `EM_FORMATRANGE` content measurement (`MeasureRichEditLogHeight()`); both the no-RTF layout and the split RTF+items layout are supported. Once the developer adjusts the size via the sizer, auto-fitting is suppressed for that project (`s_previewUserSized`, persisted to DB). Module in `dialogs.h/.cpp`; documented in `dialogs_INTERNALS.txt`
- **Dependencies Page**: New modular page for declaring external dependencies required by the installer — ListView with Name / Delivery / Required / Detection columns, Add / Edit / Remove buttons with right-click context menu. Full edit dialog with progressive disclosure per delivery type: Bundled → Required · Install step · License (optional) · Credits (optional); Auto-download → Required · Install step · Detection (optional: registry key, file path, minimum version) · Network (URL, silent args (optional), SHA-256 (optional), offline behaviour) · License (optional) · Credits (optional); Redirect URL → same as Auto-download but Network shows only URL + offline; Instructions only → Required · Install step · Instructions (row-wrapped icon grid, each page editable/removable, stored in `dep_instructions` DB table). "Before the Welcome screen (silent)" install step is automatically removed from the combo when Instructions Only is selected. Add vs. Edit title awareness (`dep.id == 0`). Dialog fonts use `SPI_GETNONCLIENTMETRICS` at the same scale as the main window (×1.2 body, ×1.5 semi-bold title). Dialog positioned near the main window title bar. Scrollable dialog (`WS_VSCROLL`) clamped to work area. Validation errors shown via the styled `ShowValidationDialog()` (i18n-correct). All state persisted to `external_deps` + `dep_instructions` DB tables on Save. Custom scrollbars (V + H) via `my_scrollbar`; H-scroll tilt-wheel works both directions; custom H-bar fades in when content overflows. Arrow buttons, track-click, and thumb drag pending. Module in `deps.h/.cpp`; documented in `deps_INTERNALS.txt`
- **Shortcuts Page**: Dedicated page for configuring installer shortcuts — Desktop shortcut (with opt-out checkbox, multiple shortcuts, 16×16 mini-icon strip below the large icon), Start Menu & Programs folder hierarchy (editable TreeView with add/remove subfolders, inline rename, custom hover tooltip, "Add Shortcut Here" button and context menu item), Pin to Start, Pin to Taskbar. Full module in `shortcuts.h/.cpp`. Right-click context menus on tree and row buttons. All shortcuts and opt-out flags persisted to the `sc_shortcuts` and `sc_menu_nodes` DB tables on Save; restored on project open via `SC_LoadFromDb`. Custom hidden scrollbar (`s_hMsbSc`, `my_scrollbar`) appears when content exceeds the window height; scroll position tracked via `SC_SetScrollOffset`/`SC_GetScrollOffset` so strip controls stay correctly positioned at any scroll offset; `WS_CLIPSIBLINGS` added to all page controls to prevent overdrawing the status bar when scrolled. All pin-strip checkboxes refresh instantly on every shortcut mutation (all 8 paths wired). Individual pin-strip checkboxes write back to `s_scShortcuts` immediately on click so pin state survives page switches; a lightweight `SC_RefreshPinLabels()` helper updates only the pin-button enable states without destroying/recreating checkbox HWNDs (avoids UB from `DestroyWindow` while `WM_LBUTTONUP` is still on the call stack). Enabled pin icons show a hover tooltip explaining how to use the checkboxes below; clicking the icon does nothing (reserved for a future shortcut-config dialog). **The Shortcuts page is feature-complete.**
- **Bold Page Titles**: Each page has a prominent semi-bold heading rendered with a dedicated `s_hPageTitleFont` (150% NONCLIENTMETRICS) — correctly applied via `WM_CTLCOLORSTATIC` ID check so the body-font override no longer clobbers it
- **Two-Row Toolbar**: 12 buttons in two compact rows — Row 1: Files, Components, Registry, Shortcuts, Dependencies, Dialogs; Row 2: Settings, Scripts, Test, Build, Save, Close Project, Exit. About «i» icon centered vertically at the right end. The active page button shows a light blue background (`RGB(196, 224, 246)`) so the current page is always obvious; Save, Close Project, and Exit are excluded as they do not navigate to a page
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
- **About Dialog System**: Full About, License, and Credits dialogs — SetupCraft logo (PNG with transparency), formatted RichEdit content with colored sections. About page has three buttons: View License (Blue, shell32 #221), Credits (Green, shell32 #294), Close (Red, shell32 #131) — all sized via `MeasureButtonWidth` for i18n correctness in all 20 languages. Credits dialog has two sections (Inno Setup in orange, Scintilla in teal) with decorative underlines, descriptive text, and clickable URLs (`ShellExecuteW` + hand cursor via `CreditsEditSubclassProc` `WM_SETCURSOR`/`CFE_LINK` detection). License OK: Green, shell32 #294. Custom `my_scrollbar` on all three RichEdits. Body font uses `SPI_GETNONCLIENTMETRICS` at ×1.2 scale — matching every other label in the project. All strings i18n via `Loc()`. Both dialog functions accept a `const std::map<std::wstring,std::wstring>& locale` parameter. Tooltip on the About icon hides reliably before the modal loop disables the parent — both on the main window and the entry screen.
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

## Developer Documentation

All subsystem documentation lives in plain-text files in the root of the repository. [`API_list.txt`](API_list.txt) is the master index — read it first. The files follow a simple naming convention:

| Suffix | Contents |
|--------|----------|
| `_API.txt` | Public interface — what to call, parameters, return values, usage examples |
| `_INTERNALS.txt` | Architecture and flow reference — how the subsystem works inside, bug history, fix guidance |

### Quick reference

| File | What it covers |
|------|---------------|
| [`API_list.txt`](API_list.txt) | Master documentation index — start here |
| [`API_INTERNALS/API/my_scrollbar_API.txt`](API_INTERNALS/API/my_scrollbar_API.txt) | Custom fade scrollbar — `msb_attach/detach/sync/reposition`, flags, wiring pattern, common mistakes |
| [`API_INTERNALS/API/tooltip_API.txt`](API_INTERNALS/API/tooltip_API.txt) | Multilingual tooltip system — creation, positioning, subclassing, locale integration |
| [`API_INTERNALS/API/edit_rtf_API.txt`](API_INTERNALS/API/edit_rtf_API.txt) | General-purpose RTF editor modal — `OpenRtfEditor()`, responsive toolbar, image embedding |
| [`API_INTERNALS/API/notes_editor_API.txt`](API_INTERNALS/API/notes_editor_API.txt) | Compact RTF notes/description popup — `OpenNotesEditor()`, RTF stream in/out |
| [`API_INTERNALS/API/vfs_picker_API.txt`](API_INTERNALS/API/vfs_picker_API.txt) | Reusable VFS file/folder picker dialog — `ShowVfsPicker()`, `VfsPickerParams`, result struct |
| [`API_INTERNALS/API/dragdrop_API.txt`](API_INTERNALS/API/dragdrop_API.txt) | Win32 TreeView/ListView drag-and-drop module — subclass detection, callbacks, parent hooks |
| [`API_INTERNALS/API/spinner_dialog_API.txt`](API_INTERNALS/API/spinner_dialog_API.txt) | Modal progress/spinner dialog — API, thread usage, progress callback protocol |
| [`API_INTERNALS/API/checkbox_API.txt`](API_INTERNALS/API/checkbox_API.txt) | Theme-aware custom checkbox — creation, drawing, TreeView state images, step-by-step guide |
| [`API_INTERNALS/API/shell32_API.txt`](API_INTERNALS/API/shell32_API.txt) | Shell icon loading reference — verified sequential indices for shell32.dll/imageres.dll |
| [`API_INTERNALS/API/skeleton_idea.txt`](API_INTERNALS/API/skeleton_idea.txt) | Future project skeleton checklist — modules, build system, decisions for publish time |
| [`API_INTERNALS/INTERNALS/button_INTERNALS.txt`](API_INTERNALS/INTERNALS/button_INTERNALS.txt) | Custom button and toolbar system — drawing, hover/tooltip flow, adding a new toolbar button |
| [`API_INTERNALS/INTERNALS/button_rightclick_combo_INTERNALS.txt`](API_INTERNALS/INTERNALS/button_rightclick_combo_INTERNALS.txt) | Reusable ListView + action buttons + right-click menu pattern |
| [`API_INTERNALS/INTERNALS/dialog_INTERNALS.txt`](API_INTERNALS/INTERNALS/dialog_INTERNALS.txt) | Custom modal dialog system — measure-then-create pattern, layout constants, Z-order policy |
| [`API_INTERNALS/INTERNALS/i18n_INTERNALS.txt`](API_INTERNALS/INTERNALS/i18n_INTERNALS.txt) | Internationalisation policy — locale file format, key naming, runtime lookup, adding new keys |
| [`API_INTERNALS/INTERNALS/files_save_load_INTERNALS.txt`](API_INTERNALS/INTERNALS/files_save_load_INTERNALS.txt) | Files-page save/load architecture — VFS, DB table, load path, bug history |
| [`API_INTERNALS/INTERNALS/shortcuts_INTERNALS.txt`](API_INTERNALS/INTERNALS/shortcuts_INTERNALS.txt) | Shortcuts page module — lifecycle, TreeView rename flow, context menu dispatch, locale keys |
| [`API_INTERNALS/INTERNALS/deps_INTERNALS.txt`](API_INTERNALS/INTERNALS/deps_INTERNALS.txt) | Dependencies page module — data model, enums, edit dialog, DB schema, wiring guide |
| [`API_INTERNALS/INTERNALS/dialogs_INTERNALS.txt`](API_INTERNALS/INTERNALS/dialogs_INTERNALS.txt) | Dialogs page module — row visibility rules, preview dialog, scrollbar wiring, locale keys |
| [`API_INTERNALS/INTERNALS/comp_deps_INTERNALS.txt`](API_INTERNALS/INTERNALS/comp_deps_INTERNALS.txt) | Component dependency panel — ListView subclass, VFS file-tree viewer, cascade-remove logic |
| [`API_INTERNALS/INTERNALS/sc_shortcut_dialog_INTERNALS.txt`](API_INTERNALS/INTERNALS/sc_shortcut_dialog_INTERNALS.txt) | Configure Shortcut modal — `SC_EditShortcutDialog()`, icon picker, layout constants |
| [`API_INTERNALS/INTERNALS/scrollbar_INTERNALS.txt`](API_INTERNALS/INTERNALS/scrollbar_INTERNALS.txt) | Shortcuts-page scrollbar — SCROLLINFO, MSB wiring, scroll-child enumeration, MSB migration notes |

## Prebuilt Binaries

Prebuilt binaries are included in the `SetupCraft` directory for quick testing without compilation.
