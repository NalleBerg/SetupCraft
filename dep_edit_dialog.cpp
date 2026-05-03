/*
 * dep_edit_dialog.cpp — "Edit Dependency" modal dialog for SetupCraft.
 *
 * Follows the measure-then-create pattern documented in dialog_INTERNALS.txt.
 * All pixel values use S(); all strings use the locale map.
 *
 * Progressive-disclosure design
 * ──────────────────────────────
 * When adding a new dependency the developer first sees only two things:
 *   - Display name (mandatory text field)
 *   - Delivery type (combo — first item is "Choose type…", not a real choice)
 *
 * Once a real delivery type is chosen via the combo, Reflow() reveals the
 * controls that make sense for that type, and the window grows to fit them.
 * The OK/Cancel buttons are always visible at the bottom.
 *
 * When editing an existing dependency (dep.delivery != DD_NONE) the dialog
 * opens with all appropriate controls already visible — no extra click needed.
 *
 * Delivery → visible sections:
 *   DD_BUNDLED:            Required · Order · License · Credits
 *   DD_AUTO_DOWNLOAD:      Required · Order · Detection (optional) · Network(all) · License · Credits
 *   DD_REDIRECT_URL:       Required · Order · Detection (optional) · Network(URL+offline) · License · Credits
 *   DD_INSTRUCTIONS_ONLY:  Required · Order
 */

#include "dep_edit_dialog.h"
#include "button.h"       // CreateCustomButtonWithIcon, DrawCustomButton
#include "checkbox.h"     // CreateCustomCheckbox, DrawCustomCheckbox
#include "ctrlw.h"        // ShowValidationDialog
#include "dpi.h"          // S()
#include "tooltip.h"      // ShowMultilingualTooltip, HideTooltip
#include <commctrl.h>
#include <commdlg.h>      // GetOpenFileNameW
#include "edit_rtf.h"     // OpenRtfEditor

extern "C" __declspec(dllimport) UINT WINAPI PrivateExtractIconsW(
    LPCWSTR szFileName, int nIconIndex, int cxIcon, int cyIcon,
    HICON* phicon, UINT* piconid, UINT nIcons, UINT flags);

// ── Layout constants (design pixels at 96 DPI) ────────────────────────────────
static const int DD_PAD_H    = 20;   // left/right padding
static const int DD_PAD_T    = 20;   // top padding
static const int DD_PAD_B    = 24;   // bottom padding (below buttons)
static const int DD_GAP      = 10;   // standard inter-row gap
static const int DD_GAP_SM   =  4;   // label → control gap
static const int DD_BTN_H    = 34;   // OK / Cancel height
static const int DD_BTN_GAP  = 15;   // gap between OK and Cancel
static const int DD_CONT_W   = 560;  // inner content column width
static const int DD_LABEL_H  = 18;   // single-line label height
static const int DD_EDIT_H   = 26;   // single-line edit height
static const int DD_CB_H     = 22;   // checkbox height
static const int DD_COMBO_H  = 26;   // combo-box height
static const int DD_MLABEL_H = 16;   // section header label
static const int DD_TITLE_H  = 28;   // dialog headline label (larger font)

// ── Sentinel value for "no delivery type chosen yet" ─────────────────────────
// Stored as CB_SETITEMDATA on combo index 0; distinguishes "Choose type…" from
// a real DepDelivery. Real types are stored at indices 1–4.
static const int DD_DELIVERY_NONE = -1;

// ── Per-invocation heap struct ────────────────────────────────────────────────
struct DepDlgData {
    ExternalDep dep;
    HINSTANCE   hInst;
    const std::map<std::wstring,std::wstring>* pLocale;
    bool        okPressed;
    std::vector<std::wstring> compNames;  // project component display names; empty = no compLink section
};

static bool         s_depDlgOk      = false;
static int          s_depDlgScrollY = 0;   // current vertical scroll offset (scaled px)
static int          s_depDlgTotalH  = 0;   // full content height (scaled px)
static std::vector<std::wstring> s_depInstrList;   // working copy of instruction pages (RTF)
static std::wstring s_depLicRtf;            // working copy of license RTF

// ── All sections tracked for Reflow() ────────────────────────────────────────
// Each "section" is a flat list of HWNDs created together; Reflow() shows or
// hides whole sections and then repositions everything below them.
struct DdSection {
    std::vector<HWND> ctrls;   // all HWNDs belonging to this section
    int               designH; // design-pixel height consumed when visible
};

// Sections in top-down order (after the always-visible top block).
static DdSection s_secRequired;     // Required checkbox
static DdSection s_secOrder;        // Install-order edit
static DdSection s_secDetection;    // Detection header + three edits (all optional)
static DdSection s_secNetAll;       // Network header + URL + silent-args + SHA-256 + offline
static DdSection s_secNetUrlOnly;   // (subset of Network) shown for DD_REDIRECT_URL
//   Note: DD_REDIRECT_URL shows only URL (row 0) and Offline (row 3) from the network
//   block.  Rather than duplicating HWNDs we track sub-visibility inside Reflow().
static DdSection s_secExitCodes;    // Acceptable exit codes: label + edit (DD_BUNDLED + DD_AUTO_DOWNLOAD)
static DdSection s_secCompLink;     // Component linkage: label + read-only edit + "…" pick button (all delivery types when project has components)
static DdSection s_secLicense;      // License section header + indicator + button + credits
static DdSection s_secInstructions; // Instructions header + indicator + button (DD_INSTRUCTIONS_ONLY only)

// The OK/Cancel buttons — always visible, always repositioned last by Reflow().
static HWND s_hBtnOK     = NULL;
static HWND s_hBtnCancel = NULL;
static int  s_btnOK_W    = 0;
static int  s_btnCancel_W= 0;

// Left X and full width shared by all controls (set once in DEP_EditDialog).
static int  s_ddLX = 0;
static int  s_ddEW = 0;

// Y coordinate (unscrolled design pixels, already scaled) where the dynamic
// section block starts — i.e. just below the always-visible name+delivery rows.
// Reflow() uses this as the starting Y for its layout pass.
static int  s_rfNetOriginY = 0;

// ── Safe locale lookup ────────────────────────────────────────────────────────
static std::wstring DL(const DepDlgData* d, const wchar_t* key, const wchar_t* fb)
{
    auto it = d->pLocale->find(key);
    return (it != d->pLocale->end()) ? it->second : fb;
}

// ── Helpers ───────────────────────────────────────────────────────────────────

static std::wstring GetEditText(HWND hDlg, int id)
{
    int len = GetWindowTextLengthW(GetDlgItem(hDlg, id));
    if (len <= 0) return L"";
    std::wstring s(len + 1, L'\0');
    GetWindowTextW(GetDlgItem(hDlg, id), &s[0], len + 1);
    s.resize(len);
    return s;
}

// Returns the DepDelivery value stored as item-data for the currently selected
// combo item, or DD_DELIVERY_NONE (-1) if "Choose type…" is selected.
static int GetDeliveryChoice(HWND hDlg)
{
    int idx = (int)SendDlgItemMessageW(hDlg, IDC_DEPDLG_DELIVERY, CB_GETCURSEL, 0, 0);
    if (idx < 0) return DD_DELIVERY_NONE;
    LRESULT v = SendDlgItemMessageW(hDlg, IDC_DEPDLG_DELIVERY, CB_GETITEMDATA, (WPARAM)idx, 0);
    return (v == CB_ERR) ? DD_DELIVERY_NONE : (int)v;
}

static int GetComboSel(HWND hDlg, int id)
{
    return (int)SendDlgItemMessageW(hDlg, id, CB_GETCURSEL, 0, 0);
}

// ── ShowSection / HideSection helpers ────────────────────────────────────────

static void ShowSection(const DdSection& sec, bool show)
{
    int sw = show ? SW_SHOW : SW_HIDE;
    for (HWND h : sec.ctrls) ShowWindow(h, sw);
}

