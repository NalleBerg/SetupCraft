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
 *   DD_BUNDLED:            Required · Arch · Order · Detection · License · Credits · Instructions
 *   DD_AUTO_DOWNLOAD:      Required · Arch · Order · Detection · Network(all) · License · Credits · Instructions
 *   DD_REDIRECT_URL:       Required · Arch · Order · Detection · Network(URL+offline) · License · Credits · Instructions
 *   DD_INSTRUCTIONS_ONLY:  Required · Order · Instructions
 */

#include "dep_edit_dialog.h"
#include "button.h"       // CreateCustomButtonWithIcon, DrawCustomButton
#include "checkbox.h"     // CreateCustomCheckbox, DrawCustomCheckbox
#include "dpi.h"          // S()
#include <commctrl.h>
#include <commdlg.h>      // GetOpenFileNameW
#include "edit_rtf.h"     // OpenRtfEditor

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
};

static bool         s_depDlgOk      = false;
static int          s_depDlgScrollY = 0;   // current vertical scroll offset (scaled px)
static int          s_depDlgTotalH  = 0;   // full content height (scaled px)
static std::wstring s_depInstrRtf;          // working copy of instructions RTF
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
static DdSection s_secArch;         // Architecture combo
static DdSection s_secOrder;        // Install-order edit
static DdSection s_secDetection;    // Detection header + three edits
static DdSection s_secNetAll;       // Network header + URL + silent-args + SHA-256 + offline
static DdSection s_secNetUrlOnly;   // (subset of Network) shown for DD_REDIRECT_URL
//   Note: DD_REDIRECT_URL shows only URL (row 0) and Offline (row 3) from the network
//   block.  Rather than duplicating HWNDs we track sub-visibility inside Reflow().
static DdSection s_secLicense;      // License section header + indicator + button + credits
static DdSection s_secInstructions; // Instructions header + indicator + button

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
    bool hasArch         = (deliveryVal == DD_BUNDLED ||
                            deliveryVal == DD_AUTO_DOWNLOAD ||
                            deliveryVal == DD_REDIRECT_URL);
    bool hasOrder        = hasAny;
    bool hasDetection    = (deliveryVal == DD_BUNDLED ||
                            deliveryVal == DD_AUTO_DOWNLOAD ||
                            deliveryVal == DD_REDIRECT_URL);
    bool hasNetAll       = (deliveryVal == DD_AUTO_DOWNLOAD);
    bool hasNetUrlOnly   = (deliveryVal == DD_REDIRECT_URL);
    bool hasLicense      = (deliveryVal == DD_BUNDLED ||
                            deliveryVal == DD_AUTO_DOWNLOAD ||
                            deliveryVal == DD_REDIRECT_URL);
    bool hasInstructions = hasAny;

    // ── Show/hide each section ────────────────────────────────────────────────
    ShowSection(s_secRequired,     hasRequired);
    ShowSection(s_secArch,         hasArch);
    ShowSection(s_secOrder,        hasOrder);
    ShowSection(s_secDetection,    hasDetection);
    ShowSection(s_secNetAll,       hasNetAll);
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
    // Architecture: label then combo
    if (hasArch && s_secArch.ctrls.size() >= 2) {
        PlaceW(s_secArch.ctrls[0], DD_LABEL_H, DD_GAP_SM, s_ddEW);
        PlaceW(s_secArch.ctrls[1], DD_COMBO_H, DD_GAP,    s_ddEW);
    }
    // Install order: label then combo
    if (hasOrder && s_secOrder.ctrls.size() >= 2) {
        PlaceW(s_secOrder.ctrls[0], DD_LABEL_H, DD_GAP_SM, s_ddEW);
        PlaceW(s_secOrder.ctrls[1], DD_COMBO_H, DD_GAP,    s_ddEW);
    }
    // Detection: header, then 3× (label + edit)
    if (hasDetection && s_secDetection.ctrls.size() >= 7) {
        PlaceW(s_secDetection.ctrls[0], DD_MLABEL_H, DD_GAP_SM, s_ddEW); // header
        PlaceW(s_secDetection.ctrls[1], DD_LABEL_H,  DD_GAP_SM, s_ddEW);
        PlaceW(s_secDetection.ctrls[2], DD_EDIT_H,   DD_GAP,    s_ddEW);
        PlaceW(s_secDetection.ctrls[3], DD_LABEL_H,  DD_GAP_SM, s_ddEW);
        PlaceW(s_secDetection.ctrls[4], DD_EDIT_H,   DD_GAP,    s_ddEW);
        PlaceW(s_secDetection.ctrls[5], DD_LABEL_H,  DD_GAP_SM, s_ddEW);
        PlaceW(s_secDetection.ctrls[6], DD_EDIT_H,   DD_GAP,    s_ddEW);
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
    }
    // License: header + indicator + button + credits label + credits edit
    if (hasLicense && s_secLicense.ctrls.size() >= 5) {
        PlaceW(s_secLicense.ctrls[0], DD_MLABEL_H, DD_GAP_SM, s_ddEW); // header
        PlaceW(s_secLicense.ctrls[1], DD_EDIT_H,   DD_GAP_SM, s_ddEW); // indicator
        // Button: keep its natural (measured) width, don't stretch
        { HWND h = s_secLicense.ctrls[2];
          RECT rc; GetWindowRect(h, &rc);
          int bw = rc.right - rc.left;
          SetWindowPos(h, NULL, s_ddLX, y - s_depDlgScrollY, bw, S(DD_BTN_H),
              SWP_NOZORDER | SWP_NOACTIVATE);
          y += S(DD_BTN_H) + S(DD_GAP); }
        PlaceW(s_secLicense.ctrls[3], DD_LABEL_H, DD_GAP_SM, s_ddEW); // credits lbl
        PlaceW(s_secLicense.ctrls[4], DD_EDIT_H,  DD_GAP,    s_ddEW); // credits edit
    }
    // Instructions: header + indicator + button
    if (hasInstructions && s_secInstructions.ctrls.size() >= 3) {
        PlaceW(s_secInstructions.ctrls[0], DD_MLABEL_H, DD_GAP_SM, s_ddEW); // header
        PlaceW(s_secInstructions.ctrls[1], DD_EDIT_H,   DD_GAP_SM, s_ddEW); // indicator
        { HWND h = s_secInstructions.ctrls[2];
          RECT rc; GetWindowRect(h, &rc);
          int bw = rc.right - rc.left;
          SetWindowPos(h, NULL, s_ddLX, y - s_depDlgScrollY, bw, S(DD_BTN_H),
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
            dis->CtlID == IDC_DEPDLG_EDIT_LIC || dis->CtlID == IDC_DEPDLG_EDIT_INSTR) {
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

        if (wmId == IDC_DEPDLG_EDIT_LIC && wmEvent == BN_CLICKED) {
            RtfEditorData ed;
            ed.initRtf    = s_depLicRtf;
            ed.titleText  = DL(pData, L"dep_section_license", L"License");
            ed.okText     = DL(pData, L"ok",     L"OK");
            ed.cancelText = DL(pData, L"cancel", L"Cancel");
            ed.preferredW = S(820);
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
            RtfEditorData ed;
            ed.initRtf    = s_depInstrRtf;
            ed.titleText  = DL(pData, L"dep_section_instructions", L"Manual install instructions");
            ed.okText     = DL(pData, L"ok",     L"OK");
            ed.cancelText = DL(pData, L"cancel", L"Cancel");
            ed.preferredW = S(820);
            ed.preferredH = S(560);
            ed.pLocale    = pData->pLocale;
            if (OpenRtfEditor(hDlg, ed)) {
                s_depInstrRtf = ed.outRtf;
                SetDlgItemTextW(hDlg, IDC_DEPDLG_INSTRUCTIONS,
                    s_depInstrRtf.empty()
                    ? DL(pData, L"dep_instr_none",        L"(no instructions)").c_str()
                    : DL(pData, L"dep_instr_has_content", L"(formatted text \u2014 click Edit\u2026 to view)").c_str());
            }
            return 0;
        }

        if (wmId == IDC_DEPDLG_OK && wmEvent == BN_CLICKED) {
            // Validate: display name is required.
            std::wstring name = GetEditText(hDlg, IDC_DEPDLG_NAME);
            if (name.empty()) {
                std::wstring err = L"Please enter a display name.";
                if (pData) {
                    auto it = pData->pLocale->find(L"dep_err_no_name");
                    if (it != pData->pLocale->end()) err = it->second;
                }
                MessageBoxW(hDlg, err.c_str(), L"Validation Error", MB_OK | MB_ICONWARNING);
                SetFocus(GetDlgItem(hDlg, IDC_DEPDLG_NAME));
                return 0;
            }

            // Validate: a real delivery type must be chosen.
            int deliveryVal = GetDeliveryChoice(hDlg);
            if (deliveryVal == DD_DELIVERY_NONE) {
                std::wstring err = L"Please choose a delivery type.";
                if (pData) {
                    auto it = pData->pLocale->find(L"dep_err_no_delivery");
                    if (it != pData->pLocale->end()) err = it->second;
                }
                MessageBoxW(hDlg, err.c_str(), L"Validation Error", MB_OK | MB_ICONWARNING);
                SetFocus(GetDlgItem(hDlg, IDC_DEPDLG_DELIVERY));
                return 0;
            }

            if (pData) {
                pData->dep.display_name     = name;
                pData->dep.delivery         = (DepDelivery)deliveryVal;
                pData->dep.is_required      = (SendDlgItemMessageW(hDlg, IDC_DEPDLG_REQUIRED,
                                                BM_GETCHECK, 0, 0) == BST_CHECKED);
                { int ai = GetComboSel(hDlg, IDC_DEPDLG_ARCH);
                  LRESULT av = SendDlgItemMessageW(hDlg, IDC_DEPDLG_ARCH, CB_GETITEMDATA, (WPARAM)ai, 0);
                  pData->dep.architecture = (av != CB_ERR) ? (DepArch)(int)av : DA_X64; }
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
                pData->dep.detect_reg_key   = GetEditText(hDlg, IDC_DEPDLG_DETECT_REG);
                pData->dep.detect_file_path = GetEditText(hDlg, IDC_DEPDLG_DETECT_FILE);
                pData->dep.min_version      = GetEditText(hDlg, IDC_DEPDLG_MIN_VER);
                pData->dep.instructions     = s_depInstrRtf;
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
                    ExternalDep& dep)
{
    s_depDlgOk    = false;
    s_depInstrRtf = dep.instructions;
    s_depLicRtf   = dep.license_text;

    DepDlgData data;
    data.dep       = dep;
    data.hInst     = hInst;
    data.pLocale   = &locale;
    data.okPressed = false;

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
    int dlgY = rcParent.top  + (rcParent.bottom - rcParent.top  - dlgH) / 2;
    if (dlgX < rcWork.left)          dlgX = rcWork.left;
    if (dlgY < rcWork.top)           dlgY = rcWork.top;
    if (dlgX + dlgW > rcWork.right)  dlgX = rcWork.right  - dlgW;
    if (dlgY + dlgH > rcWork.bottom) dlgY = rcWork.bottom - dlgH;

    auto it = locale.find(L"dep_dialog_title");
    std::wstring dlgTitle = (it != locale.end()) ? it->second : L"Edit Dependency";

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
    s_secArch         = {};
    s_secOrder        = {};
    s_secDetection    = {};
    s_secNetAll       = {};
    s_secLicense      = {};
    s_secInstructions = {};

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
        HFONT hBold = CreateFontW(-S(10), 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
        SendMessageW(hw, WM_SETFONT, (WPARAM)hBold, TRUE);
        return hw;
    };

    HFONT hFont = CreateFontW(-S(11), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    auto SF = [&](HWND hw) { if (hw && hFont) SendMessageW(hw, WM_SETFONT, (WPARAM)hFont, TRUE); };

    // ── Dialog title (always visible) ─────────────────────────────────────────
    {
        HFONT hBig = CreateFontW(-S(14), 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
        HWND hT = CreateWindowExW(0, L"STATIC", dlgTitle.c_str(),
            WS_CHILD | WS_VISIBLE | SS_LEFT,
            s_ddLX, y, s_ddEW, S(DD_LABEL_H), hDlg, NULL, hInst, NULL);
        SendMessageW(hT, WM_SETFONT, (WPARAM)hBig, TRUE);
        y += S(DD_LABEL_H) + S(DD_GAP);
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
    AddDel(L"dep_delivery_bundled",      L"Bundled (included in installer)", DD_BUNDLED);
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

    // ── Architecture (hidden) ─────────────────────────────────────────────────
    {
        HWND hLbl = MkLabel(LS(L"dep_dlg_arch", L"Target architecture:"), false);
        SF(hLbl); s_secArch.ctrls.push_back(hLbl);

        HWND hArch = MkCombo(IDC_DEPDLG_ARCH, false);
        auto AddArch = [&](const wchar_t* key, const wchar_t* fb, int ev) {
            int idx = (int)SendMessageW(hArch, CB_ADDSTRING, 0, (LPARAM)LS(key, fb).c_str());
            SendMessageW(hArch, CB_SETITEMDATA, (WPARAM)idx, (LPARAM)ev);
        };
        AddArch(L"dep_arch_any",   L"Any",   (int)DA_ANY);
        AddArch(L"dep_arch_x64",   L"x64",   (int)DA_X64);
        AddArch(L"dep_arch_arm64", L"ARM64", (int)DA_ARM64);
        { int n = (int)SendMessageW(hArch, CB_GETCOUNT, 0, 0); bool sel = false;
          for (int i = 0; i < n && !sel; ++i)
              if ((int)SendMessageW(hArch, CB_GETITEMDATA, (WPARAM)i, 0) == (int)dep.architecture)
                  { SendMessageW(hArch, CB_SETCURSEL, (WPARAM)i, 0); sel = true; }
          if (!sel) SendMessageW(hArch, CB_SETCURSEL, 1, 0); } // fallback x64
        SF(hArch); s_secArch.ctrls.push_back(hArch);
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
          // For new deps pre-select the sentinel; for existing deps match by stored value.
          if (dep.id == 0) {
              SendMessageW(hOrd, CB_SETCURSEL, 0, 0);
          } else {
              int n = (int)SendMessageW(hOrd, CB_GETCOUNT, 0, 0); bool sel = false;
              for (int i = 0; i < n && !sel; ++i)
                  if ((int)SendMessageW(hOrd, CB_GETITEMDATA, (WPARAM)i, 0) == dep.install_order)
                      { SendMessageW(hOrd, CB_SETCURSEL, (WPARAM)i, 0); sel = true; }
              if (!sel) SendMessageW(hOrd, CB_SETCURSEL, 0, 0);
          } }
        SF(hOrd); s_secOrder.ctrls.push_back(hOrd);
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
        DetRow(L"dep_dlg_min_version", L"Minimum version:",     IDC_DEPDLG_MIN_VER,      dep.min_version);
    }

    // ── Network section (hidden) ──────────────────────────────────────────────
    // ctrls order: [0]=header [1]=urlLbl [2]=urlEdit [3]=argsLbl [4]=argsEdit
    //              [5]=shaLbl [6]=shaEdit [7]=offlineLbl [8]=offlineCombo
    {
        HWND hHdr = MkSection(LS(L"dep_section_network", L"Network"), false);
        s_secNetAll.ctrls.push_back(hHdr);

        HWND hUrlL = MkLabel(LS(L"dep_dlg_url", L"Download / redirect URL:"), false);
        SF(hUrlL); s_secNetAll.ctrls.push_back(hUrlL);
        HWND hUrl  = MkEdit(IDC_DEPDLG_URL, dep.url, false);
        SF(hUrl);  s_secNetAll.ctrls.push_back(hUrl);

        HWND hArgL = MkLabel(LS(L"dep_dlg_silent_args", L"Silent install arguments:"), false);
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
    }

    // ── License section (hidden) ──────────────────────────────────────────────
    // ctrls order: [0]=header [1]=indicator [2]=editBtn [3]=creditsLbl [4]=creditsEdit
    {
        HWND hHdr = MkSection(LS(L"dep_section_license", L"License"), false);
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

        HWND hCL = MkLabel(LS(L"dep_dlg_credits", L"Credits / attribution:"), false);
        SF(hCL); s_secLicense.ctrls.push_back(hCL);
        HWND hCE = MkEdit(IDC_DEPDLG_CREDITS, dep.credits_text, false);
        SF(hCE); s_secLicense.ctrls.push_back(hCE);
    }

    // ── Instructions section (hidden) ─────────────────────────────────────────
    // ctrls order: [0]=header [1]=indicator [2]=editBtn
    {
        HWND hHdr = MkSection(LS(L"dep_section_instructions", L"Manual install instructions"), false);
        s_secInstructions.ctrls.push_back(hHdr);

        HWND hInd = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT",
            s_depInstrRtf.empty()
                ? LS(L"dep_instr_none",        L"(no instructions)").c_str()
                : LS(L"dep_instr_has_content", L"(formatted text \u2014 click Edit\u2026 to view)").c_str(),
            WS_CHILD | WS_TABSTOP | WS_BORDER | ES_READONLY | ES_LEFT,
            s_ddLX, y, s_ddEW, S(DD_EDIT_H),
            hDlg, (HMENU)(UINT_PTR)IDC_DEPDLG_INSTRUCTIONS, hInst, NULL);
        SF(hInd); s_secInstructions.ctrls.push_back(hInd);

        std::wstring eit = LS(L"dep_dlg_edit_instr", L"Edit Instructions\u2026");
        int wEit = MeasureButtonWidth(eit.c_str(), true);
        HWND hBtn = CreateCustomButtonWithIcon(hDlg, IDC_DEPDLG_EDIT_INSTR, eit,
            ButtonColor::Blue, L"shell32.dll", 87,
            s_ddLX, y, wEit, S(DD_BTN_H), hInst);
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
