/*
 * settings.cpp — Installer Settings page for SetupCraft (page index 5).
 *
 * Sections:
 *   Application  — app name, version, publisher, publisher URL, support URL, icon
 *   Build Output — output filename, compression type/level, solid compression
 *   Installation — UAC privilege level, minimum OS version
 *   Uninstall    — allow uninstall, close running applications
 *
 * Shared fields (name, version, publisher, icon path) are stored in mainwindow.cpp
 * statics and handled by mainwindow.cpp EN_CHANGE / button handlers, exactly as the
 * Registry page handles IDC_REG_PUBLISHER, IDC_REG_VERSION, IDC_REG_ADD_ICON.
 *
 * New settings are stored as file-scope statics here and persisted via
 * DB::GetSetting / DB::SetSetting with project-scoped keys.
 */

#include "settings.h"
#include "file_assoc.h"   // FA_HasAnyEnabled()
#include "mainwindow.h"   // MainWindow::MarkAsModified()
#include "tooltip.h"      // ShowMultilingualTooltip, HideTooltip, IsTooltipVisible
#include "vfs_picker.h"   // ShowVfsPicker, VfsPickerParams
#include "db.h"
#include "dpi.h"          // S()
#include "button.h"       // CreateCustomButtonWithIcon(), MeasureButtonWidth()
#include "checkbox.h"     // CreateCustomCheckbox()
#include <shellapi.h>
#include <shlobj.h>       // SHGetPathFromIDListW
#include <vector>
#include <string>
#include <map>
#include <algorithm>

// ── Module-private state (new settings only) ──────────────────────────────────
static std::wstring s_publisherUrl;
static std::wstring s_supportUrl;
static std::wstring s_outputFolder;
static std::wstring s_outputFilename;
static std::wstring s_testOutputFolder;
static std::wstring s_testOutputFilename;
static int          s_compressionType  = 2;    // 0=store, 1=zip, 2=lzma, 3=lzma2
static int          s_compressionLevel = 7;    // 0–9
static bool         s_solidCompression = true;
static int          s_uacLevel         = 0;    // 0=requireAdministrator 1=asInvoker 2=highest
static int          s_privOverrides    = 2;    // 0=none 1=commandline 2=dialog
static int          s_wizardStyle      = 1;    // 0=modern 1=classic
static int          s_minOsVersion     = 0;    // 0=none … 5=Win11

// ── Code signing statics ──────────────────────────────────────────────────────
static bool         s_signEnabled      = false;
static std::wstring s_signtoolPath;
static std::wstring s_signThumbprint;
static std::wstring s_signPfxPath;
static std::wstring s_signPfxPassword;
static std::wstring s_signTimestampUrl  = L"http://timestamp.digicert.com";
static int          s_signTimestampAlgo = 1;    // 0=sha1 1=sha256
static std::wstring s_signDescription;
// HWNDs of sign-section controls that need to be enabled/disabled by the checkbox
static HFONT s_hSectionFont = NULL;   // bold font for section header labels
static HWND         s_hSignControls[20] = {};   // dependent controls (enabled/disabled by checkbox)

// ── Installation wizard-page toggles ───────────────────────────────────────────
static bool         s_disableDirPage          = false;
static bool         s_disableProgramGroupPage = false;
static bool         s_usePreviousAppDir       = true;
static bool         s_usePreviousGroup        = true;
static int          s_dirExistsWarning        = 0;   // 0=auto 1=yes 2=no
static bool         s_allowUninstall          = true;
static bool         s_closeApps        = false;
static std::vector<std::wstring> s_pathFolders;          // full paths added by user for PATH
static HWND         s_hPathDisplay      = NULL;          // listbox showing leaf names of PATH folders
static WNDPROC      s_pathListOrigProc  = NULL;          // original listbox WNDPROC (subclassed)
static int          s_pathListHoverItem = -1;            // listbox item currently under the cursor
static bool         s_pathListTracking  = false;         // TrackMouseEvent active on listbox
static HWND         s_hSettingsPage    = NULL;           // settings page HWND (set in BuildPage)
static bool         s_changesEnvironment = false;
static std::wstring s_uninstallDisplayName;   // empty = use AppName (Inno default)
static std::wstring s_uninstallFilesDir;      // empty = use {app} (Inno default)
static int          s_installBase        = 0;    // 0={pf} 1={pf64} 2={pf32} 3={localappdata} 4={commonappdata} 5={userdocs} 6=Custom
static std::wstring s_installBaseCustom;          // used when s_installBase == 6
static HWND         s_hInstallBaseCustomEdit = NULL;

// ── Setup log statics ────────────────────────────────────────────────────────────
static bool         s_setupLogging      = false;
static std::wstring s_setupLogFolder;
static std::wstring s_setupLogFilename;
static int          s_setupLogMode      = 0;     // 0=overwrite 1=append
static HWND         s_hLogControls[8]   = {};    // dependent controls (enabled/disabled by checkbox)

// ── Installer language settings ───────────────────────────────────────────────
static int          s_langDetectionMethod = 0;   // 0=uilanguage 1=locale 2=none
static int          s_showLanguageDialog  = 0;   // 0=auto 1=yes 2=no

// ── Installer language table ─────────────────────────────────────────────────
// Contains every .isl file bundled with Inno Setup 6 standard installation.
// Russian is intentionally excluded.
// local=true: community .isl shipped in inno/ alongside template.iss (referenced by relative path).
// local=false: standard Inno .isl referenced as "compiler:X.isl".
struct InnoLang { const wchar_t* isl; const wchar_t* displayName; bool local; };
static const InnoLang kInnoLangs[] = {
    { L"Default",             L"English",              false },  // index 0 — always on
    { L"BrazilianPortuguese", L"Portuguese (Brazil)",  false },
    { L"Catalan",             L"Catalan",              false },
    { L"Czech",               L"Czech",                false },
    { L"Danish",              L"Danish",               false },
    { L"Dutch",               L"Dutch",                false },
    { L"Finnish",             L"Finnish",              false },
    { L"French",              L"French",               false },
    { L"German",              L"German",               false },
    { L"Hebrew",              L"Hebrew",               false },
    { L"Hungarian",           L"Hungarian",            false },
    { L"Italian",             L"Italian",              false },
    { L"Japanese",            L"Japanese",             false },
    { L"Norwegian",           L"Norwegian",            false },
    { L"Polish",              L"Polish",               false },
    { L"Portuguese",          L"Portuguese",           false },
    { L"Slovak",              L"Slovak",               false },
    { L"Slovenian",           L"Slovenian",            false },
    { L"Spanish",             L"Spanish",              false },
    { L"Swedish",             L"Swedish",              true  },  // community .isl in inno/
    { L"Turkish",             L"Turkish",              false },
    { L"Ukrainian",           L"Ukrainian",            false },
};
static const int kLangCount = (int)(sizeof(kInnoLangs) / sizeof(kInnoLangs[0]));
static std::vector<bool> s_installerLangs;  // size kLangCount; [0] always true

static int          s_scrollOffset     = 0;

static HFONT        s_hGuiFont         = NULL;
static HINSTANCE    s_hInst            = NULL;
static const std::map<std::wstring, std::wstring>* s_pLocale = NULL;

// ── Locale helper ─────────────────────────────────────────────────────────────
static std::wstring loc(const wchar_t* key, const wchar_t* fallback)
{
    if (!s_pLocale) return fallback;
    auto it = s_pLocale->find(key);
    return (it != s_pLocale->end()) ? it->second : fallback;
}

// ── Project-scoped settings key ───────────────────────────────────────────────
static std::wstring ProjKey(int projectId, const wchar_t* suffix)
{
    return L"sett_" + std::to_wstring(projectId) + L"_" + suffix;
}

// ── Layout constants (unscaled pixels) ───────────────────────────────────────
static const int kPadH    = 20;   // left/right page margin
static const int kLblW    = 160;  // label column width (right-aligned)
static const int kLblGap  = 6;    // gap between label column and field
static const int kRowH    = 24;   // single-line edit / combo height
static const int kRowStep = 33;   // row-to-row step (kRowH + gap)
static const int kBtnH    = 28;   // button height
static const int kSecGap  = 14;   // extra gap before section header

// ── Layout helpers ────────────────────────────────────────────────────────────

// Section header: bold label + etched divider, returns Y of first control row.
static int SectionHeader(HWND hwnd, HINSTANCE hInst, HFONT hFont,
                          int y, int clientWidth, const std::wstring& text, int labelId)
{
    HWND hLbl = CreateWindowExW(0, L"STATIC", text.c_str(),
        WS_CHILD | WS_VISIBLE | SS_LEFT,
        S(kPadH), y, clientWidth - S(kPadH) * 2, S(20),
        hwnd, (HMENU)(UINT_PTR)labelId, hInst, NULL);
    if (hFont) SendMessageW(hLbl, WM_SETFONT, (WPARAM)hFont, TRUE);
    y += S(20) + S(3);
    CreateWindowExW(0, L"STATIC", L"",
        WS_CHILD | WS_VISIBLE | SS_ETCHEDHORZ,
        S(kPadH), y, clientWidth - S(kPadH) * 2, S(2),
        hwnd, NULL, hInst, NULL);
    y += S(2) + S(9);
    return y;
}

// Label (right-aligned) + single-line edit, returns Y of next row.
static int LabelEdit(HWND hwnd, HINSTANCE hInst, HFONT hFont,
                      int y, int clientWidth,
                      const std::wstring& labelText, int editId,
                      const std::wstring& value)
{
    const int fldX = S(kPadH) + S(kLblW) + S(kLblGap);
    const int fldW = clientWidth - fldX - S(kPadH);

    HWND hL = CreateWindowExW(0, L"STATIC", labelText.c_str(),
        WS_CHILD | WS_VISIBLE | SS_RIGHT | SS_CENTERIMAGE,
        S(kPadH), y, S(kLblW), S(kRowH),
        hwnd, NULL, hInst, NULL);
    if (hFont) SendMessageW(hL, WM_SETFONT, (WPARAM)hFont, TRUE);

    HWND hE = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", value.c_str(),
        WS_CHILD | WS_VISIBLE | ES_LEFT | ES_AUTOHSCROLL,
        fldX, y, fldW, S(kRowH),
        hwnd, (HMENU)(UINT_PTR)editId, hInst, NULL);
    if (hFont) SendMessageW(hE, WM_SETFONT, (WPARAM)hFont, TRUE);

    return y + S(kRowStep);
}

// Label (right-aligned) + dropdown combo, returns Y of next row.
// *outHwnd receives the combo HWND if non-NULL.
static int LabelCombo(HWND hwnd, HINSTANCE hInst, HFONT hFont,
                        int y, int clientWidth,
                        const std::wstring& labelText, int comboId,
                        const std::vector<std::wstring>& items, int sel,
                        HWND* outHwnd = nullptr)
{
    const int fldX = S(kPadH) + S(kLblW) + S(kLblGap);
    const int maxW  = clientWidth - fldX - S(kPadH);

    // Measure the widest item so the combo is never truncated
    int measuredW = 0;
    {
        HDC hdc  = GetDC(hwnd);
        HFONT hOld = hFont ? (HFONT)SelectObject(hdc, (HGDIOBJ)hFont) : nullptr;
        for (const auto& item : items) {
            SIZE sz = {};
            GetTextExtentPoint32W(hdc, item.c_str(), (int)item.size(), &sz);
            measuredW = std::max(measuredW, (int)sz.cx);
        }
        if (hOld) SelectObject(hdc, (HGDIOBJ)hOld);
        ReleaseDC(hwnd, hdc);
        measuredW += GetSystemMetrics(SM_CXVSCROLL) + S(8); // arrow button + padding
    }
    const int fldW = std::min(std::max(measuredW, S(120)), maxW);

    HWND hL = CreateWindowExW(0, L"STATIC", labelText.c_str(),
        WS_CHILD | WS_VISIBLE | SS_RIGHT | SS_CENTERIMAGE,
        S(kPadH), y, S(kLblW), S(kRowH),
        hwnd, NULL, hInst, NULL);
    if (hFont) SendMessageW(hL, WM_SETFONT, (WPARAM)hFont, TRUE);

    HWND hC = CreateWindowExW(0, L"COMBOBOX", L"",
        WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | WS_VSCROLL,
        fldX, y, fldW, S(kRowH) + S(200),
        hwnd, (HMENU)(UINT_PTR)comboId, hInst, NULL);
    if (hFont) SendMessageW(hC, WM_SETFONT, (WPARAM)hFont, TRUE);
    for (const auto& item : items)
        SendMessageW(hC, CB_ADDSTRING, 0, (LPARAM)item.c_str());
    if (sel >= 0 && sel < (int)items.size())
        SendMessageW(hC, CB_SETCURSEL, (WPARAM)sel, 0);

    if (outHwnd) *outHwnd = hC;
    return y + S(kRowStep);
}

