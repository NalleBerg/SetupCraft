#pragma once
#include <windows.h>
#include <map>
#include <string>

// About and License dialogs — fully i18n-compatible.
// Pass the app locale map so all button labels are translated.

void ShowAboutDialog(HWND parent, const std::map<std::wstring, std::wstring>& locale);
void ShowLicenseDialog(HWND parent, const std::map<std::wstring, std::wstring>& locale);
