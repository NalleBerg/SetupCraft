# SetupCraft

An installer creation tool for making your developed packages distributable. Designed to be simple to use with a clean, native Windows interface.

**Current Release:** Version 2026.03.07.09 (Published: 07.03.2026 09:49)

> Note: This project is in active development. Entry screen and main window Files management page are complete with proper page switching.

## Features

- **DPI-Aware Scaling**: Full per-monitor DPI support via `S()` helper — all pixel values, fonts, and tooltip dimensions scale correctly at any DPI (100%, 125%, 150%, 175%, 200%+)
- **Consistent Body Font**: All labels, edits, checkboxes, TreeViews, and ListViews use a system-derived `NONCLIENTMETRICS` font at 120% scale for clear, legible text on every screen
- **Bold Page Titles**: Each page has a prominent semi-bold heading rendered with a dedicated `s_hPageTitleFont` (150% NONCLIENTMETRICS) — correctly applied via `WM_CTLCOLORSTATIC` ID check so the body-font override no longer clobbers it
- **Two-Row Toolbar**: 11 buttons in two compact rows — Row 1: Files, Components, Registry, Shortcuts, Dependencies, Dialogs; Row 2: Settings, Build, Test, Scripts, Save. About «i» icon centered vertically at the right end
- **Components Page**: Full component-based installation page — enable/disable toggle, single **Add Files / Folders** button (native `IFileOpenDialog` multi-select, auto-detects file vs folder), Edit/Remove actions, tabular ListView (6 columns), modal edit dialog with auto-fill, DB-backed persistence
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
- **Version Management**: Centralized version control via curver.txt file and NewVersion.ps1 PowerShell script for automated version updates
- **Dynamic Version Loading**: About dialog reads Published timestamp and Version from curver.txt at runtime (no recompile needed for version changes)
- **Take Me There Navigation**: Navigate to registry key in TreeView and automatically populate ListView with uninstall values (DisplayName, DisplayVersion, Publisher, etc.)
- **Spinner Dialog System**: Modal loading dialog with animated multi-line text display for long-running operations
- **Virtual Folder Support**: Create custom folder structures without physical disk paths - files and full hierarchy persist when navigating between pages
- **Context Menus on Both Panes**: Right-click in the TreeView shows Add Folder, Add Files, Create Folder, and (for non-system nodes) Remove. Right-click in the ListView shows Add Folder, Add Files, and (when a file is selected) Remove. System roots (Program Files, ProgramData, AppData, Ask at Install) never offer Remove
- **Duplicate Project Name Guard**: Saving a new project whose name already exists in the database raises a modal dialog — Overwrite (adopt existing record), Rename this one (inline rename dialog), or Cancel — preventing silent data loss
- **Smart Project Naming**: Automatically derives project name from first folder added under Program Files, and keeps updating it on every rename/replace until the user manually edits the name field (no longer stops after first save)
- **Save Always Works**: New projects with no database ID are created via `DB::InsertProject` on first save — no more "No project selected" error
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
