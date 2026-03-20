/*
 * dep_edit_dialog.cpp — "Edit Dependency" modal dialog for SetupCraft.
 *
 * Follows the measure-then-create pattern documented in dialog_INTERNALS.txt.
 * All pixel values use S(); all strings use the locale map.
 */

#include "dep_edit_dialog.h"
#include "button.h"       // CreateCustomButtonWithIcon, DrawCustomButton
#include "checkbox.h"     // CreateCustomCheckbox, DrawCustomCheckbox
#include "dpi.h"          // S()
#include <commctrl.h>
#include <commdlg.h>      // GetOpenFileNameW

// ── Layout constants (design pixels at 96 DPI) ────────────────────────────────
static const int DD_PAD_H    = 20;   // left/right padding
static const int DD_PAD_T    = 20;   // top padding
static const int DD_PAD_B    = 15;   // bottom padding (below buttons)
static const int DD_GAP      = 10;   // standard inter-row gap
static const int DD_GAP_SM   =  4;   // label → control gap
static const int DD_BTN_H    = 34;   // OK / Cancel height
static const int DD_BTN_W    = 120;  // OK / Cancel width each
static const int DD_BTN_GAP  = 15;   // gap between OK and Cancel
static const int DD_CONT_W   = 400;  // inner content column width
static const int DD_LABEL_H  = 18;   // single-line label height
static const int DD_EDIT_H   = 26;   // single-line edit height
static const int DD_CB_H     = 22;   // checkbox height
static const int DD_COMBO_H  = 26;   // combo-box height
static const int DD_MLABEL_H = 16;   // section header label
static const int DD_INSTR_H  = 80;   // multi-line instructions field height
static const int DD_BROWSE_W = 52;   // width of "Browse…" button

// ── Per-invocation heap struct ────────────────────────────────────────────────
struct DepDlgData {
    ExternalDep dep;                              // working copy
    HINSTANCE   hInst;
    const std::map<std::wstring,std::wstring>* pLocale;
    bool        okPressed;
};

static bool s_depDlgOk      = false;
static int  s_depDlgScrollY = 0;   // current vertical scroll offset (scaled px)
static int  s_depDlgTotalH  = 0;   // full content height (scaled px)

// Safe locale lookup.
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

static int GetComboSel(HWND hDlg, int id)
{
    return (int)SendDlgItemMessageW(hDlg, id, CB_GETCURSEL, 0, 0);
}

