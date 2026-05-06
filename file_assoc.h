#pragma once
/*
 * file_assoc.h — Public interface for the File Associations page (page index 10).
 *
 * All page state lives in file_assoc.cpp as file-scope statics.
 * mainwindow.cpp routes WM_COMMAND and WM_NOTIFY here via the public functions.
 */

#include <windows.h>
#include <commctrl.h>
#include <string>
#include <map>
#include <vector>
#include "db.h"

// ── Control IDs ───────────────────────────────────────────────────────────────
#define IDC_FA_LIST         8100
#define IDC_FA_ADD          8101
#define IDC_FA_EDIT         8102
#define IDC_FA_REMOVE       8103
#define IDC_FA_PAGE_TITLE   8104

// ── Context-menu command IDs ──────────────────────────────────────────────────
#define IDM_FA_CTX_EDIT     6400
#define IDM_FA_CTX_REMOVE   6401

// ── Public API ────────────────────────────────────────────────────────────────
void FA_Reset();
bool FA_HasAnyEnabled();
const std::vector<FileAssocRow>& FA_GetAssociations();

void FA_BuildPage(HWND hwnd, HINSTANCE hInst,
                  int pageY, int clientWidth,
                  HFONT hPageTitleFont, HFONT hGuiFont,
                  const std::map<std::wstring, std::wstring>& locale);

void FA_TearDown(HWND hwnd);
bool FA_OnCommand(HWND hwnd, int wmId, int wmEvent);
bool FA_OnNotify(HWND hwnd, NMHDR* hdr);
void FA_SaveToDb(int projectId);
void FA_LoadFromDb(int projectId);