// ── Reflow ────────────────────────────────────────────────────────────────────
// Called at initial open and on every CBN_SELCHANGE of the delivery combo.
// Decides which sections are visible, then lays them out top-to-bottom starting
// from a fixed origin Y (just below the always-visible name/delivery block),
// and finally places OK/Cancel at the bottom.
//
// The window is then resized to the new content height (clamped to work area),
// the scrollbar is reconfigured, and everything is redrawn.
static void Reflow(HWND hDlg, int deliveryVal)
{
    // ── Which sections are visible for this delivery type? ────────────────────
    bool hasAny          = (deliveryVal != DD_DELIVERY_NONE);
    bool hasRequired     = hasAny;
    bool hasOrder        = hasAny;
    bool hasDetection    = (deliveryVal == DD_AUTO_DOWNLOAD ||
                            deliveryVal == DD_REDIRECT_URL);
    bool hasNetAll       = (deliveryVal == DD_AUTO_DOWNLOAD);
    bool hasNetUrlOnly   = (deliveryVal == DD_REDIRECT_URL);
    bool hasExitCodes    = (deliveryVal == DD_BUNDLED || deliveryVal == DD_AUTO_DOWNLOAD);
    bool hasLicense      = (deliveryVal == DD_BUNDLED ||
                            deliveryVal == DD_AUTO_DOWNLOAD ||
                            deliveryVal == DD_REDIRECT_URL);
    bool hasInstructions = (deliveryVal == DD_INSTRUCTIONS_ONLY);
    // Component linkage: visible for all real delivery types when the project has components.
    DepDlgData* pDd = (DepDlgData*)GetWindowLongPtrW(hDlg, GWLP_USERDATA);
    bool hasCompLink = hasAny && pDd && !pDd->compNames.empty();

    // ── Show/hide each section ────────────────────────────────────────────────
    ShowSection(s_secRequired,  hasRequired);
    ShowSection(s_secOrder,     hasOrder);
    ShowSection(s_secCompLink,  hasCompLink);
    ShowSection(s_secDetection, hasDetection);
    ShowSection(s_secNetAll,    hasNetAll);
    ShowSection(s_secExitCodes, hasExitCodes);
    // For Redirect URL the Network block reuses s_secNetAll HWNDs but only shows
    // the URL row (index 0,1) and Offline row (index 6,7) — pairs: label+ctrl.
    // Indices: 0=netHdr, 1=urlLbl, 2=urlEdit, 3=argsLbl, 4=argsEdit,
    //          5=shaLbl, 6=shaEdit, 7=offlineLbl, 8=offlineCombo
    if (hasNetUrlOnly) {
        // Show only the header, URL pair, and Offline pair; hide args and SHA.
        if (s_secNetAll.ctrls.size() >= 9) {
            ShowWindow(s_secNetAll.ctrls[0], SW_SHOW); // header
            ShowWindow(s_secNetAll.ctrls[1], SW_SHOW); // URL label
            ShowWindow(s_secNetAll.ctrls[2], SW_SHOW); // URL edit
            ShowWindow(s_secNetAll.ctrls[3], SW_HIDE); // args label
            ShowWindow(s_secNetAll.ctrls[4], SW_HIDE); // args edit
            ShowWindow(s_secNetAll.ctrls[5], SW_HIDE); // SHA label
            ShowWindow(s_secNetAll.ctrls[6], SW_HIDE); // SHA edit
            ShowWindow(s_secNetAll.ctrls[7], SW_SHOW); // offline label
            ShowWindow(s_secNetAll.ctrls[8], SW_SHOW); // offline combo
        }
    }
    ShowSection(s_secLicense,      hasLicense);
    ShowSection(s_secInstructions, hasInstructions);

    // ── Manage DIO_BEFORE_WELCOME in install-order combo ─────────────────────
    // "Before the Welcome screen (silent)" makes no sense for Instructions Only
    // (the user must perform the step manually — it can never be silent).
    // Remove the item when Instructions is selected; restore it otherwise.
    if (hasOrder) {
        HWND hOrd = GetDlgItem(hDlg, IDC_DEPDLG_INSTALL_ORDER);
        if (hOrd) {
            int n = (int)SendMessageW(hOrd, CB_GETCOUNT, 0, 0);
            int preWelcomeIdx = -1;
            for (int i = 0; i < n; i++)
                if ((int)SendMessageW(hOrd, CB_GETITEMDATA, (WPARAM)i, 0) == (int)DIO_BEFORE_WELCOME)
                    { preWelcomeIdx = i; break; }

            if (hasInstructions && preWelcomeIdx >= 0) {
                // If it's currently selected, fall back to DIO_UNSPECIFIED
                int curSel = (int)SendMessageW(hOrd, CB_GETCURSEL, 0, 0);
                if (curSel == preWelcomeIdx) {
                    for (int i = 0; i < n; i++)
                        if ((int)SendMessageW(hOrd, CB_GETITEMDATA, (WPARAM)i, 0) == (int)DIO_UNSPECIFIED)
                            { SendMessageW(hOrd, CB_SETCURSEL, (WPARAM)i, 0); break; }
                }
                SendMessageW(hOrd, CB_DELETESTRING, (WPARAM)preWelcomeIdx, 0);
            } else if (!hasInstructions && preWelcomeIdx < 0) {
                // Re-insert at index 1 (right after "Choose install step…")
                DepDlgData* pd = (DepDlgData*)GetWindowLongPtrW(hDlg, GWLP_USERDATA);
                std::wstring txt = pd
                    ? DL(pd, L"dep_install_order_pre_welcome", L"Before the Welcome screen (silent)")
                    : L"Before the Welcome screen (silent)";
                int idx = (int)SendMessageW(hOrd, CB_INSERTSTRING, (WPARAM)1, (LPARAM)txt.c_str());
                if (idx != CB_ERR && idx != CB_ERRSPACE)
                    SendMessageW(hOrd, CB_SETITEMDATA, (WPARAM)idx, (LPARAM)(int)DIO_BEFORE_WELCOME);
            }
        }
    }

    // ── Lay out sections top to bottom ───────────────────────────────────────
    // s_depDlgTotalH is reset from scratch each Reflow call.
    // The fixed top block (title + name + delivery) takes a known amount of
    // space stored in s_depDlgScrollY's companion: we use the Y-position of the
    // first control in s_secRequired (or s_secOrder if Required is hidden) to
    // find where the dynamic area begins.
    //
    // Strategy: iterate all sections in order; for each visible one, place its
    // controls top-to-bottom using stored per-control sizes, advancing 'y'.
    // We do this by walking the section's ctrls vector and querying their sizes
    // via GetWindowRect (size never changes — only position does).

    // Find the Y at which the dynamic area begins (just below the delivery combo).
    // This is stored as the window-coordinate Y of the first ctrl in s_secRequired.
    // We compute it fresh each time by finding it from s_secOrder (always has ctrlss).
    // Actually, we store a fixed logical Y in s_rfNetOriginY reused as the start.
    // We repurpose s_rfNetOriginY as s_ddSectionStartY.
    int y = s_rfNetOriginY;  // logical Y (unscrolled) where dynamic sections start

    // Helper: place one control at logical y, using its existing width and the
    // passed height; advance y by height + gap.
    auto Place = [&](HWND h, int designH, int designGap) {
        if (!h) return;
        RECT rc; GetWindowRect(h, &rc);
        int w = rc.right - rc.left;
        SetWindowPos(h, NULL, s_ddLX, y - s_depDlgScrollY, w, S(designH),
            SWP_NOZORDER | SWP_NOACTIVATE);
        y += S(designH) + S(designGap);
    };
    auto PlaceW = [&](HWND h, int designH, int designGap, int forceW) {
        if (!h) return;
        SetWindowPos(h, NULL, s_ddLX, y - s_depDlgScrollY, forceW, S(designH),
            SWP_NOZORDER | SWP_NOACTIVATE);
        y += S(designH) + S(designGap);
    };

    // Required checkbox
    if (hasRequired && !s_secRequired.ctrls.empty()) {
        PlaceW(s_secRequired.ctrls[0], DD_CB_H, DD_GAP, s_ddEW);
    }
    // Install order: label then combo
    if (hasOrder && s_secOrder.ctrls.size() >= 2) {
        PlaceW(s_secOrder.ctrls[0], DD_LABEL_H, DD_GAP_SM, s_ddEW);
        PlaceW(s_secOrder.ctrls[1], DD_COMBO_H, DD_GAP,    s_ddEW);
    }
    // Component linkage (shown when project has components): label + read-only edit + "…" button
    // [0]=label  [1]=read-only edit  [2]="…" pick button
    if (hasCompLink && s_secCompLink.ctrls.size() >= 3) {
        PlaceW(s_secCompLink.ctrls[0], DD_LABEL_H, DD_GAP_SM, s_ddEW); // label
        // Edit and "…" button share one row at DD_EDIT_H height.
        int pickW = S(28);
        int editW = s_ddEW - pickW - S(DD_GAP_SM);
        SetWindowPos(s_secCompLink.ctrls[1], NULL,
            s_ddLX, y - s_depDlgScrollY, editW, S(DD_EDIT_H),
            SWP_NOZORDER | SWP_NOACTIVATE);
        SetWindowPos(s_secCompLink.ctrls[2], NULL,
            s_ddLX + editW + S(DD_GAP_SM), y - s_depDlgScrollY, pickW, S(DD_EDIT_H),
            SWP_NOZORDER | SWP_NOACTIVATE);
        y += S(DD_EDIT_H) + S(DD_GAP);
    }
    // Detection: header, then 4× (label + edit): reg_key, file_path, min_version, max_version
    if (hasDetection && s_secDetection.ctrls.size() >= 9) {
        PlaceW(s_secDetection.ctrls[0], DD_MLABEL_H, DD_GAP_SM, s_ddEW); // header
        PlaceW(s_secDetection.ctrls[1], DD_LABEL_H,  DD_GAP_SM, s_ddEW); // reg lbl
        PlaceW(s_secDetection.ctrls[2], DD_EDIT_H,   DD_GAP,    s_ddEW); // reg edit
        PlaceW(s_secDetection.ctrls[3], DD_LABEL_H,  DD_GAP_SM, s_ddEW); // file lbl
        PlaceW(s_secDetection.ctrls[4], DD_EDIT_H,   DD_GAP,    s_ddEW); // file edit
        PlaceW(s_secDetection.ctrls[5], DD_LABEL_H,  DD_GAP_SM, s_ddEW); // min_ver lbl
        PlaceW(s_secDetection.ctrls[6], DD_EDIT_H,   DD_GAP,    s_ddEW); // min_ver edit
        PlaceW(s_secDetection.ctrls[7], DD_LABEL_H,  DD_GAP_SM, s_ddEW); // max_ver lbl
        PlaceW(s_secDetection.ctrls[8], DD_EDIT_H,   DD_GAP,    s_ddEW); // max_ver edit
    }
    // Network (DD_AUTO_DOWNLOAD: all 4 rows; DD_REDIRECT_URL: URL + offline only)
    if ((hasNetAll || hasNetUrlOnly) && s_secNetAll.ctrls.size() >= 9) {
        PlaceW(s_secNetAll.ctrls[0], DD_MLABEL_H, DD_GAP_SM, s_ddEW); // header
        PlaceW(s_secNetAll.ctrls[1], DD_LABEL_H,  DD_GAP_SM, s_ddEW); // URL lbl
        PlaceW(s_secNetAll.ctrls[2], DD_EDIT_H,   DD_GAP,    s_ddEW); // URL edit
        if (hasNetAll) {
            PlaceW(s_secNetAll.ctrls[3], DD_LABEL_H, DD_GAP_SM, s_ddEW); // args lbl
            PlaceW(s_secNetAll.ctrls[4], DD_EDIT_H,  DD_GAP,    s_ddEW); // args edit
            PlaceW(s_secNetAll.ctrls[5], DD_LABEL_H, DD_GAP_SM, s_ddEW); // SHA lbl
            PlaceW(s_secNetAll.ctrls[6], DD_EDIT_H,  DD_GAP,    s_ddEW); // SHA edit
        }
        PlaceW(s_secNetAll.ctrls[7], DD_LABEL_H,  DD_GAP_SM, s_ddEW); // offline lbl
        PlaceW(s_secNetAll.ctrls[8], DD_COMBO_H,  DD_GAP,    s_ddEW); // offline combo
        if (hasNetAll && (int)s_secNetAll.ctrls.size() >= 11) {
            PlaceW(s_secNetAll.ctrls[9],  DD_LABEL_H, DD_GAP_SM, s_ddEW); // timeout lbl
            PlaceW(s_secNetAll.ctrls[10], DD_EDIT_H,  DD_GAP,    s_ddEW); // timeout edit
        }
    }
    // Exit codes (DD_BUNDLED + DD_AUTO_DOWNLOAD)
    // [0]=label  [1]=edit (narrowed)  [2]=helpBtn (square, opens reference list)
    if (hasExitCodes && s_secExitCodes.ctrls.size() >= 3) {
        PlaceW(s_secExitCodes.ctrls[0], DD_LABEL_H, DD_GAP_SM, s_ddEW); // label
        // Edit + help button share a single row; button is taller so it sets the row height.
        {
            int btnW   = S(DD_BTN_H);
            int editW  = s_ddEW - btnW - S(DD_GAP_SM);
            int rowH   = S(DD_BTN_H);
            int editOY = (rowH - S(DD_EDIT_H)) / 2; // vertically centre the edit
            SetWindowPos(s_secExitCodes.ctrls[1], NULL,
                s_ddLX, y - s_depDlgScrollY + editOY, editW, S(DD_EDIT_H),
                SWP_NOZORDER | SWP_NOACTIVATE);
            SetWindowPos(s_secExitCodes.ctrls[2], NULL,
                s_ddLX + editW + S(DD_GAP_SM), y - s_depDlgScrollY, btnW, rowH,
                SWP_NOZORDER | SWP_NOACTIVATE);
            y += rowH + S(DD_GAP);
        }
    }
    // License: header + indicator + button + credits label + credits edit
    if (hasLicense && s_secLicense.ctrls.size() >= 5) {
        PlaceW(s_secLicense.ctrls[0], DD_MLABEL_H, DD_GAP_SM, s_ddEW); // header
        PlaceW(s_secLicense.ctrls[1], DD_EDIT_H,   DD_GAP_SM, s_ddEW); // indicator
        // Button: centred under the indicator field
        { HWND h = s_secLicense.ctrls[2];
          RECT rc; GetWindowRect(h, &rc);
          int bw = rc.right - rc.left;
          int bx = s_ddLX + (s_ddEW - bw) / 2;
          SetWindowPos(h, NULL, bx, y - s_depDlgScrollY, bw, S(DD_BTN_H),
              SWP_NOZORDER | SWP_NOACTIVATE);
          y += S(DD_BTN_H) + S(DD_GAP); }
        PlaceW(s_secLicense.ctrls[3], DD_LABEL_H, DD_GAP_SM, s_ddEW); // credits lbl
        PlaceW(s_secLicense.ctrls[4], DD_EDIT_H,  DD_GAP,    s_ddEW); // credits edit
    }
    // ── Place OK / Cancel at the bottom
    // Instructions: N icons (2 cols each) in wrapped rows + add button
    if (hasInstructions) {
        auto& ctrls = s_secInstructions.ctrls;
        int N        = ((int)ctrls.size() - 1) / 2; // icon+label pairs; last ctrl is addBtn
        if (N > 0) {
            const int iconSz  = S(40);
            const int labelH  = S(16);
            const int cellW   = iconSz;
            const int cellH   = iconSz + S(4) + labelH;
            const int hGap    = S(12);
            const int vGap    = S(8);
            int iconsPerRow   = std::max(1, (s_ddEW + hGap) / (cellW + hGap));
            for (int i = 0; i < N; i++) {
                int row = i / iconsPerRow;
                int col = i % iconsPerRow;
                int rowStart     = row * iconsPerRow;
                int itemsInRow   = std::min(iconsPerRow, N - rowStart);
                int rowTotalW    = itemsInRow * cellW + (itemsInRow - 1) * hGap;
                int rowX0        = s_ddLX + (s_ddEW - rowTotalW) / 2;
                int cx = rowX0 + col * (cellW + hGap);
                int cy = y + row * (cellH + vGap);
                SetWindowPos(ctrls[i * 2],     NULL, cx, cy - s_depDlgScrollY,
                    iconSz, iconSz, SWP_NOZORDER | SWP_NOACTIVATE | SWP_SHOWWINDOW);
                SetWindowPos(ctrls[i * 2 + 1], NULL, cx, cy + iconSz + S(4) - s_depDlgScrollY,
                    cellW, labelH, SWP_NOZORDER | SWP_NOACTIVATE | SWP_SHOWWINDOW);
            }
            int rows = (N + iconsPerRow - 1) / iconsPerRow;
            y += rows * (cellH + vGap) - vGap + S(DD_GAP_SM);
        }
        // Add button — always centred at the bottom of the section
        { HWND h = ctrls.back();
          RECT rc; GetWindowRect(h, &rc);
          int bw = rc.right - rc.left;
          int bx = s_ddLX + (s_ddEW - bw) / 2;
          SetWindowPos(h, NULL, bx, y - s_depDlgScrollY, bw, S(DD_BTN_H),
              SWP_NOZORDER | SWP_NOACTIVATE);
          y += S(DD_BTN_H) + S(DD_GAP); }
    }

    // ── Place OK / Cancel at the bottom ──────────────────────────────────────
    y += S(DD_GAP);  // a little breathing room before buttons
    int btnAreaW = s_btnOK_W + S(DD_BTN_GAP) + s_btnCancel_W;
    int btnX     = s_ddLX + (s_ddEW - btnAreaW) / 2;
    SetWindowPos(s_hBtnOK,     NULL, btnX,                        y - s_depDlgScrollY,
        s_btnOK_W,     S(DD_BTN_H), SWP_NOZORDER | SWP_NOACTIVATE);
    SetWindowPos(s_hBtnCancel, NULL, btnX + s_btnOK_W + S(DD_BTN_GAP), y - s_depDlgScrollY,
        s_btnCancel_W, S(DD_BTN_H), SWP_NOZORDER | SWP_NOACTIVATE);
    y += S(DD_BTN_H) + S(DD_PAD_B);

    s_depDlgTotalH = y;

    // ── Resize the window to fit the new content ──────────────────────────────
    RECT rcWork; SystemParametersInfoW(SPI_GETWORKAREA, 0, &rcWork, 0);
    int maxH = (rcWork.bottom - rcWork.top) - S(30);

    const DWORD dlgStyle   = WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_VSCROLL;
    const DWORD dlgExStyle = WS_EX_DLGMODALFRAME;
    RECT rcNew = { 0, 0, S(DD_CONT_W) + S(DD_PAD_H) * 2, s_depDlgTotalH };
    AdjustWindowRectEx(&rcNew, dlgStyle, FALSE, dlgExStyle);
    int newDlgH = rcNew.bottom - rcNew.top;
    if (newDlgH > maxH) newDlgH = maxH;

    // Keep the window horizontally centred over its current position.
    RECT rcWin; GetWindowRect(hDlg, &rcWin);
    int dlgW = rcWin.right - rcWin.left;
    int dlgX = rcWin.left;
    int dlgY = rcWin.top;
    // Clamp to work area.
    if (dlgY + newDlgH > rcWork.bottom) dlgY = rcWork.bottom - newDlgH;
    if (dlgY < rcWork.top)              dlgY = rcWork.top;

    SetWindowPos(hDlg, NULL, dlgX, dlgY, dlgW, newDlgH,
        SWP_NOZORDER | SWP_NOACTIVATE);

    // ── Reconfigure scrollbar ─────────────────────────────────────────────────
    RECT rcC; GetClientRect(hDlg, &rcC);
    int pageH = rcC.bottom - rcC.top;
    // If the content now fits fully, scroll back to top.
    if (s_depDlgTotalH <= pageH && s_depDlgScrollY != 0) {
        int adj = s_depDlgScrollY; // move children down by this amount
        s_depDlgScrollY = 0;
        for (HWND hC = GetWindow(hDlg, GW_CHILD); hC; hC = GetWindow(hC, GW_HWNDNEXT)) {
            RECT r; GetWindowRect(hC, &r);
            POINT p = { r.left, r.top }; ScreenToClient(hDlg, &p);
            SetWindowPos(hC, NULL, p.x, p.y + adj, 0, 0,
                SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
        }
    }
    SCROLLINFO si = {};
    si.cbSize = sizeof(si);
    si.fMask  = SIF_RANGE | SIF_PAGE | SIF_POS;
    si.nMin   = 0;
    si.nMax   = (s_depDlgTotalH > pageH) ? s_depDlgTotalH - 1 : pageH;
    si.nPage  = (UINT)pageH;
    si.nPos   = s_depDlgScrollY;
    SetScrollInfo(hDlg, SB_VERT, &si, TRUE);

    InvalidateRect(hDlg, NULL, TRUE);
    UpdateWindow(hDlg);
}

// Custom message posted by an icon subclass to the dialog to remove an instruction.
#define WM_DEPINSTR_REMOVE (WM_USER + 42)

// ── Instructions icon subclass ────────────────────────────────────────────────
// Each document-icon STATIC gets an InstrIconCtx* in GWLP_USERDATA so it can
// track its own HICON, previous WndProc, and mouse-tracking flag independently.
struct InstrIconCtx {
    HICON   hIco     = NULL;
    WNDPROC prevProc = NULL;
    bool    tracking = false;
};

static LRESULT CALLBACK InstrIconSubclassProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    InstrIconCtx* ctx = (InstrIconCtx*)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
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
        if (ctx && ctx->hIco) DrawIconEx(hdc, 0, 0, ctx->hIco, rc.right, rc.bottom, 0, NULL, DI_NORMAL);
        EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_LBUTTONDBLCLK: {
        // Find which index this icon is in s_secInstructions.ctrls (icon at even positions)
        HWND hDlg = GetParent(hwnd);
        int instrIdx = -1;
        for (int i = 0; i + 1 < (int)s_secInstructions.ctrls.size() - 1; i += 2)
            if (s_secInstructions.ctrls[i] == hwnd) { instrIdx = i / 2; break; }
        if (instrIdx >= 0)
            PostMessageW(hDlg, WM_COMMAND, MAKEWPARAM(IDC_DEPDLG_EDIT_INSTR, BN_CLICKED),
                         (LPARAM)(INT_PTR)instrIdx);
        return 0;
    }
    case WM_RBUTTONUP: {
        // Find index of this icon
        int instrIdx = -1;
        for (int i = 0; i + 1 < (int)s_secInstructions.ctrls.size() - 1; i += 2)
            if (s_secInstructions.ctrls[i] == hwnd) { instrIdx = i / 2; break; }
        if (instrIdx < 0) return 0;
        DepDlgData* pd = (DepDlgData*)GetWindowLongPtrW(GetParent(hwnd), GWLP_USERDATA);
        std::wstring menuText = pd
            ? DL(pd, L"dep_dlg_instr_remove", L"Remove instructions")
            : L"Remove instructions";
        HMENU hMenu = CreatePopupMenu();
        AppendMenuW(hMenu, MF_STRING, 1, menuText.c_str());
        POINT pt; GetCursorPos(&pt);
        int cmd = (int)TrackPopupMenu(hMenu,
            TPM_RETURNCMD | TPM_RIGHTBUTTON | TPM_NONOTIFY,
            pt.x, pt.y, 0, hwnd, NULL);
        DestroyMenu(hMenu);
        if (cmd == 1)
            PostMessageW(GetParent(hwnd), WM_DEPINSTR_REMOVE, (WPARAM)instrIdx, 0);
        return 0;
    }
    case WM_MOUSEMOVE:
        if (!IsTooltipVisible()) {
            DepDlgData* pd = (DepDlgData*)GetWindowLongPtrW(GetParent(hwnd), GWLP_USERDATA);
            std::wstring tipEdit   = pd ? DL(pd, L"dep_dlg_instr_icon_tip",   L"Double-click to edit")   : L"Double-click to edit";
            std::wstring tipRemove = pd ? DL(pd, L"dep_dlg_instr_remove_tip", L"Right-click to remove") : L"Right-click to remove";
            std::vector<std::pair<std::wstring,std::wstring>> tip = {
                { L"", tipEdit + L"\n" + tipRemove }
            };
            RECT rc; GetWindowRect(hwnd, &rc);
            ShowMultilingualTooltip(tip, rc.left, rc.bottom + 5, GetParent(hwnd));
        }
        if (ctx && !ctx->tracking) {
            TRACKMOUSEEVENT tme = {};
            tme.cbSize = sizeof(tme); tme.dwFlags = TME_LEAVE; tme.hwndTrack = hwnd;
            TrackMouseEvent(&tme);
            ctx->tracking = true;
        }
        break;
    case WM_MOUSELEAVE:
        HideTooltip();
        if (ctx) ctx->tracking = false;
        break;
    case WM_NCDESTROY:
        if (ctx) {
            if (ctx->hIco) DestroyIcon(ctx->hIco);
            WNDPROC prev = ctx->prevProc;
            delete ctx;
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
            SetWindowLongPtrW(hwnd, GWLP_WNDPROC, (LONG_PTR)prev);
            return CallWindowProcW(prev, hwnd, msg, wParam, lParam);
        }
        return 0;
    }
    return CallWindowProcW(ctx ? ctx->prevProc : DefWindowProcW, hwnd, msg, wParam, lParam);
}

