#ifndef CTRLW_H
#define CTRLW_H

#include <windows.h>
#include <string>
#include <map>

// Show a quit confirmation dialog
// Returns true if user confirms quit, false otherwise
bool ShowQuitDialog(HWND hwndParent, const std::map<std::wstring, std::wstring>& locale);

// Show unsaved changes dialog
// Returns: 0 = Cancel, 1 = Save, 2 = Don't Save
int ShowUnsavedChangesDialog(HWND hwndParent, const std::map<std::wstring, std::wstring>& locale);

// Check if Ctrl+W is pressed in the current message
bool IsCtrlWPressed(UINT msg, WPARAM wParam);

#endif // CTRLW_H
