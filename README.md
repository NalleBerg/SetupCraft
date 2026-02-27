# SetupCraft

An installer creation tool for making your developed packages distributable. Designed to be simple to use with a clean, native Windows interface.

**Current Release:** Version 2026.02.27.14 (Published: 27.02.2026 14:42)

> Note: This project is in active development. Entry screen and main window Files management page are complete with proper page switching.

## Features

- **Main Window Interface**: 8-button toolbar for accessing Files, Registry, Shortcuts, Dependencies, Settings, Build, Test, and Scripts pages
- **Files Management**: Split-pane interface with TreeView (folder hierarchy with 19px indentation) and ListView (file display) for visual file selection
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
- **Virtual Folder Support**: Create custom folder structures without physical disk paths - files persist when navigating between folders
- **Context-Aware Operations**: Add Folder/Add Files buttons respect currently selected folder as parent/target
- **Smart Install Path**: Auto-updates install path display when first folder under Program Files is renamed/deleted/replaced
- **Smart Project Naming**: Automatically derives project name from first folder added, with protection against accidental overwrites
- **Install Path Display**: Read-only dark blue install path display that reflects actual folder structure
- **Context Menus**: Right-click options for creating and removing folders with confirmation dialogs
- **Add Folder/Files**: Buttons to add existing folders or individual files with automatic folder structure creation
- **Multi-Select Support**: Ctrl/Shift selection in ListView for files, checkboxes in TreeView for folders
- **Remove Functionality**: Delete individual or multiple files/folders with proper memory cleanup and virtual folder cleanup
- **File Type Icons**: System icons for all file types (exe, dll, txt, md, png, etc.) with transparent backgrounds
- **Multilingual Support**: 20 languages with native translations (Norwegian, English, Greek, Spanish, German, French, Italian, Dutch, and more)
- **SQLite Database**: Project configurations stored in `%APPDATA%\SetupCraft\SetupCraft.db`
- **Native Windows UI**: Clean interface using Windows system icons and native styling
- **Project Management**: Create, open, and delete installation projects
- **Intuitive Design**: Globe icon with multilingual tooltip showing all available languages
- **Professional Quit System**: Confirmation dialog with Yes/No buttons - triggered by Exit button, X button, or Ctrl+W keyboard shortcut
- **Unsaved Changes Warning**: Custom 3-button dialog (Save/Don't Save/Cancel) warns before closing projects with unsaved changes
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
