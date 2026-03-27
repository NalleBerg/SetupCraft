/*
 * dialogs.cpp — Dialogs page implementation for SetupCraft (page index 4).
 *
 * Each visible installer dialog type gets one row on the page:
 *   [32×32 icon]  [name label]  [Edit Content…]  [Preview…]
 *
 * Visibility is live: IDLG_BuildPage reads in-memory state from the
 * Dependencies, Shortcuts, and Components modules every time the page opens.
 *
 * The "Edit Content…" button opens the full RTF editor (edit_rtf.h).
 * The "Preview…" button opens a read-only facsimile of the installer dialog,
 * complete with Back / Next (shell32.dll icon #137 on the RIGHT) / Cancel.
 *
 * Persistence: RTF strings live in s_dialogs[]; written to DB on IDM_FILE_SAVE.
 */

#include "dialogs.h"
#include "mainwindow.h"   // MarkAsModified(), UseComponents(), AskAtInstallEnabled()
#include "deps.h"         // DEP_HasAny()
#include "shortcuts.h"    // SC_HasOptOut()
#include "edit_rtf.h"     // OpenRtfEditor()
#include "db.h"
#include "dpi.h"          // S()
#include "button.h"       // CreateCustomButtonWithIcon(), MeasureButtonWidth()
#include "checkbox.h"     // CreateCustomCheckbox, DrawCustomCheckbox
#include <richedit.h>     // EM_STREAMIN, SF_RTF, EDITSTREAM

// PrivateExtractIconsW — undocumented but reliable fixed-size icon loader.
extern "C" __declspec(dllimport) UINT WINAPI PrivateExtractIconsW(
    LPCWSTR szFileName, int nIconIndex, int cxIcon, int cyIcon,
    HICON* phicon, UINT* piconid, UINT nIcons, UINT flags);

// ── Module-private state ──────────────────────────────────────────────────────

static InstallerDialog s_dialogs[IDLG_COUNT];  // in-memory RTF content
static int s_idlgScrollOffset = 0;             // vertical scroll offset (px) while Dialogs page is visible

static HINSTANCE  s_hInst      = NULL;
static HFONT      s_hGuiFont   = NULL;
static HFONT      s_hTitleFont = NULL;
static const std::map<std::wstring,std::wstring>* s_pLocale = NULL;

// Installer-title section state (survives page switches; cleared by IDLG_Reset).
static std::wstring s_installTitle;             // text shown in installer title bar
static std::wstring s_installIconPath;          // custom .ico path; empty = default
static HICON        s_hInstallIcon    = NULL;   // currently displayed icon HANDLE
static HWND         s_hInstIconPreview = NULL;  // preview control for live updates

// Per-row icon handles (indexed by InstallerDialogType).
// Loaded in IDLG_BuildPage; destroyed in IDLG_TearDown.
static HICON  s_rowIcons[IDLG_COUNT]     = {};
static HWND   s_rowIconCtrls[IDLG_COUNT] = {};
static WNDPROC s_origIconProc            = NULL;

// shell32.dll sequential icon index for each dialog type.
// These are visual hints only — any close-enough icon works.
static const int kDialogIconIdx[IDLG_COUNT] = {
    13,   // WELCOME      — information / greeting
    70,   // LICENSE      — document
   257,   // DEPENDENCIES — package / download
   166,   // FOR_ME_ALL   — users / group
   247,   // COMPONENTS   — component
    29,   // SHORTCUTS    — arrow / link
   144,   // READY        — shield / ready to install
    35,   // INSTALL      — installation wheels
    43    // FINISH        — checkmark / complete
};

// Locale key and English fallback for each dialog type name.
static const wchar_t* const kDialogNameKeys[IDLG_COUNT] = {
    L"idlg_name_welcome",
    L"idlg_name_license",
    L"idlg_name_dependencies",
    L"idlg_name_for_me_all",
    L"idlg_name_components",
    L"idlg_name_shortcuts",
    L"idlg_name_ready",
    L"idlg_name_install",
    L"idlg_name_finish"
};
static const wchar_t* const kDialogNameFallbacks[IDLG_COUNT] = {
    L"Welcome",
    L"License",
    L"Dependencies",
    L"For Me / All Users",
    L"Components",
    L"Shortcuts",
    L"Ready to Install",
    L"Install",
    L"Finish"
};

// ── Helpers ───────────────────────────────────────────────────────────────────

static std::wstring L10n(const wchar_t* key, const wchar_t* fallback)
{
    if (!s_pLocale) return fallback;
    auto it = s_pLocale->find(key);
    return (it != s_pLocale->end()) ? it->second : fallback;
}

// Return true if the given dialog type should be shown based on live project state.
static bool IsDialogVisible(InstallerDialogType type)
{
    switch (type) {
    case IDLG_WELCOME:
    case IDLG_LICENSE:
    case IDLG_READY:
    case IDLG_INSTALL:
    case IDLG_FINISH:
        return true;
    case IDLG_DEPENDENCIES:
        return DEP_HasAny();
    case IDLG_FOR_ME_ALL:
        return MainWindow::AskAtInstallEnabled();
    case IDLG_COMPONENTS:
        return MainWindow::UseComponents();
    case IDLG_SHORTCUTS:
        return SC_HasOptOut();
    }
    return false;
}

// ── Row icon subclass proc ─────────────────────────────────────────────────────
// GWLP_USERDATA holds the HICON to draw.  WM_ERASEBKGND and WM_PAINT are
// intercepted so DrawIconEx fills the control at the exact requested size.

static LRESULT CALLBACK RowIconSubclassProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg) {
    case WM_ERASEBKGND: {
        RECT rc; GetClientRect(hwnd, &rc);
        FillRect((HDC)wParam, &rc, GetSysColorBrush(COLOR_WINDOW));
        return TRUE;
    }
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);
        RECT rc; GetClientRect(hwnd, &rc);
        FillRect(hdc, &rc, GetSysColorBrush(COLOR_WINDOW));
        HICON hIco = (HICON)(LONG_PTR)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
        if (hIco) DrawIconEx(hdc, 0, 0, hIco, rc.right, rc.bottom, 0, NULL, DI_NORMAL);
        EndPaint(hwnd, &ps);
        return 0;
    }
    }
    return CallWindowProcW(s_origIconProc, hwnd, msg, wParam, lParam);
}

// ── RTF stream-in helper ──────────────────────────────────────────────────────

struct RtfReadState { const std::string* buf; DWORD pos; };

static DWORD CALLBACK RtfReadCb(DWORD_PTR cookie, LPBYTE buf, LONG cb, LONG* pcb)
{
    RtfReadState* s = reinterpret_cast<RtfReadState*>(cookie);
    DWORD remain = (DWORD)s->buf->size() - s->pos;
    DWORD n = (DWORD)cb < remain ? (DWORD)cb : remain;
    if (n) { memcpy(buf, s->buf->c_str() + s->pos, n); s->pos += n; }
    *pcb = (LONG)n;
    return 0;
}

static void StreamRtfIn(HWND hEdit, const std::wstring& wrtf)
{
    if (wrtf.empty()) { SetWindowTextW(hEdit, L""); return; }
    int n = WideCharToMultiByte(CP_UTF8, 0, wrtf.c_str(), -1, NULL, 0, NULL, NULL);
    std::string rtf(n > 1 ? n - 1 : 0, '\0');
    if (n > 1) WideCharToMultiByte(CP_UTF8, 0, wrtf.c_str(), -1, &rtf[0], n, NULL, NULL);
    RtfReadState state = { &rtf, 0 };
    EDITSTREAM es     = { (DWORD_PTR)&state, 0, RtfReadCb };
    SendMessageW(hEdit, EM_STREAMIN, SF_RTF, (LPARAM)&es);
}

// ── Preview dialog ────────────────────────────────────────────────────────────
// A modal popup that shows an installer dialog as the end user will see it.
// A companion "sizer" panel (always-on-top, to the left) lets the developer
// set the dialog dimensions; changes apply instantly via WM_SIZE relayout.

// Per-project installer dialog preview dimensions (logical px at 96 dpi / 100%).
// Persisted via DB::SetSetting; applied as S(value) so DPI is always correct.
static int  s_previewLogW = 460;
static int  s_previewLogH = 360;
// Indexed by InstallerDialogType.  [type] becomes true once the developer has
// manually adjusted the preview via the sizer while that dialog type was shown.
// When true for a type, AutoFitComponentHeight is skipped for that type so the
// chosen size is never overwritten by automatic layout calculations.
// Reset to all-false by IDLG_Reset(); only [IDLG_COMPONENTS] is persisted to DB.
static bool s_previewUserSized[IDLG_COUNT] = {};

// Preview and sizer styles — defined once so AdjustWindowRectEx is consistent.
static const DWORD kPreviewStyle   = WS_POPUP | WS_CAPTION; // no WS_SYSMENU → no ×
static const DWORD kPreviewExStyle = WS_EX_DLGMODALFRAME;

// Interior-HWND storage; set during WM_CREATE, used by layout/navigation helpers.
struct PreviewData {
    InstallerDialogType type;        // currently displayed dialog type
    HFONT               hGuiFont;
    HFONT               hTitleFont;
    bool                running;     // false → exit the modal loop
    bool                cancelled;   // true → user pressed Cancel / Alt+F4
    int                 contentFitH;        // logical px for RichEdit in split layout (0 = avail/2)
    bool                contentNeedsScroll; // true when height was capped to 75% of screen
    // Interior controls for relayout and navigation
    HWND                hTypeTitle;  // dialog-type name STATIC at the top
    HWND                hContent;    // RichEdit (content area)
    HWND                hBack;       // Back button
    HWND                hNext;       // Next / Finish button
    HWND                hCancel;     // Cancel button
    // Extras panel — visible for dialog types with interactive controls.
    // LayoutPreviewControls splits the interior in half when showExtras is true:
    // RTF content fills the top portion; extras fill the bottom.
    bool                showExtras;    // true → split layout; false → full-height RichEdit
    HWND                hExtrasLabel;  // descriptor label above the extras controls
    HWND                hRadioMe;      // IDLG_FOR_ME_ALL — "Install just for me"
    HWND                hRadioAll;     // IDLG_FOR_ME_ALL — "Install for all users"
    std::vector<HWND>   hCompChecks;   // IDLG_COMPONENTS — one per folder component (dynamic)
    bool                contentHidden; // true when IDLG_COMPONENTS has no RTF — hide RichEdit
    HWND                hSizer;        // sizer panel (set by ShowPreviewDialog after creation)
};

