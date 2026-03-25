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
#include <richedit.h>     // EM_STREAMIN, SF_RTF, EDITSTREAM

// PrivateExtractIconsW — undocumented but reliable fixed-size icon loader.
extern "C" __declspec(dllimport) UINT WINAPI PrivateExtractIconsW(
    LPCWSTR szFileName, int nIconIndex, int cxIcon, int cyIcon,
    HICON* phicon, UINT* piconid, UINT nIcons, UINT flags);

// ── Module-private state ──────────────────────────────────────────────────────

static InstallerDialog s_dialogs[IDLG_COUNT];  // in-memory RTF content

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
// A modal popup that shows an installer dialog exactly as the end user will
// see it: page title, RTF content area, and Back / Next / Cancel buttons.
// The Next button uses shell32.dll icon #137 drawn to the RIGHT of the text.

struct PreviewData {
    InstallerDialogType type;
    HFONT               hGuiFont;
    HFONT               hTitleFont;
    bool                running;
};

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

        RECT rc; GetClientRect(hwnd, &rc);
        int cW  = rc.right;
        int cH  = rc.bottom;
        int pad = S(16);
        int btnH = S(30);
        int gap  = S(8);
        int titleH = S(36);

        // Page title label (shows dialog type name at the top)
        std::wstring titleTxt = L10n(kDialogNameKeys[(int)pd->type],
                                     kDialogNameFallbacks[(int)pd->type]);
        HWND hTitle = CreateWindowExW(0, L"STATIC", titleTxt.c_str(),
            WS_CHILD | WS_VISIBLE | SS_LEFT,
            pad, pad, cW - pad * 2, titleH,
            hwnd, NULL, cs->hInstance, NULL);
        if (pd->hTitleFont) SendMessageW(hTitle, WM_SETFONT, (WPARAM)pd->hTitleFont, TRUE);

        // Read-only RichEdit for content
        int editY = pad + titleH + gap;
        int editH = cH - editY - gap - btnH - pad;
        HWND hEdit = CreateWindowExW(WS_EX_CLIENTEDGE, reClass, L"",
            WS_CHILD | WS_VISIBLE | WS_VSCROLL |
            ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL,
            pad, editY, cW - pad * 2, editH,
            hwnd, (HMENU)(UINT_PTR)IDC_IDLG_PRV_CONTENT, cs->hInstance, NULL);
        if (pd->hGuiFont) SendMessageW(hEdit, WM_SETFONT, (WPARAM)pd->hGuiFont, TRUE);
        SendMessageW(hEdit, EM_SETBKGNDCOLOR, 0, RGB(255, 255, 255));

        // Stream in saved RTF, or show placeholder
        const std::wstring& rtf = s_dialogs[(int)pd->type].content_rtf;
        if (!rtf.empty()) {
            StreamRtfIn(hEdit, rtf);
        } else {
            std::wstring placeholder =
                L10n(L"idlg_preview_no_content", L"(No content defined for this dialog yet)");
            SetWindowTextW(hEdit, placeholder.c_str());
        }

        // Bottom button row — y aligned to bottom of client area
        int btnY = cH - pad - btnH;

        // Measure button widths from their text
        std::wstring backTxt   = L"\u25C0  " + L10n(L"idlg_preview_back",   L"Back");
        std::wstring nextTxt   = L10n(L"idlg_preview_next",   L"Next") + L"  \u25B6";
        std::wstring cancelTxt = L10n(L"idlg_preview_cancel", L"Cancel");
        int wBack   = MeasureButtonWidth(backTxt,   false);
        int wNext   = MeasureButtonWidth(nextTxt,   false);
        int wCancel = MeasureButtonWidth(cancelTxt, false);

        // Back (disabled for Welcome)
        HWND hBack = CreateWindowExW(0, L"BUTTON",
            backTxt.c_str(),
            WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            pad, btnY, wBack, btnH,
            hwnd, (HMENU)(UINT_PTR)IDC_IDLG_PRV_BACK, cs->hInstance, NULL);
        if (pd->hGuiFont) SendMessageW(hBack, WM_SETFONT, (WPARAM)pd->hGuiFont, TRUE);
        EnableWindow(hBack, pd->type != IDLG_WELCOME ? TRUE : FALSE);

        // Cancel (far right)
        HWND hCancel = CreateWindowExW(0, L"BUTTON",
            cancelTxt.c_str(),
            WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            cW - pad - wCancel, btnY, wCancel, btnH,
            hwnd, (HMENU)(UINT_PTR)IDC_IDLG_PRV_CANCEL, cs->hInstance, NULL);
        if (pd->hGuiFont) SendMessageW(hCancel, WM_SETFONT, (WPARAM)pd->hGuiFont, TRUE);

        // Next (placed between Back and Cancel)
        HWND hNext = CreateWindowExW(0, L"BUTTON",
            nextTxt.c_str(),
            WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            cW - pad - wCancel - gap - wNext, btnY, wNext, btnH,
            hwnd, (HMENU)(UINT_PTR)IDC_IDLG_PRV_NEXT, cs->hInstance, NULL);
        if (pd->hGuiFont) SendMessageW(hNext, WM_SETFONT, (WPARAM)pd->hGuiFont, TRUE);
        return 0;
    }

    case WM_COMMAND: {
        int id = LOWORD(wParam);
        if (id == IDC_IDLG_PRV_BACK  ||
            id == IDC_IDLG_PRV_NEXT  ||
            id == IDC_IDLG_PRV_CANCEL)
        {
            if (pd) pd->running = false;
            DestroyWindow(hwnd);
        }
        return 0;
    }

    case WM_CLOSE:
        if (pd) pd->running = false;
        DestroyWindow(hwnd);
        return 0;

    case WM_DESTROY:
        return 0;
    }

    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

