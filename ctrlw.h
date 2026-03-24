#ifndef CTRLW_H
#define CTRLW_H

#include <windows.h>
#include <string>
#include <map>

// Show a quit confirmation dialog
// Returns true if user confirms quit, false otherwise
bool ShowQuitDialog(HWND hwndParent, const std::map<std::wstring, std::wstring>& locale);
bool ShowCloseProjectDialog(HWND hwndParent, const std::map<std::wstring, std::wstring>& locale);

// Show unsaved changes dialog
// Returns: 0 = Cancel, 1 = Save, 2 = Don't Save
int ShowUnsavedChangesDialog(HWND hwndParent, const std::map<std::wstring, std::wstring>& locale);

// Show duplicate project name dialog
// Returns: 0 = Cancel, 1 = Overwrite, 2 = Rename
int ShowDuplicateProjectDialog(HWND hwndParent, const std::wstring& projectName,
                               const std::map<std::wstring,std::wstring>& locale);

// Show rename project input dialog
// inOutName: pre-filled with current name, returned with new name on OK
// Returns true if user confirmed, false if cancelled
bool ShowRenameProjectDialog(HWND hwndParent, std::wstring& inOutName,
                             const std::map<std::wstring,std::wstring>& locale);

// Show a generic yes/no confirmation dialog.
// title and message are pre-formatted by the caller; locale supplies Yes/No button labels.
// Returns true if user clicked Yes, false otherwise.
bool ShowConfirmDeleteDialog(HWND hwndParent, const std::wstring& title, const std::wstring& message, const std::map<std::wstring, std::wstring>& locale);

// Show a single-button OK dialog (validation errors, informational notices).
// title and message are pre-formatted by the caller; locale supplies the OK button label.
void ShowValidationDialog(HWND hwndParent, const std::wstring& title, const std::wstring& message, const std::map<std::wstring, std::wstring>& locale);

// Check if Ctrl+W is pressed in the current message
bool IsCtrlWPressed(UINT msg, WPARAM wParam);

#endif // CTRLW_H
