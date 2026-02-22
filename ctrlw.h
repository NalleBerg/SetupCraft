#ifndef CTRLW_H
#define CTRLW_H

#include <windows.h>
#include <string>
#include <map>

// Show a quit confirmation dialog
// Returns true if user confirms quit, false otherwise
bool ShowQuitDialog(HWND hwndParent, const std::map<std::wstring, std::wstring>& locale);

// Check if Ctrl+W is pressed in the current message
bool IsCtrlWPressed(UINT msg, WPARAM wParam);

#endif // CTRLW_H