// Sizer panel data; GWLP_USERDATA on the sizer window.
struct SizerData {
    PreviewData* pd;          // pointer to the shared preview state
    HWND         hPreview;    // preview window handle (set after creation)
    HFONT        hFont;       // borrowed gui font — do NOT destroy
    bool         ignoring;    // suppress EN_CHANGE during programmatic init
};

// ── Navigation helpers ────────────────────────────────────────────────────────

static InstallerDialogType NextVisibleType(InstallerDialogType cur)
{
    for (int i = (int)cur + 1; i < IDLG_COUNT; i++)
        if (IsDialogVisible((InstallerDialogType)i))
            return (InstallerDialogType)i;
    return cur; // already at the last visible
}

static InstallerDialogType PrevVisibleType(InstallerDialogType cur)
{
    for (int i = (int)cur - 1; i >= 0; i--)
        if (IsDialogVisible((InstallerDialogType)i))
            return (InstallerDialogType)i;
    return cur; // already at the first visible
}

// ── Layout helper — positions all interior controls from the current client rect ──

static void LayoutPreviewControls(HWND hwnd, PreviewData* pd)
{
    RECT rc; GetClientRect(hwnd, &rc);
    int cW = rc.right, cH = rc.bottom;
    const int pad    = S(16);
    const int btnH   = S(30);
    const int gap    = S(8);
    const int titleH = S(36);
    const int btnY   = cH - pad - btnH;

    // Title label
    if (pd->hTypeTitle && IsWindow(pd->hTypeTitle))
        SetWindowPos(pd->hTypeTitle, NULL, pad, pad,
                     cW - pad * 2, titleH, SWP_NOZORDER | SWP_NOACTIVATE);

    // Interior area between title and button row.
    int editY = pad + titleH + gap;
    int avail = btnY - gap - editY;   // total pixels available to distribute

    if (pd->showExtras) {
        // Split layout: RTF content on top (50%), extras panel below (50%).
        // Exception: if contentHidden is set (e.g. IDLG_COMPONENTS with no RTF),
        // the RichEdit is hidden and the extras panel gets the full available height.
        const int extLblH = S(22);
        const int extGap  = S(6);
        // Use the auto-measured RichEdit height when available; fall back to
        // the 50/50 split for user-resized windows where no measurement has run.
        int contentH = pd->contentHidden ? 0 :
                       (pd->contentFitH > 0 ? std::min(S(pd->contentFitH), avail)
                                            : avail / 2);
        int contentGap = pd->contentHidden ? 0 : gap;
        int extrasY    = editY + contentH + contentGap;
        int extrasH    = avail - contentH - contentGap - extLblH - extGap;
        if (extrasH < 0) extrasH = 0;

        if (pd->hContent && IsWindow(pd->hContent)) {
            if (pd->contentHidden)
                ShowWindow(pd->hContent, SW_HIDE);
            else {
                ShowWindow(pd->hContent, SW_SHOW);
                SetWindowPos(pd->hContent, NULL, pad, editY,
                             cW - pad * 2, contentH, SWP_NOZORDER | SWP_NOACTIVATE);
            }
        }

        if (pd->hExtrasLabel && IsWindow(pd->hExtrasLabel)) {
            SetWindowPos(pd->hExtrasLabel, NULL, pad, extrasY,
                         cW - pad * 2, extLblH, SWP_NOZORDER | SWP_NOACTIVATE);
            ShowWindow(pd->hExtrasLabel, SW_SHOW);
        }

        int ctrlY = extrasY + extLblH + extGap;

        // Position For Me / All Users radio buttons when visible.
        if (pd->hRadioMe && IsWindow(pd->hRadioMe) && IsWindowVisible(pd->hRadioMe)) {
            const int rH = S(24);
            SetWindowPos(pd->hRadioMe,  NULL, pad + S(4), ctrlY,
                         cW - pad * 2 - S(4), rH, SWP_NOZORDER | SWP_NOACTIVATE);
            SetWindowPos(pd->hRadioAll, NULL, pad + S(4), ctrlY + rH + S(6),
                         cW - pad * 2 - S(4), rH, SWP_NOZORDER | SWP_NOACTIVATE);
        }

        // Position component checkboxes when visible.
        {
            const int chkH = S(24);
            int cy = ctrlY;
            for (HWND h : pd->hCompChecks) {
                if (h && IsWindow(h)) {
                    SetWindowPos(h, NULL, pad + S(4), cy,
                                 cW - pad * 2 - S(4), chkH, SWP_NOZORDER | SWP_NOACTIVATE);
                    cy += chkH + S(4);
                }
            }
        }
    } else {
        // Single layout: RichEdit fills all available vertical space.
        if (pd->hContent && IsWindow(pd->hContent))
            SetWindowPos(pd->hContent, NULL, pad, editY,
                         cW - pad * 2, avail, SWP_NOZORDER | SWP_NOACTIVATE);
        if (pd->hExtrasLabel && IsWindow(pd->hExtrasLabel))
            ShowWindow(pd->hExtrasLabel, SW_HIDE);
    }

    // Measure button widths for the current type (Next/Finish text may differ)
    bool bIsFinish = (NextVisibleType(pd->type) == pd->type); // true = already at last
    std::wstring backTxt   = L"\u25C0  " + L10n(L"idlg_preview_back",   L"Back");
    std::wstring nextTxt   = bIsFinish
        ? (L10n(L"idlg_preview_finish", L"Finish") + L"  \u2714")
        : (L10n(L"idlg_preview_next",   L"Next")   + L"  \u25B6");
    std::wstring cancelTxt = L10n(L"idlg_preview_cancel", L"Cancel");
    int wBack   = MeasureButtonWidth(backTxt,   false) + S(8);
    int wNext   = MeasureButtonWidth(nextTxt,   false) + S(8);
    int wCancel = MeasureButtonWidth(cancelTxt, false) + S(8);

    if (pd->hBack && IsWindow(pd->hBack)) {
        SetWindowPos(pd->hBack, NULL, pad, btnY, wBack, btnH, SWP_NOZORDER | SWP_NOACTIVATE);
        SetWindowTextW(pd->hBack, backTxt.c_str());
        EnableWindow(pd->hBack, PrevVisibleType(pd->type) != pd->type ? TRUE : FALSE);
    }
    if (pd->hCancel && IsWindow(pd->hCancel))
        SetWindowPos(pd->hCancel, NULL, cW - pad - wCancel, btnY,
                     wCancel, btnH, SWP_NOZORDER | SWP_NOACTIVATE);
    if (pd->hNext && IsWindow(pd->hNext)) {
        SetWindowPos(pd->hNext, NULL, cW - pad - wCancel - gap - wNext, btnY,
                     wNext, btnH, SWP_NOZORDER | SWP_NOACTIVATE);
        SetWindowTextW(pd->hNext, nextTxt.c_str());
    }
}

// ── PopulateExtras — set up the type-specific extras panel ───────────────────
// Called from NavigateTo and from WM_CREATE for the initial dialog type.
// Destroys any dynamic components checkboxes, then creates/shows the controls
// appropriate for newType.  Sets pd->showExtras so LayoutPreviewControls knows
// whether to use the split layout.

static void PopulateExtras(HWND hwnd, PreviewData* pd, InstallerDialogType newType)
{
    // Destroy component checkboxes from any previous navigation.
    for (HWND h : pd->hCompChecks) {
        if (h && IsWindow(h)) DestroyWindow(h);
    }
    pd->hCompChecks.clear();

    // Restore content area visibility in case it was hidden for a previous type.
    pd->contentHidden = false;
    if (pd->hContent && IsWindow(pd->hContent))
        ShowWindow(pd->hContent, SW_SHOW);

    // Hide all persistent extras controls; only the relevant ones are shown below.
    if (pd->hRadioMe  && IsWindow(pd->hRadioMe))  ShowWindow(pd->hRadioMe,  SW_HIDE);
    if (pd->hRadioAll && IsWindow(pd->hRadioAll)) ShowWindow(pd->hRadioAll, SW_HIDE);
    if (pd->hExtrasLabel && IsWindow(pd->hExtrasLabel))
        ShowWindow(pd->hExtrasLabel, SW_HIDE);

    if (newType == IDLG_FOR_ME_ALL) {
        pd->showExtras = true;
        // The radio buttons are self-explanatory; no separate label is needed.
        if (pd->hExtrasLabel && IsWindow(pd->hExtrasLabel))
            SetWindowTextW(pd->hExtrasLabel, L"");
        ShowWindow(pd->hRadioMe,  SW_SHOW);
        ShowWindow(pd->hRadioAll, SW_SHOW);

    } else if (newType == IDLG_COMPONENTS) {
        pd->showExtras = true;

        // If no RTF text has been entered yet, hide the content area so the
        // component list fills the whole interior instead of getting half.
        if (s_dialogs[(int)IDLG_COMPONENTS].content_rtf.empty()) {
            pd->contentHidden = true;
            if (pd->hContent && IsWindow(pd->hContent))
                ShowWindow(pd->hContent, SW_HIDE);
        }

        if (pd->hExtrasLabel && IsWindow(pd->hExtrasLabel))
            SetWindowTextW(pd->hExtrasLabel,
                L10n(L"idlg_prv_comp_label", L"Select which components to install:").c_str());

        // Build controls — required components first, then optional.
        // Required → disabled custom checkbox (gray tick, black label via MarkCheckboxRequired).
        // Optional → enabled custom checkbox (interactive).
        const auto& comps = MainWindow::GetComponents();
        bool any = false;
        for (int pass = 0; pass < 2; ++pass) {
        for (const ComponentRow& c : comps) {
            if (c.source_type != L"folder" || c.display_name.empty()) continue;
            bool required = (c.is_required != 0);
            if (pass == 0 && !required) continue;   // pass 0: required only
            if (pass == 1 &&  required) continue;   // pass 1: optional only
            any = true;
            bool preselected = (c.is_preselected != 0) || required;
            std::wstring label = required
                ? c.display_name + L"  " + L10n(L"idlg_prv_comp_required", L"(required)")
                : c.display_name;
            int idx = (int)pd->hCompChecks.size();
            HWND hChk = CreateCustomCheckbox(hwnd,
                IDC_IDLG_PRV_COMP_BASE + idx,
                label, preselected,
                0, 0, 10, 22,
                s_hInst);
            if (s_hGuiFont) SendMessageW(hChk, WM_SETFONT, (WPARAM)s_hGuiFont, TRUE);
            if (required) {
                MarkCheckboxRequired(hChk);   // black label, gray tick
                EnableWindow(hChk, FALSE);
            }
            pd->hCompChecks.push_back(hChk);
        }
        }
        if (!any) {
            // Project has no folder-level components yet — show a placeholder.
            HWND hLbl = CreateWindowExW(0, L"STATIC",
                L10n(L"idlg_prv_no_comps", L"No components defined yet.").c_str(),
                WS_CHILD | WS_VISIBLE | SS_LEFT,
                0, 0, 10, 10,
                hwnd, NULL, s_hInst, NULL);
            if (s_hGuiFont) SendMessageW(hLbl, WM_SETFONT, (WPARAM)s_hGuiFont, TRUE);
            pd->hCompChecks.push_back(hLbl);
        }

    } else {
        pd->showExtras = false;
    }
}