// ── Component picker dialog ───────────────────────────────────────────────────
// A small modal popup that lists all project components in an extended-selection
// listbox. The developer picks which components this dep is linked to; an empty
// selection means "install unconditionally" (the default).
//
// Control IDs are local to this dialog class and do not conflict with anything.
#define IDC_PICKCOMP_LIST   2001
#define IDC_PICKCOMP_OK     2002
#define IDC_PICKCOMP_CANCEL 2003

struct PickCompDlgData {
    const std::vector<std::wstring>*              compNames;
    const std::map<std::wstring,std::wstring>*    pLocale;
    HFONT        hFont     = NULL;
    HINSTANCE    hInst     = NULL;
    std::wstring initial;       // current space-separated value
    std::wstring result;        // space-separated on OK
    bool         okPressed = false;
};

// Returns true if name is contained in a space-separated list.
static bool IsInSpaceSep(const wchar_t* name, const std::wstring& list)
{
    const wchar_t* p = list.c_str();
    while (*p) {
        while (*p == L' ') p++;
        const wchar_t* start = p;
        while (*p && *p != L' ') p++;
        if (p != start && std::wstring(start, p - start) == name) return true;
    }
    return false;
}

static LRESULT CALLBACK PickCompDlgProc(HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam)
{
    PickCompDlgData* pd = (PickCompDlgData*)GetWindowLongPtrW(hDlg, GWLP_USERDATA);

    switch (msg) {
    case WM_CREATE: {
        CREATESTRUCTW* cs = (CREATESTRUCTW*)lParam;
        pd = (PickCompDlgData*)cs->lpCreateParams;
        SetWindowLongPtrW(hDlg, GWLP_USERDATA, (LONG_PTR)pd);

        RECT rc; GetClientRect(hDlg, &rc);
        const int cW    = rc.right;
        const int cH    = rc.bottom;
        const int pad   = S(10);
        const int btnH  = S(30);
        const int btnW  = S(80);
        const int listH = cH - pad * 3 - btnH;

        HWND hList = CreateWindowExW(WS_EX_CLIENTEDGE, L"LISTBOX", NULL,
            WS_CHILD | WS_VISIBLE | WS_VSCROLL | WS_TABSTOP |
            LBS_EXTENDEDSEL | LBS_NOINTEGRALHEIGHT,
            pad, pad, cW - pad * 2, listH,
            hDlg, (HMENU)(UINT_PTR)IDC_PICKCOMP_LIST, pd->hInst, NULL);
        if (pd->hFont) SendMessageW(hList, WM_SETFONT, (WPARAM)pd->hFont, TRUE);

        // Populate and pre-select
        for (const std::wstring& name : *pd->compNames) {
            int idx = (int)SendMessageW(hList, LB_ADDSTRING, 0, (LPARAM)name.c_str());
            if (idx >= 0 && IsInSpaceSep(name.c_str(), pd->initial))
                SendMessageW(hList, LB_SETSEL, TRUE, (LPARAM)idx);
        }

        // OK / Cancel buttons
        auto LF = [&](const wchar_t* key, const wchar_t* fb) -> std::wstring {
            auto it = pd->pLocale->find(key);
            return (it != pd->pLocale->end()) ? it->second : fb;
        };
        int bY = listH + pad * 2;
        int bX = (cW - (btnW * 2 + pad)) / 2;

        HWND hOk = CreateWindowExW(0, L"BUTTON", LF(L"ok", L"OK").c_str(),
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
            bX, bY, btnW, btnH,
            hDlg, (HMENU)(UINT_PTR)IDC_PICKCOMP_OK, pd->hInst, NULL);
        HWND hCnl = CreateWindowExW(0, L"BUTTON", LF(L"cancel", L"Cancel").c_str(),
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
            bX + btnW + pad, bY, btnW, btnH,
            hDlg, (HMENU)(UINT_PTR)IDC_PICKCOMP_CANCEL, pd->hInst, NULL);
        if (pd->hFont) {
            SendMessageW(hOk,  WM_SETFONT, (WPARAM)pd->hFont, TRUE);
            SendMessageW(hCnl, WM_SETFONT, (WPARAM)pd->hFont, TRUE);
        }
        return 0;
    }
    case WM_COMMAND: {
        int wId = LOWORD(wParam);
        if (wId == IDC_PICKCOMP_OK) {
            HWND hList = GetDlgItem(hDlg, IDC_PICKCOMP_LIST);
            std::wstring res;
            int n = (int)SendMessageW(hList, LB_GETCOUNT, 0, 0);
            for (int i = 0; i < n; i++) {
                if (SendMessageW(hList, LB_GETSEL, (WPARAM)i, 0) > 0) {
                    wchar_t buf[512] = {};
                    SendMessageW(hList, LB_GETTEXT, (WPARAM)i, (LPARAM)buf);
                    if (!res.empty()) res += L' ';
                    res += buf;
                }
            }
            pd->result    = res;
            pd->okPressed = true;
            DestroyWindow(hDlg);
            return 0;
        }
        if (wId == IDC_PICKCOMP_CANCEL) {
            DestroyWindow(hDlg);
            return 0;
        }
        return 0;
    }
    case WM_KEYDOWN:
        if (wParam == VK_ESCAPE)  { DestroyWindow(hDlg); return 0; }
        if (wParam == VK_RETURN)  {
            SendMessageW(hDlg, WM_COMMAND,
                MAKEWPARAM(IDC_PICKCOMP_OK, BN_CLICKED), 0);
            return 0;
        }
        break;
    case WM_CLOSE:
        DestroyWindow(hDlg);
        return 0;
    case WM_DESTROY:
        return 0;
    }
    return DefWindowProcW(hDlg, msg, wParam, lParam);
}

