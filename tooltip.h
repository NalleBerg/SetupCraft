#pragma once
#include <windows.h>
#include <string>
#include <vector>
#include <utility>

// Custom multilingual tooltip system
// Displays tooltips in a 2-column grid with country codes and translations

// Entry in the tooltip: pair of (country code, translated text)
typedef std::pair<std::wstring, std::wstring> TooltipEntry;

// Initialize the tooltip system (call once at startup)
// Returns true on success
bool InitTooltipSystem(HINSTANCE hInstance);

// Cleanup tooltip system (call at shutdown)
void CleanupTooltipSystem();

// Show a multilingual tooltip at specified screen coordinates
// entries: vector of (code, text) pairs to display in 2-column grid
// x, y: screen coordinates where tooltip should appear
// parentHwnd: parent window handle
void ShowMultilingualTooltip(const std::vector<TooltipEntry>& entries, int x, int y, HWND parentHwnd);

// Hide the tooltip if visible
void HideTooltip();

// Check if tooltip is currently visible
bool IsTooltipVisible();

// (No owner API here) Public tooltip functions only.

// Build multilingual entries for a specific locale key from all available locale files
// localeKey: the key to look up in locale files (e.g., "select_language", "about_setupcraft")
// localeDir: directory containing locale files (e.g., "locale")
// availableLocales: vector of locale codes to check (e.g., {"en_GB", "de_DE", ...})
// Returns vector of (country code, translated text) pairs, sorted by country code
std::vector<TooltipEntry> BuildMultilingualEntries(
    const std::wstring& localeKey,
    const std::wstring& localeDir,
    const std::vector<std::wstring>& availableLocales);
