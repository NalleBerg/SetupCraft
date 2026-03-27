#pragma once
// ─── Notes / Description Rich-Text Editor ─────────────────────────────────────
// Standalone modal popup editor for per-component installer tooltip notes.
// Supports: Bold · Italic · Underline · Subscript · Superscript · Font size · Color.
// Plain-text length is capped at 500 characters.
// See notes_editor_API.txt for full documentation and usage examples.

#include <windows.h>
#include <string>

// ── Control IDs (scoped to the CompNotesEditor window class) ─────────────────
#define IDC_NOTES_STATUS      329   // char-count status label (internal)
#define IDC_NOTES_EDIT        330   // RichEdit control
#define IDC_NOTES_BOLD        331   // Bold toolbar button
#define IDC_NOTES_ITALIC      332   // Italic toolbar button
#define IDC_NOTES_UNDERLINE   333   // Underline toolbar button
#define IDC_NOTES_SUBSCRIPT   334   // Subscript toolbar button (X₂)
#define IDC_NOTES_SUPERSCRIPT 335   // Superscript toolbar button (X²)
#define IDC_NOTES_FONTSIZE    336   // Font-size combobox
#define IDC_NOTES_COLOR       337   // Text-color picker button
#define IDC_NOTES_OK          338   // Save button
#define IDC_NOTES_CANCEL      339   // Cancel button
#define IDC_NOTES_TABLE       340   // Insert table button

// ── Data struct ───────────────────────────────────────────────────────────────
// Fill all fields before calling OpenNotesEditor().
// i18n strings: leave empty to use built-in English fallbacks.
struct NotesEditorData {
    // ── Input / output ──────────────────────────────────────────────────────
    std::wstring initRtf;        // RTF to pre-load into the editor (empty = blank)
    std::wstring outRtf;         // RTF written back when the user clicks Save

    bool okClicked = false;      // true if the user clicked Save (vs Cancel / close)

    // ── i18n strings (all fallback to English when empty) ───────────────────
    std::wstring titleText;      // window caption,    e.g. L"Edit Notes"
    std::wstring okText;         // Save button label, e.g. L"Save"
    std::wstring cancelText;     // Cancel label,      e.g. L"Cancel"
    std::wstring charsLeftFmt;   // printf format with one %%d, e.g. L"%%d characters left"
};

// ── Public API ────────────────────────────────────────────────────────────────
// Open the notes editor as a topmost modal popup anchored to hwndParent.
// Blocks until the window is closed (own message pump, does not nest).
// Returns true and fills data.outRtf when the user clicks Save.
// Returns false (data.outRtf unchanged) on Cancel or window close.
bool OpenNotesEditor(HWND hwndParent, NotesEditorData& data);