// Opens the component picker modal.  Returns true if user pressed OK,
// and sets outResult to the new space-separated component string.
static bool RunPickCompDialog(HWND hwndParent, HINSTANCE hInst,
    const std::wstring& current,
    const std::vector<std::wstring>& compNames,
    const std::map<std::wstring,std::wstring>& locale,
    std::wstring& outResult)
{
    static bool s_pickClassReg = false;
    if (!s_pickClassReg) {
        WNDCLASSEXW wc = {};
        wc.cbSize        = sizeof(wc);
        wc.lpfnWndProc   = PickCompDlgProc;
        wc.hInstance     = hInst;
        wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
        wc.lpszClassName = L"DepPickCompDlg";
        wc.hCursor       = LoadCursor(NULL, IDC_ARROW);
        RegisterClassExW(&wc);
        s_pickClassReg = true;
    }

    // Build font
    NONCLIENTMETRICSW ncm = {}; ncm.cbSize = sizeof(ncm);
    SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(ncm), &ncm, 0);
    if (ncm.lfMessageFont.lfHeight < 0)
        ncm.lfMessageFont.lfHeight = (LONG)(ncm.lfMessageFont.lfHeight * 1.2f);
    HFONT hFont = CreateFontIndirectW(&ncm.lfMessageFont);

    // Size: listbox to fit up to 8 items, capped at S(200).
    const int nItems = (int)compNames.size();
    const int itemH  = S(18);
    const int listH  = std::min(nItems * itemH + S(8), S(200));
    const int pad    = S(10);
    const int btnH   = S(30);
    const int cW     = S(320);
    const int cH     = listH + pad * 3 + btnH;

    auto it = locale.find(L"dep_dlg_comp_pick_title");
    std::wstring title = (it != locale.end()) ? it->second : L"Select components";

    PickCompDlgData data;
    data.compNames = &compNames;
    data.pLocale   = &locale;
    data.hFont     = hFont;
    data.hInst     = hInst;
    data.initial   = current;

    const DWORD style   = WS_POPUP | WS_CAPTION | WS_SYSMENU;
    const DWORD exStyle = WS_EX_DLGMODALFRAME;
    RECT rcAdj = { 0, 0, cW, cH };
    AdjustWindowRectEx(&rcAdj, style, FALSE, exStyle);
    int dlgW = rcAdj.right - rcAdj.left;
    int dlgH = rcAdj.bottom - rcAdj.top;

    RECT rcP; GetWindowRect(hwndParent, &rcP);
    int dlgX = rcP.left + (rcP.right  - rcP.left  - dlgW) / 2;
    int dlgY = rcP.top  + (rcP.bottom - rcP.top   - dlgH) / 2;

    HWND hDlg = CreateWindowExW(exStyle, L"DepPickCompDlg", title.c_str(),
        style, dlgX, dlgY, dlgW, dlgH,
        hwndParent, NULL, hInst, &data);
    if (!hDlg) { if (hFont) DeleteObject(hFont); return false; }

    EnableWindow(hwndParent, FALSE);
    ShowWindow(hDlg, SW_SHOW);
    UpdateWindow(hDlg);

    MSG msg = {};
    while (IsWindow(hDlg)) {
        BOOL bRet = GetMessageW(&msg, NULL, 0, 0);
        if (bRet == 0) { PostQuitMessage((int)msg.wParam); break; }
        if (bRet == -1) break;
        if (!IsWindow(hDlg)) break;
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    EnableWindow(hwndParent, TRUE);
    SetForegroundWindow(hwndParent);
    if (hFont) DeleteObject(hFont);

    if (data.okPressed) { outResult = data.result; return true; }
    return false;
}

// ── Dialog proc ───────────────────────────────────────────────────────────────

