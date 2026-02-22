# SetupCraft

An installer creation tool for making your developed packages distributable. Designed to be simple to use with a clean, native Windows interface.

> Note: This project is in active development. Entry screen and main window Files management are complete.

## Features

- **Main Window Interface**: 8-button toolbar for accessing Files, Registry, Shortcuts, Dependencies, Settings, Build, Test, and Scripts pages
- **Files Management**: Split-pane interface with TreeView (folder hierarchy) and ListView (file display) for visual file selection
- **Install Directory Configuration**: Editable install path with folder picker dialog
- **Multi-Select Support**: Ctrl/Shift selection in ListView for files, checkboxes in TreeView for folders
- **Remove Functionality**: Delete individual or multiple files/folders with proper memory cleanup
- **File Type Icons**: System icons for all file types (exe, dll, txt, md, png, etc.)
- **Multilingual Support**: 20 languages with native translations (Norwegian, English, Greek, Spanish, German, French, Italian, Dutch, and more)
- **SQLite Database**: Project configurations stored in `%APPDATA%\SetupCraft\SetupCraft.db`
- **Native Windows UI**: Clean interface using Windows system icons and native styling
- **Project Management**: Create, open, and delete installation projects
- **Intuitive Design**: Globe icon with multilingual tooltip showing all available languages
- **Professional Quit System**: Confirmation dialog with Yes/No buttons - triggered by Exit button, X button, or Ctrl+W keyboard shortcut
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
✅ File/folder multi-select and removal
✅ Install directory configuration with folder picker
✅ TreeView selection updates ListView automatically
✅ System file type icons in ListView
✅ Tooltips showing full file paths
✅ Scripts page for before/after install hooks

🔄 In Progress: Add Folder/Files functionality, Build/Test implementation

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
