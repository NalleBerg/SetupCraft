#pragma once
#include <windows.h>

// ═══════════════════════════════════════════════════════════════════════════════
//  EntryScreen — the SetupCraft startup window shown before a project is loaded.
//
//  This module owns everything on the entry screen:
//    • Language combo + globe-icon tooltip  (multilingual table, §3 tooltip API)
//    • About icon tooltip + click handler   (simple single-line tooltip)
//    • New / Open / Delete project dialogs
//    • Locale loading, locale persistence
//
//  ┌─────────────────────────────────────────────────────────────────────────┐
//  │  TOOLTIP RULES — do not break these:                                    │
//  │  • Globe icon  : subclassed, SS_NOTIFY, NO WS_EX_TRANSPARENT.          │
//  │                  All tooltip logic lives in GlobeIcon_SubclassProc.     │
//  │  • About icon  : subclassed, SS_NOTIFY, NO WS_EX_TRANSPARENT.          │
//  │                  All tooltip logic lives in AboutIcon_SubclassProc.     │
//  │  • Parent WndProc has NO tooltip code — icons own their own tooltips.  │
//  │  See tooltip_API.txt §4 and §9 for the full pattern + pitfalls.        │
//  └─────────────────────────────────────────────────────────────────────────┘
// ═══════════════════════════════════════════════════════════════════════════════

// Window class name — also used by mainwindow.cpp when returning to the entry
// screen (FindWindowW / ShowWindow).
constexpr wchar_t ENTRY_SCREEN_CLASS[] = L"SetupCraft_EntryScreen";

// Register the window class, create the entry window and show it.
// Call once from wWinMain. Returns the new HWND or NULL on failure.
HWND EntryScreen_Run(HINSTANCE hInstance, int nCmdShow);