// Returns the natural content height of hRE in LOGICAL pixels (96 dpi baseline).
// Uses EM_FORMATRANGE to measure without rendering; the control need not be visible.
// After formatting the entire content into a very tall rect, fr.rc.top holds the
// y-extent of the rendered content in twips; divide by 15 to get logical pixels.
static int MeasureRichEditLogHeight(HWND hRE)
{
    if (!hRE || !IsWindow(hRE)) return 0;

    // Primary: inspect the vertical scroll range.  After ShowWindow + UpdateWindow
    // the RichEdit has fully painted, so the scrollbar reflects the true content
    // extent — including \pict images that EM_FORMATRANGE measure-only mode
    // routinely undercounts because it does not invoke the picture-rendering path.
    //
    // Windows scroll-bar convention:
    //   total content height (px) = si.nMax + 1
    //   visible area         (px) = si.nPage
    //   content overflows         when nMax + 1 > nPage
    {
        SCROLLINFO si = {};
        si.cbSize = sizeof(si);
        si.fMask  = SIF_RANGE | SIF_PAGE;
        GetScrollInfo(hRE, SB_VERT, &si);
        if (si.nPage > 0 && si.nMax + 1 > (int)si.nPage) {
            // Content overflows.  nMax + 1 is the total height in physical pixels.
            int contentPx = si.nMax + 1;
            return (int)(contentPx / g_dpiScale + 0.5f);
        }
    }

    // Fallback: EM_FORMATRANGE measurement (content fits in the visible area, or
    // the scrollbar wasn't set up yet — text-only content where EM_FORMATRANGE
    // is accurate).
    RECT rc; GetClientRect(hRE, &rc);
    int clientWPx = rc.right - rc.left;
    if (clientWPx <= 0) clientWPx = S(s_previewLogW) - 2 * S(16);

    HDC hdc = GetDC(hRE);
    if (!hdc) return 0;
    int dpiX = GetDeviceCaps(hdc, LOGPIXELSX);
    if (!dpiX) dpiX = 96;

    // 1440 twips per inch; convert physical px to twips.
    LONG widthTwips = MulDiv(clientWPx, 1440, dpiX);

    FORMATRANGE fr   = {};
    fr.hdc           = hdc;
    fr.hdcTarget     = hdc;
    fr.chrg.cpMin    = 0;
    fr.chrg.cpMax    = -1;           // format all text
    fr.rcPage        = { 0, 0, widthTwips, 0x0FFFFFFF };
    fr.rc            = fr.rcPage;    // same — no margin offset needed

    // wParam = FALSE → measure only, do not render.
    // After the call fr.rc.top is advanced to the y position past the last line.
    SendMessageW(hRE, EM_FORMATRANGE, FALSE, (LPARAM)&fr);
    SendMessageW(hRE, EM_FORMATRANGE, FALSE, 0); // release cached info

    ReleaseDC(hRE, hdc);

    // Convert twips → logical pixels (1440 twips / 96 logical px = 15 twips/lpx).
    return MulDiv((int)fr.rc.top, 96, 1440);
}

// Auto-fit the preview window height to the IDLG_COMPONENTS page contents.
// Two layouts are handled:
//
//  contentHidden = true  (no RTF — items fill the full interior)
//    editY(60) + extLblH(22) + extGap(6) + items + breathing(10) + btnH(30) + pad(16)
//    → logH = 144 + n×28   (n = max(nItems,1))
//
//  contentHidden = false  (RTF present — split: measured RTF top, items bottom)
//    The RichEdit height is measured with EM_FORMATRANGE so the full content
//    (including images) is always visible without a scrollbar.
//    → logH = 68 + measuredRtfH + 36 + n×28
//             └ editY+gap ┘        └ lower half ┘
//
// The sizer height spinner is updated to match the new size.
static void AutoFitComponentHeight(HWND hPreview, HWND hSizer, PreviewData* pd)
{
    int n = std::max((int)pd->hCompChecks.size(), 1);
    int logH;
    if (pd->contentHidden) {
        // No RTF: editY(60) + extLblH(22) + extGap(6) + items + breathing(10)
        //        + btnH(30) + pad(16) = 144 + n*28
        logH = 144 + n * 28;
    } else {
        // Split layout: LayoutPreviewControls gives the RTF area exactly avail/2,
        // where avail = logH - 114.  For the full image/content to be visible:
        //   avail/2 >= rtfLogH  →  logH >= 2*rtfLogH + 114
        // For all checkboxes to fit in the lower half:
        //   avail/2 - 36 >= n*28 - 4  →  logH >= n*56 + 178
        // Use whichever constraint is tighter, plus a small breathing margin.
        // Force a paint cycle so GetScrollInfo in MeasureRichEditLogHeight
        // sees the final scroll range (images lay out during painting).
        if (pd->hContent && IsWindow(pd->hContent))
            UpdateWindow(pd->hContent);
        int rtfLogH = MeasureRichEditLogHeight(pd->hContent);
        if (rtfLogH < 60) rtfLogH = 60;  // safe minimum
        // Tight-fit formula: give the RichEdit exactly what it needs and the
        // items panel exactly what it needs — no 50/50 wasted space.
        //   editY(60) + rtfLogH + gap(8) + extLbl(22) + extGap(6)
        //   + n*itemH(28) + breathing(10) + gap(8) + btnH(30) + pad(16) = 160 + rtfLogH + n*28
        // (Derived by equating last-item-bottom + S(10) <= btnY = cH - S(46))
        logH = 160 + rtfLogH + n * 28;
        pd->contentFitH = rtfLogH;
    }
    // ── 75% screen height cap — add scrollbar only when content overflows ─────
    {
        HMONITOR hMon = MonitorFromWindow(hPreview, MONITOR_DEFAULTTONEAREST);
        MONITORINFO mi = {}; mi.cbSize = sizeof(mi);
        int maxLogH;
        if (hMon && GetMonitorInfoW(hMon, &mi))
            maxLogH = (int)((mi.rcWork.bottom - mi.rcWork.top) * 0.75f / g_dpiScale);
        else
            maxLogH = (int)(GetSystemMetrics(SM_CYSCREEN) * 0.75f / g_dpiScale);
        bool needsScroll = logH > maxLogH;
        if (needsScroll) logH = maxLogH;
        pd->contentNeedsScroll = needsScroll;
    }
    logH = std::max(150, logH);
    s_previewLogH = logH;

    // Show or hide the vertical scrollbar on the RichEdit; when adding one,
    // widen the preview window by SM_CXVSCROLL so the text viewport stays constant.
    int sbPx = 0;  // extra physical pixels to add to preview width
    if (pd->hContent && IsWindow(pd->hContent)) {
        LONG st = GetWindowLongW(pd->hContent, GWL_STYLE);
        bool hasSb = (st & WS_VSCROLL) != 0;
        if (pd->contentNeedsScroll && !hasSb) {
            SetWindowLongW(pd->hContent, GWL_STYLE, st | WS_VSCROLL);
            SetWindowPos(pd->hContent, NULL, 0,0,0,0,
                SWP_NOMOVE|SWP_NOSIZE|SWP_NOZORDER|SWP_FRAMECHANGED|SWP_NOACTIVATE);
        } else if (!pd->contentNeedsScroll && hasSb) {
            SetWindowLongW(pd->hContent, GWL_STYLE, st & ~WS_VSCROLL);
            SetWindowPos(pd->hContent, NULL, 0,0,0,0,
                SWP_NOMOVE|SWP_NOSIZE|SWP_NOZORDER|SWP_FRAMECHANGED|SWP_NOACTIVATE);
        }
        if (pd->contentNeedsScroll) sbPx = GetSystemMetrics(SM_CXVSCROLL);
    }

    RECT adj = { 0, 0, S(s_previewLogW) + sbPx, S(logH) };
    AdjustWindowRectEx(&adj, kPreviewStyle, FALSE, kPreviewExStyle);
    SetWindowPos(hPreview, NULL, 0, 0,
                 adj.right - adj.left, adj.bottom - adj.top,
                 SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);

    if (hSizer && IsWindow(hSizer)) {
        SizerData* sd = (SizerData*)GetWindowLongPtrW(hSizer, GWLP_USERDATA);
        HWND hHS = GetDlgItem(hSizer, IDC_IDLG_SZR_H_SPIN);
        if (sd && hHS) {
            sd->ignoring = true;
            SendMessageW(hHS, UDM_SETPOS32, 0, (LPARAM)logH);
            sd->ignoring = false;
        }
    }
}

// ── Navigation — update content and UI for a new dialog type ─────────────────

