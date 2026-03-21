#pragma once
// ─── Full-Featured Rich-Text Editor ──────────────────────────────────────────
// General-purpose modal RTF editor for use throughout SetupCraft.
// Based on notes_editor — reuses all its bug-fixes and adds:
//   Strikethrough · Font face (system) · Alignment L/C/R/J · Bullet · Numbered list
//   Highlight colour · Resizable window · Face/size combo syncs on caret move
//   Optional character limit (status bar shown only when maxChars > 0)
//
// See edit_rtf_API.txt for full documentation and usage examples.

#include <windows.h>
#include <string>

// ── Control IDs (range 4500-4530 — no conflict with other modules) ───────────
#define IDC_RTFE_EDIT         4500
#define IDC_RTFE_BOLD         4501
#define IDC_RTFE_ITALIC       4502
#define IDC_RTFE_UNDERLINE    4503
#define IDC_RTFE_STRIKE       4504
#define IDC_RTFE_SUBSCRIPT    4505
#define IDC_RTFE_SUPERSCRIPT  4506
#define IDC_RTFE_FONTFACE     4507
#define IDC_RTFE_FONTSIZE     4508
#define IDC_RTFE_COLOR        4509
#define IDC_RTFE_HIGHLIGHT    4510
#define IDC_RTFE_ALIGN_L      4511
#define IDC_RTFE_ALIGN_C      4512
#define IDC_RTFE_ALIGN_R      4513
#define IDC_RTFE_ALIGN_J      4514
#define IDC_RTFE_BULLET       4515
#define IDC_RTFE_NUMBERED     4516
#define IDC_RTFE_OK           4517
#define IDC_RTFE_CANCEL       4518
#define IDC_RTFE_STATUSBAR    4519

// ── Data struct ───────────────────────────────────────────────────────────────
// Fill all fields before calling OpenRtfEditor().
// All i18n strings fall back to English when left empty.
struct RtfEditorData {
    // ── Input / output ──────────────────────────────────────────────────────
    std::wstring initRtf;        // RTF to pre-load (empty = blank document)
    std::wstring outRtf;         // RTF written back when user clicks Save
    bool         okClicked = false; // true if Save was clicked

    // ── Optional character limit ─────────────────────────────────────────────
    // -1 (default) = unlimited.  > 0 = show a "N characters left" status bar.
    int maxChars = -1;

    // ── i18n strings ────────────────────────────────────────────────────────
    std::wstring titleText;      // window caption,    e.g. L"Edit Description"
    std::wstring okText;         // Save button label, e.g. L"Save"
    std::wstring cancelText;     // Cancel label,      e.g. L"Cancel"
    std::wstring charsLeftFmt;   // printf fmt with one %d — only used when maxChars > 0

    // ── Window geometry ──────────────────────────────────────────────────────
    // 0 = use defaults (S(660) × S(520)).  Set to override.
    int preferredW = 0;
    int preferredH = 0;
};

// ── Public API ────────────────────────────────────────────────────────────────
// Open as a topmost modal popup centred over hwndParent (may be NULL).
// Runs its own message loop and blocks until the window is closed.
// Returns true and fills data.outRtf when user clicks Save.
// Returns false on Cancel or window-close (data.outRtf is unchanged).
bool OpenRtfEditor(HWND hwndParent, RtfEditorData& data);
