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
#include "my_scrollbar_vscroll.h" // msb_attach / msb_detach / msb_sync
#include "ctrlw.h"        // ShowConfirmDeleteDialog()
#include "tooltip.h"      // ShowMultilingualTooltip(), SetButtonTooltip()
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
static std::wstring s_previewAppName;           // project name used in preview finish dialog
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
// When true for a type, auto-fit is skipped so the chosen size is respected.
// Reset to all-false by IDLG_Reset(); only [IDLG_COMPONENTS] is persisted to DB.
static bool s_previewUserSized[IDLG_COUNT] = {};
static WNDPROC s_origContentProc = NULL; // original RichEdit proc — restored on destroy

// ── Auto-fit caps — easy to tune ─────────────────────────────────────────────
// Content width  may use up to this fraction of the monitor work-area width.
// Content height may use up to this fraction of the monitor work-area height.
// When height is capped a vertical scrollbar is added; its width is reserved so
// the text viewport stays exactly at the capped content width.
static constexpr float kPreviewMaxWidthPct  = 0.95f;  // 95 % of work-area width
static constexpr float kPreviewMaxHeightPct = 0.75f;  // 75 % of work-area height
// Content alignment within the preview when the window is larger than auto-fit.
// 0=Left/Top, 1=Center/Middle, 2=Right/Bottom. Defaults to Center/Middle.
static int s_previewHAlign = 1;
static int s_previewVAlign = 1;

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
    int                 contentNaturalW;    // auto-fit content area width  in logical px (0 = unknown)
    int                 contentNaturalH;    // auto-fit content area height in logical px (0 = unknown)
    // Interior controls for relayout and navigation
    HWND                hTypeTitle;  // dialog-type name STATIC at the top
    HWND                hContent;    // RichEdit (content area)
    HMSB                hContentSB;  // custom scrollbar on hContent (NULL when not needed)
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
    bool                contentHidden;   // true when IDLG_COMPONENTS has no RTF — hide RichEdit
    HWND                hSizer;          // sizer panel (set by ShowPreviewDialog after creation)
    bool                finishSelected;  // true → show feedback dialog after preview closes
    // Snapshot of sizes at open time — used to detect changes on Cancel.
    int                 openLogW;        // s_previewLogW when preview opened
    int                 openLogH;        // s_previewLogH when preview opened
    bool                openUserSized;   // s_previewUserSized[type] when preview opened
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
        // Split layout: RTF content on top, extras panel (checkboxes/radios) below.
        // Exception: if contentHidden is set (e.g. IDLG_COMPONENTS with no RTF),
        // the RichEdit is hidden and the extras panel gets the full available height.
        const int extLblH = S(22);
        const int extGap  = S(6);

        // Content height:
        //  • Developer has manually resized → give the RichEdit all height above
        //    the extras panel; extras stay at natural size, no dead space below.
        //  • Auto-fit → use the measured height (contentFitH) so the RichEdit
        //    is exactly tall enough for its content.
        bool userSized = s_previewUserSized[(int)pd->type];

        // Natural height of the extras panel so the viewport gets all surplus
        // space when the developer increases the window height.
        const int chkH = S(24), rH = S(24);
        int extrasNaturalH;
        if (!pd->hCompChecks.empty()) {
            int n = (int)pd->hCompChecks.size();
            extrasNaturalH = extLblH + extGap + n * chkH + std::max(0, n - 1) * S(4) + S(8);
        } else if (pd->hRadioMe && IsWindowVisible(pd->hRadioMe)) {
            extrasNaturalH = extLblH + extGap + rH + S(6) + rH + S(8);
        } else {
            extrasNaturalH = extLblH + extGap + chkH + S(8);
        }

        int contentH;
        if (pd->contentHidden) {
            contentH = 0;
        } else if (userSized) {
            // Grow the viewport; extras stay at natural height → no dead space.
            contentH = avail - gap - extrasNaturalH;
            if (contentH < S(60)) contentH = S(60);
        } else if (pd->contentFitH > 0) {
            contentH = std::min(S(pd->contentFitH), avail);
        } else {
            contentH = avail / 2;
        }
        int contentGap = pd->contentHidden ? 0 : gap;
        int extrasY    = editY + contentH + contentGap;
        int extrasH    = avail - contentH - contentGap - extLblH - extGap;
        if (extrasH < 0) extrasH = 0;

        if (pd->hContent && IsWindow(pd->hContent)) {
            if (pd->contentHidden) {
                ShowWindow(pd->hContent, SW_HIDE);
            } else {
                ShowWindow(pd->hContent, SW_SHOW);
                // H-Align: narrow the RichEdit to its natural content width and
                // distribute the surplus space according to the chosen alignment.
                // When userSized (developer widened the window), the natural width
                // still applies so they can see where the table actually sits.
                int naturalReW = (pd->contentNaturalW > 0)
                    ? S(pd->contentNaturalW) - 2 * pad
                    : cW - 2 * pad;
                int reW = std::min(naturalReW, cW - 2 * pad);
                if (reW <= 0) reW = cW - 2 * pad;
                int hspace = cW - 2 * pad - reW;
                int reX;
                switch (s_previewHAlign) {
                    default:
                    case 1: reX = pad + hspace / 2; break;  // Center
                    case 0: reX = pad; break;                // Left
                    case 2: reX = pad + hspace; break;       // Right
                }
                // Split layout: always start at editY, always fill contentH.
                // V-align is not applied here — in split layout the viewport
                // should be flush to the top so images are never pushed down.
                SetWindowPos(pd->hContent, NULL, reX, editY,
                             reW, contentH, SWP_NOZORDER | SWP_NOACTIVATE);
                // Sync scroll range immediately after resize.
                UpdateWindow(pd->hContent);
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
            SetWindowPos(pd->hRadioMe,  NULL, pad + S(4), ctrlY,
                         cW - pad * 2 - S(4), rH, SWP_NOZORDER | SWP_NOACTIVATE);
            SetWindowPos(pd->hRadioAll, NULL, pad + S(4), ctrlY + rH + S(6),
                         cW - pad * 2 - S(4), rH, SWP_NOZORDER | SWP_NOACTIVATE);
        }

        // Position component checkboxes when visible.
        {
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
        // Single layout: RichEdit fills available space.
        // When the window is larger than the auto-fit content size AND the
        // developer has NOT manually resized, apply H/V alignment to position
        // the RichEdit block within the extra space (gives padding "air room").
        // When the developer HAS resized (userSized), fill all available space
        // so increasing the height grows the viewport, not dead space below.
        if (pd->hContent && IsWindow(pd->hContent)) {
            bool userSized = s_previewUserSized[(int)pd->type];
            int reX, reY, reW, reH;
            if (userSized || pd->contentNaturalW <= 0) {
                // Developer-resized or no measurement → fill all available space.
                reX = pad; reY = editY;
                reW = cW - 2 * pad; reH = avail;
            } else {
                // Auto-fit size is known → apply alignment within the extra space.
                // Horizontal
                int naturalReW = S(pd->contentNaturalW) - 2 * pad;
                reW = std::min(naturalReW, cW - 2 * pad);
                if (reW <= 0) reW = cW - 2 * pad;
                int hspace = cW - 2 * pad - reW;
                switch (s_previewHAlign) {
                    default:
                    case 1: reX = pad + hspace / 2; break;  // Center
                    case 0: reX = pad; break;                // Left
                    case 2: reX = pad + hspace; break;       // Right
                }
                // Vertical
                int naturalReH = (pd->contentNaturalH > 0) ? S(pd->contentNaturalH) : avail;
                reH = std::min(naturalReH, avail);
                if (reH <= 0) reH = avail;
                int vspace = avail - reH;
                switch (s_previewVAlign) {
                    default:
                    case 1: reY = editY + vspace / 2; break;  // Middle
                    case 0: reY = editY; break;                // Top
                    case 2: reY = editY + vspace; break;       // Bottom
                }
            }
            SetWindowPos(pd->hContent, NULL, reX, reY, reW, reH,
                         SWP_NOZORDER | SWP_NOACTIVATE);
        }
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
    // Sync the custom scrollbar to reflect whether the current content
    // overflows the RichEdit's visible area (handles both auto-fit and
    // developer-manually-sized cases in one place).
    if (pd->hContent && IsWindow(pd->hContent)) {
        // Ensure WS_VSCROLL is always set so the RichEdit maintains SCROLLINFO.
        LONG cst = GetWindowLongW(pd->hContent, GWL_STYLE);
        if (!(cst & WS_VSCROLL)) {
            SetWindowLongW(pd->hContent, GWL_STYLE, cst | WS_VSCROLL);
            SetWindowPos(pd->hContent, NULL, 0, 0, 0, 0,
                SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_FRAMECHANGED | SWP_NOACTIVATE);
        }
        if (pd->hContentSB) {
            // Bar already attached: let Msb_ContentOverflows decide visibility.
            // GetScrollInfo is zeroed after EM_SHOWSCROLLBAR(FALSE) and must not
            // be used to decide whether to keep or detach the custom scrollbar.
            msb_sync(pd->hContentSB);
        } else {
            UpdateWindow(pd->hContent);  // flush layout so GetScrollInfo is current
            SCROLLINFO si = {}; si.cbSize = sizeof(si); si.fMask = SIF_ALL;
            GetScrollInfo(pd->hContent, SB_VERT, &si);
            bool overflow = si.nMax > 0 && si.nPage > 0 && (UINT)si.nPage <= (UINT)si.nMax;
            if (overflow)
                pd->hContentSB = msb_attach(pd->hContent, MSB_VERTICAL);
        }
    }

    // Force the parent to repaint vacated areas when controls shift position
    // (e.g. the extras label disappearing on a 1-px resize).
    InvalidateRect(hwnd, NULL, FALSE);
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

// Subclass proc for the content RichEdit in the preview window.
//
// RICHEDIT50W does not update its own scrollbar thumb after scrolling when the
// control is read-only or lacks focus.  This subclass ensures the thumb always
// tracks the content for BOTH writable and read-only RichEdits.
//
// Approach — pre-scroll delta arithmetic:
//   Read GetScrollInfo(SIF_ALL) BEFORE CallWindowProcW.  The pre-scroll nPos is
//   always current and in the scrollbar's own line-unit coordinate space.
//   Compute newPos from the scroll code, let the RichEdit scroll natively, then
//   SetScrollPos(newPos) to force the visual thumb update.  This avoids any
//   post-scroll read (which would return stale data from a read-only RichEdit).
static LRESULT CALLBACK ContentRichEditSubclassProc(
    HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    if (msg == WM_VSCROLL) {
        if (LOWORD(wParam) == SB_ENDSCROLL)
            return CallWindowProcW(s_origContentProc, hwnd, msg, wParam, lParam);

        /* Read scrollbar state BEFORE the scroll — always fresh. */
        SCROLLINFO si = {};
        si.cbSize = sizeof(si);
        si.fMask  = SIF_ALL;
        GetScrollInfo(hwnd, SB_VERT, &si);
        int maxPos = si.nMax - (int)si.nPage + 1;
        if (maxPos < 0) maxPos = 0;

        /* Compute desired new position in line-unit space. */
        int newPos = si.nPos;
        switch (LOWORD(wParam)) {
            case SB_LINEUP:        newPos--;                      break;
            case SB_LINEDOWN:      newPos++;                      break;
            case SB_PAGEUP:        newPos -= (int)si.nPage;       break;
            case SB_PAGEDOWN:      newPos += (int)si.nPage;       break;
            case SB_TOP:           newPos  = si.nMin;             break;
            case SB_BOTTOM:        newPos  = maxPos;              break;
            case SB_THUMBTRACK:
            case SB_THUMBPOSITION: newPos  = (int)si.nTrackPos;   break;
            default:               break;
        }
        if (newPos < si.nMin) newPos = si.nMin;
        if (newPos > maxPos)  newPos = maxPos;

        /* Let the RichEdit scroll natively, then force the thumb. */
        LRESULT r = CallWindowProcW(s_origContentProc, hwnd, msg, wParam, lParam);
        SetScrollPos(hwnd, SB_VERT, newPos, TRUE);
        return r;
    }
    return CallWindowProcW(s_origContentProc, hwnd, msg, wParam, lParam);
}

// Returns the natural content height of hRE in LOGICAL pixels (96 dpi baseline).
// Uses EM_FORMATRANGE to measure without rendering; the control need not be visible.
// After formatting the entire content into a very tall rect, fr.rc.top holds the
// y-extent of the rendered content in twips; divide by 15 to get logical pixels.
static int MeasureRichEditLogHeight(HWND hRE)
{
    if (!hRE || !IsWindow(hRE)) return 0;

    // Read the vertical scroll range.  When the window is 1px tall ALL content
    // overflows and nMax+1 = total content height in physical pixels.
    // Requires a paint pass (WS_VISIBLE + UpdateWindow).  Callers that skip
    // the paint pass will get nMax=0 here and fall through to EM_FORMATRANGE.
    {
        SCROLLINFO si = {};
        si.cbSize = sizeof(si);
        si.fMask  = SIF_RANGE | SIF_PAGE;
        GetScrollInfo(hRE, SB_VERT, &si);
        if (si.nMax > 0) {
            // nMax + 1 = total content height in physical pixels.
            return (int)((si.nMax + 1) / g_dpiScale + 0.5f);
        }
    }

    // Fallback: EM_FORMATRANGE measure-only.  Accurate for pure text; may
    // undercount images, but better than nothing if the scroll range is
    // unavailable (e.g. window not yet painted).
    RECT rc; GetClientRect(hRE, &rc);
    int clientWPx = rc.right - rc.left;
    if (clientWPx <= 0) clientWPx = S(s_previewLogW) - 2 * S(16);

    HDC hdc = GetDC(hRE);
    if (!hdc) return 0;
    int dpiX = GetDeviceCaps(hdc, LOGPIXELSX);
    if (!dpiX) dpiX = 96;
    LONG widthTwips = MulDiv(clientWPx, 1440, dpiX);

    FORMATRANGE fr   = {};
    fr.hdc           = hdc;
    fr.hdcTarget     = hdc;
    fr.chrg.cpMin    = 0;
    fr.chrg.cpMax    = -1;
    fr.rcPage        = { 0, 0, widthTwips, 0x0FFFFFFF };
    fr.rc            = fr.rcPage;

    SendMessageW(hRE, EM_FORMATRANGE, FALSE, (LPARAM)&fr);
    SendMessageW(hRE, EM_FORMATRANGE, FALSE, 0);
    ReleaseDC(hRE, hdc);

    return MulDiv((int)fr.rc.top, 96, 1440);
}

// Forward declarations (defined near ShowPreviewDialog, used here).
static void TryCancelPreview(HWND hwnd, PreviewData* pd);
static int ScanRtfNaturalWidthTwips(const std::wstring& rtf);
static void ShowFinishFeedback(HWND hOwner);

// ── AutoFitPreview — resize the preview window to fit the current dialog's RTF
// ─────────────────────────────────────────────────────────────────────────────
// Must be called AFTER the preview window is visible so the 1 px-tall hidden
// measurement RichEdit (WS_CHILD of hPreview) gets a genuine paint pass and
// populates its vertical scroll range.  Using a visible top-level parent is the
// key requirement; children of GetDesktopWindow() are never properly painted.
//
// Single layout (showExtras=false):
//   Width  — largest \picwgoal or \cellx twip value, converted to logical px.
//             Fallback: s_previewLogW (saved/previous width).
//   Height — 1 px measurement RichEdit → GetScrollInfo nMax+1 (physical px).
//   logH   = measuredRtfH + kPreviewChromeLogH (114 logical px).
//
// Split layout (showExtras=true, contentHidden=false):
//   Width  — same twip scan.
//   Height — 1 px measurement; logH = 160 + rtfLogH + n×28 (n = checkbox count).
//   pd->contentFitH = rtfLogH so LayoutPreviewControls sizes the RTF area correctly.
//
// Split layout no-RTF (contentHidden=true — extras fill the full interior):
//   Width  — unchanged (no RTF → no natural-width hint).
//   Height — 144 + n×28.
//
// Both axes capped: width ≤ kPreviewMaxWidthPct, height ≤ kPreviewMaxHeightPct.
// When the height cap is hit in single layout, WS_VSCROLL is added to
// pd->hContent and the preview is widened by SM_CXVSCROLL so the text viewport
// width remains constant.
//
// Updates: s_previewLogW, s_previewLogH, pd->contentNaturalW/H, pd->contentFitH,
// pd->contentNeedsScroll, and the sizer W/H spinners.
// Uses SWP_NOMOVE — caller handles repositioning / centring.
// Skips silently when s_previewUserSized[(int)pd->type] is true.
static void AutoFitPreview(HWND hPreview, PreviewData* pd)
{
    if (!hPreview || !IsWindow(hPreview) || !pd) return;
    if (s_previewUserSized[(int)pd->type]) return;

    // ── Work-area caps ────────────────────────────────────────────────────────
    int maxLogW, maxLogH;
    {
        HMONITOR hMon = MonitorFromWindow(hPreview, MONITOR_DEFAULTTONEAREST);
        MONITORINFO mi = {}; mi.cbSize = sizeof(mi);
        if (hMon && GetMonitorInfoW(hMon, &mi)) {
            maxLogW = (int)((mi.rcWork.right  - mi.rcWork.left) * kPreviewMaxWidthPct  / g_dpiScale + 0.5f);
            maxLogH = (int)((mi.rcWork.bottom - mi.rcWork.top)  * kPreviewMaxHeightPct / g_dpiScale + 0.5f);
        } else {
            maxLogW = (int)(GetSystemMetrics(SM_CXSCREEN) * kPreviewMaxWidthPct  / g_dpiScale + 0.5f);
            maxLogH = (int)(GetSystemMetrics(SM_CYSCREEN) * kPreviewMaxHeightPct / g_dpiScale + 0.5f);
        }
    }

    const std::wstring& rtf = s_dialogs[(int)pd->type].content_rtf;

    // ── Width: parse RTF for fixed-width markers (twips → logical px) ─────────
    // logW = preview client width (logical px).  Fallback = current s_previewLogW.
    int logW = s_previewLogW;
    if (!rtf.empty()) {
        int twips = ScanRtfNaturalWidthTwips(rtf);
        if (twips > 0) {
            int contentLogW = twips / 15;
            logW = contentLogW + 32 + 1;  // +32 = 2×pad; +1 sub-pixel tolerance
        }
    }
    // Minimum: 400 logical px so text-only and narrow-image dialogs always have
    // a comfortable word-wrap width.  Structural dialogs (wide table, wide image)
    // override this naturally via the twip scan above.
    logW = std::max(logW, 400);
    logW = std::min(logW, maxLogW);

    // ── Measurement RichEdit class ─────────────────────────────────────────────
    // 1 px tall, WS_VISIBLE, WS_CHILD of hPreview (visible top-level) → content
    // always overflows → GetScrollInfo nMax+1 = true total height incl. images.
    // No WS_EX_CLIENTEDGE: its 2px border makes the client area negative on a
    // 1px-tall window → RichEdit never lays out → GetScrollInfo returns 0.
    static HMODULE s_hReMx = NULL;
    if (!s_hReMx) {
        s_hReMx = LoadLibraryW(L"Msftedit.dll");
        if (!s_hReMx) s_hReMx = LoadLibraryW(L"Riched20.dll");
    }
    WNDCLASSEXW wcex = {}; wcex.cbSize = sizeof(wcex);
    const wchar_t* reClassX =
        (s_hReMx && GetClassInfoExW(s_hReMx, L"RICHEDIT50W", &wcex))
        ? L"RICHEDIT50W" : L"RichEdit20W";
    // Width = live RichEdit interior = (cW - 2×pad).
    int measPhysW = S(logW) - 2 * S(16);
    if (measPhysW < S(100)) measPhysW = S(100);

    // ── Height / logH ──────────────────────────────────────────────────────────
    int  logH        = s_previewLogH;  // fallback to current
    bool needsVScroll = false;

    if (pd->showExtras) {
        // ── Split layout (Components page etc.) ───────────────────────────────
        int n = std::max((int)pd->hCompChecks.size(), 1);
        if (pd->contentHidden) {
            // No RTF — extras fill the full interior.
            // editY(60) + extLblH(22) + extGap(6) + n×28 + breathing(10)
            //   + gap(8) + btnH(30) + pad(16)  =  144 + n×28
            logH = 144 + n * 28;
        } else {
            int rtfLogH = 60;  // safe minimum
            HWND hM = CreateWindowExW(0, reClassX, L"",
                WS_CHILD | WS_VISIBLE | ES_MULTILINE | ES_AUTOVSCROLL | WS_VSCROLL,
                -(measPhysW + 100), 0, measPhysW, 1,
                hPreview, NULL, s_hInst, NULL);
            int splitLineH = 18;  // one-line bottom padding (default ~9pt line)
            if (hM) {
                if (pd->hGuiFont) SendMessageW(hM, WM_SETFONT, (WPARAM)pd->hGuiFont, FALSE);
                SendMessageW(hM, EM_SETBKGNDCOLOR, 0, RGB(255, 255, 255));
                StreamRtfIn(hM, rtf);
                RedrawWindow(hM, NULL, NULL, RDW_ERASE | RDW_INVALIDATE | RDW_UPDATENOW);
                int measured = MeasureRichEditLogHeight(hM);
                if (measured > 60) {
                    rtfLogH = measured;
                    int nLines = (int)SendMessageW(hM, EM_GETLINECOUNT, 0, 0);
                    if (nLines > 0) splitLineH = std::min(measured / nLines, 24);
                }
                DestroyWindow(hM);
            }
            rtfLogH += splitLineH;  // one blank line of breathing room at the bottom
            if (rtfLogH < 60) rtfLogH = 60;
            // Extras content height (the area below extLbl+extGap, above the
            // breathing+gap+btnH+pad block).
            // • Checkboxes: n×28 design-px (chkH 24 + S(4) spacing per item).
            // • Radio buttons (FOR_ME_ALL): rH(24) + S(6) + rH(24) + breathing(8) = 62.
            //   Using n=1 → n×28 = 28 is 34 px short, jamming the second radio
            //   button against the navigation buttons.
            int extrasContentH;
            if (!pd->hCompChecks.empty()) {
                extrasContentH = n * 28;
            } else if (pd->hRadioMe && IsWindow(pd->hRadioMe) && IsWindowVisible(pd->hRadioMe)) {
                extrasContentH = 62;   // rH(24) + S(6)(6) + rH(24) + breathing(8)
            } else {
                extrasContentH = n * 28;
            }
            // editY(60) + rtfLogH + gap(8) + extLbl(22) + extGap(6)
            //   + extrasContentH + gap(8) + btnH(30) + pad(16)  =  160 + rtfLogH + extrasContentH
            logH = 160 + rtfLogH + extrasContentH;
            pd->contentFitH = rtfLogH;
            if (logH > maxLogH) {
                logH = maxLogH;
                int cappedRtfLogH = maxLogH - 160 - extrasContentH;
                if (cappedRtfLogH < 60) cappedRtfLogH = 60;
                pd->contentFitH = cappedRtfLogH;
            }
        }
        pd->contentNeedsScroll = false;  // split: items always fit below RTF
        pd->contentNaturalW    = 0;      // alignment only applies to single layout
        pd->contentNaturalH    = 0;
    } else {
        // ── Single layout ─────────────────────────────────────────────────────
        // Interior chrome: pad(16) + titleH(36) + 2×gap(8) + btnH(30) + pad(16) = 114
        constexpr int kChromeLogH = 114;
        int rtfLogH = 100;  // safe minimum
        int singleLineH = 18;  // one-line bottom padding (default ~9pt line)
        if (!rtf.empty()) {
            HWND hM = CreateWindowExW(0, reClassX, L"",
                WS_CHILD | WS_VISIBLE | ES_MULTILINE | ES_AUTOVSCROLL | WS_VSCROLL,
                -(measPhysW + 100), 0, measPhysW, 1,
                hPreview, NULL, s_hInst, NULL);
            if (hM) {
                if (pd->hGuiFont) SendMessageW(hM, WM_SETFONT, (WPARAM)pd->hGuiFont, FALSE);
                SendMessageW(hM, EM_SETBKGNDCOLOR, 0, RGB(255, 255, 255));
                StreamRtfIn(hM, rtf);
                RedrawWindow(hM, NULL, NULL, RDW_ERASE | RDW_INVALIDATE | RDW_UPDATENOW);
                int measured = MeasureRichEditLogHeight(hM);
                if (measured > 0) {
                    rtfLogH = measured;
                    int nLines = (int)SendMessageW(hM, EM_GETLINECOUNT, 0, 0);
                    if (nLines > 0) singleLineH = std::min(measured / nLines, 24);
                }
                DestroyWindow(hM);
            }
        }
        rtfLogH      = std::max(rtfLogH, 100);
        needsVScroll = (rtfLogH + singleLineH > maxLogH - kChromeLogH);
        if (needsVScroll) {
            rtfLogH = maxLogH - kChromeLogH;
        } else {
            // Add one blank line of breathing room at the bottom so the last
            // line of text is not flush against the navigation buttons.
            rtfLogH += singleLineH;
        }
        logH = rtfLogH + kChromeLogH;
        pd->contentNaturalW    = logW;      // used by H-align in LayoutPreviewControls
        pd->contentNaturalH    = rtfLogH;   // outer reH must fit content
    }

    logH = std::max(150, logH);
    s_previewLogH = logH;
    s_previewLogW = logW;

    // ── Resize the preview window (SWP_NOMOVE — caller handles repositioning) ─
    // SyncContentScrollbar (called from LayoutPreviewControls on WM_SIZE) will
    // attach / detach the custom scrollbar after hContent is laid out.
    RECT adj = { 0, 0, S(logW), S(logH) };
    AdjustWindowRectEx(&adj, kPreviewStyle, FALSE, kPreviewExStyle);
    SetWindowPos(hPreview, NULL, 0, 0,
                 adj.right - adj.left, adj.bottom - adj.top,
                 SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);

    // ── Update both sizer spinners ────────────────────────────────────────────
    if (pd->hSizer && IsWindow(pd->hSizer)) {
        SizerData* sd = (SizerData*)GetWindowLongPtrW(pd->hSizer, GWLP_USERDATA);
        if (sd) {
            HWND hWS = GetDlgItem(pd->hSizer, IDC_IDLG_SZR_W_SPIN);
            HWND hHS = GetDlgItem(pd->hSizer, IDC_IDLG_SZR_H_SPIN);
            sd->ignoring = true;
            if (hWS) SendMessageW(hWS, UDM_SETPOS32, 0, (LPARAM)s_previewLogW);
            if (hHS) SendMessageW(hHS, UDM_SETPOS32, 0, (LPARAM)s_previewLogH);
            sd->ignoring = false;
        }
    }
}

// ── Navigation — update content and UI for a new dialog type ─────────────────

static void NavigateTo(HWND hwnd, PreviewData* pd, InstallerDialogType newType)
{
    pd->type        = newType;
    pd->contentFitH = 0;  // reset; AutoFitPreview will re-measure if needed
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
            // Invalidate cached document height so the new content is measured fresh.
            if (pd->hContentSB) msb_notify_content_changed(pd->hContentSB);
            StreamRtfIn(pd->hContent, rtf);
        } else {
            std::wstring ph = L10n(L"idlg_preview_no_content", L"(No content defined for this dialog yet)");
            SetWindowTextW(pd->hContent, ph.c_str());
        }
        SendMessageW(pd->hContent, EM_SETREADONLY, TRUE, 0);
        SendMessageW(pd->hContent, EM_SETSEL, 0, 0);
        SendMessageW(pd->hContent, EM_SCROLLCARET, 0, 0);
    }

    // Reposition buttons (text and enable state may have changed)
    LayoutPreviewControls(hwnd, pd);

    // Auto-fit both dimensions for all dialog types so the window always feels
    // right out of the box.  AutoFitPreview skips internally if the developer
    // has already manually resized via the sizer.
    UpdateWindow(hwnd);
    AutoFitPreview(hwnd, pd);

    // Re-centre on monitor after AutoFitPreview may have changed the window size.
    {
        RECT rcP; GetWindowRect(hwnd, &rcP);
        int newW = rcP.right - rcP.left;
        int newH = rcP.bottom - rcP.top;
        int nx = rcP.left, ny = rcP.top;
        HMONITOR hMN = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
        MONITORINFO miN = {}; miN.cbSize = sizeof(miN);
        if (hMN && GetMonitorInfoW(hMN, &miN)) {
            RECT& wa = miN.rcWork;
            nx = wa.left + (wa.right  - wa.left - newW) / 2;
            ny = wa.top  + (wa.bottom - wa.top  - newH) / 2;
            if (nx + newW > wa.right)  nx = wa.right  - newW;
            if (ny + newH > wa.bottom) ny = wa.bottom - newH;
            if (nx < wa.left)  nx = wa.left;
            if (ny < wa.top)   ny = wa.top;
        }
        SetWindowPos(hwnd, NULL, nx, ny, 0, 0,
                     SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
        if (pd->hSizer && IsWindow(pd->hSizer)) {
            RECT rcSz; GetWindowRect(pd->hSizer, &rcSz);
            int szW = rcSz.right - rcSz.left;
            SetWindowPos(pd->hSizer, NULL, nx - S(8) - szW, ny, 0, 0,
                         SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
        }
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
        // Created with WS_VSCROLL so the RichEdit always maintains SCROLLINFO
        // internally; the custom scrollbar (my_scrollbar) hides the native gutter
        // and replaces it when SyncContentScrollbar (called from LayoutPreviewControls)
        // detects overflow after the window is sized.
        DWORD reStyle = WS_CHILD | WS_VISIBLE | ES_MULTILINE | ES_AUTOVSCROLL | WS_VSCROLL;
        pd->hContent = CreateWindowExW(0, reClass, L"",
            reStyle, 0, 0, 10, 10,
            hwnd, (HMENU)(UINT_PTR)IDC_IDLG_PRV_CONTENT, cs->hInstance, NULL);
        if (pd->hGuiFont) SendMessageW(pd->hContent, WM_SETFONT, (WPARAM)pd->hGuiFont, TRUE);
        SendMessageW(pd->hContent, EM_SETBKGNDCOLOR, 0, RGB(255, 255, 255));
        // Subclass the RichEdit to intercept SB_THUMBTRACK/SB_THUMBPOSITION and
        // drive EM_SETSCROLLPOS with the 32-bit nTrackPos from GetScrollInfo.
        s_origContentProc = (WNDPROC)(LONG_PTR)
            SetWindowLongPtrW(pd->hContent, GWLP_WNDPROC, (LONG_PTR)ContentRichEditSubclassProc);

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
                // At Finish — flag and close.  ShowFinishFeedback is called by
                // ShowPreviewDialog at the top level after the preview is torn
                // down, so there is no nested message loop.
                pd->finishSelected = true;
                pd->running = false;
            } else {
                NavigateTo(hwnd, pd, next);
            }
            return 0;
        }
        if (id == IDC_IDLG_PRV_CANCEL) {
            // If sizes changed, ask before losing them; otherwise cancel normally.
            TryCancelPreview(hwnd, pd);
            return 0;
        }
        return 0;
    }

    case WM_MOUSEWHEEL: {
        // WM_MOUSEWHEEL goes to the focused window (often a button), so we may
        // receive it directly here or via DefWindowProc bubbling from a child.
        // Pre-scroll delta: read nPos BEFORE EM_SCROLL calls so we never rely
        // on the RichEdit updating nPos synchronously afterwards.
        if (!pd || !pd->hContent || !IsWindow(pd->hContent)) return 0;
        SCROLLINFO si = {};
        si.cbSize = sizeof(si);
        si.fMask  = SIF_ALL;
        GetScrollInfo(pd->hContent, SB_VERT, &si);
        int maxPos = si.nMax - (int)si.nPage + 1;
        if (maxPos < 0) maxPos = 0;
        int delta = GET_WHEEL_DELTA_WPARAM(wParam);
        UINT lines = 3;
        SystemParametersInfoW(SPI_GETWHEELSCROLLLINES, 0, &lines, 0);
        if (lines == WHEEL_PAGESCROLL) lines = 3;
        int count = (abs(delta) * (int)lines + WHEEL_DELTA / 2) / WHEEL_DELTA;
        int newPos = si.nPos + (delta > 0 ? -count : count);
        if (newPos < si.nMin) newPos = si.nMin;
        if (newPos > maxPos)  newPos = maxPos;
        WPARAM cmd = (delta > 0) ? SB_LINEUP : SB_LINEDOWN;
        for (int i = 0; i < count; i++)
            SendMessageW(pd->hContent, EM_SCROLL, cmd, 0);
        SetScrollPos(pd->hContent, SB_VERT, newPos, TRUE);
        return 0;
    }

    case WM_CLOSE:
        // No WS_SYSMENU means no × button, but WM_CLOSE can still arrive via
        // Alt+F4 or the taskbar — treat it the same as Cancel.
        TryCancelPreview(hwnd, pd);
        return 0;

    case WM_KEYDOWN:
        // IsDialogMessageW is NOT used for the preview window (it swallows the
        // first mouse-click on a non-focused button to set keyboard focus, making
        // the Finish button unreliable — requires 2+ clicks to register).
        // Handle Escape manually so it still cancels the preview.
        if (wParam == VK_ESCAPE) {
            TryCancelPreview(hwnd, pd);
        }
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
        // Detach the custom scrollbar before the RichEdit is destroyed.
        if (pd && pd->hContentSB) {
            msb_detach(pd->hContentSB);
            pd->hContentSB = NULL;
        }
        // Restore the original RichEdit window proc before the control is destroyed.
        if (pd && pd->hContent && IsWindow(pd->hContent) && s_origContentProc)
            SetWindowLongPtrW(pd->hContent, GWLP_WNDPROC, (LONG_PTR)s_origContentProc);
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

        // Custom tooltips on the Width / Height edit fields.
        SetButtonTooltip(hWE, L10n(L"idlg_sizer_w_tip",
            L"Installer dialog width in logical pixels (96 dpi / 100% scaling).\n"
            L"The preview scales automatically to match your current display DPI.").c_str());
        SetButtonTooltip(hHE, L10n(L"idlg_sizer_h_tip",
            L"Installer dialog height in logical pixels (96 dpi / 100% scaling).\n"
            L"The preview scales automatically to match your current display DPI.").c_str());

        y += rowH + gap;

        // Row 3: Horizontal alignment
        HWND hHAL = CreateWindowExW(0, L"STATIC",
            L10n(L"idlg_sizer_ha_label", L"H-Align:").c_str(),
            WS_CHILD | WS_VISIBLE | SS_LEFT | SS_CENTERIMAGE,
            pad, y, lblW, rowH, hwnd, NULL, hI, NULL);
        if (hF) SendMessageW(hHAL, WM_SETFONT, (WPARAM)hF, TRUE);

        HWND hHAC = CreateWindowExW(0, WC_COMBOBOXW, L"",
            WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | WS_VSCROLL,
            pad + lblW + gap, y, edW + spW, rowH * 5,
            hwnd, (HMENU)(UINT_PTR)IDC_IDLG_SZR_H_ALIGN, hI, NULL);
        if (hF) SendMessageW(hHAC, WM_SETFONT, (WPARAM)hF, TRUE);
        SendMessageW(hHAC, CB_ADDSTRING, 0, (LPARAM)L10n(L"idlg_align_left",   L"Left").c_str());
        SendMessageW(hHAC, CB_ADDSTRING, 0, (LPARAM)L10n(L"idlg_align_center", L"Center").c_str());
        SendMessageW(hHAC, CB_ADDSTRING, 0, (LPARAM)L10n(L"idlg_align_right",  L"Right").c_str());
        SendMessageW(hHAC, CB_SETCURSEL, (WPARAM)s_previewHAlign, 0);

        y += rowH + gap;

        // Row 4: Vertical alignment
        HWND hVAL = CreateWindowExW(0, L"STATIC",
            L10n(L"idlg_sizer_va_label", L"V-Align:").c_str(),
            WS_CHILD | WS_VISIBLE | SS_LEFT | SS_CENTERIMAGE,
            pad, y, lblW, rowH, hwnd, NULL, hI, NULL);
        if (hF) SendMessageW(hVAL, WM_SETFONT, (WPARAM)hF, TRUE);

        HWND hVAC = CreateWindowExW(0, WC_COMBOBOXW, L"",
            WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | WS_VSCROLL,
            pad + lblW + gap, y, edW + spW, rowH * 5,
            hwnd, (HMENU)(UINT_PTR)IDC_IDLG_SZR_V_ALIGN, hI, NULL);
        if (hF) SendMessageW(hVAC, WM_SETFONT, (WPARAM)hF, TRUE);
        SendMessageW(hVAC, CB_ADDSTRING, 0, (LPARAM)L10n(L"idlg_align_top",    L"Top").c_str());
        SendMessageW(hVAC, CB_ADDSTRING, 0, (LPARAM)L10n(L"idlg_align_middle", L"Middle").c_str());
        SendMessageW(hVAC, CB_ADDSTRING, 0, (LPARAM)L10n(L"idlg_align_bottom", L"Bottom").c_str());
        SendMessageW(hVAC, CB_SETCURSEL, (WPARAM)s_previewVAlign, 0);

        y += rowH + gap;

        // Row 5: Reset button — clears the manual-sizing flag and auto-fits.
        int btnW = lblW + gap + edW + spW;
        HWND hRB = CreateWindowExW(0, WC_BUTTONW,
            L10n(L"idlg_sizer_reset_btn", L"Reset size").c_str(),
            WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            pad, y, btnW, rowH,
            hwnd, (HMENU)(UINT_PTR)IDC_IDLG_SZR_RESET, hI, NULL);
        if (hF) SendMessageW(hRB, WM_SETFONT, (WPARAM)hF, TRUE);

        y += rowH + gap;

        // Row 6: Close button — saves size and closes the preview.
        HWND hCB = CreateWindowExW(0, WC_BUTTONW,
            L10n(L"idlg_sizer_close_btn", L"Close").c_str(),
            WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            pad, y, btnW, rowH,
            hwnd, (HMENU)(UINT_PTR)IDC_IDLG_SZR_CLOSE, hI, NULL);
        if (hF) SendMessageW(hCB, WM_SETFONT, (WPARAM)hF, TRUE);

        // Custom tooltips on the Reset and Close buttons.
        SetButtonTooltip(hRB, L10n(L"idlg_sizer_reset_tip", L"Reset size to default").c_str());
        SetButtonTooltip(hCB, L10n(L"idlg_sizer_close_tip", L"Close preview with current size").c_str());

        return 0;
    }

    case WM_COMMAND: {
        if (!sd || sd->ignoring) return 0;
        int id    = LOWORD(wParam);
        int event = HIWORD(wParam);
        if (id == IDC_IDLG_SZR_RESET && event == BN_CLICKED) {
            // Reset: clear the user-sized flag and re-run auto-fit.
            if (sd->pd) {
                bool wasSized = s_previewUserSized[(int)sd->pd->type];
                s_previewUserSized[sd->pd->type] = false;
                if (sd->hPreview && IsWindow(sd->hPreview)) {
                    AutoFitPreview(sd->hPreview, sd->pd);
                    // Re-centre on monitor work area after the size change.
                    RECT rcP; GetWindowRect(sd->hPreview, &rcP);
                    int newW = rcP.right - rcP.left;
                    int newH = rcP.bottom - rcP.top;
                    int nx = rcP.left, ny = rcP.top;
                    HMONITOR hMR = MonitorFromWindow(sd->hPreview, MONITOR_DEFAULTTONEAREST);
                    MONITORINFO miR = {}; miR.cbSize = sizeof(miR);
                    if (hMR && GetMonitorInfoW(hMR, &miR)) {
                        RECT& wa = miR.rcWork;
                        nx = wa.left + (wa.right  - wa.left - newW) / 2;
                        ny = wa.top  + (wa.bottom - wa.top  - newH) / 2;
                        if (nx + newW > wa.right)  nx = wa.right  - newW;
                        if (ny + newH > wa.bottom) ny = wa.bottom - newH;
                        if (nx < wa.left)  nx = wa.left;
                        if (ny < wa.top)   ny = wa.top;
                    }
                    SetWindowPos(sd->hPreview, NULL, nx, ny, 0, 0,
                        SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
                    // Keep sizer aligned to preview top-left.
                    RECT rcSz; GetWindowRect(hwnd, &rcSz);
                    int szW = rcSz.right - rcSz.left;
                    SetWindowPos(hwnd, NULL, nx - S(8) - szW, ny, 0, 0,
                        SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
                }
                // Only mark modified if there was actually a user-set size to reset.
                if (wasSized) MainWindow::MarkAsModified();
            }
            return 0;
        }
        if (id == IDC_IDLG_SZR_CLOSE && event == BN_CLICKED) {
            // Close button: commit sizes and close the preview (non-cancel path).
            if (sd->pd) { sd->pd->cancelled = false; sd->pd->running = false; }
            return 0;
        }
        if ((id == IDC_IDLG_SZR_H_ALIGN || id == IDC_IDLG_SZR_V_ALIGN) && event == CBN_SELCHANGE) {
            HWND hHAC = GetDlgItem(hwnd, IDC_IDLG_SZR_H_ALIGN);
            HWND hVAC = GetDlgItem(hwnd, IDC_IDLG_SZR_V_ALIGN);
            s_previewHAlign = (int)SendMessageW(hHAC, CB_GETCURSEL, 0, 0);
            s_previewVAlign = (int)SendMessageW(hVAC, CB_GETCURSEL, 0, 0);
            if (sd->hPreview && IsWindow(sd->hPreview)) {
                PreviewData* ppd = (PreviewData*)GetWindowLongPtrW(sd->hPreview, GWLP_USERDATA);
                if (ppd) LayoutPreviewControls(sd->hPreview, ppd);
            }
            return 0;
        }
        if ((id == IDC_IDLG_SZR_W_EDIT || id == IDC_IDLG_SZR_H_EDIT) && event == EN_CHANGE) {
            if (sd->ignoring) return 0;  // programmatic update (UDM_SETPOS32 during init)
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

    case WM_KEYDOWN: {
        // Tab cycling between the sizer's editable controls.
        // IsDialogMessageW is NOT used for this window (it would eat first
        // WM_LBUTTONDOWN events on unfocused controls — same bug as the preview).
        if (wParam == VK_TAB) {
            bool shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
            HWND order[] = {
                GetDlgItem(hwnd, IDC_IDLG_SZR_W_EDIT),
                GetDlgItem(hwnd, IDC_IDLG_SZR_H_EDIT),
                GetDlgItem(hwnd, IDC_IDLG_SZR_H_ALIGN),
                GetDlgItem(hwnd, IDC_IDLG_SZR_V_ALIGN),
                GetDlgItem(hwnd, IDC_IDLG_SZR_RESET),
                GetDlgItem(hwnd, IDC_IDLG_SZR_CLOSE),
            };
            int n = (int)(sizeof(order) / sizeof(order[0]));
            HWND hFocus = GetFocus();
            int cur = -1;
            for (int i = 0; i < n; i++) if (order[i] == hFocus) { cur = i; break; }
            int next = (cur < 0) ? 0 : (shift ? (cur - 1 + n) % n : (cur + 1) % n);
            if (order[next]) SetFocus(order[next]);
            return 0;
        }
        break;
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

// ── ScanRtfNaturalWidthTwips ──────────────────────────────────────────────────
// Parse the RTF for absolute twip widths rather than trying to measure rendered
// horizontal overflow (which is unreliable in word-wrapping RichEdits):
//
//   \cellxNNNN    — right edge of a table cell, from the table left edge
//   \picwgoalNNNN — desired display width of an embedded image
//
// Taking the maximum value gives the natural content width independent of DPI:
//   logical px = twips / 15   (always, because twips·DPI/1440 / (DPI/96) = twips·96/1440)
//
// For plain text (no tables or images) returns 0 so the fallback width is kept.

static int ScanRtfNaturalWidthTwips(const std::wstring& rtf)
{
    int maxTwips = 0;
    static const struct { const wchar_t* kw; int len; } kKW[] = {
        { L"\\cellx",    6 },   // table cell right edge
        { L"\\picwgoal", 9 },   // embedded image width
    };
    for (auto& k : kKW) {
        size_t pos = 0;
        while ((pos = rtf.find(k.kw, pos)) != std::wstring::npos) {
            pos += k.len;
            if (pos < rtf.size() && rtf[pos] == L'-') { ++pos; continue; } // skip negative
            int val = 0;
            while (pos < rtf.size() && rtf[pos] >= L'0' && rtf[pos] <= L'9')
                val = val * 10 + (int)(rtf[pos++] - L'0');
            maxTwips = std::max(maxTwips, val);
        }
    }
    return maxTwips;
}

// ── ShowFinishFeedback — small "end of preview" dialog shown when Finish is clicked ──
// Mimics the real installer's post-Finish moment: installer title in caption,
// a "This is the end of the installer preview." message, and a disabled
// "Open <AppName>" checkbox (checked, greyed — looks like the real finish page
// but makes it clear it's only a preview).

struct FinishFbData {
    bool running;
    bool ok;
};

// Control IDs for the finish-feedback dialog
#define IDC_FFBK_MSG   1001
#define IDC_FFBK_OPEN  1002
#define IDC_FFBK_OK    1003

static LRESULT CALLBACK FinishFeedbackWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    FinishFbData* fd = (FinishFbData*)(LONG_PTR)GetWindowLongPtrW(hwnd, GWLP_USERDATA);

    switch (msg) {
    case WM_CREATE: {
        CREATESTRUCTW* cs = (CREATESTRUCTW*)lParam;
        fd = (FinishFbData*)cs->lpCreateParams;
        if (!fd) return -1;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)fd);

        HFONT hGuiFont = s_hGuiFont ? s_hGuiFont
            : (HFONT)GetStockObject(DEFAULT_GUI_FONT);

        // Message label
        std::wstring msg_ = L10n(L"idlg_preview_done_msg",
                                  L"This is the end of the installer preview.");
        HWND hMsg = CreateWindowExW(0, L"STATIC", msg_.c_str(),
            WS_CHILD | WS_VISIBLE | SS_LEFT,
            S(16), S(16), S(280), S(32),
            hwnd, (HMENU)(UINT_PTR)IDC_FFBK_MSG, cs->hInstance, NULL);
        if (hGuiFont) SendMessageW(hMsg, WM_SETFONT, (WPARAM)hGuiFont, TRUE);

        // "Open <AppName>" checkbox — ticked and disabled to simulate the real page
        std::wstring appLabel = L10n(L"idlg_preview_done_open", L"Open <<AppName>>");
        // Replace <<AppName>> placeholder with the live project name
        std::wstring::size_type p;
        while ((p = appLabel.find(L"<<AppName>>")) != std::wstring::npos)
            appLabel.replace(p, 11, s_previewAppName.empty() ? L"app" : s_previewAppName);
        HWND hChk = CreateCustomCheckbox(hwnd, IDC_FFBK_OPEN, appLabel,
            true, S(16), S(56), S(280), S(24), cs->hInstance);
        EnableWindow(hChk, FALSE);

        // OK button
        std::wstring okTxt = L10n(L"ok", L"OK");
        HWND hOk = CreateWindowExW(0, L"BUTTON", okTxt.c_str(),
            WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON,
            S(16), S(96), S(80), S(30),
            hwnd, (HMENU)(UINT_PTR)IDC_FFBK_OK, cs->hInstance, NULL);
        if (hGuiFont) SendMessageW(hOk, WM_SETFONT, (WPARAM)hGuiFont, TRUE);
        return 0;
    }

    case WM_DRAWITEM: {
        LPDRAWITEMSTRUCT dis = (LPDRAWITEMSTRUCT)lParam;
        if (DrawCustomCheckbox(dis)) return TRUE;
        return DefWindowProcW(hwnd, msg, wParam, lParam);
    }

    case WM_COMMAND:
        if (LOWORD(wParam) == IDC_FFBK_OK || LOWORD(wParam) == IDCANCEL) {
            if (fd) { fd->ok = (LOWORD(wParam) == IDC_FFBK_OK); fd->running = false; }
        }
        return 0;

    case WM_KEYDOWN:
        if (wParam == VK_RETURN || wParam == VK_ESCAPE) {
            if (fd) { fd->ok = (wParam == VK_RETURN); fd->running = false; }
        }
        return 0;

    case WM_CLOSE:
        if (fd) { fd->ok = false; fd->running = false; }
        return 0;

    case WM_CTLCOLORSTATIC:
        SetBkColor((HDC)wParam, GetSysColor(COLOR_WINDOW));
        SetTextColor((HDC)wParam, GetSysColor(COLOR_WINDOWTEXT));
        return (LRESULT)GetSysColorBrush(COLOR_WINDOW);
    }

    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

// Shows the finish-feedback dialog as a modal popup over hOwner.
// hOwner should be the preview window (not the main window) so the preview
// stays disabled while this dialog is visible.
static void ShowFinishFeedback(HWND hOwner)
{
    static bool s_classOk = false;
    if (!s_classOk) {
        WNDCLASSEXW wc    = {};
        wc.cbSize         = sizeof(wc);
        wc.hInstance      = s_hInst;
        wc.hCursor        = LoadCursor(NULL, IDC_ARROW);
        wc.hbrBackground  = (HBRUSH)(COLOR_WINDOW + 1);
        wc.lpfnWndProc    = FinishFeedbackWndProc;
        wc.lpszClassName  = L"IDLGFinishFbClass";
        RegisterClassExW(&wc);
        s_classOk = true;
    }

    FinishFbData fd = {};
    fd.running = true;
    fd.ok      = false;

    // Caption: installer title or generic fallback
    std::wstring caption = s_installTitle.empty()
        ? L10n(L"idlg_preview_done_title", L"Installation Complete")
        : s_installTitle + L"  \u2014  "
          + L10n(L"idlg_preview_done_title", L"Installation Complete");

    // Window size: fixed, small
    const DWORD style   = WS_POPUP | WS_CAPTION | WS_SYSMENU;
    const DWORD exStyle = WS_EX_DLGMODALFRAME;
    RECT adj = { 0, 0, S(312), S(142) };
    AdjustWindowRectEx(&adj, style, FALSE, exStyle);
    int wndW = adj.right - adj.left;
    int wndH = adj.bottom - adj.top;

    // Centre over owner
    RECT rcOwn; GetWindowRect(hOwner, &rcOwn);
    int px = rcOwn.left + (rcOwn.right  - rcOwn.left - wndW) / 2;
    int py = rcOwn.top  + (rcOwn.bottom - rcOwn.top  - wndH) / 2;

    HWND hFb = CreateWindowExW(exStyle,
        L"IDLGFinishFbClass", caption.c_str(), style,
        px, py, wndW, wndH, hOwner, NULL, s_hInst, &fd);
    if (!hFb) return;

    // Apply installer icon to the caption if we have one
    if (s_hInstallIcon)
        SendMessageW(hFb, WM_SETICON, ICON_SMALL, (LPARAM)s_hInstallIcon);

    EnableWindow(hOwner, FALSE);
    ShowWindow(hFb, SW_SHOW);
    UpdateWindow(hFb);

    MSG m;
    while (fd.running && GetMessageW(&m, NULL, 0, 0) > 0) {
        // hFb is a CreateWindowExW popup, not a real dialog — do NOT use
        // IsDialogMessageW here; it eats first WM_LBUTTONDOWN on non-focused
        // controls.  WM_KEYDOWN VK_RETURN/VK_ESCAPE handled in FinishFeedbackWndProc.
        TranslateMessage(&m);
        DispatchMessageW(&m);
    }

    EnableWindow(hOwner, TRUE);
    DestroyWindow(hFb);
    SetActiveWindow(hOwner);
    SetForegroundWindow(hOwner);
}

// ── TryCancelPreview ──────────────────────────────────────────────────────────
// Called from all Cancel / WM_CLOSE / Escape paths in PreviewWndProc.
// If the sizes have not changed since the preview opened, cancels normally
// (pd->cancelled = true).  If sizes changed, asks the user first:
//   • "Yes" (lose changes) → cancelled = true   — ShowPreviewDialog reverts.
//   • "No"  (keep the preview open) → do nothing, preview keeps running.

static void TryCancelPreview(HWND hwnd, PreviewData* pd)
{
    if (!pd) return;
    if (s_previewLogW != pd->openLogW || s_previewLogH != pd->openLogH
            || s_previewUserSized[(int)pd->type] != pd->openUserSized) {
        bool lose = s_pLocale
            ? ShowConfirmDeleteDialog(hwnd,
                L10n(L"idlg_preview_lose_size_title", L"Unsaved size changes"),
                L10n(L"idlg_preview_lose_size_msg",
                     L"You have manually resized this dialog. Close and lose the sizing changes?"),
                *s_pLocale)
            : true;
        if (!lose) {
            // "No" — user wants to stay in the preview; do nothing.
            return;
        }
    }
    pd->running   = false;
    pd->cancelled = true;
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
    RECT rcSz = { 0, 0, S(165), S(194) };
    AdjustWindowRectEx(&rcSz, sizerStyle, FALSE, sizerExStyle);
    int szW = rcSz.right  - rcSz.left;
    int szH = rcSz.bottom - rcSz.top;

    // ── Position both windows (preview centred on monitor; sizer to its left) ──
    MONITORINFO mi = {}; mi.cbSize = sizeof(mi);
    HMONITOR hMon = MonitorFromWindow(hwndParent, MONITOR_DEFAULTTONEAREST);
    int px, py;
    if (hMon && GetMonitorInfoW(hMon, &mi)) {
        RECT& wa = mi.rcWork;
        px = wa.left + (wa.right  - wa.left - wndW) / 2;
        py = wa.top  + (wa.bottom - wa.top  - wndH) / 2;
        if (px + wndW > wa.right)  px = wa.right  - wndW;
        if (py + wndH > wa.bottom) py = wa.bottom - wndH;
        if (px < wa.left)  px = wa.left;
        if (py < wa.top)   py = wa.top;
    } else {
        RECT rcParent; GetWindowRect(hwndParent, &rcParent);
        px = rcParent.left + (rcParent.right  - rcParent.left - wndW) / 2;
        py = rcParent.top  + (rcParent.bottom - rcParent.top  - wndH) / 2;
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
    pd.type       = type;
    pd.hGuiFont   = s_hGuiFont;
    pd.hTitleFont = s_hTitleFont;
    pd.running    = true;
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
    // SW_SHOWNOACTIVATE: show the sizer without stealing activation from the
    // preview.  SW_SHOW would activate the sizer, moving keyboard focus into its
    // edit controls so that the first click on a preview button would reactivate
    // the preview (via WM_MOUSEACTIVATE) rather than being delivered directly to
    // the button — causing the button to require multiple clicks.
    if (hSizer) { ShowWindow(hSizer, SW_SHOWNOACTIVATE); UpdateWindow(hSizer); }

    // Auto-fit AFTER the window is visible so MeasureRichEditLogHeight
    // gets a genuine paint pass (images and OLE objects need a live DC).
    // AutoFitPreview handles ALL dialog types; skips if developer resized.
    AutoFitPreview(hPreview, &pd);
    // AutoFitPreview uses SWP_NOMOVE — re-centre on monitor work area after resize.
    {
        RECT rcW; GetWindowRect(hPreview, &rcW);
        int newW  = rcW.right  - rcW.left;
        int newH  = rcW.bottom - rcW.top;
        int newPx, newPy;
        HMONITOR hM2 = MonitorFromWindow(hPreview, MONITOR_DEFAULTTONEAREST);
        MONITORINFO mi2 = {}; mi2.cbSize = sizeof(mi2);
        if (hM2 && GetMonitorInfoW(hM2, &mi2)) {
            RECT& wa2 = mi2.rcWork;
            newPx = wa2.left + (wa2.right  - wa2.left - newW) / 2;
            newPy = wa2.top  + (wa2.bottom - wa2.top  - newH) / 2;
            if (newPx + newW > wa2.right)  newPx = wa2.right  - newW;
            if (newPy + newH > wa2.bottom) newPy = wa2.bottom - newH;
            if (newPx < wa2.left)  newPx = wa2.left;
            if (newPy < wa2.top)   newPy = wa2.top;
        } else {
            newPx = rcW.left;  // unchanged
            newPy = rcW.top;
        }
        SetWindowPos(hPreview, NULL, newPx, newPy, 0, 0,
                     SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
        if (hSizer && IsWindow(hSizer)) {
            int newSxX = newPx - S(8) - szW;
            SetWindowPos(hSizer, NULL, newSxX, newPy, 0, 0,
                         SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
        }
    }
    // Invalidate hPreview and all children so WM_PAINT fires on the first
    // message-loop pass.  RDW_UPDATENOW must NOT be used here — it would
    // force a synchronous paint of the RichEdit which can take 9+ seconds
    // for complex RTF.  Async invalidation is sufficient: the message loop
    // processes WM_PAINT immediately on its first iteration.
    RedrawWindow(hPreview, NULL, NULL, RDW_INVALIDATE | RDW_ALLCHILDREN);

    // Snapshot AFTER auto-fit so Cancel only prompts if the user manually changed
    // the size after the preview was already laid out.
    pd.openLogW      = s_previewLogW;
    pd.openLogH      = s_previewLogH;
    pd.openUserSized = s_previewUserSized[(int)type];

    EnableWindow(hwndParent, FALSE);

    MSG m;
    while (pd.running && GetMessageW(&m, NULL, 0, 0) > 0) {
        // WM_MOUSEWHEEL goes to the focused window (typically a button), not the
        // window under the cursor.  Buttons drop it silently.  Intercept here and
        // route directly to the content RichEdit before any other processing.
        if (m.message == WM_MOUSEWHEEL && pd.hContent && IsWindow(pd.hContent)) {
            int delta = GET_WHEEL_DELTA_WPARAM(m.wParam);
            UINT lines = 3;
            SystemParametersInfoW(SPI_GETWHEELSCROLLLINES, 0, &lines, 0);
            if (lines == WHEEL_PAGESCROLL) lines = 3;
            int count = (abs(delta) * (int)lines + WHEEL_DELTA / 2) / WHEEL_DELTA;
            WPARAM cmd = (delta > 0) ? SB_LINEUP : SB_LINEDOWN;
            for (int i = 0; i < count; i++)
                SendMessageW(pd.hContent, EM_SCROLL, cmd, 0);
            // EM_SCROLL scrolls content but doesn't repaint the thumb.
            POINT scrollPt = {};
            SendMessageW(pd.hContent, EM_GETSCROLLPOS, 0, (LPARAM)&scrollPt);
            SetScrollPos(pd.hContent, SB_VERT, scrollPt.y, TRUE);
            continue;
        }
        // Do NOT use IsDialogMessageW here — the sizer is a CreateWindowExW popup,
        // not a real dialog.  IsDialogMessageW on non-dialog windows eats first
        // WM_LBUTTONDOWN events on non-focused controls (same class of bug that was
        // fixed for the preview window itself).  Tab navigation for the sizer's
        // spinners / edits is handled in SizerWndProc WM_KEYDOWN instead.
        TranslateMessage(&m);
        DispatchMessageW(&m);
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
    // Non-cancel close: sizes remain in the globals and will be written to DB
    // when the developer presses the main Save button (IDLG_SaveToDb).

    if (hSmIcon) DestroyIcon(hSmIcon);
    if (hLgIcon) DestroyIcon(hLgIcon);

    // Show the "Installation Complete" feedback dialog AFTER the preview is
    // fully torn down and hwndParent is re-enabled.  This eliminates the
    // nested-message-loop problem (ShowFinishFeedback used to be called from
    // inside DispatchMessageW → PreviewWndProc WM_COMMAND, creating a second
    // message loop while the outer preview loop was suspended — that caused
    // the first-click swallowing on the OK button).
    if (pd.finishSelected && !pd.cancelled)
        ShowFinishFeedback(hwndParent);
}

// ── IDLG_Reset ────────────────────────────────────────────────────────────────

void IDLG_Reset()
{
    for (int i = 0; i < IDLG_COUNT; i++) {
        s_dialogs[i].type        = (InstallerDialogType)i;
        s_dialogs[i].content_rtf = L"";
    }
    s_installTitle     = L"";
    s_previewAppName   = L"";
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

// ── IDLG_ApplyDefaults ────────────────────────────────────────────────────────
// Fills any in-memory dialog slot that is still empty with default RTF from the
// dialog_defaults table, after substituting <<AppName>>, <<AppVersion>>, and
// <<AppNameAndVersion>> (resolves to "Name, Version" or just "Name" when empty).
// Called once in mainwindow Create() so both new and existing projects see
// sensible starter text on the Dialogs page without developer effort.

// ── RtfEncodeText — encode a plain-text string for embedding inside RTF ──────
// Escapes RTF metacharacters and encodes non-ASCII as RTF \uN? Unicode escapes.
static std::wstring RtfEncodeText(const std::wstring& text)
{
    std::wstring out;
    out.reserve(text.size() * 3);
    for (wchar_t ch : text) {
        if      (ch == L'\\') { out += L"\\\\"; }
        else if (ch == L'{')  { out += L"\\{";  }
        else if (ch == L'}')  { out += L"\\}";  }
        else if (ch < 128)    { out += ch; }
        else {
            // RTF Unicode escape \uN? — N is signed decimal (sign-extend wchar_t)
            out += L"\\u";
            out += std::to_wstring((int)(short)ch);
            out += L'?';
        }
    }
    return out;
}

static std::wstring SubstitutePlaceholders(std::wstring rtf,
    const std::wstring& name, const std::wstring& version)
{
    std::wstring nameVer = version.empty() ? name : name + L", " + version;
    auto repl = [](std::wstring& s, const std::wstring& from, const std::wstring& to) {
        size_t pos = 0;
        while ((pos = s.find(from, pos)) != std::wstring::npos) {
            s.replace(pos, from.size(), to);
            pos += to.size();
        }
    };
    // Replace compound placeholder first to avoid partial matches with <<AppName>>
    repl(rtf, L"<<AppNameAndVersion>>", nameVer);
    repl(rtf, L"<<AppName>>",           name);
    repl(rtf, L"<<AppVersion>>",        version);
    // License credit note — localized text embedded in RTF.
    // The locale value is plain Unicode; <<AppName>> inside it is substituted
    // first, then the whole string is RTF-encoded before splicing into the RTF.
    if (rtf.find(L"<<LicenseCreditNote>>") != std::wstring::npos) {
        std::wstring note = L10n(L"idlg_license_credit_note",
            L"If you find <<AppName>> useful, a credit in your application\u2019s "
            L"About dialog or documentation is warmly appreciated "
            L"\u2014 though it is not required.");
        repl(note, L"<<AppName>>", name);
        repl(rtf, L"<<LicenseCreditNote>>", RtfEncodeText(note));
    }
    // Resolve <<DlgDefaultDepsBody>> based on which delivery modes the project uses.
    if (rtf.find(L"<<DlgDefaultDepsBody>>") != std::wstring::npos) {
        int mask = DEP_GetDeliveryModeMask();
        const wchar_t* key;
        if      (mask == 0 || mask == (1 << DD_BUNDLED))       key = L"idlg_default_deps_bundled";
        else if (mask == (1 << DD_AUTO_DOWNLOAD))               key = L"idlg_default_deps_download";
        else if (mask == (1 << DD_REDIRECT_URL))                key = L"idlg_default_deps_redirect";
        else if (mask == (1 << DD_INSTRUCTIONS_ONLY))           key = L"idlg_default_deps_instructions";
        else                                                     key = L"idlg_default_deps_mixed";
        std::wstring text = L10n(key,
            L"The following components are required by <<AppName>>. "
            L"If any are missing, they will be downloaded or set up automatically.");
        repl(text, L"<<AppName>>", name);
        repl(rtf, L"<<DlgDefaultDepsBody>>", RtfEncodeText(text));
    }
    // Resolve all remaining <<DlgDefault*>> placeholders from locale strings.
    struct { const wchar_t* ph; const wchar_t* key; const wchar_t* fallback; } kDlgKeys[] = {
        { L"<<DlgDefaultWelcomeTitle>>",   L"idlg_default_welcome_title",
          L"Welcome to <<AppNameAndVersion>>" },
        { L"<<DlgDefaultWelcomeBody>>",    L"idlg_default_welcome_body",
          L"This setup program will install <<AppName>> on your computer. "
          L"Click \u00abNext\u00bb to continue." },
        { L"<<DlgDefaultForMeAllBody>>",   L"idlg_default_for_me_all_body",
          L"Choose whether to install <<AppName>> for yourself only, "
          L"or for all users of this computer." },
        { L"<<DlgDefaultComponentsBody>>", L"idlg_default_components_body",
          L"Select the components of <<AppName>> you want to install." },
        { L"<<DlgDefaultShortcutsBody>>",  L"idlg_default_shortcuts_body",
          L"Choose where to create shortcuts for <<AppName>>." },
        { L"<<DlgDefaultReadyBody1>>",     L"idlg_default_ready_body1",
          L"Setup is ready to install <<AppName>> on your computer." },
        { L"<<DlgDefaultReadyBody2>>",     L"idlg_default_ready_body2",
          L"Click \u00abInstall\u00bb to begin, or \u00abBack\u00bb to review your settings." },
        { L"<<DlgDefaultInstallingBody>>", L"idlg_default_installing_body",
          L"Please wait while <<AppName>> is being installed on your computer." },
        { L"<<DlgDefaultFinishTitle>>",    L"idlg_default_finish_title",
          L"Installation Complete" },
        { L"<<DlgDefaultFinishBody1>>",    L"idlg_default_finish_body1",
          L"<<AppNameAndVersion>> has been installed on your computer." },
        { L"<<DlgDefaultFinishBody2>>",    L"idlg_default_finish_body2",
          L"Click \u00abFinish\u00bb to exit." },
    };
    for (const auto& k : kDlgKeys) {
        if (rtf.find(k.ph) == std::wstring::npos) continue;
        std::wstring text = L10n(k.key, k.fallback);
        repl(text, L"<<AppNameAndVersion>>", nameVer);
        repl(text, L"<<AppName>>",           name);
        repl(rtf, k.ph, RtfEncodeText(text));
    }
    return rtf;
}

void IDLG_ApplyDefaults(const std::wstring& appName, const std::wstring& appVersion)
{
    s_previewAppName = appName;  // keep in sync for preview Finish dialog
    auto defaults = DB::GetAllDialogDefaults();
    for (const auto& d : defaults) {
        if (d.first < 0 || d.first >= IDLG_COUNT) continue;
        if (!s_dialogs[d.first].content_rtf.empty()) continue; // already has content
        if (d.second.empty()) continue;
        s_dialogs[d.first].content_rtf =
            SubstitutePlaceholders(d.second, appName, appVersion);
    }
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
    // Save all per-type user-sized flags as a compact string (one '0'/'1' per type).
    std::wstring userSizedStr(IDLG_COUNT, L'0');
    for (int i = 0; i < IDLG_COUNT; i++) userSizedStr[i] = s_previewUserSized[i] ? L'1' : L'0';
    DB::SetSetting(L"installer_preview_user_sized_" + pid, userSizedStr);
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
    if (DB::GetSetting(L"installer_preview_user_sized_" + pid, sPrevSized) && !sPrevSized.empty()) {
        if (sPrevSized.size() == 1) {
            // Legacy: old format only stored IDLG_COMPONENTS.
            s_previewUserSized[IDLG_COMPONENTS] = (sPrevSized[0] == L'1');
        } else {
            for (int i = 0; i < IDLG_COUNT && i < (int)sPrevSized.size(); i++)
                s_previewUserSized[i] = (sPrevSized[i] == L'1');
        }
    }
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