static void NavigateTo(HWND hwnd, PreviewData* pd, InstallerDialogType newType)
{
    pd->type        = newType;
    pd->contentFitH = 0;  // reset; AutoFitComponentHeight will re-measure if needed
    PopulateExtras(hwnd, pd, newType);

    // Window caption: "{Installer title} — Preview — {Dialog name}"
    std::wstring dlgName = L10n(kDialogNameKeys[(int)newType], kDialogNameFallbacks[(int)newType]);
    std::wstring caption = s_installTitle + L"  \u2014  "
        + L10n(L"idlg_preview_caption", L"Preview") + L"  \u2014  " + dlgName;
    SetWindowTextW(hwnd, caption.c_str());

    // Update the heading label inside the window
    if (pd->hTypeTitle) SetWindowTextW(pd->hTypeTitle, dlgName.c_str());

    // Re-stream RTF (must be writable during EM_STREAMIN)
    if (pd->hContent) {
        SendMessageW(pd->hContent, EM_SETREADONLY, FALSE, 0);
        // Clear existing content
        SendMessageW(pd->hContent, EM_SETSEL, 0, (LPARAM)-1);
        SendMessageW(pd->hContent, EM_REPLACESEL, FALSE, (LPARAM)L"");

        const std::wstring& rtf = s_dialogs[(int)newType].content_rtf;
        if (!rtf.empty()) {
            StreamRtfIn(pd->hContent, rtf);
        } else {
            std::wstring ph = L10n(L"idlg_preview_no_content", L"(No content defined for this dialog yet)");
            SetWindowTextW(pd->hContent, ph.c_str());
        }
        SendMessageW(pd->hContent, EM_SETREADONLY, TRUE, 0);
        SendMessageW(pd->hContent, EM_SETSEL, 0, 0);
        SendMessageW(pd->hContent, EM_SCROLLCARET, 0, 0);
    }

    // If the scrollbar was active (content was taller than 75% of the screen),
    // remove it and revert the preview to its canonical width before laying out
    // the new page — otherwise the window stays SM_CXVSCROLL px too wide.
    if (pd->contentNeedsScroll) {
        pd->contentNeedsScroll = false;
        if (pd->hContent && IsWindow(pd->hContent)) {
            LONG st = GetWindowLongW(pd->hContent, GWL_STYLE);
            if (st & WS_VSCROLL) {
                SetWindowLongW(pd->hContent, GWL_STYLE, st & ~WS_VSCROLL);
                SetWindowPos(pd->hContent, NULL, 0,0,0,0,
                    SWP_NOMOVE|SWP_NOSIZE|SWP_NOZORDER|SWP_FRAMECHANGED|SWP_NOACTIVATE);
            }
        }
        RECT adjR = { 0, 0, S(s_previewLogW), S(s_previewLogH) };
        AdjustWindowRectEx(&adjR, kPreviewStyle, FALSE, kPreviewExStyle);
        SetWindowPos(hwnd, NULL, 0, 0, adjR.right - adjR.left, adjR.bottom - adjR.top,
                     SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
    }

    // Reposition buttons (text and enable state may have changed)
    LayoutPreviewControls(hwnd, pd);

    // Auto-fit height for the Components page (both split and no-content layouts)
    // so the window always feels right out of the box.  Skip if the developer has
    // already manually resized via the sizer — we must respect that choice.
    if (newType == IDLG_COMPONENTS && !s_previewUserSized[IDLG_COMPONENTS]) {
        // Force a full paint cycle so EM_FORMATRANGE sees the final laid-out content.
        UpdateWindow(hwnd);
        AutoFitComponentHeight(hwnd, pd->hSizer, pd);
    }
}

// ── Preview window proc ───────────────────────────────────────────────────────

static LRESULT CALLBACK PreviewWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    PreviewData* pd = (PreviewData*)(LONG_PTR)GetWindowLongPtrW(hwnd, GWLP_USERDATA);

    switch (msg) {
    case WM_CREATE: {
        CREATESTRUCTW* cs = (CREATESTRUCTW*)lParam;
        pd = (PreviewData*)cs->lpCreateParams;
        if (!pd) return -1;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)pd);

        // Ensure the RichEdit class is registered.
        static HMODULE s_hRe = NULL;
        if (!s_hRe) {
            s_hRe = LoadLibraryW(L"Msftedit.dll");
            if (!s_hRe) s_hRe = LoadLibraryW(L"Riched20.dll");
        }
        WNDCLASSEXW wce = {}; wce.cbSize = sizeof(wce);
        const wchar_t* reClass =
            (s_hRe && GetClassInfoExW(s_hRe, L"RICHEDIT50W", &wce))
            ? L"RICHEDIT50W" : L"RichEdit20W";

        // Create all interior controls at placeholder positions; LayoutPreviewControls
        // will position them correctly immediately afterwards.
        std::wstring dlgName = L10n(kDialogNameKeys[(int)pd->type],
                                    kDialogNameFallbacks[(int)pd->type]);
        pd->hTypeTitle = CreateWindowExW(0, L"STATIC", dlgName.c_str(),
            WS_CHILD | WS_VISIBLE | SS_LEFT,
            0, 0, 10, 10, hwnd, NULL, cs->hInstance, NULL);
        if (pd->hTitleFont) SendMessageW(pd->hTypeTitle, WM_SETFONT, (WPARAM)pd->hTitleFont, TRUE);

        // RichEdit — created writable so EM_STREAMIN works; made read-only after.
        // Created WITHOUT WS_VSCROLL; AutoFitComponentHeight adds it only when the
        // 75%-screen-height cap is hit, compensating the dialog width so the text
        // viewport stays the same width whether or not a scrollbar is present.
        pd->hContent = CreateWindowExW(WS_EX_CLIENTEDGE, reClass, L"",
            WS_CHILD | WS_VISIBLE | ES_MULTILINE | ES_AUTOVSCROLL,
            0, 0, 10, 10,
            hwnd, (HMENU)(UINT_PTR)IDC_IDLG_PRV_CONTENT, cs->hInstance, NULL);
        if (pd->hGuiFont) SendMessageW(pd->hContent, WM_SETFONT, (WPARAM)pd->hGuiFont, TRUE);
        SendMessageW(pd->hContent, EM_SETBKGNDCOLOR, 0, RGB(255, 255, 255));

        const std::wstring& rtf = s_dialogs[(int)pd->type].content_rtf;
        if (!rtf.empty()) {
            StreamRtfIn(pd->hContent, rtf);
        } else {
            SetWindowTextW(pd->hContent,
                L10n(L"idlg_preview_no_content", L"(No content defined for this dialog yet)").c_str());
        }
        SendMessageW(pd->hContent, EM_SETREADONLY, TRUE, 0);
        SendMessageW(pd->hContent, EM_SETSEL, 0, 0);
        SendMessageW(pd->hContent, EM_SCROLLCARET, 0, 0);

        // Buttons — text will be updated by LayoutPreviewControls anyway.
        pd->hBack = CreateWindowExW(0, L"BUTTON", L"",
            WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 0, 0, 10, 10,
            hwnd, (HMENU)(UINT_PTR)IDC_IDLG_PRV_BACK, cs->hInstance, NULL);
        if (pd->hGuiFont) SendMessageW(pd->hBack, WM_SETFONT, (WPARAM)pd->hGuiFont, TRUE);

        pd->hNext = CreateWindowExW(0, L"BUTTON", L"",
            WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 0, 0, 10, 10,
            hwnd, (HMENU)(UINT_PTR)IDC_IDLG_PRV_NEXT, cs->hInstance, NULL);
        if (pd->hGuiFont) SendMessageW(pd->hNext, WM_SETFONT, (WPARAM)pd->hGuiFont, TRUE);

        pd->hCancel = CreateWindowExW(0, L"BUTTON", L"",
            WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 0, 0, 10, 10,
            hwnd, (HMENU)(UINT_PTR)IDC_IDLG_PRV_CANCEL, cs->hInstance, NULL);
        if (pd->hGuiFont) SendMessageW(pd->hCancel, WM_SETFONT, (WPARAM)pd->hGuiFont, TRUE);
        std::wstring cancelTxt = L10n(L"idlg_preview_cancel", L"Cancel");
        SetWindowTextW(pd->hCancel, cancelTxt.c_str());

        // Extras controls — created hidden; PopulateExtras shows the right set
        // based on the initial dialog type, called just before the layout pass.
        pd->hExtrasLabel = CreateWindowExW(0, L"STATIC", L"",
            WS_CHILD | SS_LEFT,   // no WS_VISIBLE — shown by PopulateExtras
            0, 0, 10, 10,
            hwnd, (HMENU)(UINT_PTR)IDC_IDLG_PRV_EXTRAS_LABEL, cs->hInstance, NULL);
        if (pd->hGuiFont) SendMessageW(pd->hExtrasLabel, WM_SETFONT, (WPARAM)pd->hGuiFont, TRUE);

        pd->hRadioMe = CreateWindowExW(0, L"BUTTON",
            L10n(L"idlg_prv_scope_me", L"Install just for me (current user)").c_str(),
            WS_CHILD | BS_AUTORADIOBUTTON | WS_GROUP,   // no WS_VISIBLE
            0, 0, 10, 10,
            hwnd, (HMENU)(UINT_PTR)IDC_IDLG_PRV_RADIO_ME, cs->hInstance, NULL);
        if (pd->hGuiFont) SendMessageW(pd->hRadioMe, WM_SETFONT, (WPARAM)pd->hGuiFont, TRUE);
        SendMessageW(pd->hRadioMe, BM_SETCHECK, BST_CHECKED, 0);  // default: for me

        pd->hRadioAll = CreateWindowExW(0, L"BUTTON",
            L10n(L"idlg_prv_scope_all", L"Install for all users (requires administrator)").c_str(),
            WS_CHILD | BS_AUTORADIOBUTTON,   // no WS_VISIBLE
            0, 0, 10, 10,
            hwnd, (HMENU)(UINT_PTR)IDC_IDLG_PRV_RADIO_ALL, cs->hInstance, NULL);
        if (pd->hGuiFont) SendMessageW(pd->hRadioAll, WM_SETFONT, (WPARAM)pd->hGuiFont, TRUE);

        // Set up extras for the initial dialog type, then run the layout pass.
        pd->showExtras    = false;
        pd->contentHidden = false;
        PopulateExtras(hwnd, pd, pd->type);

        // Final layout pass positions all controls correctly.
        LayoutPreviewControls(hwnd, pd);
        return 0;
    }

    case WM_SIZE:
        // Triggered both by the user resizing (if WS_THICKFRAME is added later)
        // and by the sizer panel calling SetWindowPos on the preview window.
        if (pd) LayoutPreviewControls(hwnd, pd);
        return 0;

    case WM_COMMAND: {
        int id = LOWORD(wParam);
        if (!pd) return 0;

        if (id == IDC_IDLG_PRV_BACK) {
            // Navigate to the previous visible dialog type.
            InstallerDialogType prev = PrevVisibleType(pd->type);
            if (prev != pd->type) NavigateTo(hwnd, pd, prev);
            return 0;
        }
        if (id == IDC_IDLG_PRV_NEXT) {
            // Navigate to the next visible dialog type; close when already at last.
            InstallerDialogType next = NextVisibleType(pd->type);
            if (next == pd->type) {
                // At Finish — committed; do NOT cancel dimension changes.
                pd->running = false;
            } else {
                NavigateTo(hwnd, pd, next);
            }
            return 0;
        }
        if (id == IDC_IDLG_PRV_CANCEL) {
            // Mark as cancelled so ShowPreviewDialog can revert dimension changes.
            pd->running   = false;
            pd->cancelled = true;
            return 0;
        }
        return 0;
    }

    case WM_CLOSE:
        // No WS_SYSMENU means no × button, but WM_CLOSE can still arrive via
        // Alt+F4 or the taskbar — treat it the same as Cancel.
        if (pd) { pd->running = false; pd->cancelled = true; }
        return 0;

    case WM_GETMINMAXINFO: {
        // Enforce a minimum preview size so the split layout is always usable.
        RECT adj = { 0, 0, S(300), S(260) };
        AdjustWindowRectEx(&adj, kPreviewStyle, FALSE, kPreviewExStyle);
        MINMAXINFO* mm = (MINMAXINFO*)lParam;
        mm->ptMinTrackSize.x = adj.right - adj.left;
        mm->ptMinTrackSize.y = adj.bottom - adj.top;
        return 0;
    }

    case WM_DRAWITEM: {
        // Required by custom themed checkboxes (BS_OWNERDRAW internals).
        LPDRAWITEMSTRUCT dis = (LPDRAWITEMSTRUCT)lParam;
        if (DrawCustomCheckbox(dis)) return TRUE;
        return 0;
    }

    case WM_SETTINGCHANGE:
        // Repaint custom checkboxes when the system theme changes.
        OnCheckboxSettingChange(hwnd);
        return 0;

    case WM_CTLCOLORSTATIC:
        // Radio buttons send this to the parent; return a white brush to match
        // the preview window background and avoid a grey strip behind control text.
        SetBkColor((HDC)wParam, GetSysColor(COLOR_WINDOW));
        return (LRESULT)GetSysColorBrush(COLOR_WINDOW);

    case WM_DESTROY:
        // Child windows are auto-destroyed by Windows; clear the vector so we
        // don't hold dangling HWNDs.  EnableWindow for the owner is handled by
        // ShowPreviewDialog immediately after the message loop exits.
        if (pd) pd->hCompChecks.clear();
        return 0;
    }

    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