// ── Show/hide URL / SHA-256 / silent-args / offline group ────────────────────
// Called after delivery combo changes to show only relevant fields.
static void UpdateVisibility(HWND hDlg, DepDelivery d)
{
    bool hasUrl      = (d == DD_AUTO_DOWNLOAD || d == DD_REDIRECT_URL);
    bool hasDownload = (d == DD_AUTO_DOWNLOAD);
    bool hasOffline  = (d == DD_AUTO_DOWNLOAD || d == DD_REDIRECT_URL);

    // URL row: label (IDC_DEPDLG_URL - 1000 offset not used: use sibling search)
    // We identify labels by querying SetWindowTextW — simpler: just show/hide by ID.
    // The labels live at IDs derived from a naming scheme set up in WM_CREATE.
    // Here we rely on ShowWindow on the edit controls; labels below are handled
    // by helper IDs 501-520 allocated in WM_CREATE.
    auto SW = [&](HWND h, bool vis) {
        if (h && IsWindow(h)) ShowWindow(h, vis ? SW_SHOW : SW_HIDE);
    };

    SW(GetDlgItem(hDlg, IDC_DEPDLG_URL),         hasUrl);
    SW(GetDlgItem(hDlg, 501),                     hasUrl);   // URL label
    SW(GetDlgItem(hDlg, IDC_DEPDLG_SILENT_ARGS), hasDownload);
    SW(GetDlgItem(hDlg, 502),                     hasDownload); // silent args label
    SW(GetDlgItem(hDlg, IDC_DEPDLG_SHA256),      hasDownload);
    SW(GetDlgItem(hDlg, 503),                     hasDownload); // SHA-256 label
    SW(GetDlgItem(hDlg, IDC_DEPDLG_OFFLINE),     hasOffline);
    SW(GetDlgItem(hDlg, 504),                     hasOffline); // offline label
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
            dis->CtlID == IDC_DEPDLG_LIC_BROWSE) {
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
            int sel = GetComboSel(hDlg, IDC_DEPDLG_DELIVERY);
            UpdateVisibility(hDlg, (DepDelivery)sel);
            return 0;
        }

        if (wmId == IDC_DEPDLG_LIC_BROWSE && wmEvent == BN_CLICKED) {
            wchar_t fileBuf[MAX_PATH] = {};
            OPENFILENAMEW ofn = {};
            ofn.lStructSize  = sizeof(ofn);
            ofn.hwndOwner    = hDlg;
            ofn.lpstrFilter  = L"License files\0*.rtf;*.txt\0RTF files\0*.rtf\0Text files\0*.txt\0All files\0*.*\0";
            ofn.lpstrFile    = fileBuf;
            ofn.nMaxFile     = MAX_PATH;
            ofn.Flags        = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
            if (GetOpenFileNameW(&ofn)) {
                SetDlgItemTextW(hDlg, IDC_DEPDLG_LIC_PATH, fileBuf);
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

            if (pData) {
                pData->dep.display_name     = name;
                pData->dep.delivery         = (DepDelivery)GetComboSel(hDlg, IDC_DEPDLG_DELIVERY);
                pData->dep.is_required      = (SendDlgItemMessageW(hDlg, IDC_DEPDLG_REQUIRED,
                                                BM_GETCHECK, 0, 0) == BST_CHECKED);
                { int ai = GetComboSel(hDlg, IDC_DEPDLG_ARCH);
                  LRESULT av = SendDlgItemMessageW(hDlg, IDC_DEPDLG_ARCH, CB_GETITEMDATA, (WPARAM)ai, 0);
                  pData->dep.architecture = (av != CB_ERR) ? (DepArch)(int)av : DA_X64; }
                pData->dep.offline_behavior = (DepOffline)GetComboSel(hDlg, IDC_DEPDLG_OFFLINE);

                // Install order (numeric field).
                std::wstring orderStr = GetEditText(hDlg, IDC_DEPDLG_INSTALL_ORDER);
                pData->dep.install_order = orderStr.empty() ? 0 : _wtoi(orderStr.c_str());

                pData->dep.url              = GetEditText(hDlg, IDC_DEPDLG_URL);
                pData->dep.silent_args      = GetEditText(hDlg, IDC_DEPDLG_SILENT_ARGS);
                pData->dep.sha256           = GetEditText(hDlg, IDC_DEPDLG_SHA256);
                pData->dep.detect_reg_key   = GetEditText(hDlg, IDC_DEPDLG_DETECT_REG);
                pData->dep.detect_file_path = GetEditText(hDlg, IDC_DEPDLG_DETECT_FILE);
                pData->dep.min_version      = GetEditText(hDlg, IDC_DEPDLG_MIN_VER);
                pData->dep.instructions     = GetEditText(hDlg, IDC_DEPDLG_INSTRUCTIONS);
                pData->dep.credits_text     = GetEditText(hDlg, IDC_DEPDLG_CREDITS);
                pData->dep.license_path     = GetEditText(hDlg, IDC_DEPDLG_LIC_PATH);
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
    s_depDlgOk = false;

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

    // ── Measure content height ────────────────────────────────────────────────
    // Rows (top-down, design pixels):
    //   Title label + gap
    //   Display name label + gap_sm + edit + gap
    //   Delivery combo label + gap_sm + combo + gap
    //   Required checkbox + gap
    //   Architecture label + gap_sm + combo + gap
    //   Install order label + gap_sm + edit + gap
    //   --- Detection section ---
    //   Section label + gap_sm
    //   Registry key label + gap_sm + edit + gap
    //   File path label + gap_sm + edit + gap
    //   Min version label + gap_sm + edit + gap
    //   --- Network section (always created, conditionally shown) ---
    //   URL label + gap_sm + edit + gap
    //   Silent args label + gap_sm + edit + gap
    //   SHA-256 label + gap_sm + edit + gap
    //   Offline label + gap_sm + combo + gap
    //   --- License section ---
    //   Section label + gap_sm
    //   License path edit + browse button + gap
    //   Credits label + gap_sm + edit + gap
    //   --- Instructions ---
    //   Section label + gap_sm
    //   Instructions edit + gap
    //   --- Buttons ---
    //   OK / Cancel

    auto AddRow = [](int& y, int labelH, int editH, int gapSm, int gap) {
        y += labelH + gapSm + editH + gap;
    };

    int contentH = DD_PAD_T;
    // Title
    contentH += DD_LABEL_H + DD_GAP;
    // Name:
    AddRow(contentH, DD_LABEL_H, DD_EDIT_H, DD_GAP_SM, DD_GAP);
    // Delivery:
    AddRow(contentH, DD_LABEL_H, DD_COMBO_H, DD_GAP_SM, DD_GAP);
    // Required checkbox:
    contentH += DD_CB_H + DD_GAP;
    // Architecture:
    AddRow(contentH, DD_LABEL_H, DD_COMBO_H, DD_GAP_SM, DD_GAP);
    // Install order:
    AddRow(contentH, DD_LABEL_H, DD_EDIT_H, DD_GAP_SM, DD_GAP);
    // ── Detection section ──
    contentH += DD_MLABEL_H + DD_GAP_SM;
    AddRow(contentH, DD_LABEL_H, DD_EDIT_H, DD_GAP_SM, DD_GAP); // reg key
    AddRow(contentH, DD_LABEL_H, DD_EDIT_H, DD_GAP_SM, DD_GAP); // file path
    AddRow(contentH, DD_LABEL_H, DD_EDIT_H, DD_GAP_SM, DD_GAP); // min version
    // ── Network section ──
    contentH += DD_MLABEL_H + DD_GAP_SM;
    AddRow(contentH, DD_LABEL_H, DD_EDIT_H, DD_GAP_SM, DD_GAP); // url
    AddRow(contentH, DD_LABEL_H, DD_EDIT_H, DD_GAP_SM, DD_GAP); // silent args
    AddRow(contentH, DD_LABEL_H, DD_EDIT_H, DD_GAP_SM, DD_GAP); // sha256
    AddRow(contentH, DD_LABEL_H, DD_COMBO_H, DD_GAP_SM, DD_GAP); // offline
    // ── License section ──
    contentH += DD_MLABEL_H + DD_GAP_SM;
    contentH += DD_EDIT_H + DD_GAP;  // license path + browse (same row)
    AddRow(contentH, DD_LABEL_H, DD_EDIT_H, DD_GAP_SM, DD_GAP); // credits
    // ── Instructions ──
    contentH += DD_MLABEL_H + DD_GAP_SM;
    contentH += DD_INSTR_H + DD_GAP;
    // Buttons
    contentH += DD_BTN_H + DD_PAD_B;

    // ── Size the window ───────────────────────────────────────────────────────
    s_depDlgScrollY = 0;
    s_depDlgTotalH  = S(contentH);

    const DWORD dlgStyle   = WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_VSCROLL;
    const DWORD dlgExStyle = WS_EX_DLGMODALFRAME;

    RECT rc = { 0, 0, S(DD_CONT_W) + S(DD_PAD_H) * 2, S(contentH) };
    AdjustWindowRectEx(&rc, dlgStyle, FALSE, dlgExStyle);
    int dlgW = rc.right  - rc.left;
    int dlgH = rc.bottom - rc.top;

    // Clamp height to work area.
    RECT rcWork; SystemParametersInfoW(SPI_GETWORKAREA, 0, &rcWork, 0);
    int maxH = (rcWork.bottom - rcWork.top) - S(30);
    if (dlgH > maxH) dlgH = maxH;

    // Centre over parent; clamp to work area.
    RECT rcParent;
    GetWindowRect(hwndParent, &rcParent);
    int dlgX = rcParent.left + (rcParent.right  - rcParent.left - dlgW) / 2;
    int dlgY = rcParent.top  + (rcParent.bottom - rcParent.top  - dlgH) / 2;
    if (dlgX < rcWork.left)          dlgX = rcWork.left;
    if (dlgY < rcWork.top)           dlgY = rcWork.top;
    if (dlgX + dlgW > rcWork.right)  dlgX = rcWork.right  - dlgW;
    if (dlgY + dlgH > rcWork.bottom) dlgY = rcWork.bottom - dlgH;

    auto it = locale.find(L"dep_dialog_title");
    std::wstring dlgTitle = (it != locale.end()) ? it->second : L"Edit Dependency";

    HWND hDlg = CreateWindowExW(
        dlgExStyle,
        L"DepEditDialog", dlgTitle.c_str(),
        dlgStyle,
        dlgX, dlgY, dlgW, dlgH,
        hwndParent, NULL, hInst, &data);

    if (!hDlg) return false;

    // ── Build controls ────────────────────────────────────────────────────────
    RECT rcClient; GetClientRect(hDlg, &rcClient);
    int cW  = rcClient.right - rcClient.left;
    int lX  = S(DD_PAD_H);           // left x
    int rX  = cW - S(DD_PAD_H);      // right x edge
    int eW  = rX - lX;               // full edit width
    int y   = S(DD_PAD_T);

    auto LS = [&](const wchar_t* key, const wchar_t* fb) -> std::wstring {
        auto it2 = locale.find(key); return it2 != locale.end() ? it2->second : fb;
    };

    auto MkLabel = [&](const std::wstring& text, int h = DD_LABEL_H) -> HWND {
        HWND hw = CreateWindowExW(0, L"STATIC", text.c_str(),
            WS_CHILD | WS_VISIBLE | SS_LEFT,
            lX, y, eW, S(h), hDlg, NULL, hInst, NULL);
        return hw;
    };

    auto MkEdit = [&](int id, const std::wstring& txt, bool ro = false) -> HWND {
        DWORD style = WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_BORDER | ES_AUTOHSCROLL;
        if (ro) style |= ES_READONLY;
        HWND hw = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", txt.c_str(), style,
            lX, y, eW, S(DD_EDIT_H), hDlg, (HMENU)(UINT_PTR)id, hInst, NULL);
        return hw;
    };

    auto MkCombo = [&](int id) -> HWND {
        HWND hw = CreateWindowExW(0, L"COMBOBOX", L"",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | CBS_DROPDOWNLIST,
            lX, y, eW, S(DD_COMBO_H) * 6, hDlg, (HMENU)(UINT_PTR)id, hInst, NULL);
        return hw;
    };

    auto MkSectionHeader = [&](const std::wstring& text) -> HWND {
        HWND hw = CreateWindowExW(0, L"STATIC", text.c_str(),
            WS_CHILD | WS_VISIBLE | SS_LEFT,
            lX, y, eW, S(DD_MLABEL_H), hDlg, NULL, hInst, NULL);
        // Bold font for section headers.
        HFONT hBold = CreateFontW(-S(10), 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
        SendMessageW(hw, WM_SETFONT, (WPARAM)hBold, TRUE);
        return hw;
    };

    HFONT hFont = CreateFontW(-S(11), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");

    auto SetFont = [&](HWND hw) { if (hw && hFont) SendMessageW(hw, WM_SETFONT, (WPARAM)hFont, TRUE); };

    // Dialog title static (visual heading, bold large).
    {
        HFONT hBig = CreateFontW(-S(14), 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
        HWND hT = CreateWindowExW(0, L"STATIC", dlgTitle.c_str(),
            WS_CHILD | WS_VISIBLE | SS_LEFT,
            lX, y, eW, S(DD_LABEL_H), hDlg, NULL, hInst, NULL);
        SendMessageW(hT, WM_SETFONT, (WPARAM)hBig, TRUE);
        y += S(DD_LABEL_H) + S(DD_GAP);
    }

    // ── Display name ──────────────────────────────────────────────────────────
    SetFont(MkLabel(LS(L"dep_dlg_name", L"Display name:")));
    y += S(DD_LABEL_H) + S(DD_GAP_SM);
    HWND hName = MkEdit(IDC_DEPDLG_NAME, dep.display_name);
    SetFont(hName); SetFocus(hName);
    y += S(DD_EDIT_H) + S(DD_GAP);

    // ── Delivery type ─────────────────────────────────────────────────────────
    SetFont(MkLabel(LS(L"dep_dlg_delivery", L"Delivery type:")));
    y += S(DD_LABEL_H) + S(DD_GAP_SM);
    HWND hDelivery = MkCombo(IDC_DEPDLG_DELIVERY);
    SendMessageW(hDelivery, CB_ADDSTRING, 0, (LPARAM)LS(L"dep_delivery_bundled",      L"Bundled (included in installer)").c_str());
    SendMessageW(hDelivery, CB_ADDSTRING, 0, (LPARAM)LS(L"dep_delivery_download",     L"Auto-download").c_str());
    SendMessageW(hDelivery, CB_ADDSTRING, 0, (LPARAM)LS(L"dep_delivery_redirect",     L"Redirect URL").c_str());
    SendMessageW(hDelivery, CB_ADDSTRING, 0, (LPARAM)LS(L"dep_delivery_instructions", L"Instructions only").c_str());
    SendMessageW(hDelivery, CB_SETCURSEL, (WPARAM)dep.delivery, 0);
    SetFont(hDelivery);
    y += S(DD_COMBO_H) + S(DD_GAP);

    // ── Required checkbox ─────────────────────────────────────────────────────
    HWND hReq = CreateCustomCheckbox(hDlg, IDC_DEPDLG_REQUIRED,
        LS(L"dep_dlg_required", L"Required (cannot be skipped by the user)"),
        dep.is_required, lX, y, eW, S(DD_CB_H), hInst);
    SetFont(hReq);
    y += S(DD_CB_H) + S(DD_GAP);

    // ── Architecture ──────────────────────────────────────────────────────────
    SetFont(MkLabel(LS(L"dep_dlg_arch", L"Target architecture:")));
    y += S(DD_LABEL_H) + S(DD_GAP_SM);
    HWND hArch = MkCombo(IDC_DEPDLG_ARCH);
    SendMessageW(hArch, CB_ADDSTRING, 0, (LPARAM)LS(L"dep_arch_any",   L"Any").c_str());
    // x86 (32-bit) not supported — only Any / x64 / ARM64.
    auto AddArch = [&](const wchar_t* key, const wchar_t* fb, int enumVal) {
        int idx = (int)SendMessageW(hArch, CB_ADDSTRING, 0, (LPARAM)LS(key, fb).c_str());
        SendMessageW(hArch, CB_SETITEMDATA, (WPARAM)idx, (LPARAM)enumVal);
    };
    AddArch(L"dep_arch_any",   L"Any",   (int)DA_ANY);
    AddArch(L"dep_arch_x64",   L"x64",   (int)DA_X64);
    AddArch(L"dep_arch_arm64", L"ARM64", (int)DA_ARM64);
    { // Select the item matching dep.architecture; fall back to x64.
        int n = (int)SendMessageW(hArch, CB_GETCOUNT, 0, 0); bool sel = false;
        for (int i = 0; i < n && !sel; i++) {
            if ((int)SendMessageW(hArch, CB_GETITEMDATA, (WPARAM)i, 0) == (int)dep.architecture)
                { SendMessageW(hArch, CB_SETCURSEL, (WPARAM)i, 0); sel = true; }
        }
        if (!sel) SendMessageW(hArch, CB_SETCURSEL, 1, 0); // fallback: x64
    }
    SetFont(hArch);
    y += S(DD_COMBO_H) + S(DD_GAP);

    // ── Install order ─────────────────────────────────────────────────────────
    SetFont(MkLabel(LS(L"dep_dlg_install_order", L"Install order (lower = first):")));
    y += S(DD_LABEL_H) + S(DD_GAP_SM);
    wchar_t orderBuf[16]; _itow_s(dep.install_order, orderBuf, 10);
    HWND hOrder = MkEdit(IDC_DEPDLG_INSTALL_ORDER, orderBuf);
    SetFont(hOrder);
    y += S(DD_EDIT_H) + S(DD_GAP);

    // ── Detection section ─────────────────────────────────────────────────────
    MkSectionHeader(LS(L"dep_section_detection", L"Detection"));
    y += S(DD_MLABEL_H) + S(DD_GAP_SM);

    SetFont(MkLabel(LS(L"dep_dlg_detect_reg", L"Registry key (HKLM):")));
    y += S(DD_LABEL_H) + S(DD_GAP_SM);
    SetFont(MkEdit(IDC_DEPDLG_DETECT_REG, dep.detect_reg_key));
    y += S(DD_EDIT_H) + S(DD_GAP);

    SetFont(MkLabel(LS(L"dep_dlg_detect_file", L"File path to detect:")));
    y += S(DD_LABEL_H) + S(DD_GAP_SM);
    SetFont(MkEdit(IDC_DEPDLG_DETECT_FILE, dep.detect_file_path));
    y += S(DD_EDIT_H) + S(DD_GAP);

    SetFont(MkLabel(LS(L"dep_dlg_min_version", L"Minimum version:")));
    y += S(DD_LABEL_H) + S(DD_GAP_SM);
    SetFont(MkEdit(IDC_DEPDLG_MIN_VER, dep.min_version));
    y += S(DD_EDIT_H) + S(DD_GAP);

    // ── Network section (conditionally visible) ───────────────────────────────
    MkSectionHeader(LS(L"dep_section_network", L"Network"));
    y += S(DD_MLABEL_H) + S(DD_GAP_SM);

    // URL label: register auxiliary ID 501 so UpdateVisibility can hide/show it.
    {
        HWND hL = CreateWindowExW(0, L"STATIC",
            LS(L"dep_dlg_url", L"Download / redirect URL:").c_str(),
            WS_CHILD | WS_VISIBLE | SS_LEFT,
            lX, y, eW, S(DD_LABEL_H),
            hDlg, (HMENU)(UINT_PTR)501, hInst, NULL);
        SetFont(hL);
    }
    y += S(DD_LABEL_H) + S(DD_GAP_SM);
    SetFont(MkEdit(IDC_DEPDLG_URL, dep.url));
    y += S(DD_EDIT_H) + S(DD_GAP);

    {
        HWND hL = CreateWindowExW(0, L"STATIC",
            LS(L"dep_dlg_silent_args", L"Silent install arguments:").c_str(),
            WS_CHILD | WS_VISIBLE | SS_LEFT,
            lX, y, eW, S(DD_LABEL_H),
            hDlg, (HMENU)(UINT_PTR)502, hInst, NULL);
        SetFont(hL);
    }
    y += S(DD_LABEL_H) + S(DD_GAP_SM);
    SetFont(MkEdit(IDC_DEPDLG_SILENT_ARGS, dep.silent_args));
    y += S(DD_EDIT_H) + S(DD_GAP);

    {
        HWND hL = CreateWindowExW(0, L"STATIC",
            LS(L"dep_dlg_sha256", L"SHA-256 (hex):").c_str(),
            WS_CHILD | WS_VISIBLE | SS_LEFT,
            lX, y, eW, S(DD_LABEL_H),
            hDlg, (HMENU)(UINT_PTR)503, hInst, NULL);
        SetFont(hL);
    }
    y += S(DD_LABEL_H) + S(DD_GAP_SM);
    SetFont(MkEdit(IDC_DEPDLG_SHA256, dep.sha256));
    y += S(DD_EDIT_H) + S(DD_GAP);

    {
        HWND hL = CreateWindowExW(0, L"STATIC",
            LS(L"dep_dlg_offline", L"If offline:").c_str(),
            WS_CHILD | WS_VISIBLE | SS_LEFT,
            lX, y, eW, S(DD_LABEL_H),
            hDlg, (HMENU)(UINT_PTR)504, hInst, NULL);
        SetFont(hL);
    }
    y += S(DD_LABEL_H) + S(DD_GAP_SM);
    HWND hOffline = MkCombo(IDC_DEPDLG_OFFLINE);
    SendMessageW(hOffline, CB_ADDSTRING, 0, (LPARAM)LS(L"dep_offline_abort",    L"Abort installation").c_str());
    SendMessageW(hOffline, CB_ADDSTRING, 0, (LPARAM)LS(L"dep_offline_warn",     L"Warn, then continue").c_str());
    SendMessageW(hOffline, CB_ADDSTRING, 0, (LPARAM)LS(L"dep_offline_skip_opt", L"Skip if optional").c_str());
    SendMessageW(hOffline, CB_SETCURSEL, (WPARAM)dep.offline_behavior, 0);
    SetFont(hOffline);
    y += S(DD_COMBO_H) + S(DD_GAP);

    // ── License section ───────────────────────────────────────────────────────
    MkSectionHeader(LS(L"dep_section_license", L"License"));
    y += S(DD_MLABEL_H) + S(DD_GAP_SM);

    // License path (read-only edit) + Browse button on same row.
    int browseW = S(DD_BROWSE_W);
    HWND hLicPath = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT",
        dep.license_path.c_str(),
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_BORDER | ES_AUTOHSCROLL | ES_READONLY,
        lX, y, eW - browseW - S(DD_GAP), S(DD_EDIT_H),
        hDlg, (HMENU)(UINT_PTR)IDC_DEPDLG_LIC_PATH, hInst, NULL);
    SetFont(hLicPath);
    HWND hLicBrowse = CreateCustomButtonWithIcon(
        hDlg, IDC_DEPDLG_LIC_BROWSE,
        LS(L"dep_dlg_lic_browse", L"Browse…").c_str(),
        ButtonColor::Blue,
        L"shell32.dll", 3,
        lX + eW - browseW, y, browseW, S(DD_EDIT_H), hInst);
    (void)hLicBrowse;
    y += S(DD_EDIT_H) + S(DD_GAP);

    SetFont(MkLabel(LS(L"dep_dlg_credits", L"Credits / attribution:")));
    y += S(DD_LABEL_H) + S(DD_GAP_SM);
    SetFont(MkEdit(IDC_DEPDLG_CREDITS, dep.credits_text));
    y += S(DD_EDIT_H) + S(DD_GAP);

    // ── Instructions ──────────────────────────────────────────────────────────
    MkSectionHeader(LS(L"dep_section_instructions", L"Manual install instructions"));
    y += S(DD_MLABEL_H) + S(DD_GAP_SM);

    HWND hInstr = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT",
        dep.instructions.c_str(),
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_BORDER |
        ES_MULTILINE | ES_AUTOVSCROLL | WS_VSCROLL,
        lX, y, eW, S(DD_INSTR_H),
        hDlg, (HMENU)(UINT_PTR)IDC_DEPDLG_INSTRUCTIONS, hInst, NULL);
    SetFont(hInstr);
    y += S(DD_INSTR_H) + S(DD_GAP);

    // ── OK / Cancel buttons ───────────────────────────────────────────────────
    int btnAreaW = S(DD_BTN_W) * 2 + S(DD_BTN_GAP);
    int btnX     = lX + (eW - btnAreaW) / 2;

    CreateCustomButtonWithIcon(
        hDlg, IDC_DEPDLG_OK,
        LS(L"ok", L"OK").c_str(),
        ButtonColor::Green,
        L"shell32.dll", 258,
        btnX, y, S(DD_BTN_W), S(DD_BTN_H), hInst);

    CreateCustomButtonWithIcon(
        hDlg, IDC_DEPDLG_CANCEL,
        LS(L"cancel", L"Cancel").c_str(),
        ButtonColor::Red,
        L"shell32.dll", 131,
        btnX + S(DD_BTN_W) + S(DD_BTN_GAP), y, S(DD_BTN_W), S(DD_BTN_H), hInst);

    // ── Vertical scrollbar ────────────────────────────────────────────────────
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

    // Initial visibility based on delivery type.
    UpdateVisibility(hDlg, dep.delivery);

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
