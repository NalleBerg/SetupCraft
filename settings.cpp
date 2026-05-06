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
#include "mainwindow.h"   // MainWindow::MarkAsModified()
#include "db.h"
#include "dpi.h"          // S()
#include "button.h"       // CreateCustomButtonWithIcon(), MeasureButtonWidth()
#include "checkbox.h"     // CreateCustomCheckbox()
#include <shellapi.h>
#include <shlobj.h>       // SHBrowseForFolderW, SHGetPathFromIDListW
#include <vector>
#include <string>
#include <map>
#include <algorithm>

// ── Module-private state (new settings only) ──────────────────────────────────
static std::wstring s_publisherUrl;
static std::wstring s_supportUrl;
static std::wstring s_outputFolder;
static std::wstring s_outputFilename;
static int          s_compressionType  = 2;    // 0=store, 1=zip, 2=lzma, 3=lzma2
static int          s_compressionLevel = 7;    // 0–9
static bool         s_solidCompression = true;
static int          s_uacLevel         = 0;    // 0=requireAdministrator 1=asInvoker 2=highest
static int          s_minOsVersion     = 0;    // 0=none … 5=Win11
static bool         s_allowUninstall   = true;
static bool         s_closeApps        = false;
static int          s_installBase        = 0;    // 0={pf} 1={pf64} 2={pf32} 3={localappdata} 4={commonappdata} 5={userdocs} 6=Custom
static std::wstring s_installBaseCustom;          // used when s_installBase == 6
static HWND         s_hInstallBaseCustomEdit = NULL;

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
                          int y, int clientWidth, const std::wstring& text)
{
    HWND hLbl = CreateWindowExW(0, L"STATIC", text.c_str(),
        WS_CHILD | WS_VISIBLE | SS_LEFT,
        S(kPadH), y, clientWidth - S(kPadH) * 2, S(20),
        hwnd, NULL, hInst, NULL);
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
    const int fldW = std::min(S(240), clientWidth - fldX - S(kPadH));

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
    s_compressionType  = 2;
    s_compressionLevel = 7;
    s_solidCompression = true;
    s_uacLevel         = 0;
    s_minOsVersion     = 0;
    s_allowUninstall   = true;
    s_closeApps        = false;
    s_installBase        = 0;
    s_installBaseCustom  = L"";
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
    y = SectionHeader(hwnd, hInst, hGuiFont, y, clientWidth,
                      loc(L"sett_sec_application", L"Application"));

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
    y = SectionHeader(hwnd, hInst, hGuiFont, y, clientWidth,
                      loc(L"sett_sec_build", L"Build Output"));

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
    y = SectionHeader(hwnd, hInst, hGuiFont, y, clientWidth,
                      loc(L"sett_sec_install", L"Installation"));

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

    // ════════════════════════════════════════════════════════════════════════
    // Section 4: Installer Languages
    // ╚══════════════════════════════════════════════════════════════════════
    y += S(kSecGap);
    y = SectionHeader(hwnd, hInst, hGuiFont, y, clientWidth,
                      loc(L"sett_sec_languages", L"Installer Languages"));

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

    // ╔══════════════════════════════════════════════════════════════════════
    // Section 5: Uninstall
    // ════════════════════════════════════════════════════════════════════════
    y += S(kSecGap);
    y = SectionHeader(hwnd, hInst, hGuiFont, y, clientWidth,
                      loc(L"sett_sec_uninstall", L"Uninstall"));

    // Allow uninstall toggle
    y = FieldCheckbox(hwnd, hInst, hGuiFont, y, clientWidth,
                      loc(L"sett_allow_uninstall_lbl",
                          L"Allow users to uninstall this application"),
                      IDC_SETT_ALLOW_UNINSTALL, s_allowUninstall);

    // Close applications before install
    y = FieldCheckbox(hwnd, hInst, hGuiFont, y, clientWidth,
                      loc(L"sett_close_apps_lbl",
                          L"Close running applications before installing"),
                      IDC_SETT_CLOSE_APPS, s_closeApps);

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
    if (wmId == IDC_SETT_CLOSE_APPS && wmEvent == BN_CLICKED) {
        s_closeApps =
            (SendDlgItemMessageW(hwnd, IDC_SETT_CLOSE_APPS, BM_GETCHECK, 0, 0) == BST_CHECKED);
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
    DB::SetSetting(K(L"compression_type"),  std::to_wstring(s_compressionType));
    DB::SetSetting(K(L"compression_level"), std::to_wstring(s_compressionLevel));
    DB::SetSetting(K(L"solid"),             s_solidCompression ? L"1" : L"0");
    DB::SetSetting(K(L"uac_level"),         std::to_wstring(s_uacLevel));
    DB::SetSetting(K(L"min_os"),            std::to_wstring(s_minOsVersion));
    DB::SetSetting(K(L"allow_uninstall"),   s_allowUninstall ? L"1" : L"0");
    DB::SetSetting(K(L"close_apps"),        s_closeApps ? L"1" : L"0");
    DB::SetSetting(K(L"install_base"),        std::to_wstring(s_installBase));
    DB::SetSetting(K(L"install_base_custom"), s_installBaseCustom);
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
    if (DB::GetSetting(K(L"compression_type"),  val)) s_compressionType  = _wtoi(val.c_str());
    if (DB::GetSetting(K(L"compression_level"), val)) s_compressionLevel = _wtoi(val.c_str());
    if (DB::GetSetting(K(L"solid"),             val)) s_solidCompression = (val == L"1");
    if (DB::GetSetting(K(L"uac_level"),         val)) s_uacLevel         = _wtoi(val.c_str());
    if (DB::GetSetting(K(L"min_os"),            val)) s_minOsVersion     = _wtoi(val.c_str());
    if (DB::GetSetting(K(L"allow_uninstall"),   val)) s_allowUninstall   = (val != L"0");
    if (DB::GetSetting(K(L"close_apps"),        val)) s_closeApps        = (val == L"1");
    if (DB::GetSetting(K(L"install_base"),        val)) s_installBase       = _wtoi(val.c_str());
    if (DB::GetSetting(K(L"install_base_custom"), val)) s_installBaseCustom = val;
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
}

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
    cfg.compressionType   = s_compressionType;
    cfg.compressionLevel  = s_compressionLevel;
    cfg.solidCompression  = s_solidCompression;
    cfg.uacLevel          = s_uacLevel;
    cfg.minOsVersion      = s_minOsVersion;
    cfg.allowUninstall    = s_allowUninstall;
    cfg.closeApps         = s_closeApps;
    return cfg;
}