// ── Sizer panel ───────────────────────────────────────────────────────────────
// A small always-on-top floating window with Width and Height spinners.
// Changes to either value immediately resize the preview window so the
// developer sees the result in real time.

// Forward declaration so SizerWndProc can call it.
static void ResizePreview(SizerData* sd, int logW, int logH);

static LRESULT CALLBACK SizerWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    SizerData* sd = (SizerData*)(LONG_PTR)GetWindowLongPtrW(hwnd, GWLP_USERDATA);

    switch (msg) {
    case WM_CREATE: {
        CREATESTRUCTW* cs = (CREATESTRUCTW*)lParam;
        sd = (SizerData*)cs->lpCreateParams;
        if (!sd) return -1;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)sd);

        // Require InitCommonControlsEx for UPDOWN_CLASS registration.
        INITCOMMONCONTROLSEX icc = { sizeof(icc), ICC_UPDOWN_CLASS };
        InitCommonControlsEx(&icc);

        HFONT hF = sd->hFont;
        HINSTANCE hI = cs->hInstance;
        const int pad  = S(10);
        const int lblW = S(80);
        const int edW  = S(52);
        const int spW  = S(18);
        const int rowH = S(22);
        const int gap  = S(8);
        int y = pad;

        // Row 1: Width
        HWND hWL = CreateWindowExW(0, L"STATIC",
            L10n(L"idlg_sizer_w_label", L"Width (px):").c_str(),
            WS_CHILD | WS_VISIBLE | SS_LEFT | SS_CENTERIMAGE,
            pad, y, lblW, rowH, hwnd, NULL, hI, NULL);
        if (hF) SendMessageW(hWL, WM_SETFONT, (WPARAM)hF, TRUE);

        HWND hWE = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
            WS_CHILD | WS_VISIBLE | ES_NUMBER | ES_RIGHT,
            pad + lblW + gap, y, edW, rowH,
            hwnd, (HMENU)(UINT_PTR)IDC_IDLG_SZR_W_EDIT, hI, NULL);
        if (hF) SendMessageW(hWE, WM_SETFONT, (WPARAM)hF, TRUE);

        HWND hWS = CreateWindowExW(0, UPDOWN_CLASSW, L"",
            WS_CHILD | WS_VISIBLE | UDS_SETBUDDYINT | UDS_ALIGNRIGHT | UDS_ARROWKEYS | UDS_HOTTRACK,
            0, 0, spW, rowH,
            hwnd, (HMENU)(UINT_PTR)IDC_IDLG_SZR_W_SPIN, hI, NULL);
        SendMessageW(hWS, UDM_SETBUDDY,   (WPARAM)hWE,  0);
        SendMessageW(hWS, UDM_SETRANGE32, 200,           1400);
        sd->ignoring = true;
        SendMessageW(hWS, UDM_SETPOS32,   0, (LPARAM)s_previewLogW);
        sd->ignoring = false;

        y += rowH + gap;

        // Row 2: Height
        HWND hHL = CreateWindowExW(0, L"STATIC",
            L10n(L"idlg_sizer_h_label", L"Height (px):").c_str(),
            WS_CHILD | WS_VISIBLE | SS_LEFT | SS_CENTERIMAGE,
            pad, y, lblW, rowH, hwnd, NULL, hI, NULL);
        if (hF) SendMessageW(hHL, WM_SETFONT, (WPARAM)hF, TRUE);

        HWND hHE = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
            WS_CHILD | WS_VISIBLE | ES_NUMBER | ES_RIGHT,
            pad + lblW + gap, y, edW, rowH,
            hwnd, (HMENU)(UINT_PTR)IDC_IDLG_SZR_H_EDIT, hI, NULL);
        if (hF) SendMessageW(hHE, WM_SETFONT, (WPARAM)hF, TRUE);

        HWND hHS = CreateWindowExW(0, UPDOWN_CLASSW, L"",
            WS_CHILD | WS_VISIBLE | UDS_SETBUDDYINT | UDS_ALIGNRIGHT | UDS_ARROWKEYS | UDS_HOTTRACK,
            0, 0, spW, rowH,
            hwnd, (HMENU)(UINT_PTR)IDC_IDLG_SZR_H_SPIN, hI, NULL);
        SendMessageW(hHS, UDM_SETBUDDY,   (WPARAM)hHE,  0);
        SendMessageW(hHS, UDM_SETRANGE32, 150,           1000);
        sd->ignoring = true;
        SendMessageW(hHS, UDM_SETPOS32,   0, (LPARAM)s_previewLogH);
        sd->ignoring = false;

        // Tooltips on the edit fields
        HWND hTT = CreateWindowExW(0, TOOLTIPS_CLASSW, NULL,
            WS_POPUP | TTS_ALWAYSTIP | TTS_NOPREFIX,
            CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT,
            hwnd, NULL, hI, NULL);
        SendMessageW(hTT, TTM_SETMAXTIPWIDTH, 0, S(280));

        TOOLINFOW ti = {}; ti.cbSize = sizeof(ti); ti.uFlags = TTF_IDISHWND | TTF_SUBCLASS;
        ti.hwnd = hwnd;

        std::wstring wTip = L10n(L"idlg_sizer_w_tip",
            L"Installer dialog width in logical pixels (96 dpi / 100% scaling).\n"
            L"The preview scales automatically to match your current display DPI.");
        ti.uId = (UINT_PTR)hWE;
        ti.lpszText = const_cast<wchar_t*>(wTip.c_str());
        SendMessageW(hTT, TTM_ADDTOOLW, 0, (LPARAM)&ti);

        std::wstring hTip = L10n(L"idlg_sizer_h_tip",
            L"Installer dialog height in logical pixels (96 dpi / 100% scaling).\n"
            L"The preview scales automatically to match your current display DPI.");
        ti.uId = (UINT_PTR)hHE;
        ti.lpszText = const_cast<wchar_t*>(hTip.c_str());
        SendMessageW(hTT, TTM_ADDTOOLW, 0, (LPARAM)&ti);

        return 0;
    }

    case WM_COMMAND: {
        if (!sd || sd->ignoring) return 0;
        int id    = LOWORD(wParam);
        int event = HIWORD(wParam);
        if ((id == IDC_IDLG_SZR_W_EDIT || id == IDC_IDLG_SZR_H_EDIT) && event == EN_CHANGE) {
            // Read both values; clamp; update globals; resize preview.
            HWND hWE = GetDlgItem(hwnd, IDC_IDLG_SZR_W_EDIT);
            HWND hHE = GetDlgItem(hwnd, IDC_IDLG_SZR_H_EDIT);
            wchar_t buf[16];
            GetWindowTextW(hWE, buf, _countof(buf));  int newW = _wtoi(buf);
            GetWindowTextW(hHE, buf, _countof(buf));  int newH = _wtoi(buf);
            // Only act on valid non-zero values to avoid thrashing on empty edit.
            if (newW >= 200 && newH >= 150) {
                newW = std::min(newW, 1400);
                newH = std::min(newH, 1000);
                s_previewLogW = newW;
                s_previewLogH = newH;
                // The developer has made a deliberate sizing choice — stop
                // auto-fitting from this point on for this dialog type.
                s_previewUserSized[sd->pd->type] = true;
                if (sd->hPreview && IsWindow(sd->hPreview))
                    ResizePreview(sd, newW, newH);
                MainWindow::MarkAsModified();
            }
        }
        return 0;
    }

    case WM_CLOSE:
        // Sizer cannot be closed independently; it closes with the preview.
        return 0;

    case WM_DESTROY:
        return 0;
    }

    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