// Left-indented custom checkbox aligned to field column, returns Y of next row.
static int FieldCheckbox(HWND hwnd, HINSTANCE hInst, HFONT hFont, int y, int clientWidth,
                           const std::wstring& text, int cbId, bool checked)
{
    const int fldX = S(kPadH) + S(kLblW) + S(kLblGap);
    const int fldW = clientWidth - fldX - S(kPadH);
    HWND hChk = CreateCustomCheckbox(hwnd, cbId, text, checked, fldX, y, fldW, S(kRowH), hInst);
    if (hFont) SendMessageW(hChk, WM_SETFONT, (WPARAM)hFont, TRUE);
    return y + S(kRowStep);
}

// ── SETT_Reset ────────────────────────────────────────────────────────────────
void SETT_Reset()
{
    s_publisherUrl     = L"";
    s_supportUrl       = L"";
    s_outputFolder     = L"";
    s_outputFilename   = L"";
    s_testOutputFolder   = L"";
    s_testOutputFilename = L"";
    s_compressionType  = 2;
    s_compressionLevel = 7;
    s_solidCompression = true;
    s_uacLevel         = 0;
    s_privOverrides    = 2;
    s_wizardStyle      = 1;
    s_minOsVersion     = 0;
    s_signEnabled      = false;
    s_signtoolPath     = L"";
    s_signThumbprint   = L"";
    s_signPfxPath      = L"";
    s_signPfxPassword  = L"";
    s_signTimestampUrl  = L"http://timestamp.digicert.com";
    s_signTimestampAlgo = 1;
    s_signDescription  = L"";
    s_disableDirPage          = false;
    s_disableProgramGroupPage = false;
    s_usePreviousAppDir       = true;
    s_usePreviousGroup        = true;
    s_dirExistsWarning        = 0;
    memset(s_hSignControls, 0, sizeof(s_hSignControls));
    s_allowUninstall   = true;
    s_closeApps        = false;
    s_pathFolders.clear();
    s_changesEnvironment = false;
    s_uninstallDisplayName = L"";
    s_uninstallFilesDir    = L"";
    s_installBase        = 0;
    s_installBaseCustom  = L"";
    s_setupLogging       = false;
    s_setupLogFolder     = L"";
    s_setupLogFilename   = L"";
    s_setupLogMode       = 0;
    memset(s_hLogControls, 0, sizeof(s_hLogControls));
    s_langDetectionMethod = 0;
    s_showLanguageDialog  = 0;
    s_installerLangs.assign(kLangCount, false);
    s_installerLangs[0] = true;  // English always included
    s_scrollOffset     = 0;
    s_hGuiFont         = NULL;
    s_hInst            = NULL;
    s_pLocale          = NULL;
}

// ── SETT_TearDown ─────────────────────────────────────────────────────────────
void SETT_TearDown(HWND /*hwnd*/)
{
    // Controls are destroyed by SwitchPage's child-window enumeration.
    s_scrollOffset           = 0;
    s_hGuiFont               = NULL;
    s_hInst                  = NULL;
    s_pLocale                = NULL;
    s_hInstallBaseCustomEdit = NULL;
    s_hPathDisplay           = NULL;
    s_hSettingsPage          = NULL;
    s_pathListOrigProc  = NULL;
    s_pathListHoverItem = -1;
    s_pathListTracking  = false;
    if (s_hSectionFont)  { DeleteObject(s_hSectionFont);   s_hSectionFont  = NULL; }
}

HFONT SETT_GetSectionFont()
{
    return s_hSectionFont;
}

// helper: enable/disable all code-signing dependent controls
static void ApplySignEnable(BOOL enable)
{
    for (int i = 0; i < 9; i++)
        if (s_hSignControls[i]) EnableWindow(s_hSignControls[i], enable);
}

// helper: enable/disable all setup-log dependent controls
static void ApplyLogEnable(BOOL enable)
{
    for (int i = 0; i < 8; i++)
        if (s_hLogControls[i]) EnableWindow(s_hLogControls[i], enable);
}

// ── PathListSubclassProc ──────────────────────────────────────────────────────
// Subclass proc for s_hPathDisplay (LISTBOX).
// WM_MOUSEMOVE — detects hovered item; shows project tooltip (full Inno path).
// WM_MOUSELEAVE — hides tooltip when cursor leaves the listbox.
// WM_NCDESTROY  — restores original WNDPROC.
static LRESULT CALLBACK PathListSubclassProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg) {
    case WM_MOUSEMOVE: {
        POINT pt = { (LONG)(short)LOWORD(lParam), (LONG)(short)HIWORD(lParam) };
        LRESULT r    = SendMessageW(hWnd, LB_ITEMFROMPOINT, 0, MAKELPARAM(pt.x, pt.y));
        int  idx     = (int)LOWORD(r);
        BOOL outside = (BOOL)HIWORD(r);
        if (outside) idx = -1;

        if (idx != s_pathListHoverItem) {
            s_pathListHoverItem = idx;
            HideTooltip();  // item changed — dismiss previous tip immediately
        }

        if (idx >= 0 && idx < (int)s_pathFolders.size()) {
            if (!IsTooltipVisible()) {
                const std::wstring& fullPath = s_pathFolders[idx];
                std::vector<TooltipEntry> entries;
                entries.push_back({ L"", fullPath });
                // Convert client-coords to screen before passing to ShowMultilingualTooltip
                POINT scr = pt;
                ClientToScreen(hWnd, &scr);
                ShowMultilingualTooltip(entries, scr.x + S(12), scr.y + S(20),
                                        GetParent(hWnd));
            }
        } else {
            HideTooltip();
        }

        // Start TrackMouseEvent so WM_MOUSELEAVE fires when cursor exits the listbox
        if (!s_pathListTracking) {
            TRACKMOUSEEVENT tme = { sizeof(tme), TME_LEAVE, hWnd, 0 };
            TrackMouseEvent(&tme);
            s_pathListTracking = true;
        }
        break;
    }
    case WM_MOUSELEAVE:
        HideTooltip();
        s_pathListHoverItem = -1;
        s_pathListTracking  = false;
        break;
    case WM_NCDESTROY:
        SetWindowLongPtrW(hWnd, GWLP_WNDPROC, (LONG_PTR)s_pathListOrigProc);
        break;
    }
    return CallWindowProcW(s_pathListOrigProc, hWnd, msg, wParam, lParam);
}

// ── RefreshPathDisplay ────────────────────────────────────────────────────────
// Rebuilds the listbox items (leaf names) from s_pathFolders.
// Syncs the Remove button enabled state and the ChangesEnvironment checkbox.
static void RefreshPathDisplay(HWND hwndPage)
{
    if (s_hPathDisplay) {
        SendMessageW(s_hPathDisplay, LB_RESETCONTENT, 0, 0);
        for (const auto& p : s_pathFolders) {
            size_t pos = p.find_last_of(L"\\/");
            std::wstring leaf = (pos != std::wstring::npos) ? p.substr(pos + 1) : p;
            if (leaf.empty()) leaf = p;
            SendMessageW(s_hPathDisplay, LB_ADDSTRING, 0, (LPARAM)leaf.c_str());
        }
    }
    if (hwndPage) {
        int sel = s_hPathDisplay
                ? (int)SendMessageW(s_hPathDisplay, LB_GETCURSEL, 0, 0)
                : LB_ERR;
        HWND hRem = GetDlgItem(hwndPage, IDC_SETT_PATH_REMOVE_BTN);
        if (hRem) EnableWindow(hRem, (sel != LB_ERR) ? TRUE : FALSE);
        bool hasPath = !s_pathFolders.empty();
        if (!hasPath) {
            s_changesEnvironment = false;
            SendDlgItemMessageW(hwndPage, IDC_SETT_CHANGES_ENV, BM_SETCHECK, BST_UNCHECKED, 0);
        }
        EnableWindow(GetDlgItem(hwndPage, IDC_SETT_CHANGES_ENV), hasPath ? TRUE : FALSE);
    }
}

// ── SETT_GetScrollOffset / SETT_SetScrollOffset ───────────────────────────────
int  SETT_GetScrollOffset()          { return s_scrollOffset; }
void SETT_SetScrollOffset(int offset) { s_scrollOffset = offset; }

