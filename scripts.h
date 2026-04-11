#pragma once
/*
 * scripts.h — Public interface for SetupCraft's Scripts page (page index 8).
 *
 * ── Calling sequence ──────────────────────────────────────────────────────────
 *   On project open/close : call SCR_Reset() to clear all scripts state.
 *   On SwitchPage(8)      : call SCR_BuildPage() to create all page controls.
 *   On SwitchPage away    : call SCR_TearDown() to destroy page controls.
 *   In WM_COMMAND         : call SCR_OnCommand(); return 0 when it returns true.
 *   On IDM_FILE_SAVE      : call SCR_SaveToDb(projectId).
 *   On project load       : call SCR_LoadFromDb(projectId) after SCR_Reset().
 *
 * ── Persistence ───────────────────────────────────────────────────────────────
 *   Script content and settings live in memory until IDM_FILE_SAVE is processed.
 *   Stored in the `settings` table under project-scoped keys.
 *   SCR_Reset() is the only thing that clears in-memory state.
 *
 * ── Architecture notes ────────────────────────────────────────────────────────
 *   Full implementation in scripts.cpp.
 *   Control IDs range: 7300–7329.
 *   Two slots: pre-install (slot 0) and post-install (slot 1).
 *   Each slot has: enabled flag, type (bat/ps1), and free-text content.
 *   Master enable checkbox gates both slots.
 */

#include <windows.h>
#include <string>
#include <map>

// ── Script slot indices ───────────────────────────────────────────────────────
#define SCR_SLOT_PRE  0   // Before installation
#define SCR_SLOT_POST 1   // After installation
#define SCR_SLOT_COUNT 2

// ── Script type constants ─────────────────────────────────────────────────────
#define SCR_TYPE_BAT  0   // .bat / cmd file
#define SCR_TYPE_PS1  1   // .ps1 / PowerShell file

// ── Control IDs (range 7300–7329) ────────────────────────────────────────────
#define IDC_SCR_PAGE_TITLE       7300   // "Run Scripts" heading STATIC
#define IDC_SCR_MASTER_ENABLE    7301   // Master enable checkbox
#define IDC_SCR_ENABLE_HINT      7302   // Hint label below master checkbox

// Pre-install slot controls (slot 0)
#define IDC_SCR_PRE_ENABLE       7310   // "Run before install" checkbox
#define IDC_SCR_PRE_BAT          7311   // "Batch (.bat)" radio
#define IDC_SCR_PRE_PS1          7312   // "PowerShell (.ps1)" radio
#define IDC_SCR_PRE_EDIT_LABEL   7313   // "Script content:" label
#define IDC_SCR_PRE_EDIT         7314   // Multiline edit
#define IDC_SCR_PRE_LOAD         7315   // "Load from file…" button
#define IDC_SCR_PRE_TEST         7316   // "Test in terminal" button

// Post-install slot controls (slot 1)
#define IDC_SCR_POST_ENABLE      7320   // "Run after install" checkbox
#define IDC_SCR_POST_BAT         7321   // "Batch (.bat)" radio
#define IDC_SCR_POST_PS1         7322   // "PowerShell (.ps1)" radio
#define IDC_SCR_POST_EDIT_LABEL  7323   // "Script content:" label
#define IDC_SCR_POST_EDIT        7324   // Multiline edit
#define IDC_SCR_POST_LOAD        7325   // "Load from file…" button
#define IDC_SCR_POST_TEST        7326   // "Test in terminal" button

// ── Public API ────────────────────────────────────────────────────────────────

// Clear all in-memory scripts state for a new/closed project session.
// Call before loading any project data.
void SCR_Reset();

// Build the Scripts page inside hwnd.  Returns the absolute Y coordinate
// (in hwnd client coords) of the first pixel below the last content row —
// used by SwitchPage to size the vertical scrollbar.
//   hwnd            — the main window; controls are direct children of hwnd.
//   hInst           — application HINSTANCE.
//   pageY           — y-coordinate where the page content area begins.
//   clientWidth     — full client width of hwnd.
//   hPageTitleFont  — semi-bold headline font.
//   hGuiFont        — scaled body font.
//   locale          — current locale string map.
int SCR_BuildPage(HWND hwnd, HINSTANCE hInst,
                  int pageY, int clientWidth,
                  HFONT hPageTitleFont, HFONT hGuiFont,
                  const std::map<std::wstring, std::wstring>& locale);

// Tear down Scripts page controls and reset scroll position.
// Called from SwitchPage generic teardown.
void SCR_TearDown(HWND hwnd);

// Route WM_COMMAND messages.  Returns true if the command was handled
// (caller should return 0 from WM_COMMAND in that case).
bool SCR_OnCommand(HWND hwnd, int wmId, int wmEvent, HWND hCtrl);

// Persist current in-memory scripts state to the settings table.
void SCR_SaveToDb(int projectId);

// Load scripts state from the settings table into memory.
// Call after SCR_Reset() when opening a project.
void SCR_LoadFromDb(int projectId);