static void ShowPreviewDialog(HWND hwndParent, InstallerDialogType type)
{
    // Register the window class once.
    static bool s_classRegistered = false;
    if (!s_classRegistered) {
        WNDCLASSEXW wc = {};
        wc.cbSize        = sizeof(wc);
        wc.lpfnWndProc   = PreviewWndProc;
        wc.hInstance     = s_hInst;
        wc.hCursor       = LoadCursor(NULL, IDC_ARROW);
        wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
        wc.lpszClassName = L"IDLGPreviewClass";
        RegisterClassExW(&wc);
        s_classRegistered = true;
    }

    PreviewData pd    = {};
    pd.type           = type;
    pd.hGuiFont       = s_hGuiFont;
    pd.hTitleFont     = s_hTitleFont;
    pd.running        = true;

    // Client area target
    const DWORD style   = WS_POPUP | WS_CAPTION | WS_SYSMENU;
    const DWORD exStyle = WS_EX_DLGMODALFRAME;
    int clientW = S(460);
    int clientH = S(360);
    RECT rcAdj = { 0, 0, clientW, clientH };
    AdjustWindowRectEx(&rcAdj, style, FALSE, exStyle);
    int wndW = rcAdj.right  - rcAdj.left;
    int wndH = rcAdj.bottom - rcAdj.top;

    // Centre over parent
    RECT rcParent; GetWindowRect(hwndParent, &rcParent);
    int x = rcParent.left + (rcParent.right  - rcParent.left - wndW) / 2;
    int y = rcParent.top  + (rcParent.bottom - rcParent.top  - wndH) / 2;

    // Window caption: "Preview — <dialog type name>"
    std::wstring dlgName = L10n(kDialogNameKeys[(int)type], kDialogNameFallbacks[(int)type]);
    std::wstring caption = L10n(L"idlg_preview_caption", L"Preview") + L" — " + dlgName;

    HWND hPreview = CreateWindowExW(exStyle,
        L"IDLGPreviewClass", caption.c_str(), style,
        x, y, wndW, wndH,
        hwndParent, NULL, s_hInst, &pd);
    if (!hPreview) return;

    ShowWindow(hPreview, SW_SHOW);
    UpdateWindow(hPreview);

    // Disable parent for modal behaviour
    EnableWindow(hwndParent, FALSE);

    MSG m;
    while (pd.running && GetMessageW(&m, NULL, 0, 0) > 0) {
        if (!IsDialogMessageW(hPreview, &m)) {
            TranslateMessage(&m);
            DispatchMessageW(&m);
        }
    }

    EnableWindow(hwndParent, TRUE);
    SetForegroundWindow(hwndParent);
}

// ── IDLG_Reset ────────────────────────────────────────────────────────────────

void IDLG_Reset()
{
    for (int i = 0; i < IDLG_COUNT; i++) {
        s_dialogs[i].type        = (InstallerDialogType)i;
        s_dialogs[i].content_rtf = L"";
    }
    s_installTitle    = L"";
    s_installIconPath = L"";
    // s_hInstallIcon and s_hInstIconPreview are managed by BuildPage/TearDown.
}

// ── IDLG_TearDown ─────────────────────────────────────────────────────────────

void IDLG_TearDown(HWND /*hwnd*/)
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
}

// ── IDLG_BuildPage ────────────────────────────────────────────────────────────

void IDLG_BuildPage(HWND hwnd, HINSTANCE hInst,
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
        SetButtonTooltip(hPreview, L10n(L"idlg_btn_preview_tip",
            L"Preview this dialog as the end user will see it").c_str());

        y += rowH + gap;
    }
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