// ── SETT_BuildPage ────────────────────────────────────────────────────────────
int SETT_BuildPage(HWND hwnd, HINSTANCE hInst,
                   int pageY, int clientWidth,
                   HFONT hPageTitleFont, HFONT hGuiFont,
                   const std::map<std::wstring, std::wstring>& locale,
                   const std::wstring& appName,
                   const std::wstring& appVersion,
                   const std::wstring& appPublisher,
                   const std::wstring& appIconPath,
                   const std::wstring& appId)
{
    s_hInst   = hInst;
    s_hGuiFont = hGuiFont;
    s_pLocale  = &locale;
    s_hSettingsPage = hwnd;

    // Create bold font for section headers
    if (s_hSectionFont) { DeleteObject(s_hSectionFont); s_hSectionFont = NULL; }
    if (hGuiFont) {
        LOGFONTW lf = {};
        GetObjectW(hGuiFont, sizeof(lf), &lf);
        lf.lfWeight = FW_BOLD;
        s_hSectionFont = CreateFontIndirectW(&lf);
    }

    int y = pageY + S(12);

    // ── Page title ────────────────────────────────────────────────────────────
    {
        HWND hTitle = CreateWindowExW(0, L"STATIC",
            loc(L"sett_page_title", L"Installer Settings").c_str(),
            WS_CHILD | WS_VISIBLE | SS_LEFT,
            S(kPadH), y, clientWidth - S(kPadH) * 2, S(38),
            hwnd, (HMENU)(UINT_PTR)IDC_SETT_PAGE_TITLE, hInst, NULL);
        if (hPageTitleFont) SendMessageW(hTitle, WM_SETFONT, (WPARAM)hPageTitleFont, TRUE);
        y += S(38) + S(12);
    }

    // ════════════════════════════════════════════════════════════════════════
    // Section 1: Application
    // ════════════════════════════════════════════════════════════════════════
    y = SectionHeader(hwnd, hInst, s_hSectionFont, y, clientWidth,
                      loc(L"sett_sec_application", L"Application"), IDC_SETT_SEC_APPLICATION);

    // App name (shared with Files page IDC_PROJECT_NAME)
    y = LabelEdit(hwnd, hInst, hGuiFont, y, clientWidth,
                  loc(L"sett_app_name_lbl", L"App name:"),
                  IDC_SETT_APP_NAME, appName);

    // Version (shared with Registry page IDC_REG_VERSION)
    y = LabelEdit(hwnd, hInst, hGuiFont, y, clientWidth,
                  loc(L"sett_app_version_lbl", L"Version:"),
                  IDC_SETT_APP_VERSION, appVersion);

    // Publisher (shared with Registry page IDC_REG_PUBLISHER)
    y = LabelEdit(hwnd, hInst, hGuiFont, y, clientWidth,
                  loc(L"sett_publisher_lbl", L"Publisher:"),
                  IDC_SETT_PUBLISHER, appPublisher);

    // Publisher URL (new)
    y = LabelEdit(hwnd, hInst, hGuiFont, y, clientWidth,
                  loc(L"sett_publisher_url_lbl", L"Publisher URL:"),
                  IDC_SETT_PUBLISHER_URL, s_publisherUrl);

    // Support URL (new)
    y = LabelEdit(hwnd, hInst, hGuiFont, y, clientWidth,
                  loc(L"sett_support_url_lbl", L"Support URL:"),
                  IDC_SETT_SUPPORT_URL, s_supportUrl);

    // AppId: read-only GUID display + Regenerate button (IDC_SETT_REGEN_GUID handled by mainwindow.cpp)
    {
        const int fldX  = S(kPadH) + S(kLblW) + S(kLblGap);
        const int editW = S(300);
        const int btnW  = S(28);

        HWND hL = CreateWindowExW(0, L"STATIC",
            loc(L"sett_app_id_lbl", L"AppId (GUID):").c_str(),
            WS_CHILD | WS_VISIBLE | SS_RIGHT | SS_CENTERIMAGE,
            S(kPadH), y, S(kLblW), S(kRowH),
            hwnd, NULL, hInst, NULL);
        if (hGuiFont) SendMessageW(hL, WM_SETFONT, (WPARAM)hGuiFont, TRUE);

        HWND hAid = CreateWindowExW(0, L"STATIC", appId.c_str(),
            WS_CHILD | WS_VISIBLE | SS_LEFT | SS_CENTERIMAGE | SS_NOPREFIX,
            fldX, y, editW, S(kRowH),
            hwnd, (HMENU)(UINT_PTR)IDC_SETT_APP_ID, hInst, NULL);
        if (hGuiFont) SendMessageW(hAid, WM_SETFONT, (WPARAM)hGuiFont, TRUE);

        CreateCustomButtonWithIcon(
            hwnd, IDC_SETT_REGEN_GUID, L"", ButtonColor::Blue,
            L"shell32.dll", 238,
            fldX + editW + S(4), y, btnW, S(kRowH), hInst);

        y += S(kRowStep);
    }

    // App icon row: preview (48×48) + Change Icon button
    {
        const int fldX = S(kPadH) + S(kLblW) + S(kLblGap);
        const int iconSz = S(48);

        // Label
        HWND hLbl = CreateWindowExW(0, L"STATIC",
            loc(L"sett_app_icon_lbl", L"App icon:").c_str(),
            WS_CHILD | WS_VISIBLE | SS_RIGHT | SS_CENTERIMAGE,
            S(kPadH), y, S(kLblW), iconSz,
            hwnd, NULL, hInst, NULL);
        if (hGuiFont) SendMessageW(hLbl, WM_SETFONT, (WPARAM)hGuiFont, TRUE);

        // Icon preview
        HWND hPrev = CreateWindowExW(WS_EX_CLIENTEDGE, L"STATIC", L"",
            WS_CHILD | WS_VISIBLE | SS_ICON | SS_CENTERIMAGE,
            fldX, y, iconSz, iconSz,
            hwnd, (HMENU)(UINT_PTR)IDC_SETT_ICON_PREVIEW, hInst, NULL);
        if (hPrev) {
            HICON hIco = NULL;
            if (!appIconPath.empty())
                hIco = (HICON)LoadImageW(NULL, appIconPath.c_str(),
                                          IMAGE_ICON, 48, 48, LR_LOADFROMFILE);
            if (!hIco)
                hIco = ExtractIconW(hInst, L"shell32.dll", 2);
            if (hIco)
                SendMessageW(hPrev, STM_SETICON, (WPARAM)hIco, 0);
        }

        // Change Icon button (vertically centred with icon)
        std::wstring btnTxt = loc(L"sett_change_icon_btn", L"Change Icon\u2026");
        int wBtn = MeasureButtonWidth(btnTxt, true);
        CreateCustomButtonWithIcon(
            hwnd, IDC_SETT_CHANGE_ICON, btnTxt.c_str(), ButtonColor::Blue,
            L"shell32.dll", 127,
            fldX + iconSz + S(10), y + (iconSz - S(kBtnH)) / 2,
            wBtn, S(kBtnH), hInst);

        y += iconSz + S(12);
    }

    // ════════════════════════════════════════════════════════════════════════
    // Section 2: Build Output
    // ════════════════════════════════════════════════════════════════════════
    y += S(kSecGap);
    y = SectionHeader(hwnd, hInst, s_hSectionFont, y, clientWidth,
                      loc(L"sett_sec_build", L"Build Output"), IDC_SETT_SEC_BUILD);

    // Output folder (with Browse button)
    {
        const int fldX = S(kPadH) + S(kLblW) + S(kLblGap);
        const int btnW = S(28);
        const int fldW = clientWidth - fldX - S(kPadH) - S(6) - btnW;

        HWND hL = CreateWindowExW(0, L"STATIC",
            loc(L"sett_output_folder_lbl", L"Output folder:").c_str(),
            WS_CHILD | WS_VISIBLE | SS_RIGHT | SS_CENTERIMAGE,
            S(kPadH), y, S(kLblW), S(kRowH),
            hwnd, NULL, hInst, NULL);
        if (hGuiFont) SendMessageW(hL, WM_SETFONT, (WPARAM)hGuiFont, TRUE);

        HWND hE = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", s_outputFolder.c_str(),
            WS_CHILD | WS_VISIBLE | ES_LEFT | ES_AUTOHSCROLL,
            fldX, y, fldW, S(kRowH),
            hwnd, (HMENU)(UINT_PTR)IDC_SETT_OUTPUT_FOLDER, hInst, NULL);
        if (hGuiFont) SendMessageW(hE, WM_SETFONT, (WPARAM)hGuiFont, TRUE);

        // Browse button — same icon as Files page install-folder browse (shell32 #4, Blue)
        CreateCustomButtonWithIcon(
            hwnd, IDC_SETT_OUTPUT_FOLDER_BTN, L"", ButtonColor::Blue,
            L"shell32.dll", 4,
            fldX + fldW + S(6), y, btnW, S(kRowH), hInst);

        y += S(kRowStep);
    }

    // Output filename
    y = LabelEdit(hwnd, hInst, hGuiFont, y, clientWidth,
                  loc(L"sett_output_file_lbl", L"Output filename:"),
                  IDC_SETT_OUTPUT_FILE, s_outputFilename);

    // Compression type combo
    {
        std::vector<std::wstring> comprItems = {
            loc(L"sett_compression_store", L"Store (no compression)"),
            loc(L"sett_compression_zip",   L"Zip"),
            loc(L"sett_compression_lzma",  L"LZMA"),
            loc(L"sett_compression_lzma2", L"LZMA2"),
        };
        y = LabelCombo(hwnd, hInst, hGuiFont, y, clientWidth,
                       loc(L"sett_compression_lbl", L"Compression:"),
                       IDC_SETT_COMPRESSION, comprItems, s_compressionType);
    }

    // Compression level combo (0–9)
    {
        std::vector<std::wstring> levelItems;
        for (int i = 0; i <= 9; i++) {
            std::wstring label = std::to_wstring(i);
            if (i == 0) label += L"  \u2014 " + loc(L"sett_comp_level_fastest", L"fastest");
            if (i == 9) label += L"  \u2014 " + loc(L"sett_comp_level_smallest", L"smallest");
            levelItems.push_back(label);
        }
        y = LabelCombo(hwnd, hInst, hGuiFont, y, clientWidth,
                       loc(L"sett_comp_level_lbl", L"Compression Level:"),
                       IDC_SETT_COMP_LEVEL, levelItems, s_compressionLevel);
    }

    // Solid compression checkbox
    y = FieldCheckbox(hwnd, hInst, hGuiFont, y, clientWidth,
                      loc(L"sett_solid_lbl", L"Solid compression (better ratio, slower)"),
                      IDC_SETT_SOLID, s_solidCompression);

    // ════════════════════════════════════════════════════════════════════════
    // Section 3: Installation
    // ════════════════════════════════════════════════════════════════════════
    y += S(kSecGap);
    y = SectionHeader(hwnd, hInst, s_hSectionFont, y, clientWidth,
                      loc(L"sett_sec_install", L"Installation"), IDC_SETT_SEC_INSTALL);

    // UAC privilege level — three radios on one line
    {
        HWND hLbl = CreateWindowExW(0, L"STATIC",
            loc(L"sett_uac_lbl", L"Privileges:").c_str(),
            WS_CHILD | WS_VISIBLE | SS_RIGHT | SS_CENTERIMAGE,
            S(kPadH), y, S(kLblW), S(kRowH),
            hwnd, NULL, hInst, NULL);
        if (hGuiFont) SendMessageW(hLbl, WM_SETFONT, (WPARAM)hGuiFont, TRUE);

        struct { int id; const wchar_t* key; const wchar_t* def; } uac[] = {
            { IDC_SETT_UAC_REQADMIN, L"sett_uac_admin",   L"Administrator"          },
            { IDC_SETT_UAC_INVOKER,  L"sett_uac_invoker", L"Invoker (no elevation)" },
            { IDC_SETT_UAC_HIGHEST,  L"sett_uac_highest", L"Highest available"      },
        };
        int rx = S(kPadH) + S(kLblW) + S(kLblGap);
        for (int i = 0; i < 3; i++) {
            std::wstring txt = loc(uac[i].key, uac[i].def);
            DWORD style = WS_CHILD | WS_VISIBLE | BS_AUTORADIOBUTTON;
            if (i == 0) style |= WS_GROUP;
            HWND hR = CreateWindowExW(0, L"BUTTON", txt.c_str(), style,
                rx, y, S(175), S(kRowH),
                hwnd, (HMENU)(UINT_PTR)uac[i].id, hInst, NULL);
            if (hGuiFont) SendMessageW(hR, WM_SETFONT, (WPARAM)hGuiFont, TRUE);
            SendMessageW(hR, BM_SETCHECK,
                (s_uacLevel == i) ? BST_CHECKED : BST_UNCHECKED, 0);
            rx += S(180);
        }
        y += S(kRowStep);
    }

    // Privileges overrides allowed — works with the UAC radios above.
    // Determines whether the end user can switch between per-user / all-users at runtime.
    // "dialog" enables the For Me/All Users wizard page in the installer.
    {
        std::vector<std::wstring> overrideItems = {
            loc(L"sett_priv_override_none",        L"None (no override)"),
            loc(L"sett_priv_override_commandline", L"Command line only"),
            loc(L"sett_priv_override_dialog",      L"Dialog (For Me / All Users page)"),
        };
        y = LabelCombo(hwnd, hInst, hGuiFont, y, clientWidth,
                       loc(L"sett_priv_override_lbl", L"Allow override:"),
                       IDC_SETT_PRIV_OVERRIDES, overrideItems, s_privOverrides);
    }

    // Minimum OS version combo
    {
        std::vector<std::wstring> osItems = {
            loc(L"sett_minos_none",  L"None (any Windows)"),
            loc(L"sett_minos_win7",  L"Windows 7"),
            loc(L"sett_minos_win8",  L"Windows 8"),
            loc(L"sett_minos_win81", L"Windows 8.1"),
            loc(L"sett_minos_win10", L"Windows 10"),
            loc(L"sett_minos_win11", L"Windows 11"),
        };
        y = LabelCombo(hwnd, hInst, hGuiFont, y, clientWidth,
                       loc(L"sett_min_os_lbl", L"Minimum OS:"),
                       IDC_SETT_MIN_OS, osItems, s_minOsVersion);
    }

    // Wizard style combo
    {
        std::vector<std::wstring> styleItems = {
            loc(L"sett_wizard_modern",  L"Modern (Inno 6 sidebar bitmap)"),
            loc(L"sett_wizard_classic", L"Classic (top banner, Inno 5 style)"),
        };
        y = LabelCombo(hwnd, hInst, hGuiFont, y, clientWidth,
                       loc(L"sett_wizard_style_lbl", L"Wizard style:"),
                       IDC_SETT_WIZARD_STYLE, styleItems, s_wizardStyle);
    }

    // Default dir base combo + custom Inno constant edit (inline, same row)
    {
        const int fldX   = S(kPadH) + S(kLblW) + S(kLblGap);
        const int comboW = S(185);
        const int editX  = fldX + comboW + S(6);
        const int editW  = clientWidth - editX - S(kPadH);

        HWND hLbl = CreateWindowExW(0, L"STATIC",
            loc(L"sett_install_base_lbl", L"Default dir base:").c_str(),
            WS_CHILD | WS_VISIBLE | SS_RIGHT | SS_CENTERIMAGE,
            S(kPadH), y, S(kLblW), S(kRowH),
            hwnd, NULL, hInst, NULL);
        if (hGuiFont) SendMessageW(hLbl, WM_SETFONT, (WPARAM)hGuiFont, TRUE);

        std::vector<std::wstring> baseItems = {
            loc(L"sett_install_base_pf",            L"{pf}  \u2014  Program Files"),
            loc(L"sett_install_base_pf64",          L"{pf64}  \u2014  64-bit Program Files"),
            loc(L"sett_install_base_pf32",          L"{pf32}  \u2014  32-bit Program Files"),
            loc(L"sett_install_base_localappdata",  L"{localappdata}  \u2014  AppData\\Local"),
            loc(L"sett_install_base_commonappdata", L"{commonappdata}  \u2014  AppData (all users)"),
            loc(L"sett_install_base_userdocs",      L"{userdocs}  \u2014  Documents"),
            loc(L"sett_install_base_custom",        L"Custom\u2026"),
        };
        HWND hCbo = CreateWindowExW(0, L"COMBOBOX", L"",
            WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | WS_VSCROLL,
            fldX, y, comboW, S(kRowH) + S(200),
            hwnd, (HMENU)(UINT_PTR)IDC_SETT_INSTALL_BASE, hInst, NULL);
        if (hGuiFont) SendMessageW(hCbo, WM_SETFONT, (WPARAM)hGuiFont, TRUE);
        for (const auto& item : baseItems)
            SendMessageW(hCbo, CB_ADDSTRING, 0, (LPARAM)item.c_str());
        SendMessageW(hCbo, CB_SETCURSEL, (WPARAM)s_installBase, 0);

        // Custom edit — visible only when Custom (index 6) is selected
        HWND hCustomEdit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT",
            s_installBaseCustom.c_str(),
            WS_CHILD | WS_VISIBLE | ES_LEFT | ES_AUTOHSCROLL,
            editX, y, (editW > 0 ? editW : S(120)), S(kRowH),
            hwnd, (HMENU)(UINT_PTR)IDC_SETT_INSTALL_BASE_CUSTOM, hInst, NULL);
        if (hGuiFont) SendMessageW(hCustomEdit, WM_SETFONT, (WPARAM)hGuiFont, TRUE);
        s_hInstallBaseCustomEdit = hCustomEdit;
        if (s_installBase != 6) ShowWindow(hCustomEdit, SW_HIDE);

        y += S(kRowStep);
    }

    // Disable "Where to install?" page
    y = FieldCheckbox(hwnd, hInst, hGuiFont, y, clientWidth,
                      loc(L"sett_disable_dir_page_lbl",
                          L"Hide \"Where to install?\" wizard page (fixed location)"),
                      IDC_SETT_DISABLE_DIR_PAGE, s_disableDirPage);

    // Disable "Select Start Menu Folder" page
    y = FieldCheckbox(hwnd, hInst, hGuiFont, y, clientWidth,
                      loc(L"sett_disable_prog_group_page_lbl",
                          L"Hide \"Select Start Menu folder\" wizard page"),
                      IDC_SETT_DISABLE_PROG_GROUP_PAGE, s_disableProgramGroupPage);

    // Remember previously chosen install path / group across upgrades
    y = FieldCheckbox(hwnd, hInst, hGuiFont, y, clientWidth,
                      loc(L"sett_use_prev_app_dir_lbl",
                          L"Remember last install path across upgrades (UsePreviousAppDir)"),
                      IDC_SETT_USE_PREV_APP_DIR, s_usePreviousAppDir);
    y = FieldCheckbox(hwnd, hInst, hGuiFont, y, clientWidth,
                      loc(L"sett_use_prev_group_lbl",
                          L"Remember last Start Menu folder across upgrades (UsePreviousGroup)"),
                      IDC_SETT_USE_PREV_GROUP, s_usePreviousGroup);

    // Existing directory warning
    {
        std::vector<std::wstring> dewItems = {
            loc(L"sett_dir_exists_auto", L"Auto (warn only when not upgrading)"),
            loc(L"sett_dir_exists_yes",  L"Yes (always warn)"),
            loc(L"sett_dir_exists_no",   L"No (never warn)"),
        };
        y = LabelCombo(hwnd, hInst, hGuiFont, y, clientWidth,
                       loc(L"sett_dir_exists_warning_lbl", L"Dir-exists warning:"),
                       IDC_SETT_DIR_EXISTS_WARNING, dewItems, s_dirExistsWarning);
    }

    // ════════════════════════════════════════════════════════════════════════
    // Section 4: Installer Languages
    // ╚══════════════════════════════════════════════════════════════════════
    y += S(kSecGap);
    y = SectionHeader(hwnd, hInst, s_hSectionFont, y, clientWidth,
                      loc(L"sett_sec_languages", L"Installer Languages"), IDC_SETT_SEC_LANGUAGES);

    // Hint text
    {
        HWND hHint = CreateWindowExW(0, L"STATIC",
            loc(L"sett_languages_hint",
                L"English is always included. Tick additional languages — "
                L"the installer will show a language picker when 2 or more are selected.").c_str(),
            WS_CHILD | WS_VISIBLE | SS_LEFT,
            S(kPadH), y, clientWidth - S(kPadH) * 2, S(kRowH) * 2,
            hwnd, NULL, hInst, NULL);
        if (hGuiFont) SendMessageW(hHint, WM_SETFONT, (WPARAM)hGuiFont, TRUE);
        y += S(kRowH) * 2 + S(6);
    }

    // Language checkbox grid — 3 columns
    {
        const int nLangs  = ((int)s_installerLangs.size() < kLangCount)
                             ? (int)s_installerLangs.size() : kLangCount;
        const int colW    = (clientWidth - S(kPadH) * 2) / 3;
        const int chkH    = S(kRowH);
        const int chkStep = S(kRowH) + S(3);

        for (int i = 0; i < nLangs; i++) {
            int col = i % 3;
            int row = i / 3;
            int cx  = S(kPadH) + col * colW;
            int cy  = y + row * chkStep;
            HWND hChk = CreateCustomCheckbox(hwnd, IDC_SETT_LANG_BASE + i,
                kInnoLangs[i].displayName,
                s_installerLangs[i],
                cx, cy, colW - S(4), chkH, hInst);
            if (hGuiFont) SendMessageW(hChk, WM_SETFONT, (WPARAM)hGuiFont, TRUE);
            if (i == 0) EnableWindow(hChk, FALSE);  // English always included
        }
        int numRows = (nLangs + 2) / 3;
        y += numRows * chkStep + S(10);
    }

    // Language detection method
    {
        std::initializer_list<std::wstring> ldItems = {
            loc(L"sett_lang_detection_uilanguage", L"Windows display language (uilanguage)"),
            loc(L"sett_lang_detection_locale",     L"Windows region (locale)"),
            loc(L"sett_lang_detection_none",       L"None"),
        };
        y = LabelCombo(hwnd, hInst, hGuiFont, y, clientWidth,
                       loc(L"sett_lang_detection_lbl", L"Language detection:"),
                       IDC_SETT_LANG_DETECTION, ldItems, s_langDetectionMethod);
    }

    // Show language picker dialog
    {
        std::initializer_list<std::wstring> sldItems = {
            loc(L"sett_show_lang_dlg_auto", L"Auto (if no match found)"),
            loc(L"sett_show_lang_dlg_yes",  L"Always"),
            loc(L"sett_show_lang_dlg_no",   L"Never"),
        };
        y = LabelCombo(hwnd, hInst, hGuiFont, y, clientWidth,
                       loc(L"sett_show_lang_dlg_lbl", L"Show language picker:"),
                       IDC_SETT_SHOW_LANG_DLG, sldItems, s_showLanguageDialog);
    }

    // ╔══════════════════════════════════════════════════════════════════════
    // Section 5: System Integration
    // ════════════════════════════════════════════════════════════════════════
    y += S(kSecGap);
    y = SectionHeader(hwnd, hInst, s_hSectionFont, y, clientWidth,
                      loc(L"sett_sec_system_integration", L"System Integration"), IDC_SETT_SEC_SYS_INT);

    // Close applications before install
    y = FieldCheckbox(hwnd, hInst, hGuiFont, y, clientWidth,
                      loc(L"sett_close_apps_lbl",
                          L"Close running applications before installing"),
                      IDC_SETT_CLOSE_APPS, s_closeApps);

    // PATH folders: label + Add button + Remove-last button + display label
    {
        const int fldX   = S(kPadH) + S(kLblW) + S(kLblGap);
        const int btnW   = S(28);
        const int btnGap = S(4);

        HWND hL = CreateWindowExW(0, L"STATIC",
            loc(L"sett_path_folders_lbl", L"PATH folders:").c_str(),
            WS_CHILD | WS_VISIBLE | SS_RIGHT | SS_CENTERIMAGE,
            S(kPadH), y, S(kLblW), S(kRowH), hwnd, NULL, hInst, NULL);
        if (hGuiFont) SendMessageW(hL, WM_SETFONT, (WPARAM)hGuiFont, TRUE);

        // Add folder button (blue, folder icon)
        CreateCustomButtonWithIcon(hwnd, IDC_SETT_PATH_ADD_BTN, L"",
            ButtonColor::Blue, L"shell32.dll", 4,
            fldX, y, btnW, S(kRowH), hInst);

        // Remove last button (red X icon)
        HWND hRem = CreateCustomButtonWithIcon(hwnd, IDC_SETT_PATH_REMOVE_BTN, L"",
            ButtonColor::Red, L"shell32.dll", 131,
            fldX + btnW + btnGap, y, btnW, S(kRowH), hInst);
        EnableWindow(hRem, s_pathFolders.empty() ? FALSE : TRUE);

        y += S(kRowStep);

        // Listbox — one leaf name per row; single-select; scrollable;
        // tooltip shows the full Inno constant path for the hovered item.
        s_hPathDisplay = CreateWindowExW(WS_EX_CLIENTEDGE, L"LISTBOX",
            NULL,
            WS_CHILD | WS_VISIBLE | WS_VSCROLL | LBS_NOTIFY | LBS_NOINTEGRALHEIGHT,
            fldX, y, clientWidth - fldX - S(kPadH), S(kRowH) * 3,
            hwnd, (HMENU)(UINT_PTR)IDC_SETT_PATH_DISPLAY, hInst, NULL);
        if (hGuiFont) SendMessageW(s_hPathDisplay, WM_SETFONT, (WPARAM)hGuiFont, TRUE);

        // Per-item tooltip — driven by PathListSubclassProc on WM_MOUSEMOVE;
        // uses project ShowMultilingualTooltip / HideTooltip (tooltip.h).

        // Subclass listbox to show/hide tooltip and track hovered item
        if (s_hPathDisplay)
            s_pathListOrigProc = (WNDPROC)SetWindowLongPtrW(
                s_hPathDisplay, GWLP_WNDPROC, (LONG_PTR)PathListSubclassProc);

        // Populate listbox from current state
        RefreshPathDisplay(hwnd);
        y += S(kRowH) * 3 + S(9);
    }

    // Broadcast WM_SETTINGCHANGE after install (needed when adding to PATH)
    y = FieldCheckbox(hwnd, hInst, hGuiFont, y, clientWidth,
                      loc(L"sett_changes_env_lbl",
                          L"Broadcast environment change (required when modifying PATH)"),
                      IDC_SETT_CHANGES_ENV, s_changesEnvironment);
    if (s_pathFolders.empty())
        EnableWindow(GetDlgItem(hwnd, IDC_SETT_CHANGES_ENV), FALSE);

    y += S(20);   // bottom padding

    // ╔══════════════════════════════════════════════════════════════════════
    // Section 6: Uninstall
    // ════════════════════════════════════════════════════════════════════════
    y += S(kSecGap);
    y = SectionHeader(hwnd, hInst, s_hSectionFont, y, clientWidth,
                      loc(L"sett_sec_uninstall", L"Uninstall"), IDC_SETT_SEC_UNINSTALL);

    // Allow uninstall toggle
    y = FieldCheckbox(hwnd, hInst, hGuiFont, y, clientWidth,
                      loc(L"sett_allow_uninstall_lbl",
                          L"Allow users to uninstall this application"),
                      IDC_SETT_ALLOW_UNINSTALL, s_allowUninstall);

    // UninstallDisplayName override
    y = LabelEdit(hwnd, hInst, hGuiFont, y, clientWidth,
                  loc(L"sett_uninstall_display_name_lbl",
                      L"Uninstall display name:"),
                  IDC_SETT_UNINSTALL_DISPLAY_NAME, s_uninstallDisplayName);

    // UninstallFilesDir override — edit + VFS folder picker button
    {
        const int fldX = S(kPadH) + S(kLblW) + S(kLblGap);
        const int btnW = S(28);
        const int fldW = clientWidth - fldX - S(kPadH) - btnW - S(6);

        HWND hL = CreateWindowExW(0, L"STATIC",
            loc(L"sett_uninstall_files_dir_lbl", L"Uninstaller location:").c_str(),
            WS_CHILD | WS_VISIBLE | SS_RIGHT | SS_CENTERIMAGE,
            S(kPadH), y, S(kLblW), S(kRowH),
            hwnd, NULL, hInst, NULL);
        if (hGuiFont) SendMessageW(hL, WM_SETFONT, (WPARAM)hGuiFont, TRUE);

        HWND hE = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", s_uninstallFilesDir.c_str(),
            WS_CHILD | WS_VISIBLE | ES_LEFT | ES_AUTOHSCROLL,
            fldX, y, fldW, S(kRowH),
            hwnd, (HMENU)(UINT_PTR)IDC_SETT_UNINSTALL_FILES_DIR, hInst, NULL);
        if (hGuiFont) SendMessageW(hE, WM_SETFONT, (WPARAM)hGuiFont, TRUE);

        CreateCustomButtonWithIcon(hwnd, IDC_SETT_UNINSTALL_FILES_DIR_BTN, L"",
            ButtonColor::Blue, L"shell32.dll", 4,
            fldX + fldW + S(6), y, btnW, S(kRowH), hInst);

        y += S(kRowStep);
    }

    y += S(20);   // bottom padding

    // ════════════════════════════════════════════════════════════════════════
    // Section 7: Setup Log
    // ════════════════════════════════════════════════════════════════════════
    y += S(kSecGap);
    y = SectionHeader(hwnd, hInst, s_hSectionFont, y, clientWidth,
                      loc(L"sett_sec_setup_log", L"Setup Log"), IDC_SETT_SEC_SETUP_LOG);
    memset(s_hLogControls, 0, sizeof(s_hLogControls));
    int nLogCtrl = 0;
    auto trackLog = [&](HWND h) { if (nLogCtrl < 8) s_hLogControls[nLogCtrl++] = h; };

    // Enable logging checkbox
    {
        const int fldX = S(kPadH) + S(kLblW) + S(kLblGap);
        const int fldW = clientWidth - fldX - S(kPadH);
        HWND hChk = CreateCustomCheckbox(hwnd, IDC_SETT_SETUP_LOG_ENABLE,
            loc(L"sett_setup_log_enable_lbl", L"Write setup log to file"),
            s_setupLogging, fldX, y, fldW, S(kRowH), hInst);
        if (hGuiFont) SendMessageW(hChk, WM_SETFONT, (WPARAM)hGuiFont, TRUE);
        y += S(kRowStep);
    }

    // Log folder (with browse button)
    {
        const int fldX = S(kPadH) + S(kLblW) + S(kLblGap);
        const int btnW = S(28);
        const int fldW = clientWidth - fldX - S(kPadH) - S(6) - btnW;
        HWND hL = CreateWindowExW(0, L"STATIC",
            loc(L"sett_setup_log_folder_lbl", L"Log folder:").c_str(),
            WS_CHILD | WS_VISIBLE | SS_RIGHT | SS_CENTERIMAGE,
            S(kPadH), y, S(kLblW), S(kRowH), hwnd, NULL, hInst, NULL);
        if (hGuiFont) SendMessageW(hL, WM_SETFONT, (WPARAM)hGuiFont, TRUE);
        HWND hE = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", s_setupLogFolder.c_str(),
            WS_CHILD | WS_VISIBLE | ES_LEFT | ES_AUTOHSCROLL,
            fldX, y, fldW, S(kRowH),
            hwnd, (HMENU)(UINT_PTR)IDC_SETT_SETUP_LOG_FOLDER, hInst, NULL);
        if (hGuiFont) SendMessageW(hE, WM_SETFONT, (WPARAM)hGuiFont, TRUE);
        HWND hBtn = CreateCustomButtonWithIcon(
            hwnd, IDC_SETT_SETUP_LOG_FOLDER_BTN, L"", ButtonColor::Blue,
            L"shell32.dll", 4, fldX + fldW + S(6), y, btnW, S(kRowH), hInst);
        trackLog(hL); trackLog(hE); trackLog(hBtn);
        y += S(kRowStep);
    }

    // Log filename
    {
        const int fldX = S(kPadH) + S(kLblW) + S(kLblGap);
        const int fldW = clientWidth - fldX - S(kPadH);
        HWND hL = CreateWindowExW(0, L"STATIC",
            loc(L"sett_setup_log_file_lbl", L"Log filename:").c_str(),
            WS_CHILD | WS_VISIBLE | SS_RIGHT | SS_CENTERIMAGE,
            S(kPadH), y, S(kLblW), S(kRowH), hwnd, NULL, hInst, NULL);
        if (hGuiFont) SendMessageW(hL, WM_SETFONT, (WPARAM)hGuiFont, TRUE);
        HWND hE = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", s_setupLogFilename.c_str(),
            WS_CHILD | WS_VISIBLE | ES_LEFT | ES_AUTOHSCROLL,
            fldX, y, fldW, S(kRowH),
            hwnd, (HMENU)(UINT_PTR)IDC_SETT_SETUP_LOG_FILE, hInst, NULL);
        if (hGuiFont) SendMessageW(hE, WM_SETFONT, (WPARAM)hGuiFont, TRUE);
        trackLog(hL); trackLog(hE);
        y += S(kRowStep);
    }

    // Write mode combo
    {
        std::vector<std::wstring> modeItems = {
            loc(L"sett_setup_log_overwrite", L"Overwrite (new file each run)"),
            loc(L"sett_setup_log_append",    L"Append to existing log"),
        };
        HWND hModeCbo = nullptr;
        y = LabelCombo(hwnd, hInst, hGuiFont, y, clientWidth,
                       loc(L"sett_setup_log_mode_lbl", L"Write mode:"),
                       IDC_SETT_SETUP_LOG_MODE, modeItems, s_setupLogMode,
                       &hModeCbo);
        if (hModeCbo) trackLog(hModeCbo);
    }

    ApplyLogEnable(s_setupLogging ? TRUE : FALSE);

    y += S(20);   // bottom padding

    // ══════════════════════════════════════════════════════════════════════
    // Section 8: Code Signing
    // ══════════════════════════════════════════════════════════════════════
    y += S(kSecGap);
    y = SectionHeader(hwnd, hInst, s_hSectionFont, y, clientWidth,
                      loc(L"sett_sec_signing", L"Code Signing"), IDC_SETT_SEC_SIGNING);
    memset(s_hSignControls, 0, sizeof(s_hSignControls));
    int nSignCtrl = 0;

    // helper lambda to add a HWND to the tracking array
    auto trackCtrl = [&](HWND h) { if (nSignCtrl < 20) s_hSignControls[nSignCtrl++] = h; };

    // Enable checkbox
    {
        const int fldX = S(kPadH) + S(kLblW) + S(kLblGap);
        const int fldW = clientWidth - fldX - S(kPadH);
        HWND hChk = CreateCustomCheckbox(hwnd, IDC_SETT_SIGN_ENABLE,
            loc(L"sett_sign_enable_lbl", L"Sign installer with Authenticode"),
            s_signEnabled, fldX, y, fldW, S(kRowH), hInst);
        if (hGuiFont) SendMessageW(hChk, WM_SETFONT, (WPARAM)hGuiFont, TRUE);
        y += S(kRowStep);
    }

    // signtool.exe path (label + edit + browse button)
    {
        const int fldX = S(kPadH) + S(kLblW) + S(kLblGap);
        const int btnW = S(28);
        const int fldW = clientWidth - fldX - S(kPadH) - S(6) - btnW;
        HWND hL = CreateWindowExW(0, L"STATIC",
            loc(L"sett_signtool_lbl", L"Signtool.exe:").c_str(),
            WS_CHILD | WS_VISIBLE | SS_RIGHT | SS_CENTERIMAGE,
            S(kPadH), y, S(kLblW), S(kRowH), hwnd, NULL, hInst, NULL);
        if (hGuiFont) SendMessageW(hL, WM_SETFONT, (WPARAM)hGuiFont, TRUE);
        HWND hE = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", s_signtoolPath.c_str(),
            WS_CHILD | WS_VISIBLE | ES_LEFT | ES_AUTOHSCROLL,
            fldX, y, fldW, S(kRowH),
            hwnd, (HMENU)(UINT_PTR)IDC_SETT_SIGNTOOL_PATH, hInst, NULL);
        if (hGuiFont) SendMessageW(hE, WM_SETFONT, (WPARAM)hGuiFont, TRUE);
        HWND hBtn = (HWND)CreateCustomButtonWithIcon(
            hwnd, IDC_SETT_SIGNTOOL_BTN, L"", ButtonColor::Blue,
            L"shell32.dll", 4, fldX + fldW + S(6), y, btnW, S(kRowH), hInst);
        trackCtrl(hL); trackCtrl(hE); trackCtrl(hBtn);
        y += S(kRowStep);
    }

    // Certificate thumbprint
    {
        const int fldX = S(kPadH) + S(kLblW) + S(kLblGap);
        const int fldW = clientWidth - fldX - S(kPadH);
        HWND hL = CreateWindowExW(0, L"STATIC",
            loc(L"sett_sign_thumbprint_lbl", L"Cert thumbprint:").c_str(),
            WS_CHILD | WS_VISIBLE | SS_RIGHT | SS_CENTERIMAGE,
            S(kPadH), y, S(kLblW), S(kRowH), hwnd, NULL, hInst, NULL);
        if (hGuiFont) SendMessageW(hL, WM_SETFONT, (WPARAM)hGuiFont, TRUE);
        HWND hE = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", s_signThumbprint.c_str(),
            WS_CHILD | WS_VISIBLE | ES_LEFT | ES_AUTOHSCROLL,
            fldX, y, fldW, S(kRowH),
            hwnd, (HMENU)(UINT_PTR)IDC_SETT_SIGN_THUMBPRINT, hInst, NULL);
        if (hGuiFont) SendMessageW(hE, WM_SETFONT, (WPARAM)hGuiFont, TRUE);
        trackCtrl(hL); trackCtrl(hE);
        y += S(kRowStep);
    }

    // PFX path (label + edit + browse button)
    {
        const int fldX = S(kPadH) + S(kLblW) + S(kLblGap);
        const int btnW = S(28);
        const int fldW = clientWidth - fldX - S(kPadH) - S(6) - btnW;
        HWND hL = CreateWindowExW(0, L"STATIC",
            loc(L"sett_sign_pfx_lbl", L"PFX file:").c_str(),
            WS_CHILD | WS_VISIBLE | SS_RIGHT | SS_CENTERIMAGE,
            S(kPadH), y, S(kLblW), S(kRowH), hwnd, NULL, hInst, NULL);
        if (hGuiFont) SendMessageW(hL, WM_SETFONT, (WPARAM)hGuiFont, TRUE);
        HWND hE = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", s_signPfxPath.c_str(),
            WS_CHILD | WS_VISIBLE | ES_LEFT | ES_AUTOHSCROLL,
            fldX, y, fldW, S(kRowH),
            hwnd, (HMENU)(UINT_PTR)IDC_SETT_SIGN_PFX_PATH, hInst, NULL);
        if (hGuiFont) SendMessageW(hE, WM_SETFONT, (WPARAM)hGuiFont, TRUE);
        HWND hBtn = (HWND)CreateCustomButtonWithIcon(
            hwnd, IDC_SETT_SIGN_PFX_BTN, L"", ButtonColor::Blue,
            L"shell32.dll", 4, fldX + fldW + S(6), y, btnW, S(kRowH), hInst);
        trackCtrl(hL); trackCtrl(hE); trackCtrl(hBtn);
        y += S(kRowStep);
    }

    // PFX password
    {
        const int fldX = S(kPadH) + S(kLblW) + S(kLblGap);
        const int fldW = clientWidth - fldX - S(kPadH);
        HWND hL = CreateWindowExW(0, L"STATIC",
            loc(L"sett_sign_pfx_pass_lbl", L"PFX password:").c_str(),
            WS_CHILD | WS_VISIBLE | SS_RIGHT | SS_CENTERIMAGE,
            S(kPadH), y, S(kLblW), S(kRowH), hwnd, NULL, hInst, NULL);
        if (hGuiFont) SendMessageW(hL, WM_SETFONT, (WPARAM)hGuiFont, TRUE);
        HWND hE = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", s_signPfxPassword.c_str(),
            WS_CHILD | WS_VISIBLE | ES_LEFT | ES_AUTOHSCROLL | ES_PASSWORD,
            fldX, y, fldW, S(kRowH),
            hwnd, (HMENU)(UINT_PTR)IDC_SETT_SIGN_PFX_PASS, hInst, NULL);
        if (hGuiFont) SendMessageW(hE, WM_SETFONT, (WPARAM)hGuiFont, TRUE);
        trackCtrl(hL); trackCtrl(hE);
        y += S(kRowStep);
    }

    // Timestamp URL
    {
        y = LabelEdit(hwnd, hInst, hGuiFont, y, clientWidth,
                      loc(L"sett_sign_ts_url_lbl", L"Timestamp URL:"),
                      IDC_SETT_SIGN_TS_URL, s_signTimestampUrl);
        trackCtrl(GetDlgItem(hwnd, IDC_SETT_SIGN_TS_URL));
    }

    // Timestamp algorithm combo
    {
        std::vector<std::wstring> algoItems = {
            L"SHA-1",
            L"SHA-256",
        };
        y = LabelCombo(hwnd, hInst, hGuiFont, y, clientWidth,
                       loc(L"sett_sign_ts_algo_lbl", L"Timestamp digest:"),
                       IDC_SETT_SIGN_TS_ALGO, algoItems, s_signTimestampAlgo);
        trackCtrl(GetDlgItem(hwnd, IDC_SETT_SIGN_TS_ALGO));
    }

    // Description override
    {
        y = LabelEdit(hwnd, hInst, hGuiFont, y, clientWidth,
                      loc(L"sett_sign_desc_lbl", L"Description:"),
                      IDC_SETT_SIGN_DESC, s_signDescription);
        trackCtrl(GetDlgItem(hwnd, IDC_SETT_SIGN_DESC));
    }

    // Apply enabled/disabled state to dependent controls
    ApplySignEnable(s_signEnabled ? TRUE : FALSE);

    y += S(20);   // bottom padding
    return y;
}

