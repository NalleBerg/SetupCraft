#pragma once
#include <windows.h>
#include <map>
#include <string>

// Page-manual popup dialog.
// Shows a richly-formatted, vertically-scrollable how-to page for the given
// SetupCraft page.  The window is resizable and maximisable (native title-bar
// button), so the developer can read it full-screen.
//
// pageIndex matches the SwitchPage() indices used in mainwindow.cpp:
//   0  Files
//   1  Registry
//   2  Shortcuts
//   3  Dependencies
//   4  Dialogs
//   5  Settings
//   6  Build
//   7  Test
//   8  Scripts
//   9  Components
//  10  File Associations
//
// Implemented so far: 0 (Files), 9 (Components).  Other indices show a
// placeholder.  Add a new PopulateXxxManual() case in page_manual.cpp as each
// page is written.

void ShowPageManual(HWND parent, int pageIndex,
                    const std::map<std::wstring, std::wstring>& locale);