static LRESULT CALLBACK DepDlgProc(HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam)
{
    DepDlgData* pData = (DepDlgData*)GetWindowLongPtrW(hDlg, GWLP_USERDATA);

    switch (msg) {
    case WM_CREATE: {
        // lParam = CREATESTRUCT*, whose lpCreateParams = DepDlgData*
        CREATESTRUCTW* cs = (CREATESTRUCTW*)lParam;
        pData = (DepDlgData*)cs->lpCreateParams;
        SetWindowLongPtrW(hDlg, GWLP_USERDATA, (LONG_PTR)pData);
        return 0;
    }

    case WM_DRAWITEM: {
        LPDRAWITEMSTRUCT dis = (LPDRAWITEMSTRUCT)lParam;
        if (DrawCustomCheckbox(dis)) return TRUE;
        if (dis->CtlID == IDC_DEPDLG_OK || dis->CtlID == IDC_DEPDLG_CANCEL ||
            dis->CtlID == IDC_DEPDLG_EDIT_LIC || dis->CtlID == IDC_DEPDLG_EDIT_INSTR ||
            dis->CtlID == IDC_DEPDLG_EXIT_CODES_HELP) {
            ButtonColor color = (ButtonColor)GetWindowLongPtr(dis->hwndItem, GWLP_USERDATA);
            HFONT hFont = CreateFontW(-S(12), 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
                DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
            LRESULT r = DrawCustomButton(dis, color, hFont);
            if (hFont) DeleteObject(hFont);
            return r;
        }
        return FALSE;
    }

    case WM_COMMAND: {
        int wmId    = LOWORD(wParam);
        int wmEvent = HIWORD(wParam);

        if (wmId == IDC_DEPDLG_DELIVERY && wmEvent == CBN_SELCHANGE) {
            int choice = GetDeliveryChoice(hDlg);
            Reflow(hDlg, choice);
            return 0;
        }

        if (wmId == IDC_DEPDLG_EXIT_CODES_HELP && wmEvent == BN_CLICKED) {
            // Show the common exit codes reference list.
            std::wstring title = DL(pData, L"dep_dlg_exit_codes_ref_title", L"Common Exit Codes");
            std::wstring body  = DL(pData, L"dep_dlg_exit_codes_ref_body",
                L"Reboot codes (safe to add):\n"
                L"  3010 \u2014 Reboot required (success)\n"
                L"  1641 \u2014 Reboot initiated (success)\n"
                L"\n"
                L"Other common codes (reference):\n"
                L"  1 \u2014 General error\n"
                L"  2 \u2014 File not found\n"
                L"  5 \u2014 Access denied\n"
                L"  1602 \u2014 User cancelled\n"
                L"  1603 \u2014 Fatal install error\n"
                L"  1618 \u2014 Another install running\n"
                L"  1638 \u2014 Another version installed\n"
                L"\n"
                L"0 is always success. For 3010/1641,\n"
                L"SetupCraft will warn before reboot.");
            ShowValidationDialog(hDlg, title, body, *pData->pLocale);
            return 0;
        }

        if (wmId == IDC_DEPDLG_COMP_PICK && wmEvent == BN_CLICKED) {
            if (!pData || pData->compNames.empty()) return 0;
            std::wstring current = GetEditText(hDlg, IDC_DEPDLG_COMP_EDIT);
            std::wstring result;
            if (RunPickCompDialog(hDlg, pData->hInst, current,
                                  pData->compNames, *pData->pLocale, result))
                SetDlgItemTextW(hDlg, IDC_DEPDLG_COMP_EDIT, result.c_str());
            return 0;
        }

        if (wmId == IDC_DEPDLG_EDIT_LIC && wmEvent == BN_CLICKED) {
            RtfEditorData ed;
            ed.initRtf    = s_depLicRtf;
            ed.titleText  = DL(pData, L"dep_section_license", L"License");
            ed.okText     = DL(pData, L"ok",     L"OK");
            ed.cancelText = DL(pData, L"cancel", L"Cancel");
            ed.preferredW = S(880);
            ed.preferredH = S(560);
            ed.pLocale    = pData->pLocale;
            if (OpenRtfEditor(hDlg, ed)) {
                s_depLicRtf = ed.outRtf;
                SetDlgItemTextW(hDlg, IDC_DEPDLG_LIC_PATH,
                    s_depLicRtf.empty()
                    ? DL(pData, L"dep_lic_none",        L"(no license)").c_str()
                    : DL(pData, L"dep_lic_has_content", L"(formatted text \u2014 click Edit\u2026 to view)").c_str());
            }
            return 0;
        }

        if (wmId == IDC_DEPDLG_EDIT_INSTR && wmEvent == BN_CLICKED) {
            // lParam encodes who triggered this:
            //   >= 0  : index of an existing instruction (double-click on its icon)
            //   -1 (button HWND cast misinterpretation handled below): Add button
            // We distinguish by sign: PostMessage from icon uses a non-HWND small int.
            INT_PTR lpVal = (INT_PTR)lParam;
            bool fromIcon = (lpVal >= 0 && lpVal < (INT_PTR)s_depInstrList.size());
            int  editIdx  = fromIcon ? (int)lpVal : -1;

            RtfEditorData ed;
            ed.initRtf    = fromIcon ? s_depInstrList[editIdx] : L"";
            ed.titleText  = DL(pData, L"dep_section_instructions", L"Manual install instructions");
            ed.okText     = DL(pData, L"ok",     L"OK");
            ed.cancelText = DL(pData, L"cancel", L"Cancel");
            ed.preferredW = S(880);
            ed.preferredH = S(560);
            ed.pLocale    = pData->pLocale;
            if (OpenRtfEditor(hDlg, ed) && !ed.outRtf.empty()) {
                if (fromIcon) {
                    // Replace existing page in-place (icon stays, no layout change)
                    s_depInstrList[editIdx] = ed.outRtf;
                } else {
                    // Append new page; create icon+label controls dynamically
                    s_depInstrList.push_back(ed.outRtf);
                    int newIdx = (int)s_depInstrList.size() - 1;
                    int iconSz = S(40);
                    wchar_t shell32Path[MAX_PATH];
                    GetSystemDirectoryW(shell32Path, MAX_PATH);
                    wcscat_s(shell32Path, L"\\shell32.dll");
                    HWND hIco = CreateWindowExW(0, L"STATIC", NULL,
                        WS_CHILD | SS_NOTIFY,
                        0, 0, iconSz, iconSz,
                        hDlg, (HMENU)(UINT_PTR)(IDC_DEPDLG_INSTR_ICON + newIdx), pData->hInst, NULL);
                    if (hIco) {
                        InstrIconCtx* ctx = new InstrIconCtx();
                        PrivateExtractIconsW(shell32Path, 70, iconSz, iconSz, &ctx->hIco, NULL, 1, 0);
                        if (!ctx->hIco) ExtractIconExW(shell32Path, 70, &ctx->hIco, NULL, 1);
                        ctx->prevProc = (WNDPROC)SetWindowLongPtrW(
                            hIco, GWLP_WNDPROC, (LONG_PTR)InstrIconSubclassProc);
                        SetWindowLongPtrW(hIco, GWLP_USERDATA, (LONG_PTR)ctx);
                    }
                    std::wstring numStr = std::to_wstring(newIdx + 1);
                    HWND hLbl = CreateWindowExW(0, L"STATIC", numStr.c_str(),
                        WS_CHILD | SS_CENTER,
                        0, 0, S(40), S(16),
                        hDlg, NULL, pData->hInst, NULL);
                    // Bold body font for the number label
                    NONCLIENTMETRICSW ncmD = {}; ncmD.cbSize = sizeof(ncmD);
                    SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(ncmD), &ncmD, 0);
                    LOGFONTW lfB = ncmD.lfMessageFont; lfB.lfWeight = FW_BOLD;
                    HFONT hFB = CreateFontIndirectW(&lfB);
                    if (hLbl && hFB) SendMessageW(hLbl, WM_SETFONT, (WPARAM)hFB, FALSE);
                    // Insert before the Add button (last element)
                    auto& ctrls = s_secInstructions.ctrls;
                    ctrls.insert(ctrls.end() - 1, hIco);
                    ctrls.insert(ctrls.end() - 1, hLbl);
                    Reflow(hDlg, GetDeliveryChoice(hDlg));
                }
            }
            return 0;
        }

        if (wmId == IDC_DEPDLG_OK && wmEvent == BN_CLICKED) {
            // Validate: display name is required.
            std::wstring name = GetEditText(hDlg, IDC_DEPDLG_NAME);
            if (name.empty()) {
                std::wstring err = L"Please enter a display name for this dependency.";
                std::wstring ttl = L"Validation Error";
                if (pData) {
                    auto it = pData->pLocale->find(L"dep_err_no_name");
                    if (it != pData->pLocale->end()) err = it->second;
                    auto it2 = pData->pLocale->find(L"validation_error");
                    if (it2 != pData->pLocale->end()) ttl = it2->second;
                }
                ShowValidationDialog(hDlg, ttl, err, pData ? *pData->pLocale : std::map<std::wstring,std::wstring>{});
                SetFocus(GetDlgItem(hDlg, IDC_DEPDLG_NAME));
                return 0;
            }

            // Validate: a real delivery type must be chosen.
            int deliveryVal = GetDeliveryChoice(hDlg);
            if (deliveryVal == DD_DELIVERY_NONE) {
                std::wstring err = L"Please choose a delivery type.";
                std::wstring ttl = L"Validation Error";
                if (pData) {
                    auto it = pData->pLocale->find(L"dep_err_no_delivery");
                    if (it != pData->pLocale->end()) err = it->second;
                    auto it2 = pData->pLocale->find(L"validation_error");
                    if (it2 != pData->pLocale->end()) ttl = it2->second;
                }
                ShowValidationDialog(hDlg, ttl, err, pData ? *pData->pLocale : std::map<std::wstring,std::wstring>{});
                SetFocus(GetDlgItem(hDlg, IDC_DEPDLG_DELIVERY));
                return 0;
            }

            if (pData) {
                pData->dep.display_name     = name;
                pData->dep.delivery         = (DepDelivery)deliveryVal;
                pData->dep.is_required      = (SendDlgItemMessageW(hDlg, IDC_DEPDLG_REQUIRED,
                                                BM_GETCHECK, 0, 0) == BST_CHECKED);
                pData->dep.offline_behavior = (DepOffline)GetComboSel(hDlg, IDC_DEPDLG_OFFLINE);

                // Install order: read item data from selected combo item; sentinel (-1) is valid.
                { HWND hOrd = GetDlgItem(hDlg, IDC_DEPDLG_INSTALL_ORDER);
                  int sel = (int)SendMessageW(hOrd, CB_GETCURSEL, 0, 0);
                  pData->dep.install_order = (sel >= 0)
                      ? (int)SendMessageW(hOrd, CB_GETITEMDATA, (WPARAM)sel, 0)
                      : (int)DIO_UNSPECIFIED; }

                pData->dep.url              = GetEditText(hDlg, IDC_DEPDLG_URL);
                pData->dep.silent_args      = GetEditText(hDlg, IDC_DEPDLG_SILENT_ARGS);
                pData->dep.sha256           = GetEditText(hDlg, IDC_DEPDLG_SHA256);
                pData->dep.download_timeout_sec = _wtoi(GetEditText(hDlg, IDC_DEPDLG_TIMEOUT).c_str());
                pData->dep.extra_exit_codes     = GetEditText(hDlg, IDC_DEPDLG_EXIT_CODES);
                pData->dep.detect_reg_key   = GetEditText(hDlg, IDC_DEPDLG_DETECT_REG);
                pData->dep.detect_file_path = GetEditText(hDlg, IDC_DEPDLG_DETECT_FILE);
                pData->dep.min_version      = GetEditText(hDlg, IDC_DEPDLG_MIN_VER);
                pData->dep.max_version      = GetEditText(hDlg, IDC_DEPDLG_MAX_VER);
                pData->dep.required_components = GetEditText(hDlg, IDC_DEPDLG_COMP_EDIT);
                pData->dep.instructions_list = s_depInstrList;
                pData->dep.license_text     = s_depLicRtf;
                pData->dep.license_path     = L"";
                pData->dep.credits_text     = GetEditText(hDlg, IDC_DEPDLG_CREDITS);
                pData->okPressed            = true;
            }
            s_depDlgOk = true;
            DestroyWindow(hDlg);
            return 0;
        }

        if (wmId == IDC_DEPDLG_CANCEL && wmEvent == BN_CLICKED) {
            s_depDlgOk = false;
            DestroyWindow(hDlg);
            return 0;
        }
        return 0;
    }

    case WM_VSCROLL: {
        SCROLLINFO si = {};
        si.cbSize = sizeof(si);
        si.fMask  = SIF_ALL;
        GetScrollInfo(hDlg, SB_VERT, &si);
        int oldPos = si.nPos;
        switch (LOWORD(wParam)) {
            case SB_LINEUP:        si.nPos -= S(20);        break;
            case SB_LINEDOWN:      si.nPos += S(20);        break;
            case SB_PAGEUP:        si.nPos -= (int)si.nPage; break;
            case SB_PAGEDOWN:      si.nPos += (int)si.nPage; break;
            case SB_THUMBTRACK:
            case SB_THUMBPOSITION: si.nPos  = si.nTrackPos;  break;
            default: break;
        }
        si.fMask = SIF_POS;
        SetScrollInfo(hDlg, SB_VERT, &si, TRUE);
        GetScrollInfo(hDlg, SB_VERT, &si);  // get clamped position
        int delta = oldPos - si.nPos;
        if (delta) {
            s_depDlgScrollY = si.nPos;
            // Move ALL children (including hidden) so positions stay consistent.
            for (HWND hC = GetWindow(hDlg, GW_CHILD); hC; hC = GetWindow(hC, GW_HWNDNEXT)) {
                RECT rc; GetWindowRect(hC, &rc);
                POINT pt = { rc.left, rc.top }; ScreenToClient(hDlg, &pt);
                SetWindowPos(hC, NULL, pt.x, pt.y + delta, 0, 0,
                    SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOREDRAW);
            }
            InvalidateRect(hDlg, NULL, TRUE);
            UpdateWindow(hDlg);
        }
        return 0;
    }

    case WM_MOUSEWHEEL: {
        int cmd = (GET_WHEEL_DELTA_WPARAM(wParam) > 0) ? SB_LINEUP : SB_LINEDOWN;
        for (int i = 0; i < 3; i++)
            SendMessageW(hDlg, WM_VSCROLL, cmd, 0);
        return 0;
    }

    case WM_CTLCOLORSTATIC: {
        HDC hdc = (HDC)wParam;
        SetBkColor  (hdc, GetSysColor(COLOR_WINDOW));
        SetTextColor(hdc, GetSysColor(COLOR_WINDOWTEXT));
        return (LRESULT)GetSysColorBrush(COLOR_WINDOW);
    }

    default:
        // Remove an instruction page (posted by right-click context menu on an icon)
        if ((UINT)msg == WM_DEPINSTR_REMOVE) {
            int instrIdx = (int)wParam;
            int N = (int)s_depInstrList.size();
            if (instrIdx < 0 || instrIdx >= N) return 0;
            s_depInstrList.erase(s_depInstrList.begin() + instrIdx);
            auto& ctrls = s_secInstructions.ctrls;
            int base = instrIdx * 2;
            DestroyWindow(ctrls[base]);     // icon  (WM_NCDESTROY frees InstrIconCtx)
            DestroyWindow(ctrls[base + 1]); // label
            ctrls.erase(ctrls.begin() + base, ctrls.begin() + base + 2);
            // Renumber remaining labels
            for (int i = instrIdx; i < (int)s_depInstrList.size(); i++)
                SetWindowTextW(ctrls[i * 2 + 1], std::to_wstring(i + 1).c_str());
            Reflow(hDlg, GetDeliveryChoice(hDlg));
            return 0;
        }
        break;

    case WM_KEYDOWN:
        if (wParam == VK_ESCAPE) {
            s_depDlgOk = false;
            DestroyWindow(hDlg);
            return 0;
        }
        break;

    case WM_CLOSE:
        s_depDlgOk = false;
        DestroyWindow(hDlg);
        return 0;

    case WM_DESTROY:
        return 0;
    }
    return DefWindowProcW(hDlg, msg, wParam, lParam);
}