// ── SETT_OnCommand ────────────────────────────────────────────────────────────
bool SETT_OnCommand(HWND hwnd, int wmId, int wmEvent, HWND /*hCtrl*/)
{
    // IDC_SETT_APP_NAME, IDC_SETT_APP_VERSION, IDC_SETT_PUBLISHER,
    // IDC_SETT_CHANGE_ICON and IDC_SETT_REGEN_GUID are handled by
    // mainwindow.cpp (same as the Registry-page equivalents).

    if (wmId == IDC_SETT_OUTPUT_FOLDER && wmEvent == EN_CHANGE) {
        wchar_t buf[MAX_PATH] = {};
        GetDlgItemTextW(hwnd, IDC_SETT_OUTPUT_FOLDER, buf, MAX_PATH);
        s_outputFolder = buf;
        MainWindow::MarkAsModified();
        return true;
    }
    if (wmId == IDC_SETT_OUTPUT_FOLDER_BTN && wmEvent == BN_CLICKED) {
        // Browse for output folder
        BROWSEINFOW bi = {};
        bi.hwndOwner = hwnd;
        bi.lpszTitle = loc(L"sett_output_folder_picker_title", L"Select output folder").c_str();
        bi.ulFlags   = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE | BIF_USENEWUI;
        LPITEMIDLIST pidl = SHBrowseForFolderW(&bi);
        if (pidl) {
            wchar_t path[MAX_PATH] = {};
            if (SHGetPathFromIDListW(pidl, path)) {
                s_outputFolder = path;
                SetDlgItemTextW(hwnd, IDC_SETT_OUTPUT_FOLDER, path);
                MainWindow::MarkAsModified();
            }
            CoTaskMemFree(pidl);
        }
        return true;
    }
    if (wmId == IDC_SETT_PUBLISHER_URL && wmEvent == EN_CHANGE) {
        wchar_t buf[512] = {};
        GetDlgItemTextW(hwnd, IDC_SETT_PUBLISHER_URL, buf, 512);
        s_publisherUrl = buf;
        MainWindow::MarkAsModified();
        return true;
    }
    if (wmId == IDC_SETT_SUPPORT_URL && wmEvent == EN_CHANGE) {
        wchar_t buf[512] = {};
        GetDlgItemTextW(hwnd, IDC_SETT_SUPPORT_URL, buf, 512);
        s_supportUrl = buf;
        MainWindow::MarkAsModified();
        return true;
    }
    if (wmId == IDC_SETT_OUTPUT_FILE && wmEvent == EN_CHANGE) {
        wchar_t buf[MAX_PATH] = {};
        GetDlgItemTextW(hwnd, IDC_SETT_OUTPUT_FILE, buf, MAX_PATH);
        s_outputFilename = buf;
        MainWindow::MarkAsModified();
        return true;
    }
    if (wmId == IDC_SETT_COMPRESSION && wmEvent == CBN_SELCHANGE) {
        int sel = (int)SendDlgItemMessageW(hwnd, IDC_SETT_COMPRESSION, CB_GETCURSEL, 0, 0);
        if (sel >= 0) s_compressionType = sel;
        MainWindow::MarkAsModified();
        return true;
    }
    if (wmId == IDC_SETT_COMP_LEVEL && wmEvent == CBN_SELCHANGE) {
        int sel = (int)SendDlgItemMessageW(hwnd, IDC_SETT_COMP_LEVEL, CB_GETCURSEL, 0, 0);
        if (sel >= 0) s_compressionLevel = sel;
        MainWindow::MarkAsModified();
        return true;
    }
    if (wmId == IDC_SETT_SOLID && wmEvent == BN_CLICKED) {
        s_solidCompression =
            (SendDlgItemMessageW(hwnd, IDC_SETT_SOLID, BM_GETCHECK, 0, 0) == BST_CHECKED);
        MainWindow::MarkAsModified();
        return true;
    }
    if ((wmId == IDC_SETT_UAC_REQADMIN ||
         wmId == IDC_SETT_UAC_INVOKER  ||
         wmId == IDC_SETT_UAC_HIGHEST) && wmEvent == BN_CLICKED) {
        s_uacLevel = (wmId == IDC_SETT_UAC_REQADMIN) ? 0 :
                     (wmId == IDC_SETT_UAC_INVOKER)  ? 1 : 2;
        MainWindow::MarkAsModified();
        return true;
    }
    if (wmId == IDC_SETT_PRIV_OVERRIDES && wmEvent == CBN_SELCHANGE) {
        int sel = (int)SendDlgItemMessageW(hwnd, IDC_SETT_PRIV_OVERRIDES, CB_GETCURSEL, 0, 0);
        if (sel >= 0) s_privOverrides = sel;
        MainWindow::MarkAsModified();
        return true;
    }
    if (wmId == IDC_SETT_WIZARD_STYLE && wmEvent == CBN_SELCHANGE) {
        int sel = (int)SendDlgItemMessageW(hwnd, IDC_SETT_WIZARD_STYLE, CB_GETCURSEL, 0, 0);
        if (sel >= 0) s_wizardStyle = sel;
        MainWindow::MarkAsModified();
        return true;
    }
    if (wmId == IDC_SETT_MIN_OS && wmEvent == CBN_SELCHANGE) {
        int sel = (int)SendDlgItemMessageW(hwnd, IDC_SETT_MIN_OS, CB_GETCURSEL, 0, 0);
        if (sel >= 0) s_minOsVersion = sel;
        MainWindow::MarkAsModified();
        return true;
    }
    if (wmId >= IDC_SETT_LANG_BASE + 1 &&
        wmId <  IDC_SETT_LANG_BASE + kLangCount &&
        wmEvent == BN_CLICKED) {
        int idx = wmId - IDC_SETT_LANG_BASE;
        if (idx > 0 && idx < (int)s_installerLangs.size())
            s_installerLangs[idx] =
                (SendDlgItemMessageW(hwnd, wmId, BM_GETCHECK, 0, 0) == BST_CHECKED);
        MainWindow::MarkAsModified();
        return true;
    }
    if (wmId == IDC_SETT_INSTALL_BASE && wmEvent == CBN_SELCHANGE) {
        int sel = (int)SendDlgItemMessageW(hwnd, IDC_SETT_INSTALL_BASE, CB_GETCURSEL, 0, 0);
        if (sel >= 0) s_installBase = sel;
        if (s_hInstallBaseCustomEdit)
            ShowWindow(s_hInstallBaseCustomEdit, (sel == 6) ? SW_SHOW : SW_HIDE);
        MainWindow::MarkAsModified();
        return true;
    }
    if (wmId == IDC_SETT_INSTALL_BASE_CUSTOM && wmEvent == EN_CHANGE) {
        wchar_t buf[512] = {};
        GetDlgItemTextW(hwnd, IDC_SETT_INSTALL_BASE_CUSTOM, buf, 512);
        s_installBaseCustom = buf;
        MainWindow::MarkAsModified();
        return true;
    }
    if (wmId == IDC_SETT_ALLOW_UNINSTALL && wmEvent == BN_CLICKED) {
        s_allowUninstall =
            (SendDlgItemMessageW(hwnd, IDC_SETT_ALLOW_UNINSTALL, BM_GETCHECK, 0, 0) == BST_CHECKED);
        MainWindow::MarkAsModified();
        return true;
    }
    if (wmId == IDC_SETT_UNINSTALL_DISPLAY_NAME && wmEvent == EN_CHANGE) {
        wchar_t buf[512] = {};
        GetDlgItemTextW(hwnd, IDC_SETT_UNINSTALL_DISPLAY_NAME, buf, 512);
        s_uninstallDisplayName = buf;
        MainWindow::MarkAsModified();
        return true;
    }
    if (wmId == IDC_SETT_UNINSTALL_FILES_DIR && wmEvent == EN_CHANGE) {
        wchar_t buf[512] = {};
        GetDlgItemTextW(hwnd, IDC_SETT_UNINSTALL_FILES_DIR, buf, 512);
        s_uninstallFilesDir = buf;
        MainWindow::MarkAsModified();
        return true;
    }
    if (wmId == IDC_SETT_UNINSTALL_FILES_DIR_BTN && wmEvent == BN_CLICKED) {
        VfsPickerParams p;
        p.title           = loc(L"sett_uninstall_files_dir_picker_title", L"Select uninstaller folder");
        p.okText          = loc(L"scdlg_ok",                             L"OK");
        p.cancelText      = loc(L"scdlg_cancel",                          L"Cancel");
        p.foldersLabel    = loc(L"vfspicker_folders_label",               L"Folders");
        p.noSelMessage    = loc(L"vfspicker_folder_no_sel",
            L"Please select a folder from the left pane.");
        p.rootLabel_ProgramFiles  = loc(L"vfspicker_root_program_files", L"Program Files");
        p.rootLabel_ProgramData   = loc(L"vfspicker_root_program_data",  L"ProgramData");
        p.rootLabel_AppData       = loc(L"vfspicker_root_appdata",       L"AppData (Roaming)");
        p.rootLabel_AskAtInstall  = loc(L"vfspicker_root_ask_install",   L"Ask at install");
        p.singleSelect    = true;
        p.showFilePane    = true;
        p.fileFilter      = VfsPicker_IsExecutable;
        p.allowFolderPick = true;
        std::vector<VfsPickerResult> picks;
        if (s_pLocale && ShowVfsPicker(hwnd, s_hInst, p, *s_pLocale, picks)) {
            s_uninstallFilesDir = picks[0].virtualFolderPath;
            SetDlgItemTextW(hwnd, IDC_SETT_UNINSTALL_FILES_DIR, s_uninstallFilesDir.c_str());
            MainWindow::MarkAsModified();
        }
        return true;
    }
    if (wmId == IDC_SETT_CLOSE_APPS && wmEvent == BN_CLICKED) {
        s_closeApps =
            (SendDlgItemMessageW(hwnd, IDC_SETT_CLOSE_APPS, BM_GETCHECK, 0, 0) == BST_CHECKED);
        MainWindow::MarkAsModified();
        return true;
    }
    if (wmId == IDC_SETT_CHANGES_ENV && wmEvent == BN_CLICKED) {
        s_changesEnvironment =
            (SendDlgItemMessageW(hwnd, IDC_SETT_CHANGES_ENV, BM_GETCHECK, 0, 0) == BST_CHECKED);
        MainWindow::MarkAsModified();
        return true;
    }
    if (wmId == IDC_SETT_PATH_ADD_BTN && wmEvent == BN_CLICKED) {
        VfsPickerParams p;
        p.title           = loc(L"sett_path_folder_picker_title", L"Select folder to add to PATH");
        p.okText          = loc(L"scdlg_ok",                      L"OK");
        p.cancelText      = loc(L"scdlg_cancel",                   L"Cancel");
        p.foldersLabel    = loc(L"vfspicker_folders_label",        L"Folders");
        p.noSelMessage    = loc(L"vfspicker_folder_no_sel",
            L"Please select a folder from the left pane.");
        p.rootLabel_ProgramFiles  = loc(L"vfspicker_root_program_files", L"Program Files");
        p.rootLabel_ProgramData   = loc(L"vfspicker_root_program_data",  L"ProgramData");
        p.rootLabel_AppData       = loc(L"vfspicker_root_appdata",       L"AppData (Roaming)");
        p.rootLabel_AskAtInstall  = loc(L"vfspicker_root_ask_install",   L"Ask at install");
        p.singleSelect    = true;
        p.showFilePane    = false;
        p.allowFolderPick = true;
        std::vector<VfsPickerResult> picks;
        if (s_pLocale && ShowVfsPicker(hwnd, s_hInst, p, *s_pLocale, picks)) {
            // virtualFolderPath is the Inno constant installed-location path
            // (e.g. {pf}\MyApp\bin); sourcePath is the real disk source folder.
            s_pathFolders.push_back(picks[0].virtualFolderPath);
            RefreshPathDisplay(hwnd);
            // Auto-select the newly added item
            int newIdx = (int)s_pathFolders.size() - 1;
            if (s_hPathDisplay) {
                SendMessageW(s_hPathDisplay, LB_SETCURSEL, newIdx, 0);
                EnableWindow(GetDlgItem(hwnd, IDC_SETT_PATH_REMOVE_BTN), TRUE);
            }
            MainWindow::MarkAsModified();
        }
        return true;
    }
    if (wmId == IDC_SETT_PATH_DISPLAY && wmEvent == LBN_SELCHANGE) {
        int sel = s_hPathDisplay
                ? (int)SendMessageW(s_hPathDisplay, LB_GETCURSEL, 0, 0)
                : LB_ERR;
        HWND hRem = GetDlgItem(hwnd, IDC_SETT_PATH_REMOVE_BTN);
        if (hRem) EnableWindow(hRem, (sel != LB_ERR) ? TRUE : FALSE);
        return true;
    }
    if (wmId == IDC_SETT_PATH_REMOVE_BTN && wmEvent == BN_CLICKED) {
        int sel = s_hPathDisplay
                ? (int)SendMessageW(s_hPathDisplay, LB_GETCURSEL, 0, 0)
                : LB_ERR;
        if (sel != LB_ERR && sel < (int)s_pathFolders.size()) {
            s_pathFolders.erase(s_pathFolders.begin() + sel);
            RefreshPathDisplay(hwnd);
            // Re-select the nearest remaining item
            int remaining = (int)s_pathFolders.size();
            if (remaining > 0 && s_hPathDisplay) {
                int newSel = (sel < remaining) ? sel : remaining - 1;
                SendMessageW(s_hPathDisplay, LB_SETCURSEL, newSel, 0);
                EnableWindow(GetDlgItem(hwnd, IDC_SETT_PATH_REMOVE_BTN), TRUE);
            }
            MainWindow::MarkAsModified();
        }
        return true;
    }
    if (wmId == IDC_SETT_DISABLE_DIR_PAGE && wmEvent == BN_CLICKED) {
        s_disableDirPage =
            (SendDlgItemMessageW(hwnd, IDC_SETT_DISABLE_DIR_PAGE, BM_GETCHECK, 0, 0) == BST_CHECKED);
        MainWindow::MarkAsModified();
        return true;
    }
    if (wmId == IDC_SETT_DISABLE_PROG_GROUP_PAGE && wmEvent == BN_CLICKED) {
        s_disableProgramGroupPage =
            (SendDlgItemMessageW(hwnd, IDC_SETT_DISABLE_PROG_GROUP_PAGE, BM_GETCHECK, 0, 0) == BST_CHECKED);
        MainWindow::MarkAsModified();
        return true;
    }
    if (wmId == IDC_SETT_USE_PREV_APP_DIR && wmEvent == BN_CLICKED) {
        s_usePreviousAppDir =
            (SendDlgItemMessageW(hwnd, IDC_SETT_USE_PREV_APP_DIR, BM_GETCHECK, 0, 0) == BST_CHECKED);
        MainWindow::MarkAsModified();
        return true;
    }
    if (wmId == IDC_SETT_USE_PREV_GROUP && wmEvent == BN_CLICKED) {
        s_usePreviousGroup =
            (SendDlgItemMessageW(hwnd, IDC_SETT_USE_PREV_GROUP, BM_GETCHECK, 0, 0) == BST_CHECKED);
        MainWindow::MarkAsModified();
        return true;
    }
    if (wmId == IDC_SETT_DIR_EXISTS_WARNING && wmEvent == CBN_SELCHANGE) {
        int sel = (int)SendDlgItemMessageW(hwnd, IDC_SETT_DIR_EXISTS_WARNING, CB_GETCURSEL, 0, 0);
        if (sel >= 0) s_dirExistsWarning = sel;
        MainWindow::MarkAsModified();
        return true;
    }
    if (wmId == IDC_SETT_LANG_DETECTION && wmEvent == CBN_SELCHANGE) {
        int sel = (int)SendDlgItemMessageW(hwnd, IDC_SETT_LANG_DETECTION, CB_GETCURSEL, 0, 0);
        if (sel >= 0) s_langDetectionMethod = sel;
        MainWindow::MarkAsModified();
        return true;
    }
    if (wmId == IDC_SETT_SHOW_LANG_DLG && wmEvent == CBN_SELCHANGE) {
        int sel = (int)SendDlgItemMessageW(hwnd, IDC_SETT_SHOW_LANG_DLG, CB_GETCURSEL, 0, 0);
        if (sel >= 0) s_showLanguageDialog = sel;
        MainWindow::MarkAsModified();
        return true;
    }
    // ── Setup Log ─────────────────────────────────────────────────────────────────────────────────
    if (wmId == IDC_SETT_SETUP_LOG_ENABLE && wmEvent == BN_CLICKED) {
        s_setupLogging =
            (SendDlgItemMessageW(hwnd, IDC_SETT_SETUP_LOG_ENABLE, BM_GETCHECK, 0, 0) == BST_CHECKED);
        ApplyLogEnable(s_setupLogging ? TRUE : FALSE);
        MainWindow::MarkAsModified();
        return true;
    }
    if (wmId == IDC_SETT_SETUP_LOG_FOLDER && wmEvent == EN_CHANGE) {
        wchar_t buf[MAX_PATH] = {};
        GetDlgItemTextW(hwnd, IDC_SETT_SETUP_LOG_FOLDER, buf, MAX_PATH);
        s_setupLogFolder = buf;
        MainWindow::MarkAsModified();
        return true;
    }
    if (wmId == IDC_SETT_SETUP_LOG_FOLDER_BTN && wmEvent == BN_CLICKED) {
        BROWSEINFOW bi = {};
        bi.hwndOwner = hwnd;
        bi.lpszTitle = loc(L"sett_setup_log_folder_picker_title", L"Select log folder").c_str();
        bi.ulFlags   = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE | BIF_USENEWUI;
        LPITEMIDLIST pidl = SHBrowseForFolderW(&bi);
        if (pidl) {
            wchar_t path[MAX_PATH] = {};
            if (SHGetPathFromIDListW(pidl, path)) {
                s_setupLogFolder = path;
                SetDlgItemTextW(hwnd, IDC_SETT_SETUP_LOG_FOLDER, path);
                MainWindow::MarkAsModified();
            }
            CoTaskMemFree(pidl);
        }
        return true;
    }
    if (wmId == IDC_SETT_SETUP_LOG_FILE && wmEvent == EN_CHANGE) {
        wchar_t buf[MAX_PATH] = {};
        GetDlgItemTextW(hwnd, IDC_SETT_SETUP_LOG_FILE, buf, MAX_PATH);
        s_setupLogFilename = buf;
        MainWindow::MarkAsModified();
        return true;
    }
    if (wmId == IDC_SETT_SETUP_LOG_MODE && wmEvent == CBN_SELCHANGE) {
        int sel = (int)SendDlgItemMessageW(hwnd, IDC_SETT_SETUP_LOG_MODE, CB_GETCURSEL, 0, 0);
        if (sel >= 0) s_setupLogMode = sel;
        MainWindow::MarkAsModified();
        return true;
    }
    // ── Code signing ──────────────────────────────────────────────────────────────────── ──────────────────────────────────────────────────────
    if (wmId == IDC_SETT_SIGN_ENABLE && wmEvent == BN_CLICKED) {
        s_signEnabled =
            (SendDlgItemMessageW(hwnd, IDC_SETT_SIGN_ENABLE, BM_GETCHECK, 0, 0) == BST_CHECKED);
        ApplySignEnable(s_signEnabled ? TRUE : FALSE);
        MainWindow::MarkAsModified();
        return true;
    }
    if (wmId == IDC_SETT_SIGNTOOL_PATH && wmEvent == EN_CHANGE) {
        wchar_t buf[MAX_PATH] = {};
        GetDlgItemTextW(hwnd, IDC_SETT_SIGNTOOL_PATH, buf, MAX_PATH);
        s_signtoolPath = buf;
        MainWindow::MarkAsModified();
        return true;
    }
    if (wmId == IDC_SETT_SIGNTOOL_BTN && wmEvent == BN_CLICKED) {
        wchar_t path[MAX_PATH] = {};
        OPENFILENAMEW ofn = {};
        ofn.lStructSize = sizeof(ofn);
        ofn.hwndOwner   = hwnd;
        ofn.lpstrFilter = L"signtool.exe\0signtool.exe\0Executables (*.exe)\0*.exe\0All Files (*.*)\0*.*\0";
        ofn.lpstrFile   = path;
        ofn.nMaxFile    = MAX_PATH;
        ofn.Flags       = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
        if (GetOpenFileNameW(&ofn)) {
            s_signtoolPath = path;
            SetDlgItemTextW(hwnd, IDC_SETT_SIGNTOOL_PATH, path);
            MainWindow::MarkAsModified();
        }
        return true;
    }
    if (wmId == IDC_SETT_SIGN_THUMBPRINT && wmEvent == EN_CHANGE) {
        wchar_t buf[256] = {};
        GetDlgItemTextW(hwnd, IDC_SETT_SIGN_THUMBPRINT, buf, 256);
        s_signThumbprint = buf;
        MainWindow::MarkAsModified();
        return true;
    }
    if (wmId == IDC_SETT_SIGN_PFX_PATH && wmEvent == EN_CHANGE) {
        wchar_t buf[MAX_PATH] = {};
        GetDlgItemTextW(hwnd, IDC_SETT_SIGN_PFX_PATH, buf, MAX_PATH);
        s_signPfxPath = buf;
        MainWindow::MarkAsModified();
        return true;
    }
    if (wmId == IDC_SETT_SIGN_PFX_BTN && wmEvent == BN_CLICKED) {
        wchar_t path[MAX_PATH] = {};
        OPENFILENAMEW ofn = {};
        ofn.lStructSize = sizeof(ofn);
        ofn.hwndOwner   = hwnd;
        ofn.lpstrFilter = L"PFX Certificate (*.pfx;*.p12)\0*.pfx;*.p12\0All Files (*.*)\0*.*\0";
        ofn.lpstrFile   = path;
        ofn.nMaxFile    = MAX_PATH;
        ofn.Flags       = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
        if (GetOpenFileNameW(&ofn)) {
            s_signPfxPath = path;
            SetDlgItemTextW(hwnd, IDC_SETT_SIGN_PFX_PATH, path);
            MainWindow::MarkAsModified();
        }
        return true;
    }
    if (wmId == IDC_SETT_SIGN_PFX_PASS && wmEvent == EN_CHANGE) {
        wchar_t buf[256] = {};
        GetDlgItemTextW(hwnd, IDC_SETT_SIGN_PFX_PASS, buf, 256);
        s_signPfxPassword = buf;
        MainWindow::MarkAsModified();
        return true;
    }
    if (wmId == IDC_SETT_SIGN_TS_URL && wmEvent == EN_CHANGE) {
        wchar_t buf[512] = {};
        GetDlgItemTextW(hwnd, IDC_SETT_SIGN_TS_URL, buf, 512);
        s_signTimestampUrl = buf;
        MainWindow::MarkAsModified();
        return true;
    }
    if (wmId == IDC_SETT_SIGN_TS_ALGO && wmEvent == CBN_SELCHANGE) {
        int sel = (int)SendDlgItemMessageW(hwnd, IDC_SETT_SIGN_TS_ALGO, CB_GETCURSEL, 0, 0);
        if (sel >= 0) s_signTimestampAlgo = sel;
        MainWindow::MarkAsModified();
        return true;
    }
    if (wmId == IDC_SETT_SIGN_DESC && wmEvent == EN_CHANGE) {
        wchar_t buf[512] = {};
        GetDlgItemTextW(hwnd, IDC_SETT_SIGN_DESC, buf, 512);
        s_signDescription = buf;
        MainWindow::MarkAsModified();
        return true;
    }
    return false;
}