// Resize the preview window to logical dimensions logW × logH (at 96 dpi).
// The sizer calls this on every EN_CHANGE; WM_SIZE on the preview then
// calls LayoutPreviewControls automatically.
static void ResizePreview(SizerData* sd, int logW, int logH)
{
    // Do NOT reset contentFitH here — the measured RTF height stays valid
    // regardless of width changes and the user must not see the layout shift.
    RECT adj = { 0, 0, S(logW), S(logH) };
    AdjustWindowRectEx(&adj, kPreviewStyle, FALSE, kPreviewExStyle);
    SetWindowPos(sd->hPreview, NULL, 0, 0,
                 adj.right - adj.left, adj.bottom - adj.top,
                 SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
}

// ── ShowPreviewDialog ─────────────────────────────────────────────────────────

static void ShowPreviewDialog(HWND hwndParent, InstallerDialogType type)
{
    // Register window classes once per process.
    static bool s_classesOk = false;
    if (!s_classesOk) {
        WNDCLASSEXW wc = {};
        wc.cbSize = sizeof(wc);
        wc.hInstance     = s_hInst;
        wc.hCursor       = LoadCursor(NULL, IDC_ARROW);
        wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);

        wc.lpfnWndProc   = PreviewWndProc;
        wc.lpszClassName = L"IDLGPreviewClass";
        RegisterClassExW(&wc);

        wc.lpfnWndProc   = SizerWndProc;
        wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
        wc.lpszClassName = L"IDLGSizerClass";
        RegisterClassExW(&wc);

        s_classesOk = true;
    }

    // ── Compute preview outer size ────────────────────────────────────────────
    RECT rcAdj = { 0, 0, S(s_previewLogW), S(s_previewLogH) };
    AdjustWindowRectEx(&rcAdj, kPreviewStyle, FALSE, kPreviewExStyle);
    int wndW = rcAdj.right  - rcAdj.left;
    int wndH = rcAdj.bottom - rcAdj.top;

    // ── Compute sizer outer size ──────────────────────────────────────────────
    const DWORD sizerStyle   = WS_POPUP | WS_CAPTION | WS_BORDER;
    // WS_EX_TOOLWINDOW keeps sizer off Alt+Tab. No WS_EX_TOPMOST — we want
    // the sizer to stay above the preview (achieved by ownership) but NOT
    // float above unrelated apps when the developer switches programs.
    const DWORD sizerExStyle = WS_EX_TOOLWINDOW;
    RECT rcSz = { 0, 0, S(165), S(82) };
    AdjustWindowRectEx(&rcSz, sizerStyle, FALSE, sizerExStyle);
    int szW = rcSz.right  - rcSz.left;
    int szH = rcSz.bottom - rcSz.top;

    // ── Position both windows (preview centred; sizer to its left) ────────────
    RECT rcParent; GetWindowRect(hwndParent, &rcParent);
    int px = rcParent.left + (rcParent.right  - rcParent.left - wndW) / 2;
    int py = rcParent.top  + (rcParent.bottom - rcParent.top  - wndH) / 2;
    // Clamp preview to work area
    MONITORINFO mi = {}; mi.cbSize = sizeof(mi);
    HMONITOR hMon = MonitorFromWindow(hwndParent, MONITOR_DEFAULTTONEAREST);
    if (hMon && GetMonitorInfoW(hMon, &mi)) {
        RECT& wa = mi.rcWork;
        if (px + wndW > wa.right)  px = wa.right  - wndW;
        if (py + wndH > wa.bottom) py = wa.bottom - wndH;
        if (px < wa.left)  px = wa.left;
        if (py < wa.top)   py = wa.top;
    }
    int sxX = px - S(8) - szW; // sizer to the left of preview
    int sxY = py;               // aligned to preview top

    // ── Installer icon for the preview title bar ──────────────────────────────
    wchar_t sysDir[MAX_PATH];
    GetSystemDirectoryW(sysDir, _countof(sysDir));
    std::wstring shell32Path = std::wstring(sysDir) + L"\\shell32.dll";
    int szSm = GetSystemMetrics(SM_CXSMICON);
    int szLg = GetSystemMetrics(SM_CXICON);
    HICON hSmIcon = NULL, hLgIcon = NULL;
    if (!s_installIconPath.empty()) {
        hSmIcon = (HICON)LoadImageW(NULL, s_installIconPath.c_str(),
            IMAGE_ICON, szSm, szSm, LR_LOADFROMFILE);
        hLgIcon = (HICON)LoadImageW(NULL, s_installIconPath.c_str(),
            IMAGE_ICON, szLg, szLg, LR_LOADFROMFILE);
    }
    if (!hSmIcon) PrivateExtractIconsW(shell32Path.c_str(), 2, szSm, szSm, &hSmIcon, NULL, 1, 0);
    if (!hLgIcon) PrivateExtractIconsW(shell32Path.c_str(), 2, szLg, szLg, &hLgIcon, NULL, 1, 0);

    // Save state so Cancel can revert any dimension changes and un-mark
    // the project as modified (if it was clean before the preview opened).
    int  savedLogW = s_previewLogW;
    int  savedLogH = s_previewLogH;
    bool savedUserSized[IDLG_COUNT];
    memcpy(savedUserSized, s_previewUserSized, sizeof(savedUserSized));
    bool wasModified = MainWindow::HasUnsavedChanges();

    // ── Create preview ────────────────────────────────────────────────────────
    PreviewData pd = {};
    pd.type      = type;
    pd.hGuiFont  = s_hGuiFont;
    pd.hTitleFont = s_hTitleFont;
    pd.running   = true;

    // Window caption: "{Installer title} — Preview — {Dialog name}"
    std::wstring dlgName = L10n(kDialogNameKeys[(int)type], kDialogNameFallbacks[(int)type]);
    std::wstring caption = s_installTitle + L"  \u2014  "
        + L10n(L"idlg_preview_caption", L"Preview") + L"  \u2014  " + dlgName;

    HWND hPreview = CreateWindowExW(kPreviewExStyle,
        L"IDLGPreviewClass", caption.c_str(), kPreviewStyle,
        px, py, wndW, wndH, hwndParent, NULL, s_hInst, &pd);
    if (!hPreview) { if (hSmIcon) DestroyIcon(hSmIcon); if (hLgIcon) DestroyIcon(hLgIcon); return; }

    if (hSmIcon) SendMessageW(hPreview, WM_SETICON, ICON_SMALL, (LPARAM)hSmIcon);
    if (hLgIcon) SendMessageW(hPreview, WM_SETICON, ICON_BIG,   (LPARAM)hLgIcon);

    // ── Create sizer ──────────────────────────────────────────────────────────
    SizerData sd  = {};
    sd.pd         = &pd;
    sd.hPreview   = hPreview;
    sd.hFont      = s_hGuiFont;
    sd.ignoring   = false;

    std::wstring sizerCaption = L10n(L"idlg_sizer_title", L"Dialog Size");
    // Owner = hPreview: Windows guarantees owned popups are Z-ordered above
    // their owner, so the sizer stays above the preview without being globally
    // always-on-top.  When the preview is behind another app, so is the sizer.
    HWND hSizer = CreateWindowExW(sizerExStyle,
        L"IDLGSizerClass", sizerCaption.c_str(), sizerStyle,
        sxX, sxY, szW, szH, hPreview, NULL, s_hInst, &sd);
    if (!hSizer) hSizer = NULL; // non-fatal; preview works without the sizer

    // Give the preview access to the sizer so NavigateTo can update the
    // height spinner when auto-fitting the component list.
    pd.hSizer = hSizer;

    ShowWindow(hPreview, SW_SHOW);
    UpdateWindow(hPreview);
    if (hSizer) { ShowWindow(hSizer, SW_SHOW); UpdateWindow(hSizer); }

    // Auto-fit height AFTER the window is visible so EM_FORMATRANGE measures
    // a fully rendered RichEdit (images and OLE objects need a live DC).
    if (pd.type == IDLG_COMPONENTS && !s_previewUserSized[IDLG_COMPONENTS]) {
        AutoFitComponentHeight(hPreview, hSizer, &pd);
        // AutoFitComponentHeight uses SWP_NOMOVE — re-centre vertically after resize.
        RECT rcW; GetWindowRect(hPreview, &rcW);
        int newH  = rcW.bottom - rcW.top;
        int newPy = rcParent.top + (rcParent.bottom - rcParent.top - newH) / 2;
        HMONITOR hM2 = MonitorFromWindow(hPreview, MONITOR_DEFAULTTONEAREST);
        MONITORINFO mi2 = {}; mi2.cbSize = sizeof(mi2);
        if (hM2 && GetMonitorInfoW(hM2, &mi2)) {
            int bTop = mi2.rcWork.top, bBot = mi2.rcWork.bottom;
            if (newPy + newH > bBot) newPy = bBot - newH;
            if (newPy < bTop)        newPy = bTop;
        }
        SetWindowPos(hPreview, NULL, px, newPy, 0, 0,
                     SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
        if (hSizer && IsWindow(hSizer))
            SetWindowPos(hSizer, NULL, sxX, newPy, 0, 0,
                         SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
    }

    EnableWindow(hwndParent, FALSE);

    MSG m;
    while (pd.running && GetMessageW(&m, NULL, 0, 0) > 0) {
        bool handled = false;
        if (hSizer && IsWindow(hSizer) && IsDialogMessageW(hSizer,   &m)) handled = true;
        if (!handled && IsWindow(hPreview) && IsDialogMessageW(hPreview, &m)) handled = true;
        if (!handled) { TranslateMessage(&m); DispatchMessageW(&m); }
    }

    // Standard Win32 modal pattern: enable owner BEFORE destroying the popup
    // so Windows automatically activates it when the popup disappears.
    EnableWindow(hwndParent, TRUE);
    if (hSizer  && IsWindow(hSizer))  DestroyWindow(hSizer);
    if (hPreview && IsWindow(hPreview)) DestroyWindow(hPreview);
    SetActiveWindow(hwndParent);
    SetForegroundWindow(hwndParent);

    // If the developer pressed Cancel (or Alt+F4), revert any dimension
    // changes and un-mark the project as modified if it was clean before.
    if (pd.cancelled) {
        s_previewLogW = savedLogW;
        s_previewLogH = savedLogH;
        memcpy(s_previewUserSized, savedUserSized, sizeof(s_previewUserSized));
        if (!wasModified) MainWindow::MarkAsSaved();
    }

    if (hSmIcon) DestroyIcon(hSmIcon);
    if (hLgIcon) DestroyIcon(hLgIcon);
}

// ── IDLG_Reset ────────────────────────────────────────────────────────────────

void IDLG_Reset()
{
    for (int i = 0; i < IDLG_COUNT; i++) {
        s_dialogs[i].type        = (InstallerDialogType)i;
        s_dialogs[i].content_rtf = L"";
    }
    s_installTitle     = L"";
    s_installIconPath  = L"";
    memset(s_previewUserSized, 0, sizeof(s_previewUserSized));
    // s_hInstallIcon and s_hInstIconPreview are managed by BuildPage/TearDown.
}

// ── IDLG_TearDown ─────────────────────────────────────────────────────────────

void IDLG_TearDown(HWND hwnd)
{
    // Restore the original STATIC wndproc on all icon controls, then destroy
    // the HICON resources.  The STATIC windows themselves are destroyed by
    // SwitchPage's generic child-window teardown loop.
    for (int i = 0; i < IDLG_COUNT; i++) {
        if (s_rowIconCtrls[i] && IsWindow(s_rowIconCtrls[i]) && s_origIconProc) {
            SetWindowLongPtrW(s_rowIconCtrls[i], GWLP_WNDPROC, (LONG_PTR)s_origIconProc);
        }
        if (s_rowIcons[i]) {
            DestroyIcon(s_rowIcons[i]);
            s_rowIcons[i] = NULL;
        }
        s_rowIconCtrls[i] = NULL;
    }
    s_origIconProc = NULL;

    // Destroy the installer-title section icon.
    if (s_hInstallIcon) {
        DestroyIcon(s_hInstallIcon);
        s_hInstallIcon = NULL;
    }
    s_hInstIconPreview = NULL;

    // Reset scroll offset and remove the vertical scrollbar from the main window.
    s_idlgScrollOffset = 0;
    if (hwnd && IsWindow(hwnd)) {
        LONG ws = GetWindowLongW(hwnd, GWL_STYLE);
        if (ws & WS_VSCROLL) {
            SetWindowLongW(hwnd, GWL_STYLE, ws & ~WS_VSCROLL);
            SetWindowPos(hwnd, NULL, 0, 0, 0, 0,
                SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_FRAMECHANGED);
        }
    }
}

// ── IDLG_BuildPage ────────────────────────────────────────────────────────────

int IDLG_BuildPage(HWND hwnd, HINSTANCE hInst,
                   int pageY, int clientWidth,
                   HFONT hPageTitleFont, HFONT hGuiFont,
                   const std::map<std::wstring,std::wstring>& locale)
{
    s_hInst      = hInst;
    s_hGuiFont   = hGuiFont;
    s_hTitleFont = hPageTitleFont;
    s_pLocale    = &locale;

    // ── Layout constants ──────────────────────────────────────────────────────
    const int padH    = S(20);
    const int padT    = S(20);
    const int gap     = S(10);
    const int iconSz  = S(32);
    const int nameW   = S(200);
    const int rowH    = S(36);   // uniform row height; icon & buttons centred
    const int btnH    = S(30);
    const int titleH  = S(38);

    // Measure button widths once (same for all rows)
    std::wstring editTxt    = L10n(L"idlg_btn_edit",    L"Edit Content\u2026");
    std::wstring previewTxt = L10n(L"idlg_btn_preview", L"Preview\u2026");
    int wEdit    = MeasureButtonWidth(editTxt,    true);
    int wPreview = MeasureButtonWidth(previewTxt, true);

    // Path to shell32.dll for icon loading
    wchar_t sysDir[MAX_PATH];
    GetSystemDirectoryW(sysDir, _countof(sysDir));
    std::wstring shell32Path = std::wstring(sysDir) + L"\\shell32.dll";

    // ── Page title ────────────────────────────────────────────────────────────
    int y = pageY + padT;
    HWND hTitle = CreateWindowExW(0, L"STATIC",
        L10n(L"idlg_page_title", L"Installer Dialogs").c_str(),
        WS_CHILD | WS_VISIBLE | SS_LEFT,
        padH, y, clientWidth - padH * 2, titleH,
        hwnd, (HMENU)(UINT_PTR)IDC_IDLG_PAGE_TITLE, hInst, NULL);
    if (hPageTitleFont) SendMessageW(hTitle, WM_SETFONT, (WPARAM)hPageTitleFont, TRUE);
    y += titleH + gap;

    // ── Installer-title section ───────────────────────────────────────────────
    // Mirrors the Registry page layout: icon preview on the left, Change Icon
    // button next to it, and the title edit field on the right half.
    // The icon and title here will appear in the installer's own title bar.
    {
        const int instIconSz = S(48);
        const int sectH      = S(62);   // tall enough for a 48-px icon

        // Icon preview (SS_ICON, no border — same style as Registry page)
        s_hInstIconPreview = CreateWindowExW(WS_EX_CLIENTEDGE, L"STATIC", L"",
            WS_CHILD | WS_VISIBLE | SS_ICON | SS_CENTERIMAGE,
            padH, y + (sectH - instIconSz) / 2, instIconSz, instIconSz,
            hwnd, (HMENU)(UINT_PTR)IDC_IDLG_INST_ICON_PREVIEW, hInst, NULL);

        // Load the icon: custom if set, otherwise shell32 #2 (default app icon)
        if (s_hInstallIcon) { DestroyIcon(s_hInstallIcon); s_hInstallIcon = NULL; }
        if (!s_installIconPath.empty()) {
            s_hInstallIcon = (HICON)LoadImageW(NULL, s_installIconPath.c_str(),
                IMAGE_ICON, instIconSz, instIconSz, LR_LOADFROMFILE);
        }
        if (!s_hInstallIcon) {
            // Default: shell32 icon index 2 — same default as the Registry page
            PrivateExtractIconsW(shell32Path.c_str(), 2,
                instIconSz, instIconSz, &s_hInstallIcon, NULL, 1, 0);
        }
        if (s_hInstallIcon && s_hInstIconPreview)
            SendMessageW(s_hInstIconPreview, STM_SETICON, (WPARAM)s_hInstallIcon, 0);

        // "Change Icon…" button — to the right of the icon
        std::wstring changeTxt = L10n(L"idlg_change_icon", L"Change Icon\u2026");
        int wChange = MeasureButtonWidth(changeTxt, true);
        int btnX    = padH + instIconSz + S(8);
        HWND hChange = CreateCustomButtonWithIcon(
            hwnd, IDC_IDLG_INST_CHANGE_ICON, changeTxt.c_str(),
            ButtonColor::Blue,
            L"shell32.dll", 127,  // folder/browse icon — same as Registry page
            btnX, y + (sectH - btnH) / 2, wChange, btnH, hInst);
        SetButtonTooltip(hChange,
            L10n(L"idlg_change_icon_tip", L"Select a custom icon for the installer").c_str());

        // "Installer title:" label — placed right after the button with a gap
        int lblX = btnX + wChange + S(12);
        HWND hLbl = CreateWindowExW(0, L"STATIC",
            L10n(L"idlg_inst_title_label", L"Installer title:").c_str(),
            WS_CHILD | WS_VISIBLE | SS_LEFT | SS_CENTERIMAGE,
            lblX, y + (sectH - S(22)) / 2, S(110), S(22),
            hwnd, (HMENU)(UINT_PTR)IDC_IDLG_INST_TITLE_LABEL, hInst, NULL);
        if (hGuiFont) SendMessageW(hLbl, WM_SETFONT, (WPARAM)hGuiFont, TRUE);

        // Title edit field — fills the remainder of the row
        int editX = lblX + S(110) + S(6);
        HWND hTitleEdit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT",
            s_installTitle.c_str(),
            WS_CHILD | WS_VISIBLE | WS_BORDER | ES_LEFT | ES_AUTOHSCROLL,
            editX, y + (sectH - S(24)) / 2, clientWidth - editX - padH, S(24),
            hwnd, (HMENU)(UINT_PTR)IDC_IDLG_INST_TITLE_EDIT, hInst, NULL);
        if (hGuiFont) SendMessageW(hTitleEdit, WM_SETFONT, (WPARAM)hGuiFont, TRUE);

        y += sectH + gap;

        // Horizontal divider between installer-title section and dialog rows
        CreateWindowExW(0, L"STATIC", L"",
            WS_CHILD | WS_VISIBLE | SS_ETCHEDHORZ,
            padH, y, clientWidth - padH * 2, S(2),
            hwnd, NULL, hInst, NULL);
        y += S(2) + gap;
    }

    // ── One row per visible dialog type ──────────────────────────────────────
    for (int i = 0; i < IDLG_COUNT; i++) {
        InstallerDialogType type = (InstallerDialogType)i;
        if (!IsDialogVisible(type)) continue;

        int iconY  = y + (rowH - iconSz) / 2;
        int btnY   = y + (rowH - btnH)   / 2;
        int nameY  = y + (rowH - S(20))  / 2;   // approx text height

        // ── Icon static ───────────────────────────────────────────────────────
        HWND hIcon = CreateWindowExW(0, L"STATIC", L"",
            WS_CHILD | WS_VISIBLE | SS_NOTIFY,
            padH, iconY, iconSz, iconSz,
            hwnd, (HMENU)(UINT_PTR)(IDC_IDLG_ROW_BASE + i * 4 + 0), hInst, NULL);

        // Load the icon at exact size
        HICON hIco = NULL;
        PrivateExtractIconsW(shell32Path.c_str(), kDialogIconIdx[i],
            iconSz, iconSz, &hIco, NULL, 1, 0);
        SetWindowLongPtrW(hIcon, GWLP_USERDATA, (LONG_PTR)hIco);
        s_rowIcons[i]     = hIco;
        s_rowIconCtrls[i] = hIcon;

        // Subclass (store original proc once from the first icon)
        WNDPROC prev = (WNDPROC)SetWindowLongPtrW(
            hIcon, GWLP_WNDPROC, (LONG_PTR)RowIconSubclassProc);
        if (!s_origIconProc) s_origIconProc = prev;

        // ── Name label ────────────────────────────────────────────────────────
        HWND hName = CreateWindowExW(0, L"STATIC",
            L10n(kDialogNameKeys[i], kDialogNameFallbacks[i]).c_str(),
            WS_CHILD | WS_VISIBLE | SS_LEFT | SS_CENTERIMAGE,
            padH + iconSz + gap, nameY, nameW, rowH,
            hwnd, (HMENU)(UINT_PTR)(IDC_IDLG_ROW_BASE + i * 4 + 1), hInst, NULL);
        if (hGuiFont) SendMessageW(hName, WM_SETFONT, (WPARAM)hGuiFont, TRUE);

        // ── "Edit Content…" button ────────────────────────────────────────────
        int editX = padH + iconSz + gap + nameW + gap;
        HWND hEdit = CreateCustomButtonWithIcon(
            hwnd, IDC_IDLG_ROW_BASE + i * 4 + 2,
            editTxt.c_str(),
            ButtonColor::Blue,
            L"shell32.dll", 87,   // magnifier / edit
            editX, btnY, wEdit, btnH, hInst);
        SetButtonTooltip(hEdit, L10n(L"idlg_btn_edit_tip",
            L"Edit the content of this installer dialog").c_str());

        // ── "Preview…" button ─────────────────────────────────────────────────
        int previewX = editX + wEdit + gap;
        HWND hPreview = CreateCustomButtonWithIcon(
            hwnd, IDC_IDLG_ROW_BASE + i * 4 + 3,
            previewTxt.c_str(),
            ButtonColor::Blue,
            L"shell32.dll", 23,   // window / preview
            previewX, btnY, wPreview, btnH, hInst);
        {
            std::wstring tip =
                L10n(L"idlg_btn_preview_tip",  L"Preview this dialog as the end user will see it") + L"\n"
              + L10n(L"idlg_btn_preview_tip2", L"Use the sizer panel on the left to adjust the dialog size \u2014 changes apply live.") + L"\n"
              + L10n(L"idlg_btn_preview_tip3", L"This is a true preview: what you see here is what the end user will see. That also includes the size of the dialog.");
            SetButtonTooltip(hPreview, tip.c_str());
        }

        y += rowH + gap;
    }

    return y;  // absolute Y of first pixel below page content (used for scrollbar sizing)
}

// ── IDLG_OnCommand ────────────────────────────────────────────────────────────

bool IDLG_OnCommand(HWND hwnd, int wmId, int wmEvent, HWND /*hCtrl*/)
{
    // ── Installer-title section handlers ─────────────────────────────────────

    // Title edit field: mirror content to in-memory state as the user types.
    if (wmId == IDC_IDLG_INST_TITLE_EDIT && wmEvent == EN_CHANGE) {
        HWND hEdit = GetDlgItem(hwnd, IDC_IDLG_INST_TITLE_EDIT);
        if (hEdit) {
            wchar_t buf[512] = {};
            GetWindowTextW(hEdit, buf, _countof(buf));
            s_installTitle = buf;
            MainWindow::MarkAsModified();
        }
        return true;
    }

    // Change Icon button: open file picker and update the preview.
    if (wmId == IDC_IDLG_INST_CHANGE_ICON && wmEvent == BN_CLICKED) {
        OPENFILENAMEW ofn = {};
        wchar_t szFile[MAX_PATH] = {};
        ofn.lStructSize  = sizeof(OPENFILENAMEW);
        ofn.hwndOwner    = hwnd;
        ofn.lpstrFile    = szFile;
        ofn.nMaxFile     = MAX_PATH;
        // Only .ico files; OFN_EXPLORER gives the modern folder-tree sidebar
        ofn.lpstrFilter  = L"Icon Files (*.ico)\0*.ico\0";
        ofn.nFilterIndex = 1;
        ofn.Flags = OFN_EXPLORER | OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST | OFN_HIDEREADONLY;

        if (GetOpenFileNameW(&ofn)) {
            s_installIconPath = szFile;
            // Load at the control's display size for best quality
            const int instIconSz = S(48);
            HICON hNew = (HICON)LoadImageW(NULL, s_installIconPath.c_str(),
                IMAGE_ICON, instIconSz, instIconSz, LR_LOADFROMFILE);
            if (hNew) {
                if (s_hInstallIcon) DestroyIcon(s_hInstallIcon);
                s_hInstallIcon = hNew;
                if (s_hInstIconPreview && IsWindow(s_hInstIconPreview))
                    SendMessageW(s_hInstIconPreview, STM_SETICON,
                                 (WPARAM)s_hInstallIcon, 0);
                MainWindow::MarkAsModified();
            }
        }
        return true;
    }

    // ── Dialog-row buttons ────────────────────────────────────────────────────
    if (wmEvent != BN_CLICKED) return false;

    int base = wmId - IDC_IDLG_ROW_BASE;
    if (base < 0 || base >= IDLG_COUNT * 4) return false;

    int typeIdx = base / 4;
    int offset  = base % 4;

    if (typeIdx < 0 || typeIdx >= IDLG_COUNT) return false;

    if (offset == 2) {
        // "Edit Content…" — open the RTF editor for this dialog type
        InstallerDialog& dlg = s_dialogs[typeIdx];
        RtfEditorData ed;
        ed.initRtf    = dlg.content_rtf;
        ed.titleText  = L10n(L"idlg_edit_title",  L"Edit Dialog Content")
                        + L" — "
                        + L10n(kDialogNameKeys[typeIdx], kDialogNameFallbacks[typeIdx]);
        ed.okText     = L10n(L"idlg_edit_save",   L"Save");
        ed.cancelText = L10n(L"idlg_edit_cancel", L"Cancel");
        ed.pLocale    = s_pLocale;
        if (OpenRtfEditor(hwnd, ed)) {
            dlg.content_rtf = ed.outRtf;
            MainWindow::MarkAsModified();
        }
        return true;
    }

    if (offset == 3) {
        // "Preview…" — show the installer dialog facsimile
        ShowPreviewDialog(hwnd, (InstallerDialogType)typeIdx);
        return true;
    }

    return false;
}

// ── IDLG_SaveToDb ─────────────────────────────────────────────────────────────

void IDLG_SaveToDb(int projectId)
{
    DB::DeleteInstallerDialogsForProject(projectId);
    for (int i = 0; i < IDLG_COUNT; i++) {
        DB::UpsertInstallerDialog(projectId, i, s_dialogs[i].content_rtf);
    }

    // Persist installer title and icon path using the project-scoped settings
    // pattern (same scheme as ask_at_install_<id>, last_picker_folder_<id>, etc.).
    std::wstring pid = std::to_wstring(projectId);
    DB::SetSetting(L"installer_title_" + pid, s_installTitle);
    DB::SetSetting(L"installer_icon_"  + pid, s_installIconPath);
    DB::SetSetting(L"installer_preview_w_" + pid, std::to_wstring(s_previewLogW));
    DB::SetSetting(L"installer_preview_h_" + pid, std::to_wstring(s_previewLogH));
    DB::SetSetting(L"installer_preview_user_sized_" + pid, s_previewUserSized[IDLG_COMPONENTS] ? L"1" : L"0");
}

// ── IDLG_LoadFromDb ───────────────────────────────────────────────────────────

void IDLG_LoadFromDb(int projectId)
{
    // Initialise all slots to empty first, then overwrite with DB data.
    for (int i = 0; i < IDLG_COUNT; i++) {
        s_dialogs[i].type        = (InstallerDialogType)i;
        s_dialogs[i].content_rtf = L"";
    }

    auto rows = DB::GetInstallerDialogsForProject(projectId);
    for (auto& row : rows) {
        if (row.first >= 0 && row.first < IDLG_COUNT)
            s_dialogs[row.first].content_rtf = row.second;
    }

    // Restore installer title and icon path saved during the last explicit Save.
    // Only override if a value was actually stored (preserves the default project
    // name on the very first open before a Save has occurred).
    std::wstring pid = std::to_wstring(projectId);
    std::wstring savedTitle, savedIcon;
    if (DB::GetSetting(L"installer_title_" + pid, savedTitle))
        s_installTitle = savedTitle;
    if (DB::GetSetting(L"installer_icon_"  + pid, savedIcon))
        s_installIconPath = savedIcon;
    std::wstring sPrevW, sPrevH, sPrevSized;
    if (DB::GetSetting(L"installer_preview_w_" + pid, sPrevW) && !sPrevW.empty()) s_previewLogW = _wtoi(sPrevW.c_str());
    if (DB::GetSetting(L"installer_preview_h_" + pid, sPrevH) && !sPrevH.empty()) s_previewLogH = _wtoi(sPrevH.c_str());
    if (DB::GetSetting(L"installer_preview_user_sized_" + pid, sPrevSized)) s_previewUserSized[IDLG_COMPONENTS] = (sPrevSized == L"1");
}

// ── IDLG_SetInstallerInfo ─────────────────────────────────────────────────────
// Called from mainwindow when a project is opened or created, so the section
// shows the right defaults before the page is built for the first time.

void IDLG_SetInstallerInfo(const std::wstring& title, const std::wstring& iconPath)
{
    s_installTitle    = title;
    s_installIconPath = iconPath;
}

// ── IDLG_GetInstallerTitle / IDLG_GetInstallerIconPath ────────────────────────

std::wstring IDLG_GetInstallerTitle()    { return s_installTitle; }
std::wstring IDLG_GetInstallerIconPath() { return s_installIconPath; }

// ── Scroll-offset accessors ───────────────────────────────────────────────────

void IDLG_SetScrollOffset(int off) { s_idlgScrollOffset = off; }
int  IDLG_GetScrollOffset()        { return s_idlgScrollOffset; }

