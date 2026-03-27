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
#include <map>
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
#define IDC_RTFE_IMAGE        4520
#define IDC_RTFE_OPEN         4521    // "Open file…" button (shell32.dll icon 38)
#define IDC_RTFE_TABLE        4522    // Insert table button

// ── Table dialog / context-menu IDs ──────────────────────────────────────────
// Used inside the table-properties dialog (separate window, not a child of the editor).
#define IDC_RTFE_TD_ROWS      4540
#define IDC_RTFE_TD_COLS      4541
#define IDC_RTFE_TD_WIDTH     4542    // table width % spinner
#define IDC_RTFE_TD_BWIDTH    4543    // border width (px)
#define IDC_RTFE_TD_BTYPE     4544    // border type combobox
#define IDC_RTFE_TD_BCOLOR    4545    // border colour button
#define IDC_RTFE_TD_COLW      4546    // column width % spinner (0 = equal)
#define IDC_RTFE_TD_ROWH      4547    // row height px (0 = auto)
#define IDC_RTFE_TD_HALIGN    4548    // H alignment combo (L/C/R)
#define IDC_RTFE_TD_VALIGN    4549    // V alignment combo (T/M/B)
#define IDC_RTFE_TD_OK        4550
#define IDC_RTFE_TD_CANCEL    4551
#define IDC_RTFE_TD_COLWPX    4552    // column width px edit
#define IDC_RTFE_TD_COLWPCT   4553    // column width % edit (linked to px)
#define IDC_RTFE_TD_CHALIGN   4554    // cell H alignment combo (L/C/R)
// Context-menu command IDs
#define IDM_RTFE_TABLE_PROPS      5001    // "Table properties…" menu item
#define IDM_RTFE_CELL_ALIGN_L     5002    // cell align left
#define IDM_RTFE_CELL_ALIGN_C     5003    // cell align centre
#define IDM_RTFE_CELL_ALIGN_R     5004    // cell align right

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

    // ── Locale map ──────────────────────────────────────────────────────────
    // Optional pointer to the application locale map.  When non-null, the
    // editor uses it to translate toolbar tooltips and the image-picker dialog.
    // All rtfe_* locale keys are documented in edit_rtf_API.txt section 14.
    const std::map<std::wstring, std::wstring>* pLocale = nullptr;
};

// ── Public API ────────────────────────────────────────────────────────────────
// Open as a topmost modal popup centred over hwndParent (may be NULL).
// Runs its own message loop and blocks until the window is closed.
// Returns true and fills data.outRtf when user clicks Save.
// Returns false on Cancel or window-close (data.outRtf is unchanged).
bool OpenRtfEditor(HWND hwndParent, RtfEditorData& data);