// ── SETT_SaveToDb ─────────────────────────────────────────────────────────────
void SETT_SaveToDb(int projectId)
{
    if (projectId <= 0) return;
    auto K = [&](const wchar_t* s) { return ProjKey(projectId, s); };
    DB::SetSetting(K(L"publisher_url"),     s_publisherUrl);
    DB::SetSetting(K(L"support_url"),       s_supportUrl);
    DB::SetSetting(K(L"output_folder"),     s_outputFolder);
    DB::SetSetting(K(L"output_filename"),   s_outputFilename);
    DB::SetSetting(K(L"test_output_folder"),   s_testOutputFolder);
    DB::SetSetting(K(L"test_output_filename"), s_testOutputFilename);
    DB::SetSetting(K(L"compression_type"),  std::to_wstring(s_compressionType));
    DB::SetSetting(K(L"compression_level"), std::to_wstring(s_compressionLevel));
    DB::SetSetting(K(L"solid"),             s_solidCompression ? L"1" : L"0");
    DB::SetSetting(K(L"uac_level"),         std::to_wstring(s_uacLevel));
    DB::SetSetting(K(L"priv_overrides"),     std::to_wstring(s_privOverrides));
    DB::SetSetting(K(L"wizard_style"),        std::to_wstring(s_wizardStyle));
    DB::SetSetting(K(L"min_os"),            std::to_wstring(s_minOsVersion));
    DB::SetSetting(K(L"allow_uninstall"),   s_allowUninstall ? L"1" : L"0");
    DB::SetSetting(K(L"uninstall_display_name"), s_uninstallDisplayName);
    DB::SetSetting(K(L"uninstall_files_dir"),    s_uninstallFilesDir);
    DB::SetSetting(K(L"close_apps"),        s_closeApps ? L"1" : L"0");
    {
        std::wstring joined;
        for (const auto& p : s_pathFolders) {
            if (!joined.empty()) joined += L";";
            joined += p;
        }
        DB::SetSetting(K(L"path_folders"), joined);
    }
    DB::SetSetting(K(L"changes_env"),        s_changesEnvironment ? L"1" : L"0");
    DB::SetSetting(K(L"disable_dir_page"),       s_disableDirPage ? L"1" : L"0");
    DB::SetSetting(K(L"disable_prog_group_page"), s_disableProgramGroupPage ? L"1" : L"0");
    DB::SetSetting(K(L"use_prev_app_dir"),        s_usePreviousAppDir ? L"1" : L"0");
    DB::SetSetting(K(L"use_prev_group"),          s_usePreviousGroup  ? L"1" : L"0");
    DB::SetSetting(K(L"dir_exists_warning"),      std::to_wstring(s_dirExistsWarning));
    DB::SetSetting(K(L"sign_enabled"),      s_signEnabled ? L"1" : L"0");
    DB::SetSetting(K(L"sign_tool_path"),    s_signtoolPath);
    DB::SetSetting(K(L"sign_thumbprint"),   s_signThumbprint);
    DB::SetSetting(K(L"sign_pfx_path"),     s_signPfxPath);
    DB::SetSetting(K(L"sign_pfx_pass"),     s_signPfxPassword);
    DB::SetSetting(K(L"sign_ts_url"),       s_signTimestampUrl);
    DB::SetSetting(K(L"sign_ts_algo"),      std::to_wstring(s_signTimestampAlgo));
    DB::SetSetting(K(L"sign_description"),  s_signDescription);
    DB::SetSetting(K(L"install_base"),        std::to_wstring(s_installBase));
    DB::SetSetting(K(L"install_base_custom"), s_installBaseCustom);
    DB::SetSetting(K(L"setup_logging"),       s_setupLogging      ? L"1" : L"0");
    DB::SetSetting(K(L"setup_log_folder"),    s_setupLogFolder);
    DB::SetSetting(K(L"setup_log_filename"),  s_setupLogFilename);
    DB::SetSetting(K(L"setup_log_mode"),      std::to_wstring(s_setupLogMode));
    {
        std::wstring langs;
        for (int i = 1; i < kLangCount; i++) {
            if (i < (int)s_installerLangs.size() && s_installerLangs[i]) {
                if (!langs.empty()) langs += L',';
                langs += kInnoLangs[i].isl;
            }
        }
        DB::SetSetting(K(L"installer_langs"), langs);
    }
    DB::SetSetting(K(L"lang_detection_method"), std::to_wstring(s_langDetectionMethod));
    DB::SetSetting(K(L"show_lang_dlg"),         std::to_wstring(s_showLanguageDialog));
}

