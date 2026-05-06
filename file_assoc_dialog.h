#pragma once
/*
 * file_assoc_dialog.h — Add/Edit File Association modal dialog for SetupCraft.
 */

#include <windows.h>
#include <string>
#include <map>
#include "db.h"

// Opens a modal dialog to add or edit a file association.
// row       — on entry: initial field values (blank for Add, existing for Edit).
//             on return: values entered by the user (only meaningful if true returned).
// isNew     — true when creating a new association, false when editing an existing one.
// Returns true if the user clicked OK, false if they cancelled.
bool FA_ShowEditDialog(HWND hwndParent,
                       HINSTANCE hInst,
                       const std::map<std::wstring, std::wstring>& locale,
                       FileAssocRow& row,
                       bool isNew);