// ── DEP_EditDialog ────────────────────────────────────────────────────────────

bool DEP_EditDialog(HWND hwndParent, HINSTANCE hInst,
                    const std::map<std::wstring, std::wstring>& locale,
                    ExternalDep& dep,
                    const std::vector<std::wstring>& compNames)
{
    s_depDlgOk    = false;
    s_depInstrList = dep.instructions_list;
    s_depLicRtf   = dep.license_text;

    DepDlgData data;
    data.dep       = dep;
    data.hInst     = hInst;
    data.pLocale   = &locale;
    data.okPressed = false;
    data.compNames = compNames;

    // ── Register window class (once) ──────────────────────────────────────────
    static bool s_classRegistered = false;
    if (!s_classRegistered) {
        WNDCLASSEXW wc = {};
        wc.cbSize        = sizeof(wc);
        wc.style         = CS_HREDRAW | CS_VREDRAW;
        wc.lpfnWndProc   = DepDlgProc;
        wc.hInstance     = hInst;
        wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
        wc.lpszClassName = L"DepEditDialog";
        wc.hCursor       = LoadCursor(NULL, IDC_ARROW);
        RegisterClassExW(&wc);
        s_classRegistered = true;
    }

    // ── Measure the initial (compact) window height ───────────────────────────
    // Initial view shows only: title + name row + delivery row + buttons.
    // All other sections are created hidden and revealed by Reflow() later.
    int contentH = DD_PAD_T;
    contentH += DD_LABEL_H + DD_GAP;                          // title
    contentH += DD_LABEL_H + DD_GAP_SM + DD_EDIT_H  + DD_GAP; // name
    contentH += DD_LABEL_H + DD_GAP_SM + DD_COMBO_H + DD_GAP; // delivery
    contentH += DD_GAP + DD_BTN_H + DD_PAD_B;                 // buttons

    s_depDlgScrollY = 0;
    s_depDlgTotalH  = S(contentH);

    const DWORD dlgStyle   = WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_VSCROLL;
    const DWORD dlgExStyle = WS_EX_DLGMODALFRAME;

    RECT rc = { 0, 0, S(DD_CONT_W) + S(DD_PAD_H) * 2, S(contentH) };
    AdjustWindowRectEx(&rc, dlgStyle, FALSE, dlgExStyle);
    int dlgW = rc.right  - rc.left;
    int dlgH = rc.bottom - rc.top;

    // Clamp and centre over parent.
    RECT rcWork; SystemParametersInfoW(SPI_GETWORKAREA, 0, &rcWork, 0);
    int maxH = (rcWork.bottom - rcWork.top) - S(30);
    if (dlgH > maxH) dlgH = maxH;
    RECT rcParent; GetWindowRect(hwndParent, &rcParent);
    int dlgX = rcParent.left + (rcParent.right  - rcParent.left - dlgW) / 2;
    int dlgY = rcParent.top + S(3);
    if (dlgX < rcWork.left)          dlgX = rcWork.left;
    if (dlgY < rcWork.top)           dlgY = rcWork.top;
    if (dlgX + dlgW > rcWork.right)  dlgX = rcWork.right  - dlgW;
    if (dlgY + dlgH > rcWork.bottom) dlgY = rcWork.bottom - dlgH;

    bool isNewDep = (dep.id == 0);
    auto it = locale.find(isNewDep ? L"dep_dialog_add_title" : L"dep_dialog_title");
    std::wstring dlgTitle = (it != locale.end()) ? it->second
                          : (isNewDep ? L"Add Dependency" : L"Edit Dependency");

    HWND hDlg = CreateWindowExW(
        dlgExStyle, L"DepEditDialog", dlgTitle.c_str(),
        dlgStyle, dlgX, dlgY, dlgW, dlgH,
        hwndParent, NULL, hInst, &data);
    if (!hDlg) return false;

    // ── Build all controls ────────────────────────────────────────────────────
    RECT rcClient; GetClientRect(hDlg, &rcClient);
    int cW = rcClient.right - rcClient.left;
    s_ddLX = S(DD_PAD_H);
    s_ddEW = cW - s_ddLX * 2;
    int y  = S(DD_PAD_T);

    // Initialise section structs fresh for this invocation.
    s_secRequired     = {};
    s_secOrder        = {};
    s_secDetection    = {};
    s_secNetAll       = {};
    s_secExitCodes    = {};
    s_secCompLink     = {};
    s_secLicense      = {};
    s_secInstructions = {};

    // Use the same scaled fonts as the rest of the application.
    // Body: lfMessageFont * 1.2; Title: lfMessageFont * 1.5, semi-bold.
    // (Matches mainwindow.cpp s_scaledFont / s_hPageTitleFont construction.)
    NONCLIENTMETRICSW ncm = {}; ncm.cbSize = sizeof(ncm);
    SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(ncm), &ncm, 0);
    if (ncm.lfMessageFont.lfHeight < 0)
        ncm.lfMessageFont.lfHeight = (LONG)(ncm.lfMessageFont.lfHeight * 1.2f);
    ncm.lfMessageFont.lfQuality = CLEARTYPE_QUALITY;
    ncm.lfMessageFont.lfCharSet = DEFAULT_CHARSET;

    auto LS = [&](const wchar_t* key, const wchar_t* fb) -> std::wstring {
        auto it2 = locale.find(key); return it2 != locale.end() ? it2->second : fb;
    };

    // Creates a STATIC label at current y (visible, full width).
    auto MkLabel = [&](const std::wstring& text, bool visible = true) -> HWND {
        DWORD vis = visible ? WS_VISIBLE : 0;
        return CreateWindowExW(0, L"STATIC", text.c_str(),
            WS_CHILD | vis | SS_LEFT,
            s_ddLX, y, s_ddEW, S(DD_LABEL_H), hDlg, NULL, hInst, NULL);
    };
    // Creates a single-line EDIT at current y.
    auto MkEdit = [&](int id, const std::wstring& txt, bool visible = true, bool ro = false) -> HWND {
        DWORD vis = visible ? WS_VISIBLE : 0;
        DWORD st  = WS_CHILD | vis | WS_TABSTOP | WS_BORDER | ES_AUTOHSCROLL;
        if (ro) st |= ES_READONLY;
        return CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", txt.c_str(), st,
            s_ddLX, y, s_ddEW, S(DD_EDIT_H), hDlg, (HMENU)(UINT_PTR)id, hInst, NULL);
    };
    // Creates a CBS_DROPDOWNLIST combo at current y.
    auto MkCombo = [&](int id, bool visible = true) -> HWND {
        DWORD vis = visible ? WS_VISIBLE : 0;
        return CreateWindowExW(0, L"COMBOBOX", L"",
            WS_CHILD | vis | WS_TABSTOP | CBS_DROPDOWNLIST,
            s_ddLX, y, s_ddEW, S(DD_COMBO_H) * 6, hDlg, (HMENU)(UINT_PTR)id, hInst, NULL);
    };
    // Creates a bold section-header STATIC at current y.
    auto MkSection = [&](const std::wstring& text, bool visible = true) -> HWND {
        DWORD vis = visible ? WS_VISIBLE : 0;
        HWND hw = CreateWindowExW(0, L"STATIC", text.c_str(),
            WS_CHILD | vis | SS_LEFT,
            s_ddLX, y, s_ddEW, S(DD_MLABEL_H), hDlg, NULL, hInst, NULL);
        LOGFONTW lfBold = ncm.lfMessageFont;
        lfBold.lfWeight = FW_BOLD;
        HFONT hBold = CreateFontIndirectW(&lfBold);
        SendMessageW(hw, WM_SETFONT, (WPARAM)hBold, TRUE);
        return hw;
    };

    HFONT hFont = CreateFontIndirectW(&ncm.lfMessageFont);
    auto SF = [&](HWND hw) { if (hw && hFont) SendMessageW(hw, WM_SETFONT, (WPARAM)hFont, TRUE); };
    LOGFONTW lfBoldBody = ncm.lfMessageFont; lfBoldBody.lfWeight = FW_BOLD;
    HFONT hFontBold = CreateFontIndirectW(&lfBoldBody);
    auto SFBold = [&](HWND hw) { if (hw && hFontBold) SendMessageW(hw, WM_SETFONT, (WPARAM)hFontBold, TRUE); };

    // ── Dialog title (always visible) ─────────────────────────────────────────
    {
        // Title font: same family, ~150 % of body size, semi-bold — matches s_hPageTitleFont.
        LOGFONTW lfTitle = ncm.lfMessageFont; // already * 1.2
        if (lfTitle.lfHeight < 0)
            lfTitle.lfHeight = (LONG)(lfTitle.lfHeight * (1.5f / 1.2f)); // bring to * 1.5 overall
        lfTitle.lfWeight = FW_SEMIBOLD;
        HFONT hBig = CreateFontIndirectW(&lfTitle);
        HWND hT = CreateWindowExW(0, L"STATIC", dlgTitle.c_str(),
            WS_CHILD | WS_VISIBLE | SS_LEFT,
            s_ddLX, y, s_ddEW, S(DD_TITLE_H), hDlg, NULL, hInst, NULL);
        SendMessageW(hT, WM_SETFONT, (WPARAM)hBig, TRUE);
        y += S(DD_TITLE_H) + S(DD_GAP);
    }

    // ── Display name (always visible) ─────────────────────────────────────────
    SF(MkLabel(LS(L"dep_dlg_name", L"Display name:")));
    y += S(DD_LABEL_H) + S(DD_GAP_SM);
    HWND hName = MkEdit(IDC_DEPDLG_NAME, dep.display_name);
    SF(hName); SetFocus(hName);
    y += S(DD_EDIT_H) + S(DD_GAP);

    // ── Delivery type (always visible) ───────────────────────────────────────
    SF(MkLabel(LS(L"dep_dlg_delivery", L"Delivery type:")));
    y += S(DD_LABEL_H) + S(DD_GAP_SM);
    HWND hDelivery = MkCombo(IDC_DEPDLG_DELIVERY);
    // Index 0 = sentinel "Choose type…" (item data = DD_DELIVERY_NONE = -1)
    { int idx = (int)SendMessageW(hDelivery, CB_ADDSTRING, 0,
          (LPARAM)LS(L"dep_dlg_choose_type", L"Choose type\u2026").c_str());
      SendMessageW(hDelivery, CB_SETITEMDATA, (WPARAM)idx, (LPARAM)DD_DELIVERY_NONE); }
    // Indices 1–4 = real delivery types (item data = DepDelivery enum value)
    auto AddDel = [&](const wchar_t* key, const wchar_t* fb, int val) {
        int idx = (int)SendMessageW(hDelivery, CB_ADDSTRING, 0, (LPARAM)LS(key, fb).c_str());
        SendMessageW(hDelivery, CB_SETITEMDATA, (WPARAM)idx, (LPARAM)val);
    };
    AddDel(L"dep_dlg_delivery_bundled",  L"Bundled (included in installer)", DD_BUNDLED);
    AddDel(L"dep_delivery_download",     L"Auto-download",                   DD_AUTO_DOWNLOAD);
    AddDel(L"dep_delivery_redirect",     L"Redirect URL",                    DD_REDIRECT_URL);
    AddDel(L"dep_delivery_instructions", L"Instructions only",               DD_INSTRUCTIONS_ONLY);
    // Select: for a new dep (delivery == DD_BUNDLED by default) check whether
    // this is truly a new record (id==0) and pre-select sentinel; for an existing
    // dep find the matching item by its item data.
    if (dep.id == 0) {
        SendMessageW(hDelivery, CB_SETCURSEL, 0, 0); // "Choose type…"
    } else {
        int n = (int)SendMessageW(hDelivery, CB_GETCOUNT, 0, 0);
        bool sel = false;
        for (int i = 1; i < n && !sel; ++i) {
            if ((int)SendMessageW(hDelivery, CB_GETITEMDATA, (WPARAM)i, 0) == (int)dep.delivery)
                { SendMessageW(hDelivery, CB_SETCURSEL, (WPARAM)i, 0); sel = true; }
        }
        if (!sel) SendMessageW(hDelivery, CB_SETCURSEL, 0, 0);
    }
    SF(hDelivery);
    y += S(DD_COMBO_H) + S(DD_GAP);

    // ── s_rfNetOriginY marks where the dynamic section block starts ───────────
    // Reflow() reads this to know where to begin placing dynamic content.
    s_rfNetOriginY = y;

    // ── Required checkbox (hidden until a type is chosen) ─────────────────────
    {
        HWND h = CreateCustomCheckbox(hDlg, IDC_DEPDLG_REQUIRED,
            LS(L"dep_dlg_required", L"Required (cannot be skipped by the user)"),
            dep.is_required, s_ddLX, y, s_ddEW, S(DD_CB_H), hInst);
        SF(h);
        ShowWindow(h, SW_HIDE); // hidden by default; Reflow shows as needed
        s_secRequired.ctrls.push_back(h);
    }

    // ── Install order (hidden) ────────────────────────────────────────────────
    {
        HWND hLbl = MkLabel(LS(L"dep_dlg_install_order", L"Install step:"), false);
        SF(hLbl); s_secOrder.ctrls.push_back(hLbl);
        HWND hOrd = MkCombo(IDC_DEPDLG_INSTALL_ORDER, false);
        { auto AddOrd = [&](const wchar_t* key, const wchar_t* fb, int val) {
              int idx = (int)SendMessageW(hOrd, CB_ADDSTRING, 0, (LPARAM)LS(key, fb).c_str());
              SendMessageW(hOrd, CB_SETITEMDATA, (WPARAM)idx, (LPARAM)val);
          };
          // Index 0: sentinel — leaving install step unset is valid ("Nothing chosen").
          AddOrd(L"dep_install_order_choose",        L"Choose install step\u2026",           (int)DIO_UNSPECIFIED);
          AddOrd(L"dep_install_order_pre_welcome",   L"Before the Welcome screen (silent)",  (int)DIO_BEFORE_WELCOME);
          AddOrd(L"dep_install_order_post_welcome",  L"After the Welcome dialog",            (int)DIO_AFTER_WELCOME);
          AddOrd(L"dep_install_order_pre_install",   L"Before install (after License page)", (int)DIO_BEFORE_INSTALL);
          AddOrd(L"dep_install_order_post_install",  L"After the main program installs",     (int)DIO_AFTER_INSTALL);
          AddOrd(L"dep_install_order_custom_dialog", L"At a custom dialog step",             (int)DIO_CUSTOM_DIALOG);
          // For new deps pre-select DIO_BEFORE_INSTALL (most common case);
          // for existing deps match by stored value.
          if (dep.id == 0) {
              int n2 = (int)SendMessageW(hOrd, CB_GETCOUNT, 0, 0); bool sel2 = false;
              for (int i = 0; i < n2 && !sel2; ++i)
                  if ((int)SendMessageW(hOrd, CB_GETITEMDATA, (WPARAM)i, 0) == (int)DIO_BEFORE_INSTALL)
                      { SendMessageW(hOrd, CB_SETCURSEL, (WPARAM)i, 0); sel2 = true; }
              if (!sel2) SendMessageW(hOrd, CB_SETCURSEL, 0, 0);
          } else {
              int n = (int)SendMessageW(hOrd, CB_GETCOUNT, 0, 0); bool sel = false;
              for (int i = 0; i < n && !sel; ++i)
                  if ((int)SendMessageW(hOrd, CB_GETITEMDATA, (WPARAM)i, 0) == dep.install_order)
                      { SendMessageW(hOrd, CB_SETCURSEL, (WPARAM)i, 0); sel = true; }
              if (!sel) SendMessageW(hOrd, CB_SETCURSEL, 0, 0);
          } }
        SF(hOrd); s_secOrder.ctrls.push_back(hOrd);
    }

    // ── Component linkage section (hidden; shown when project has components) ─
    // ctrls: [0]=label  [1]=read-only edit (space-separated names)  [2]="…" pick button
    {
        HWND hLbl = MkLabel(LS(L"dep_dlg_components", L"Required components (optional):"), false);
        SF(hLbl); s_secCompLink.ctrls.push_back(hLbl);
        HWND hEd = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT",
            dep.required_components.c_str(),
            WS_CHILD | WS_TABSTOP | WS_BORDER | ES_AUTOHSCROLL | ES_READONLY,
            s_ddLX, y, s_ddEW, S(DD_EDIT_H), hDlg,
            (HMENU)(UINT_PTR)IDC_DEPDLG_COMP_EDIT, hInst, NULL);
        SF(hEd); s_secCompLink.ctrls.push_back(hEd);
        HWND hBtn = CreateWindowExW(0, L"BUTTON", L"\u2026",
            WS_CHILD | WS_TABSTOP | BS_PUSHBUTTON,
            s_ddLX, y, S(28), S(DD_EDIT_H), hDlg,
            (HMENU)(UINT_PTR)IDC_DEPDLG_COMP_PICK, hInst, NULL);
        SF(hBtn); s_secCompLink.ctrls.push_back(hBtn);
    }

    // ── Detection section (hidden) ────────────────────────────────────────────
    {
        HWND hHdr = MkSection(LS(L"dep_section_detection", L"Detection"), false);
        s_secDetection.ctrls.push_back(hHdr);

        auto DetRow = [&](const wchar_t* lkey, const wchar_t* lfb,
                          int editId, const std::wstring& val) {
            HWND hl = MkLabel(LS(lkey, lfb), false); SF(hl);
            s_secDetection.ctrls.push_back(hl);
            HWND he = MkEdit(editId, val, false); SF(he);
            s_secDetection.ctrls.push_back(he);
        };
        DetRow(L"dep_dlg_detect_reg",  L"Registry key (HKLM):", IDC_DEPDLG_DETECT_REG,  dep.detect_reg_key);
        DetRow(L"dep_dlg_detect_file", L"File path to detect:", IDC_DEPDLG_DETECT_FILE,  dep.detect_file_path);
        DetRow(L"dep_dlg_min_version", L"Minimum required version (optional):", IDC_DEPDLG_MIN_VER, dep.min_version);
        DetRow(L"dep_dlg_max_version", L"Maximum allowed version (optional):",  IDC_DEPDLG_MAX_VER, dep.max_version);
    }

    // ── Network section (hidden) ──────────────────────────────────────────────
    // ctrls order: [0]=header [1]=urlLbl [2]=urlEdit [3]=argsLbl [4]=argsEdit
    //              [5]=shaLbl [6]=shaEdit [7]=offlineLbl [8]=offlineCombo
    //              [9]=timeoutLbl [10]=timeoutEdit  (DD_AUTO_DOWNLOAD only)
    {
        HWND hHdr = MkSection(LS(L"dep_section_network", L"Network"), false);
        s_secNetAll.ctrls.push_back(hHdr);

        HWND hUrlL = MkLabel(LS(L"dep_dlg_url", L"Download / redirect URL:"), false);
        SF(hUrlL); s_secNetAll.ctrls.push_back(hUrlL);
        HWND hUrl  = MkEdit(IDC_DEPDLG_URL, dep.url, false);
        SF(hUrl);  s_secNetAll.ctrls.push_back(hUrl);

        HWND hArgL = MkLabel(LS(L"dep_dlg_silent_args", L"Silent install arguments (optional):"), false);
        SF(hArgL); s_secNetAll.ctrls.push_back(hArgL);
        HWND hArg  = MkEdit(IDC_DEPDLG_SILENT_ARGS, dep.silent_args, false);
        SF(hArg);  s_secNetAll.ctrls.push_back(hArg);

        HWND hShaL = MkLabel(LS(L"dep_dlg_sha256", L"SHA-256 (hex):"), false);
        SF(hShaL); s_secNetAll.ctrls.push_back(hShaL);
        HWND hSha  = MkEdit(IDC_DEPDLG_SHA256, dep.sha256, false);
        SF(hSha);  s_secNetAll.ctrls.push_back(hSha);

        HWND hOffL = MkLabel(LS(L"dep_dlg_offline", L"If offline:"), false);
        SF(hOffL); s_secNetAll.ctrls.push_back(hOffL);
        HWND hOff  = MkCombo(IDC_DEPDLG_OFFLINE, false);
        SendMessageW(hOff, CB_ADDSTRING, 0, (LPARAM)LS(L"dep_offline_abort",    L"Abort installation").c_str());
        SendMessageW(hOff, CB_ADDSTRING, 0, (LPARAM)LS(L"dep_offline_warn",     L"Warn, then continue").c_str());
        SendMessageW(hOff, CB_ADDSTRING, 0, (LPARAM)LS(L"dep_offline_skip_opt", L"Skip if optional").c_str());
        SendMessageW(hOff, CB_SETCURSEL, (WPARAM)dep.offline_behavior, 0);
        SF(hOff);  s_secNetAll.ctrls.push_back(hOff);

        HWND hTmoL = MkLabel(LS(L"dep_dlg_timeout", L"Download timeout (seconds, 0 = no timeout):"), false);
        SF(hTmoL); s_secNetAll.ctrls.push_back(hTmoL);
        HWND hTmo = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT",
            std::to_wstring(dep.download_timeout_sec).c_str(),
            WS_CHILD | WS_TABSTOP | WS_BORDER | ES_AUTOHSCROLL | ES_NUMBER,
            s_ddLX, y, s_ddEW, S(DD_EDIT_H),
            hDlg, (HMENU)(UINT_PTR)IDC_DEPDLG_TIMEOUT, hInst, NULL);
        SF(hTmo); s_secNetAll.ctrls.push_back(hTmo);
    }

    // ── Exit codes section (hidden) ────────────────────────────────────────
    // Shown for DD_BUNDLED and DD_AUTO_DOWNLOAD (modes where an exe is actually run).
    // Developer lists space-separated exit codes that are acceptable beyond 0, e.g. "3010 1641".
    // 3010 = ERROR_SUCCESS_REBOOT_REQUIRED, 1641 = ERROR_SUCCESS_REBOOT_INITIATED.
    // SetupCraft detects these at install-time and prompts the user before any reboot.
    // ctrls order: [0]=label  [1]=edit  [2]=helpBtn (shell32 #23; opens code reference list)
    {
        HWND hExL = MkLabel(LS(L"dep_dlg_exit_codes", L"Acceptable exit codes beyond 0 (space-separated):"), false);
        SF(hExL); s_secExitCodes.ctrls.push_back(hExL);
        HWND hEx  = MkEdit(IDC_DEPDLG_EXIT_CODES, dep.extra_exit_codes, false);
        SF(hEx);  s_secExitCodes.ctrls.push_back(hEx);
        HWND hExHelp = CreateCustomButtonWithIcon(
            hDlg, IDC_DEPDLG_EXIT_CODES_HELP, L"",
            ButtonColor::Blue, L"shell32.dll", 23,
            s_ddLX, y, S(DD_BTN_H), S(DD_BTN_H), hInst);
        SF(hExHelp); s_secExitCodes.ctrls.push_back(hExHelp);
        SetButtonTooltip(hExHelp,
            LS(L"dep_dlg_exit_codes_help_tip", L"Common exit codes reference").c_str());
    }

    // ── License section (hidden) ──────────────────────────────────────────────
    // ctrls order: [0]=header [1]=indicator [2]=editBtn [3]=creditsLbl [4]=creditsEdit
    {
        HWND hHdr = MkSection(LS(L"dep_section_license", L"License (optional)"), false);
        s_secLicense.ctrls.push_back(hHdr);

        HWND hInd = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT",
            s_depLicRtf.empty()
                ? LS(L"dep_lic_none",        L"(no license)").c_str()
                : LS(L"dep_lic_has_content", L"(formatted text \u2014 click Edit\u2026 to view)").c_str(),
            WS_CHILD | WS_TABSTOP | WS_BORDER | ES_READONLY | ES_LEFT,
            s_ddLX, y, s_ddEW, S(DD_EDIT_H),
            hDlg, (HMENU)(UINT_PTR)IDC_DEPDLG_LIC_PATH, hInst, NULL);
        SF(hInd); s_secLicense.ctrls.push_back(hInd);

        std::wstring elt = LS(L"dep_dlg_edit_lic", L"Edit License\u2026");
        int wElt = MeasureButtonWidth(elt.c_str(), true);
        HWND hBtn = CreateCustomButtonWithIcon(hDlg, IDC_DEPDLG_EDIT_LIC, elt,
            ButtonColor::Blue, L"shell32.dll", 38,
            s_ddLX, y, wElt, S(DD_BTN_H), hInst);
        s_secLicense.ctrls.push_back(hBtn);

        HWND hCL = MkLabel(LS(L"dep_dlg_credits", L"Credits / attribution (optional):"), false);
        SF(hCL); s_secLicense.ctrls.push_back(hCL);
        HWND hCE = MkEdit(IDC_DEPDLG_CREDITS, dep.credits_text, false);
        SF(hCE); s_secLicense.ctrls.push_back(hCE);
    }

    // ── Instructions section (hidden; shown only for DD_INSTRUCTIONS_ONLY) ───
    // ctrls layout: [icon0, label0, icon1, label1, ..., addBtn]
    // Helper: create one icon+label pair for instruction at index i and push onto ctrls.
    auto MkInstrIcon = [&](int i) {
        int iconSz = S(40);
        wchar_t shell32Path[MAX_PATH];
        GetSystemDirectoryW(shell32Path, MAX_PATH);
        wcscat_s(shell32Path, L"\\shell32.dll");
        HWND hIco = CreateWindowExW(0, L"STATIC", NULL,
            WS_CHILD | SS_NOTIFY,
            s_ddLX, y, iconSz, iconSz,
            hDlg, (HMENU)(UINT_PTR)(IDC_DEPDLG_INSTR_ICON + i), hInst, NULL);
        if (hIco) {
            InstrIconCtx* ctx = new InstrIconCtx();
            PrivateExtractIconsW(shell32Path, 70, iconSz, iconSz, &ctx->hIco, NULL, 1, 0);
            if (!ctx->hIco) ExtractIconExW(shell32Path, 70, &ctx->hIco, NULL, 1);
            ctx->prevProc = (WNDPROC)SetWindowLongPtrW(
                hIco, GWLP_WNDPROC, (LONG_PTR)InstrIconSubclassProc);
            SetWindowLongPtrW(hIco, GWLP_USERDATA, (LONG_PTR)ctx);
        }
        s_secInstructions.ctrls.push_back(hIco);
        // Number label, centered below the icon
        std::wstring numStr = std::to_wstring(i + 1);
        HWND hLbl = CreateWindowExW(0, L"STATIC", numStr.c_str(),
            WS_CHILD | SS_CENTER,
            s_ddLX, y, S(40), S(16),
            hDlg, NULL, hInst, NULL);
        SFBold(hLbl);
        s_secInstructions.ctrls.push_back(hLbl);
    };
    {
        for (int i = 0; i < (int)s_depInstrList.size(); i++) MkInstrIcon(i);
        // Add Instructions… button — always the last element in ctrls
        std::wstring ait = LS(L"dep_dlg_add_instr", L"Add Instructions\u2026");
        int wAit = MeasureButtonWidth(ait.c_str(), true);
        HWND hBtn = CreateCustomButtonWithIcon(hDlg, IDC_DEPDLG_EDIT_INSTR, ait,
            ButtonColor::Blue, L"shell32.dll", 87,
            s_ddLX, y, wAit, S(DD_BTN_H), hInst);
        s_secInstructions.ctrls.push_back(hBtn);
    }

    // ── OK / Cancel buttons (always visible; Reflow will position them) ───────
    {
        std::wstring okTxt  = LS(L"ok",     L"OK");
        std::wstring cnlTxt = LS(L"cancel", L"Cancel");
        s_btnOK_W     = MeasureButtonWidth(okTxt,  true);
        s_btnCancel_W = MeasureButtonWidth(cnlTxt, true);
        s_hBtnOK = CreateCustomButtonWithIcon(hDlg, IDC_DEPDLG_OK, okTxt.c_str(),
            ButtonColor::Green, L"shell32.dll", 258,
            s_ddLX, y, s_btnOK_W, S(DD_BTN_H), hInst);
        s_hBtnCancel = CreateCustomButtonWithIcon(hDlg, IDC_DEPDLG_CANCEL, cnlTxt.c_str(),
            ButtonColor::Red, L"shell32.dll", 131,
            s_ddLX, y, s_btnCancel_W, S(DD_BTN_H), hInst);
    }

    // ── Initial Reflow: set up for the current delivery value ─────────────────
    // For a new dep (id==0) this is DD_DELIVERY_NONE → compact view.
    // For an existing dep it reveals the appropriate sections immediately.
    {
        int initDelivery = (dep.id == 0) ? DD_DELIVERY_NONE : (int)dep.delivery;
        Reflow(hDlg, initDelivery);
    }

    // ── Scrollbar initial state ───────────────────────────────────────────────
    {
        RECT rcC; GetClientRect(hDlg, &rcC);
        SCROLLINFO si = {};
        si.cbSize = sizeof(si);
        si.fMask  = SIF_RANGE | SIF_PAGE | SIF_POS;
        si.nMin   = 0;
        si.nMax   = s_depDlgTotalH - 1;
        si.nPage  = (UINT)(rcC.bottom - rcC.top);
        si.nPos   = 0;
        SetScrollInfo(hDlg, SB_VERT, &si, TRUE);
    }

    // ── Disable the parent and run a private message loop ─────────────────────
    EnableWindow(hwndParent, FALSE);
    ShowWindow(hDlg, SW_SHOW);
    UpdateWindow(hDlg);

    MSG msg = {};
    while (IsWindow(hDlg)) {
        BOOL bRet = GetMessageW(&msg, NULL, 0, 0);
        if (bRet == 0) { PostQuitMessage((int)msg.wParam); break; }  // WM_QUIT: re-post
        if (bRet == -1) break;
        if (!IsWindow(hDlg)) break;
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    EnableWindow(hwndParent, TRUE);
    SetForegroundWindow(hwndParent);

    if (s_depDlgOk) {
        dep = data.dep;
        return true;
    }
    return false;
}