// ── SETT_LoadFromDb ───────────────────────────────────────────────────────────
void SETT_LoadFromDb(int projectId)
{
    if (projectId <= 0) return;
    auto K = [&](const wchar_t* s) { return ProjKey(projectId, s); };
    std::wstring val;
    if (DB::GetSetting(K(L"publisher_url"),     val)) s_publisherUrl     = val;
    if (DB::GetSetting(K(L"support_url"),       val)) s_supportUrl       = val;
    if (DB::GetSetting(K(L"output_folder"),     val)) s_outputFolder     = val;
    if (DB::GetSetting(K(L"output_filename"),   val)) s_outputFilename   = val;
    if (DB::GetSetting(K(L"test_output_folder"),   val)) s_testOutputFolder   = val;
    if (DB::GetSetting(K(L"test_output_filename"), val)) s_testOutputFilename = val;
    if (DB::GetSetting(K(L"compression_type"),  val)) s_compressionType  = _wtoi(val.c_str());
    if (DB::GetSetting(K(L"compression_level"), val)) s_compressionLevel = _wtoi(val.c_str());
    if (DB::GetSetting(K(L"solid"),             val)) s_solidCompression = (val == L"1");
    if (DB::GetSetting(K(L"uac_level"),         val)) s_uacLevel         = _wtoi(val.c_str());
    if (DB::GetSetting(K(L"priv_overrides"),     val)) s_privOverrides    = _wtoi(val.c_str());
    if (DB::GetSetting(K(L"wizard_style"),        val)) s_wizardStyle      = _wtoi(val.c_str());
    if (DB::GetSetting(K(L"min_os"),            val)) s_minOsVersion     = _wtoi(val.c_str());
    if (DB::GetSetting(K(L"allow_uninstall"),   val)) s_allowUninstall   = (val != L"0");
    if (DB::GetSetting(K(L"uninstall_display_name"), val)) s_uninstallDisplayName = val;
    if (DB::GetSetting(K(L"uninstall_files_dir"),    val)) s_uninstallFilesDir    = val;
    if (DB::GetSetting(K(L"close_apps"),        val)) s_closeApps        = (val == L"1");
    s_pathFolders.clear();
    if (DB::GetSetting(K(L"path_folders"), val) && !val.empty()) {
        std::wstring tok;
        for (wchar_t ch : val) {
            if (ch == L';') {
                if (!tok.empty()) s_pathFolders.push_back(tok);
                tok.clear();
            } else {
                tok += ch;
            }
        }
        if (!tok.empty()) s_pathFolders.push_back(tok);
    } else if (DB::GetSetting(K(L"add_to_path"), val) && val == L"1") {
        // Migration: old bool means "add {app}" to PATH
        s_pathFolders.push_back(L"{app}");
    }
    if (DB::GetSetting(K(L"changes_env"),        val)) s_changesEnvironment = (val == L"1");
    if (DB::GetSetting(K(L"disable_dir_page"),       val)) s_disableDirPage          = (val == L"1");
    if (DB::GetSetting(K(L"disable_prog_group_page"), val)) s_disableProgramGroupPage = (val == L"1");
    if (DB::GetSetting(K(L"use_prev_app_dir"),        val)) s_usePreviousAppDir       = (val != L"0");
    if (DB::GetSetting(K(L"use_prev_group"),          val)) s_usePreviousGroup        = (val != L"0");
    if (DB::GetSetting(K(L"dir_exists_warning"),      val)) s_dirExistsWarning        = _wtoi(val.c_str());
    if (DB::GetSetting(K(L"sign_enabled"),      val)) s_signEnabled      = (val == L"1");
    if (DB::GetSetting(K(L"sign_tool_path"),    val)) s_signtoolPath     = val;
    if (DB::GetSetting(K(L"sign_thumbprint"),   val)) s_signThumbprint   = val;
    if (DB::GetSetting(K(L"sign_pfx_path"),     val)) s_signPfxPath      = val;
    if (DB::GetSetting(K(L"sign_pfx_pass"),     val)) s_signPfxPassword  = val;
    if (DB::GetSetting(K(L"sign_ts_url"),       val)) s_signTimestampUrl = val;
    if (DB::GetSetting(K(L"sign_ts_algo"),      val)) s_signTimestampAlgo = _wtoi(val.c_str());
    if (DB::GetSetting(K(L"sign_description"),  val)) s_signDescription  = val;
    if (DB::GetSetting(K(L"install_base"),        val)) s_installBase       = _wtoi(val.c_str());
    if (DB::GetSetting(K(L"install_base_custom"), val)) s_installBaseCustom = val;
    if (DB::GetSetting(K(L"setup_logging"),       val)) s_setupLogging      = (val == L"1");
    if (DB::GetSetting(K(L"setup_log_folder"),    val)) s_setupLogFolder     = val;
    if (DB::GetSetting(K(L"setup_log_filename"),  val)) s_setupLogFilename   = val;
    if (DB::GetSetting(K(L"setup_log_mode"),      val)) s_setupLogMode       = _wtoi(val.c_str());
    // Installer languages
    if ((int)s_installerLangs.size() < kLangCount)
        s_installerLangs.assign(kLangCount, false);
    s_installerLangs[0] = true;
    for (int i = 1; i < kLangCount; i++) s_installerLangs[i] = false;
    if (DB::GetSetting(K(L"installer_langs"), val) && !val.empty()) {
        std::wstring tok;
        for (wchar_t ch : val) {
            if (ch == L',') {
                for (int i = 1; i < kLangCount; i++)
                    if (tok == kInnoLangs[i].isl) { s_installerLangs[i] = true; break; }
                tok.clear();
            } else {
                tok += ch;
            }
        }
        if (!tok.empty())
            for (int i = 1; i < kLangCount; i++)
                if (tok == kInnoLangs[i].isl) { s_installerLangs[i] = true; break; }
    }
    if (DB::GetSetting(K(L"lang_detection_method"), val)) s_langDetectionMethod = _wtoi(val.c_str());
    if (DB::GetSetting(K(L"show_lang_dlg"),         val)) s_showLanguageDialog   = _wtoi(val.c_str());
}

// ── SETT_IsSelectFolderDisabled ───────────────────────────────────────────────
bool SETT_IsSelectFolderDisabled() { return s_disableDirPage; }

// ── SETT_GetInstallBasePath ───────────────────────────────────────────────────
std::wstring SETT_GetInstallBasePath()
{
    static const wchar_t* kBases[] = {
        L"{pf}", L"{pf64}", L"{pf32}",
        L"{localappdata}", L"{commonappdata}", L"{userdocs}"
    };
    if (s_installBase >= 0 && s_installBase < 6)
        return kBases[s_installBase];
    // Custom: return the custom value, fall back to {pf} if empty
    return s_installBaseCustom.empty() ? L"{pf}" : s_installBaseCustom;
}
// ── SETT_GetInstallerLanguages ─────────────────────────────────────────────
std::vector<InnoLangEntry> SETT_GetInstallerLanguages()
{
    std::vector<InnoLangEntry> result;
    for (int i = 0; i < kLangCount; i++)
        if (i < (int)s_installerLangs.size() && s_installerLangs[i])
            result.push_back({ kInnoLangs[i].isl, kInnoLangs[i].local });
    if (result.empty())
        result.push_back({ L"Default", false });  // fallback — always at least English
    return result;
}

// ── SETT_GetBuildConfig ────────────────────────────────────────────────────────
SBuildConfig SETT_GetBuildConfig()
{
    SBuildConfig cfg;
    cfg.publisherUrl      = s_publisherUrl;
    cfg.supportUrl        = s_supportUrl;
    cfg.outputFolder      = s_outputFolder;
    cfg.outputFilename    = s_outputFilename;
    cfg.testOutputFolder    = s_testOutputFolder;
    cfg.testOutputFilename  = s_testOutputFilename;
    cfg.compressionType   = s_compressionType;
    cfg.compressionLevel  = s_compressionLevel;
    cfg.solidCompression  = s_solidCompression;
    cfg.uacLevel          = s_uacLevel;
    cfg.privOverridesAllowed = s_privOverrides;
    cfg.wizardStyle          = s_wizardStyle;
    cfg.minOsVersion      = s_minOsVersion;
    cfg.allowUninstall    = s_allowUninstall;
    cfg.uninstallDisplayName = s_uninstallDisplayName;
    cfg.uninstallFilesDir    = s_uninstallFilesDir;
    cfg.closeApps         = s_closeApps;
    cfg.pathFolders          = s_pathFolders;
    cfg.changesEnvironment   = s_changesEnvironment;
    cfg.changesAssociations  = FA_HasAnyEnabled();
    cfg.disableDirPage           = s_disableDirPage;
    cfg.disableProgramGroupPage  = s_disableProgramGroupPage;
    cfg.usePreviousAppDir        = s_usePreviousAppDir;
    cfg.usePreviousGroup         = s_usePreviousGroup;
    cfg.dirExistsWarning         = s_dirExistsWarning;
    cfg.signEnabled         = s_signEnabled;
    cfg.signtoolPath        = s_signtoolPath;
    cfg.signThumbprint      = s_signThumbprint;
    cfg.signPfxPath         = s_signPfxPath;
    cfg.signPfxPassword     = s_signPfxPassword;
    cfg.signTimestampUrl    = s_signTimestampUrl;
    cfg.signTimestampAlgo   = s_signTimestampAlgo;
    cfg.signDescription     = s_signDescription;
    cfg.setupLogging        = s_setupLogging;
    cfg.setupLogFolder      = s_setupLogFolder;
    cfg.setupLogFilename    = s_setupLogFilename;
    cfg.setupLogMode        = s_setupLogMode;
    cfg.langDetectionMethod = s_langDetectionMethod;
    cfg.showLanguageDialog  = s_showLanguageDialog;
    cfg.testOutputFolder    = s_testOutputFolder;
    cfg.testOutputFilename  = s_testOutputFilename;
    return cfg;
}

// ── SETT_SetTestOutputFolder / SETT_SetTestOutputFilename ─────────────────────
// Called by test_page.cpp when the user edits the test-output fields.
void SETT_SetTestOutputFolder(const std::wstring& v)
{
    s_testOutputFolder = v;
    MainWindow::MarkAsModified();
}

void SETT_SetTestOutputFilename(const std::wstring& v)
{
    s_testOutputFilename = v;
    MainWindow::MarkAsModified();
}
