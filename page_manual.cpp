// page_manual.cpp
// Resizable, richly-formatted per-page how-to dialog.
//
// Architecture mirrors ShowAboutDialog() in about.cpp:
//  - GDI+ logo drawn at the top of the RichEdit via a subclass proc.
//  - AppendManual() is an independent copy of AppendRichText so this module
//    carries no dependency on about.cpp.
//  - EM_SETTARGETDEVICE(NULL,0) forces word-wrap to the control width so
//    there is never any horizontal scrollbar, regardless of window size.
//  - WM_SIZE resizes the RichEdit and repositions the Close button so the
//    layout is fully live-resizeable and works in maximised mode.
//  - WM_GETMINMAXINFO respects the work area (taskbar) when maximising.

#include "page_manual.h"
#include "dpi.h"
#include "button.h"
#include "my_scrollbar_vscroll.h"
#include <richedit.h>
#include <shellapi.h>
#include <gdiplus.h>
#pragma comment(lib, "gdiplus.lib")
#include <string>
#include <map>
#include <vector>
using namespace Gdiplus;

// ─── Per-instance data ───────────────────────────────────────────────────────

// One color icon painted over a section-header emoji glyph.
// charPos is the char index of the emoji in the RichEdit text;
// EM_POSFROMCHAR maps it to client pixels at paint time.
struct IconEntry {
    LONG  charPos;
    HICON hIcon;
    int   size;   // drawn square at size x size pixels
};

struct ManualWndData {
    int                                         pageIndex;
    const std::map<std::wstring,std::wstring>*  pLocale;
    Image*                                      pLogo;
    HMSB                                        hScrollbar;
    WNDPROC                                     origEditProc;
    HWND                                        hEdit;
    HWND                                        hCloseBtn;
    int                                         closeW;   // button width, for WM_SIZE
    int                                         PAD;      // S(10) cached
    int                                         BTN_H;    // S(34) cached
    bool                                        richFontDirty;
    std::vector<IconEntry>                      sectionIcons; // color icons over emoji glyphs
};

// ─── Locale helper ───────────────────────────────────────────────────────────

static std::wstring ML(const ManualWndData* pd,
                       const wchar_t* key, const wchar_t* fallback)
{
    if (!pd || !pd->pLocale) return fallback;
    auto it = pd->pLocale->find(key);
    return (it != pd->pLocale->end()) ? it->second : fallback;
}

// ─── Rich-text append helper ─────────────────────────────────────────────────
// Self-contained copy of AppendRichText (about.cpp is not a dependency).
// fontSize == 0  → use the body font size derived from NONCLIENTMETRICS.
// centered == true → centre the paragraph; resets to left afterwards.

static void AppendManual(HWND hEdit, ManualWndData* pd,
                         const std::wstring& text,
                         bool bold, COLORREF color,
                         int fontSize = 0, bool centered = false)
{
    // Measure the body font once per dialog open, using the RichEdit's own DC
    // so twip sizing matches msftedit.dll's internal DPI reference.
    static wchar_t s_face[LF_FACESIZE] = {};
    static int     s_baseTwips         = 0;

    if (pd->richFontDirty) {
        pd->richFontDirty = false;
        NONCLIENTMETRICSW ncm = {}; ncm.cbSize = sizeof(ncm);
        SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(ncm), &ncm, 0);
        wcscpy_s(s_face, ncm.lfMessageFont.lfFaceName);
        HDC hdcEdit = GetDC(hEdit);
        int dpiY = GetDeviceCaps(hdcEdit, LOGPIXELSY);
        ReleaseDC(hEdit, hdcEdit);
        if (dpiY <= 0) dpiY = 96;
        // × 1.2 matches the body-font scale used everywhere else in the project.
        s_baseTwips = MulDiv(
            (int)(abs(ncm.lfMessageFont.lfHeight) * 1.2f + 0.5f),
            1440, dpiY);
        if (s_baseTwips <= 0) s_baseTwips = 180; // fallback: ~9 pt
    }

    int twips = (fontSize > 0) ? fontSize * 20 : s_baseTwips;

    CHARFORMAT2W cf = {};
    cf.cbSize      = sizeof(CHARFORMAT2W);
    // CFM_CHARSET + DEFAULT_CHARSET enables RichEdit font linking: the control
    // automatically substitutes the right font (e.g. "Segoe UI Emoji") for any
    // codepoint the primary face cannot render, so emoji show correctly.
    cf.dwMask      = CFM_COLOR | CFM_BOLD | CFM_SIZE | CFM_FACE | CFM_CHARSET;
    cf.bCharSet    = DEFAULT_CHARSET;
    cf.crTextColor = color;
    cf.dwEffects   = bold ? CFE_BOLD : 0;
    cf.yHeight     = twips;
    wcscpy_s(cf.szFaceName, s_face);

    GETTEXTLENGTHEX gtl = {}; gtl.flags = GTL_DEFAULT; gtl.codepage = 1200;
    LONG len = (LONG)SendMessageW(hEdit, EM_GETTEXTLENGTHEX, (WPARAM)&gtl, 0);
    SendMessageW(hEdit, EM_SETSEL, len, len);

    if (centered) {
        PARAFORMAT2 pf = {}; pf.cbSize = sizeof(pf);
        pf.dwMask = PFM_ALIGNMENT; pf.wAlignment = PFA_CENTER;
        SendMessageW(hEdit, EM_SETPARAFORMAT, 0, (LPARAM)&pf);
    }
    SendMessageW(hEdit, EM_SETCHARFORMAT, SCF_SELECTION, (LPARAM)&cf);
    SendMessageW(hEdit, EM_REPLACESEL,    FALSE, (LPARAM)text.c_str());
    if (centered) {
        PARAFORMAT2 pf = {}; pf.cbSize = sizeof(pf);
        pf.dwMask = PFM_ALIGNMENT; pf.wAlignment = PFA_LEFT;
        SendMessageW(hEdit, EM_SETPARAFORMAT, 0, (LPARAM)&pf);
    }
}

// Short alias — avoids repeating hEdit and pd on every line.
#define AM(text, bold, color, ...) AppendManual(hEdit, pd, (text), (bold), (color), ##__VA_ARGS__)

// ─── Logo subclass ───────────────────────────────────────────────────────────
// Paints the SC logo above the scrolled text content, exactly like About.

static LRESULT CALLBACK ManualEditSubclassProc(HWND hwnd, UINT msg,
                                               WPARAM wP, LPARAM lP)
{
    ManualWndData* pd = (ManualWndData*)GetPropW(hwnd, L"ManualData");
    if (!pd) return DefWindowProcW(hwnd, msg, wP, lP);

    if (msg == WM_PAINT) {
        // Let RichEdit draw its text first, then paint our overlays on top.
        CallWindowProcW(pd->origEditProc, hwnd, msg, wP, lP);
        HDC hdc = GetDC(hwnd);
        RECT rc; GetClientRect(hwnd, &rc);

        // ── Logo (GDI+ bitmap, offset by scroll position) ───────────────────
        if (pd->pLogo) {
            Graphics g(hdc);
            g.SetInterpolationMode(InterpolationModeHighQualityBicubic);
            POINT scr = {0, 0};
            SendMessageW(hwnd, EM_GETSCROLLPOS, 0, (LPARAM)&scr);
            int logoW = (int)(pd->pLogo->GetWidth()  * 0.75);
            int logoH = (int)(pd->pLogo->GetHeight() * 0.75);
            int lx = (rc.right - logoW) / 2;
            int ly = S(10) - scr.y;
            if (ly + logoH > 0 && ly < rc.bottom)
                g.DrawImage(pd->pLogo, lx, ly, logoW, logoH);
        }

        // ── Section-header icons (color shell32 icons over monochrome emoji) ─
        // EM_POSFROMCHAR already returns client-space coordinates (scroll-adjusted),
        // so no manual scroll offset is needed here.
        for (const auto& entry : pd->sectionIcons) {
            if (!entry.hIcon) continue;
            POINT ipt = {};
            if (SendMessageW(hwnd, EM_POSFROMCHAR, (WPARAM)&ipt, entry.charPos) != -1) {
                if (ipt.y + entry.size > 0 && ipt.y < rc.bottom)
                    DrawIconEx(hdc, ipt.x, ipt.y + 1,
                               entry.hIcon, entry.size, entry.size,
                               0, NULL, DI_NORMAL);
            }
        }

        ReleaseDC(hwnd, hdc);
        return 0;
    }
    return CallWindowProcW(pd->origEditProc, hwnd, msg, wP, lP);
}

// ─── Files page content ──────────────────────────────────────────────────────

// ─── Helper: load a system icon and register it at the current text end ──────
// Call immediately BEFORE the AM() that outputs the emoji prefix character.
// The icon is drawn at that character's position in the WM_PAINT subclass.

static void RegisterShell32Icon(HWND hEdit, ManualWndData* pd, int iconIdx)
{
    GETTEXTLENGTHEX gtl = {}; gtl.flags = GTL_DEFAULT; gtl.codepage = 1200;
    LONG charPos = (LONG)SendMessageW(hEdit, EM_GETTEXTLENGTHEX, (WPARAM)&gtl, 0);
    wchar_t sysDir[MAX_PATH], dllPath[MAX_PATH];
    GetSystemDirectoryW(sysDir, MAX_PATH);
    wcscpy_s(dllPath, sysDir); wcscat_s(dllPath, L"\\shell32.dll");
    HICON hIco = NULL;
    ExtractIconExW(dllPath, iconIdx, &hIco, NULL, 1); // prefer large (32x32) for quality
    if (!hIco) ExtractIconExW(dllPath, iconIdx, NULL, &hIco, 1);
    if (hIco) pd->sectionIcons.push_back({ charPos, hIco, S(18) });
}

static void RegisterSystemIcon(HWND hEdit, ManualWndData* pd, LPCWSTR sysIconId)
{
    GETTEXTLENGTHEX gtl = {}; gtl.flags = GTL_DEFAULT; gtl.codepage = 1200;
    LONG charPos = (LONG)SendMessageW(hEdit, EM_GETTEXTLENGTHEX, (WPARAM)&gtl, 0);
    HICON hIco = LoadIconW(NULL, sysIconId);
    if (hIco) pd->sectionIcons.push_back({ charPos, hIco, S(18) });
}

static void PopulateFilesManual(HWND hEdit, ManualWndData* pd)
{
    // Reserve blank lines at the top for the logo image (same trick as About).
    int logoH = pd->pLogo ? (int)(pd->pLogo->GetHeight() * 0.75) : 0;
    int lines  = (logoH + S(10)) / S(15);
    for (int i = 0; i < lines; i++)
        AM(L"\r\n", false, RGB(0,0,0));

    // ── Page title ──────────────────────────────────────────────────────────
    // shell32 #3 = yellow closed folder — paints over the 📁 emoji glyph.
    RegisterShell32Icon(hEdit, pd, 3);
    AM(ML(pd, L"man_files_h1",
          L"📁  Files Page — What does it do?") + L"\r\n\r\n",
       true, RGB(0,70,140), 14, true);

    AM(ML(pd, L"man_files_p1",
          L"The Files page is where you define every file and folder that will be "
          L"installed on the end user's machine. Think of it as building the virtual "
          L"file system of your application before it is packaged into an installer. "
          L"Everything you add here will be placed in the correct location when the "
          L"user runs the setup program.") + L"\r\n\r\n",
       false, RGB(40,40,40));

    AM(L"━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\r\n\r\n", false, RGB(100,140,180), 0, true);

    // ── Section 2: Folder tree ───────────────────────────────────────────────    // shell32 #5 = open yellow folder.
    RegisterShell32Icon(hEdit, pd, 5);    AM(ML(pd, L"man_files_h2",
          L"🌳  Left pane — Folder tree") + L"\r\n",
       true, RGB(0,70,140), 12);
    AM(ML(pd, L"man_files_p2",
          L"The tree shows four install roots. Place your files under the one "
          L"that matches where they belong on the end user's machine:") + L"\r\n",
       false, RGB(40,40,40));

    auto bullet = [&](const wchar_t* key, const wchar_t* fb) {
        AM(L"  \u2022  ", true, RGB(0,70,140));
        AM(ML(pd, key, fb) + L"\r\n", false, RGB(60,60,60));
    };
    bullet(L"man_files_r2a",
           L"Program Files \u2014 standard location for 64-bit applications (most apps go here).");
    bullet(L"man_files_r2b",
           L"ProgramData \u2014 shared data files that all users on the machine can read.");
    bullet(L"man_files_r2c",
           L"AppData (Roaming) \u2014 per-user data that follows the user when roaming.");
    bullet(L"man_files_r2d",
           L"Ask at install \u2014 the end user chooses the location during setup.");

    AM(L"\r\n", false, RGB(0,0,0));
    AM(ML(pd, L"man_files_p2b",
          L"Drag any folder to reorganise it in the tree. Right-click a folder for a "
          L"context menu with options to add a subfolder, rename it, or remove it. "
          L"Click any folder to see its files on the right.") + L"\r\n\r\n",
       false, RGB(40,40,40));

    AM(L"━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\r\n\r\n", false, RGB(100,140,180), 0, true);

    // ── Section 3: File list ─────────────────────────────────────────────────    // shell32 #1 = generic file/document icon.
    RegisterShell32Icon(hEdit, pd, 1);    AM(ML(pd, L"man_files_h3",
          L"📄  Right pane — File list") + L"\r\n",
       true, RGB(0,70,140), 12);
    AM(ML(pd, L"man_files_p3",
          L"Shows all files inside the currently selected folder. The columns are:") + L"\r\n",
       false, RGB(40,40,40));
    bullet(L"man_files_r3a", L"Destination \u2014 where the file lands inside the chosen install root.");
    bullet(L"man_files_r3b", L"Source Path \u2014 the full path on your development machine.");
    bullet(L"man_files_r3c", L"Flags \u2014 any special Inno Setup flags applied to this file.");
    bullet(L"man_files_r3d",
           L"Component \u2014 which install component this file belongs to "
           L"(assigned on the Components page).");

    AM(L"\r\n", false, RGB(0,0,0));
    AM(L"━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\r\n\r\n", false, RGB(100,140,180), 0, true);

    // ── Section 4: Controls ──────────────────────────────────────────────────    // shell32 #221 = information / about (i) icon — closest available to a
    // settings/controls icon in the verified shell32 index list.
    RegisterShell32Icon(hEdit, pd, 221);    AM(ML(pd, L"man_files_h4", L"\u2699  Controls") + L"\r\n",
       true, RGB(0,70,140), 12);

    auto ctrl = [&](const wchar_t* lblKey, const wchar_t* lblFb,
                    const wchar_t* txtKey, const wchar_t* txtFb) {
        AM(L"  ", false, RGB(0,0,0));
        AM(ML(pd, lblKey, lblFb) + L" \u2014 ", true, RGB(0,0,0));
        AM(ML(pd, txtKey, txtFb) + L"\r\n\r\n", false, RGB(60,60,60));
    };
    ctrl(L"man_files_l4a", L"Add Folder",
         L"man_files_p4a",
         L"Opens a folder picker. The entire folder is added recursively — all "
         L"subfolders and files — under the currently selected install root.");
    ctrl(L"man_files_l4b", L"Add Files",
         L"man_files_p4b",
         L"Opens a multi-file picker. All selected files are placed inside the "
         L"currently highlighted folder in the tree.");
    ctrl(L"man_files_l4c", L"Remove",
         L"man_files_p4c",
         L"Removes the checked items. Tick items in the tree or file list first "
         L"\u2014 Ctrl+click to toggle individual items, Shift+click for a "
         L"contiguous range.");
    ctrl(L"man_files_l4d", L"Browse (\u2026)",
         L"man_files_p4d",
         L"Changes the base install path shown in the Install folder field. The "
         L"actual Inno base token ({pf}, {pf64}, etc.) is set in "
         L"Settings \u2192 Installation \u2192 Default dir base.");

    AM(L"━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\r\n\r\n", false, RGB(100,140,180), 0, true);

    // ── Section 5: Project name & Install folder ─────────────────────────────    // shell32 #258 = verified icon used for info on the Components page.
    RegisterShell32Icon(hEdit, pd, 258);    AM(ML(pd, L"man_files_h5",
          L"📋  Project name and Install folder") + L"\r\n",
       true, RGB(0,70,140), 12);
    AM(ML(pd, L"man_files_p5",
          L"Project name is the human-readable label for this installer project. "
          L"Install folder shows the full default installation path "
          L"(for example C:\\Program Files\\MyApp). "
          L"To change the base location token go to "
          L"Settings \u2192 Installation \u2192 Default dir base.") + L"\r\n\r\n",
       false, RGB(40,40,40));

    AM(L"━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\r\n\r\n", false, RGB(100,140,180), 0, true);

    // ── Section 6: Ask at install ────────────────────────────────────────────    // shell32 #23 = question mark / help icon.
    RegisterShell32Icon(hEdit, pd, 23);    AM(ML(pd, L"man_files_h6", L"\u2753  Ask at install") + L"\r\n",
       true, RGB(0,70,140), 12);
    AM(ML(pd, L"man_files_p6",
          L"When this checkbox is ticked the installer shows a dialog during setup "
          L"that lets the end user choose whether to install for themselves only or "
          L"for all users on the machine. An AskAtInstall root appears in the folder "
          L"tree \u2014 files placed there are handled accordingly by Inno Setup.") + L"\r\n\r\n",
       false, RGB(40,40,40));

    AM(L"━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\r\n\r\n", false, RGB(100,140,180), 0, true);

    // ── Warning ──────────────────────────────────────────────────────────────
    // IDI_WARNING = system yellow warning triangle (always available, full color).
    RegisterSystemIcon(hEdit, pd, IDI_WARNING);
    AM(L"\u26A0  ", true, RGB(160,80,0), 11);
    AM(ML(pd, L"man_files_warn",
          L"Save before switching to the Components page. Components are linked "
          L"to files by their permanent database ID, and files only receive that "
          L"ID after the first Save. Newly added files that have never been saved "
          L"will not appear in the Components picker yet.") + L"\r\n\r\n",
       false, RGB(120,50,0));

    AM(L"━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\r\n\r\n", false, RGB(100,140,180), 0, true);

    // ── Next step ────────────────────────────────────────────────────────────    // shell32 #294 = green checkmark / OK.
    RegisterShell32Icon(hEdit, pd, 294);    AM(L"\u25B6  ", true, RGB(0,120,0), 11);
    AM(ML(pd, L"man_files_next",
          L"Next step: go to the Components page to group your files into optional "
          L"install components, or skip directly to Settings to configure the "
          L"installer name, icon, output folder, and languages.") + L"\r\n",
       false, RGB(0,80,0));
}

// ─── Components page content ─────────────────────────────────────────────────

static void PopulateComponentsManual(HWND hEdit, ManualWndData* pd)
{
    // Reserve blank lines at the top for the logo (same as Files page).
    int logoH = pd->pLogo ? (int)(pd->pLogo->GetHeight() * 0.75) : 0;
    int lines  = (logoH + S(10)) / S(15);
    for (int i = 0; i < lines; i++)
        AM(L"\r\n", false, RGB(0,0,0));

    // ── Page title ──────────────────────────────────────────────────────────
    // shell32 #221 = information / about (i) icon.
    RegisterShell32Icon(hEdit, pd, 221);
    AM(ML(pd, L"man_comp_h1",
          L"\u2139  Components Page \u2014 What does it do?") + L"\r\n\r\n",
       true, RGB(0,70,140), 14, true);

    AM(ML(pd, L"man_comp_p1",
          L"The Components page lets you split your installer into optional pieces "
          L"that the end user can tick or untick during setup. For example a developer "
          L"might offer a core application, an optional documentation pack, and an "
          L"optional SDK \u2014 each as a separate component. Inno Setup emits a "
          L"component-selection wizard page so the user controls exactly what gets "
          L"installed.") + L"\r\n\r\n",
       false, RGB(40,40,40));

    AM(ML(pd, L"man_comp_p1b",
          L"If you do not need per-component selection, leave the "
          L"\u201cUse component-based installation\u201d checkbox unticked and all "
          L"files are packaged as one complete bundle \u2014 the installer skips the "
          L"component-selection wizard page entirely.") + L"\r\n\r\n",
       false, RGB(40,40,40));

    AM(L"\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\r\n\r\n", false, RGB(100,140,180), 0, true);

    // ── Enable toggle ────────────────────────────────────────────────────────
    // shell32 #258 = shield / info badge.
    RegisterShell32Icon(hEdit, pd, 258);
    AM(ML(pd, L"man_comp_h2",
          L"\U0001F4CB  Use component-based installation") + L"\r\n",
       true, RGB(0,70,140), 12);
    AM(ML(pd, L"man_comp_p2",
          L"This checkbox at the top of the page activates the whole component system. "
          L"When it is ticked, Inno Setup generates a [Components] section and the "
          L"selection wizard page is shown to the user. When it is unticked, no "
          L"[Components] section is emitted and all files install unconditionally.") + L"\r\n\r\n",
       false, RGB(40,40,40));

    AM(L"\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\r\n\r\n", false, RGB(100,140,180), 0, true);

    // ── Left pane: folder tree ───────────────────────────────────────────────
    // shell32 #3 = closed yellow folder.
    RegisterShell32Icon(hEdit, pd, 3);
    AM(ML(pd, L"man_comp_h3",
          L"\U0001F4C1  Left pane \u2014 Folder tree") + L"\r\n",
       true, RGB(0,70,140), 12);
    AM(ML(pd, L"man_comp_p3",
          L"The tree mirrors the virtual file system you built on the Files page. "
          L"It shows the same four install roots \u2014 Program Files, ProgramData, "
          L"AppData (Roaming), and AskAtInstall \u2014 and their subfolder structure. "
          L"Click any folder to see the components belonging to it in the right pane.") + L"\r\n\r\n",
       false, RGB(40,40,40));
    AM(ML(pd, L"man_comp_p3b",
          L"Folders that contain at least one Required component are marked with a "
          L"special icon (folder with a blue checkmark badge). This makes it easy "
          L"to see at a glance which parts of the tree will always be installed "
          L"regardless of what the user selects.") + L"\r\n\r\n",
       false, RGB(40,40,40));

    AM(L"\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\r\n\r\n", false, RGB(100,140,180), 0, true);

    // ── Right pane: component list ───────────────────────────────────────────
    // shell32 #1 = generic file / document icon.
    RegisterShell32Icon(hEdit, pd, 1);
    AM(ML(pd, L"man_comp_h4",
          L"\U0001F4C4  Right pane \u2014 Component list") + L"\r\n",
       true, RGB(0,70,140), 12);
    AM(ML(pd, L"man_comp_p4",
          L"Shows all components belonging to the selected folder. Columns:") + L"\r\n",
       false, RGB(40,40,40));

    auto bullet = [&](const wchar_t* key, const wchar_t* fb) {
        AM(L"  \u2022  ", true, RGB(0,70,140));
        AM(ML(pd, key, fb) + L"\r\n", false, RGB(60,60,60));
    };
    bullet(L"man_comp_r4a",
           L"Name \u2014 the display name shown to the user in the installer wizard.");
    bullet(L"man_comp_r4b",
           L"Description \u2014 optional subtitle shown below the name in the wizard.");
    bullet(L"man_comp_r4c",
           L"Required \u2014 whether this component is always installed silently.");
    bullet(L"man_comp_r4d",
           L"Type \u2014 Folder (covers all files in that folder) or File (single file).");
    bullet(L"man_comp_r4e",
           L"Install Types \u2014 which preset install types include this component.");
    bullet(L"man_comp_r4f",
           L"Source Path \u2014 the disk path of the file or folder this component covers.");
    AM(L"\r\n", false, RGB(0,0,0));

    AM(L"\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\r\n\r\n", false, RGB(100,140,180), 0, true);

    // ── Edit dialog ──────────────────────────────────────────────────────────
    // shell32 #221 = information (i) icon, used as a proxy for "controls / settings".
    RegisterShell32Icon(hEdit, pd, 221);
    AM(ML(pd, L"man_comp_h5",
          L"\u2699  Edit button \u2014 component properties") + L"\r\n",
       true, RGB(0,70,140), 12);
    AM(ML(pd, L"man_comp_p5",
          L"Select a component in the list and click Edit (or double-click) to open "
          L"the component properties dialog. Fields:") + L"\r\n",
       false, RGB(40,40,40));

    auto ctrl = [&](const wchar_t* lblKey, const wchar_t* lblFb,
                    const wchar_t* txtKey, const wchar_t* txtFb) {
        AM(L"  ", false, RGB(0,0,0));
        AM(ML(pd, lblKey, lblFb) + L" \u2014 ", true, RGB(0,0,0));
        AM(ML(pd, txtKey, txtFb) + L"\r\n\r\n", false, RGB(60,60,60));
    };
    ctrl(L"man_comp_l5a", L"Display Name",
         L"man_comp_p5a",
         L"The name shown in the wizard\u2019s component list. Keep it short and descriptive.");
    ctrl(L"man_comp_l5b", L"Description",
         L"man_comp_p5b",
         L"Optional subtitle that appears under the name. Useful for explaining what "
         L"the component contains or why the user might want it.");
    ctrl(L"man_comp_l5c", L"Source Path",
         L"man_comp_p5c",
         L"The disk path of the file or folder this component covers. For folder "
         L"components all files in that folder and its subfolders are included.");
    ctrl(L"man_comp_l5d", L"Destination Path",
         L"man_comp_p5d",
         L"The install root this component belongs to (Program Files, ProgramData, etc.). "
         L"Inherited from the VFS tree; edit only if you need to override it.");
    ctrl(L"man_comp_l5e", L"Dependencies",
         L"man_comp_p5e",
         L"Other components that must be selected whenever this one is selected. "
         L"Inno Setup enforces mutual-selection rules via generated [Code] Pascal. "
         L"The project must be saved at least once before dependencies can be assigned.");
    ctrl(L"man_comp_l5f", L"Install Types",
         L"man_comp_p5f",
         L"Space-separated list of install-type internal names this component belongs to "
         L"(e.g. \u201cfull compact\u201d). Leave empty to include in all types.");

    AM(L"\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\r\n\r\n", false, RGB(100,140,180), 0, true);

    // ── Component flags ──────────────────────────────────────────────────────
    // shell32 #294 = green checkmark — represents tick-boxes / flags.
    RegisterShell32Icon(hEdit, pd, 294);
    AM(ML(pd, L"man_comp_h6",
          L"\u2714  Component flags") + L"\r\n",
       true, RGB(0,70,140), 12);
    AM(ML(pd, L"man_comp_p6",
          L"Five checkboxes in the edit dialog control how Inno Setup treats the "
          L"component. Only tick the flags that apply \u2014 most optional components "
          L"need none of them:") + L"\r\n\r\n",
       false, RGB(40,40,40));

    auto flag = [&](const wchar_t* nameKey, const wchar_t* nameFb,
                    const wchar_t* descKey, const wchar_t* descFb) {
        AM(L"  ", false, RGB(0,0,0));
        AM(ML(pd, nameKey, nameFb), true, RGB(0,70,140));
        AM(L" \u2014 " + ML(pd, descKey, descFb) + L"\r\n", false, RGB(60,60,60));
    };
    flag(L"man_comp_f_required",   L"Required",
         L"man_comp_f_required_d",
         L"Always installed silently. Hidden from the wizard component list. "
         L"Use for mandatory runtime files the user must not be able to skip.");
    flag(L"man_comp_f_preselected", L"Pre-selected",
         L"man_comp_f_preselected_d",
         L"Ticked by default in the wizard. The user can still untick it. "
         L"Implied automatically when Required is set.");
    flag(L"man_comp_f_fixed",      L"Fixed",
         L"man_comp_f_fixed_d",
         L"Visible in the wizard but greyed-out so it cannot be deselected. "
         L"Inno emits Flags: fixed. Different from Required \u2014 the user can still see it.");
    flag(L"man_comp_f_exclusive",  L"Exclusive",
         L"man_comp_f_exclusive_d",
         L"Radio-button behaviour: selecting this component deselects all other exclusive "
         L"components in the same group. Inno emits Flags: exclusive. "
         L"Useful for mutually-exclusive options such as 32-bit vs 64-bit.");
    flag(L"man_comp_f_restart",    L"Restart required",
         L"man_comp_f_restart_d",
         L"Inno emits Flags: restart \u2014 if this component is installed, the setup "
         L"wizard offers a reboot at the end. Use for drivers or shell-extension DLLs "
         L"that cannot replace locked system files without a reboot.");
    AM(L"\r\n", false, RGB(0,0,0));

    AM(L"\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\r\n\r\n", false, RGB(100,140,180), 0, true);

    // ── Install Types ────────────────────────────────────────────────────────
    // shell32 #258 = shield / info badge, reused for the Manage Install Types topic.
    RegisterShell32Icon(hEdit, pd, 258);
    AM(ML(pd, L"man_comp_h7",
          L"\U0001F4CB  Manage Install Types") + L"\r\n",
       true, RGB(0,70,140), 12);
    AM(ML(pd, L"man_comp_p7",
          L"Install types are preset bundles of components \u2014 for example "
          L"\u201cFull\u201d, \u201cCompact\u201d, and \u201cCustom\u201d. The user "
          L"picks one from a dropdown at the start of the wizard and Inno pre-selects "
          L"the matching set of components. Click Manage Install Types at the bottom "
          L"of the page to add, edit, or remove types.") + L"\r\n\r\n",
       false, RGB(40,40,40));
    AM(ML(pd, L"man_comp_p7b",
          L"Each type has an internal name (no spaces, e.g. \u201cfull\u201d), a "
          L"display description shown in the wizard dropdown (e.g. \u201cFull "
          L"installation\u201d), and an optional Custom flag. A Custom type lets the "
          L"user manually adjust which components are selected after choosing a preset "
          L"\u2014 Inno emits Flags: iscustom for it. Assign a component to one or "
          L"more types via the Install Types field in the component edit dialog.") + L"\r\n\r\n",
       false, RGB(40,40,40));

    AM(L"\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\r\n\r\n", false, RGB(100,140,180), 0, true);

    // ── Warning: save before assigning dependencies ──────────────────────────
    RegisterSystemIcon(hEdit, pd, IDI_WARNING);
    AM(L"\u26A0  ", true, RGB(160,80,0), 11);
    AM(ML(pd, L"man_comp_warn",
          L"Save the project before assigning dependencies. Components receive a "
          L"permanent database ID only after the first Save. The dependency picker "
          L"will not list components that have never been saved. After saving, "
          L"press Choose again in the edit dialog to assign dependencies.") + L"\r\n\r\n",
       false, RGB(120,50,0));

    AM(L"\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\r\n\r\n", false, RGB(100,140,180), 0, true);

    // ── Next step ────────────────────────────────────────────────────────────
    // shell32 #294 = green checkmark.
    RegisterShell32Icon(hEdit, pd, 294);
    AM(L"\u25B6  ", true, RGB(0,120,0), 11);
    AM(ML(pd, L"man_comp_next",
          L"Next step: go to the Dependencies page to define external prerequisites "
          L"(redistributables, runtimes) that your installer will check for and "
          L"optionally download before setup begins.") + L"\r\n",
       false, RGB(0,80,0));
}

// ─── Dependencies page content ────────────────────────────────────────────────

static void PopulateDepsManual(HWND hEdit, ManualWndData* pd)
{
    // Reserve blank lines at the top for the logo image (same trick as other pages).
    int logoH = pd->pLogo ? (int)(pd->pLogo->GetHeight() * 0.75) : 0;
    int lines  = (logoH + S(10)) / S(15);
    for (int i = 0; i < lines; i++)
        AM(L"\r\n", false, RGB(0,0,0));

    // ── Page title ───────────────────────────────────────────────────────────
    // shell32 #278 = network/link dots — same icon used on the Dependencies toolbar button.
    RegisterShell32Icon(hEdit, pd, 278);
    AM(ML(pd, L"man_deps_h1",
          L"\U0001F517  Dependencies Page \u2014 What does it do?") + L"\r\n\r\n",
       true, RGB(0,70,140), 14, true);

    AM(ML(pd, L"man_deps_p1",
          L"The Dependencies page lets you declare every external prerequisite "
          L"(runtime, redistributable, or optional package) that your installer "
          L"should check for, and \u2014 depending on the delivery mode \u2014 "
          L"automatically install or guide the end user to obtain before your "
          L"application is set up. Each entry in the list becomes a [Run] or "
          L"[Code] block in the generated Inno Setup script.") + L"\r\n\r\n",
       false, RGB(40,40,40));

    AM(L"\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\r\n\r\n", false, RGB(100,140,180), 0, true);

    // ── Section 2: The list ──────────────────────────────────────────────────
    // shell32 #1 = generic document.
    RegisterShell32Icon(hEdit, pd, 1);
    AM(ML(pd, L"man_deps_h2",
          L"\U0001F4C4  The dependencies list") + L"\r\n",
       true, RGB(0,70,140), 12);
    AM(ML(pd, L"man_deps_p2",
          L"Each row represents one external prerequisite. The columns are:") + L"\r\n",
       false, RGB(40,40,40));

    auto bullet = [&](const wchar_t* key, const wchar_t* fb) {
        AM(L"  \u2022  ", true, RGB(0,70,140));
        AM(ML(pd, key, fb) + L"\r\n", false, RGB(60,60,60));
    };
    bullet(L"man_deps_r2a", L"Name \u2014 the display name shown in the installer and in this list.");
    bullet(L"man_deps_r2b", L"Required \u2014 \u2713 means the installer will abort if this dep cannot be installed; unchecked means optional.");
    bullet(L"man_deps_r2c", L"Delivery \u2014 how the dep reaches the end user\u2019s machine (Bundled, Auto Download, Redirect URL, Instructions Only).");
    bullet(L"man_deps_r2d", L"Architecture \u2014 Any, x64, or ARM64.");
    bullet(L"man_deps_r2e", L"Install order \u2014 the wizard stage at which this dep is installed (Before Welcome, After Welcome, Before Install, After Install, Custom Dialog).");

    AM(L"\r\n", false, RGB(0,0,0));
    AM(ML(pd, L"man_deps_p2b",
          L"Use Add to create a new entry, Edit to open the full edit dialog for "
          L"the selected row, and Remove to delete it. You can also double-click "
          L"a row to edit it.") + L"\r\n\r\n",
       false, RGB(40,40,40));

    AM(L"\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\r\n\r\n", false, RGB(100,140,180), 0, true);

    // ── Section 3: Edit dialog — core fields ─────────────────────────────────
    // shell32 #221 = information i — used for "edit dialog" sections across pages.
    RegisterShell32Icon(hEdit, pd, 221);
    AM(ML(pd, L"man_deps_h3",
          L"\u2699  Edit dialog \u2014 core fields") + L"\r\n",
       true, RGB(0,70,140), 12);

    auto ctrl = [&](const wchar_t* lblKey, const wchar_t* lblFb,
                    const wchar_t* txtKey, const wchar_t* txtFb) {
        AM(L"  ", false, RGB(0,0,0));
        AM(ML(pd, lblKey, lblFb) + L" \u2014 ", true, RGB(0,0,0));
        AM(ML(pd, txtKey, txtFb) + L"\r\n\r\n", false, RGB(60,60,60));
    };
    ctrl(L"man_deps_l3a", L"Display Name",
         L"man_deps_p3a",
         L"The human-readable label shown in the generated installer and in the "
         L"list. Keep it short and descriptive (e.g. \u201cMicrosoft Visual C++ 2022 "
         L"Redistributable (x64)\u201d).");
    ctrl(L"man_deps_l3b", L"Required",
         L"man_deps_p3b",
         L"When checked the installer aborts with an error if this prerequisite "
         L"cannot be satisfied. Uncheck for optional enhancements.");
    ctrl(L"man_deps_l3c", L"Delivery",
         L"man_deps_p3c",
         L"Chooses how the prerequisite is delivered. See the Delivery modes section below.");
    ctrl(L"man_deps_l3d", L"Architecture",
         L"man_deps_p3d",
         L"Any, x64, or ARM64. SetupCraft does not support 32-bit (x86) targets.");
    ctrl(L"man_deps_l3e", L"Install order",
         L"man_deps_p3e",
         L"The wizard stage at which this dep is installed: Before Welcome (silent, "
         L"before any UI), After Welcome, Before Install, After Install, or Custom Dialog "
         L"(developer-defined). Leave as Unspecified to let Inno Setup decide.");
    ctrl(L"man_deps_l3f", L"Credits",
         L"man_deps_p3f",
         L"Short attribution line shown in the About / Credits section of the "
         L"generated installer (e.g. \u201cPowered by .NET 8.0 \u00a9 Microsoft\u201d).");

    AM(L"\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\r\n\r\n", false, RGB(100,140,180), 0, true);

    // ── Section 4: Delivery modes ─────────────────────────────────────────────
    // shell32 #23 = question mark / help — the "how does it arrive?" question.
    RegisterShell32Icon(hEdit, pd, 23);
    AM(ML(pd, L"man_deps_h4",
          L"\u2753  Delivery modes") + L"\r\n",
       true, RGB(0,70,140), 12);
    bullet(L"man_deps_r4a",
           L"Bundled \u2014 the installer package includes the prerequisite file. "
           L"The generated script runs it silently from inside the installer.");
    bullet(L"man_deps_r4b",
           L"Auto Download \u2014 the installer fetches the file at run time from "
           L"the URL you specify, verifies the SHA-256 hash, and runs it silently. "
           L"Set a download timeout (seconds; 0 = wait forever) and list any "
           L"acceptable non-zero exit codes in Extra exit codes.");
    bullet(L"man_deps_r4c",
           L"Redirect URL \u2014 the installer opens the URL in the default browser "
           L"and pauses. The user downloads and runs the prerequisite manually; "
           L"the installer waits for confirmation before continuing.");
    bullet(L"man_deps_r4d",
           L"Instructions Only \u2014 no download or file is involved. The installer "
           L"displays the text from the Instructions field and asks the user to "
           L"complete the step before proceeding.");
    AM(L"\r\n", false, RGB(0,0,0));

    AM(L"\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\r\n\r\n", false, RGB(100,140,180), 0, true);

    // ── Section 5: Detection ─────────────────────────────────────────────────
    // shell32 #166 = registry / settings icon — natural fit for registry-key detection.
    RegisterShell32Icon(hEdit, pd, 166);
    AM(ML(pd, L"man_deps_h5",
          L"\U0001F50D  Detection \u2014 registry and file checks") + L"\r\n",
       true, RGB(0,70,140), 12);
    AM(ML(pd, L"man_deps_p5",
          L"Before installing a prerequisite the generated installer checks whether "
          L"it is already present. Two detection paths are available \u2014 either or "
          L"both may be filled in:") + L"\r\n",
       false, RGB(40,40,40));
    bullet(L"man_deps_r5a",
           L"Registry key \u2014 an HKLM path (e.g. SOFTWARE\\Microsoft\\VisualStudio\\14.0). "
           L"If the key exists, the dep is considered installed.");
    bullet(L"man_deps_r5b",
           L"File path \u2014 an absolute path on the end user\u2019s machine "
           L"(e.g. C:\\Windows\\System32\\msvcp140.dll). "
           L"If the file exists, the dep is considered installed.");
    AM(L"\r\n", false, RGB(0,0,0));
    AM(ML(pd, L"man_deps_p5b",
          L"Version check: set Min version and/or Max version to restrict the "
          L"acceptable version range. Choose where the version string is read from "
          L"with the Version source combo: None (no version check), Registry (reads "
          L"the value data of the registry key), or File (reads the FileVersion "
          L"resource of the detected file).") + L"\r\n\r\n",
       false, RGB(40,40,40));

    AM(L"\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\r\n\r\n", false, RGB(100,140,180), 0, true);

    // ── Section 6: Download settings ─────────────────────────────────────────
    // shell32 #258 = badge / download-related flag icon.
    RegisterShell32Icon(hEdit, pd, 258);
    AM(ML(pd, L"man_deps_h6",
          L"\U0001F4CB  Download settings") + L"\r\n",
       true, RGB(0,70,140), 12);
    AM(ML(pd, L"man_deps_p6",
          L"Relevant when Delivery is Bundled or Auto Download:") + L"\r\n",
       false, RGB(40,40,40));
    bullet(L"man_deps_r6a",
           L"URL \u2014 the download address (Auto Download) or redirect page (Redirect URL).");
    bullet(L"man_deps_r6b",
           L"SHA-256 \u2014 hex digest of the expected installer file. "
           L"The generated installer verifies this after downloading; mismatches abort.");
    bullet(L"man_deps_r6c",
           L"Silent args \u2014 command-line arguments passed to the prerequisite "
           L"installer (e.g. /quiet /norestart). These suppress any UI so the "
           L"install feels seamless.");
    bullet(L"man_deps_r6d",
           L"Download timeout \u2014 maximum seconds to wait for the download to "
           L"complete. Set to 0 to wait indefinitely (default).");
    bullet(L"man_deps_r6e",
           L"Extra exit codes \u2014 space-separated list of additional exit codes "
           L"to treat as success (e.g. 3010 for \u201creboot required\u201d, "
           L"1641 for \u201creboot initiated\u201d). SetupCraft detects these and "
           L"prompts the user before any reboot.");
    AM(L"\r\n", false, RGB(0,0,0));

    AM(L"\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\r\n\r\n", false, RGB(100,140,180), 0, true);

    // ── Section 7: Offline behavior ───────────────────────────────────────────
    // IDI_WARNING = system yellow warning triangle.
    RegisterSystemIcon(hEdit, pd, IDI_WARNING);
    AM(L"\u26A0  ", true, RGB(160,80,0), 11);
    AM(ML(pd, L"man_deps_h7",
          L"Offline behavior") + L"\r\n",
       true, RGB(160,80,0), 12);
    AM(ML(pd, L"man_deps_p7",
          L"Controls what happens when the installer cannot reach the download URL "
          L"(Auto Download only):") + L"\r\n",
       false, RGB(40,40,40));
    bullet(L"man_deps_r7a",
           L"Abort \u2014 installer exits with an error. Use this for required runtimes "
           L"that the application cannot function without.");
    bullet(L"man_deps_r7b",
           L"Warn and continue \u2014 shows a warning to the user, then proceeds "
           L"without installing the dep. Use for optional enhancements.");
    bullet(L"man_deps_r7c",
           L"Skip silently \u2014 skips the dep without any message. Only valid "
           L"when the dep is not required.");
    AM(L"\r\n", false, RGB(0,0,0));

    AM(L"\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\r\n\r\n", false, RGB(100,140,180), 0, true);

    // ── Section 8: License and credits ───────────────────────────────────────
    // shell32 #152 = document with lines / text file icon.
    RegisterShell32Icon(hEdit, pd, 152);
    AM(ML(pd, L"man_deps_h8",
          L"\U0001F4DD  License and credits") + L"\r\n",
       true, RGB(0,70,140), 12);
    AM(ML(pd, L"man_deps_p8",
          L"Some redistributables come with their own license that must be shown "
          L"to the end user. Use Browse\u2026 to pick an .rtf or .txt license file "
          L"from disk, or click Edit License\u2026 to write the license text inline. "
          L"When a license is set the generated installer displays it on a dedicated "
          L"license page for that prerequisite before installing it.") + L"\r\n\r\n",
       false, RGB(40,40,40));
    AM(ML(pd, L"man_deps_p8b",
          L"Credits is a short attribution line (e.g. \u201cPowered by .NET 8.0 "
          L"\u00a9 Microsoft\u201d) that appears in the About / Credits section of "
          L"the generated installer. Instructions adds one or more RTF guidance "
          L"pages \u2014 used only with the Instructions Only delivery mode.") + L"\r\n\r\n",
       false, RGB(40,40,40));

    AM(L"\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\r\n\r\n", false, RGB(100,140,180), 0, true);

    // ── Section 9: Component linkage ─────────────────────────────────────────
    // shell32 #294 = green checkmark — highlights the "connection" to components.
    RegisterShell32Icon(hEdit, pd, 294);
    AM(ML(pd, L"man_deps_h9",
          L"\U0001F9E9  Component linkage") + L"\r\n",
       true, RGB(0,70,140), 12);
    AM(ML(pd, L"man_deps_p9",
          L"When your project has components defined, an additional field appears "
          L"in the edit dialog: Linked components. Click the \u2026 picker button to "
          L"choose which components this prerequisite is tied to. The dep will only "
          L"be installed when at least one of the linked components is selected by "
          L"the user. Leave empty to install the dep unconditionally for all "
          L"installation types.") + L"\r\n\r\n",
       false, RGB(40,40,40));

    AM(L"\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\r\n\r\n", false, RGB(100,140,180), 0, true);

    // ── Warning ───────────────────────────────────────────────────────────────
    RegisterSystemIcon(hEdit, pd, IDI_WARNING);
    AM(L"\u26A0  ", true, RGB(160,80,0), 11);
    AM(ML(pd, L"man_deps_warn",
          L"Save the project before linking components to a dependency. Components "
          L"only receive a permanent database ID after the first Save; unsaved "
          L"components will not appear in the component picker.") + L"\r\n\r\n",
       false, RGB(120,50,0));

    AM(L"\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\r\n\r\n", false, RGB(100,140,180), 0, true);

    // ── Next step ─────────────────────────────────────────────────────────────
    // shell32 #294 = green checkmark.
    RegisterShell32Icon(hEdit, pd, 294);
    AM(L"\u25B6  ", true, RGB(0,120,0), 11);
    AM(ML(pd, L"man_deps_next",
          L"Next step: go to Dialogs to customise the installer\u2019s wizard pages, "
          L"or proceed to Settings to configure the installer name, icon, output "
          L"folder, and supported languages.") + L"\r\n",
       false, RGB(0,80,0));
}

// ─── Dialogs page content ─────────────────────────────────────────────────────

static void PopulateDialogsManual(HWND hEdit, ManualWndData* pd)
{
    // Reserve blank lines at the top for the logo image.
    int logoH = pd->pLogo ? (int)(pd->pLogo->GetHeight() * 0.75) : 0;
    int lines  = (logoH + S(10)) / S(15);
    for (int i = 0; i < lines; i++)
        AM(L"\r\n", false, RGB(0,0,0));

    // ── Page title ───────────────────────────────────────────────────────────
    // shell32 #23 = question-mark / help balloon — same icon used on the toolbar button.
    RegisterShell32Icon(hEdit, pd, 23);
    AM(ML(pd, L"man_dlg_h1",
          L"\u2753  Dialogs Page \u2014 What does it do?") + L"\r\n\r\n",
       true, RGB(0,70,140), 14, true);

    AM(ML(pd, L"man_dlg_p1",
          L"The Dialogs page lets you customise every wizard screen the end user "
          L"sees during installation. For each dialog you can write or paste RTF "
          L"content (the welcome message, license text, finish note, etc.), enable "
          L"or disable the dialog entirely, and preview how it will look side-by-side "
          L"with the real installer layout. Changes are stored in the project database "
          L"and reflected in the generated Inno Setup script.") + L"\r\n\r\n",
       false, RGB(40,40,40));

    AM(L"\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\r\n\r\n", false, RGB(100,140,180), 0, true);

    // ── Section 2: The wizard sequence ──────────────────────────────────────
    // shell32 #294 = green checkmark — "the steps in order".
    RegisterShell32Icon(hEdit, pd, 294);
    AM(ML(pd, L"man_dlg_h2",
          L"\u2714  Wizard dialog sequence") + L"\r\n",
       true, RGB(0,70,140), 12);
    AM(ML(pd, L"man_dlg_p2",
          L"The rows appear in the order the end user experiences them:") + L"\r\n",
       false, RGB(40,40,40));

    auto bullet = [&](const wchar_t* key, const wchar_t* fb) {
        AM(L"  \u2022  ", true, RGB(0,70,140));
        AM(ML(pd, key, fb) + L"\r\n", false, RGB(60,60,60));
    };
    bullet(L"man_dlg_r2a", L"Welcome \u2014 the first screen; typically a greeting and brief description of the installer.");
    bullet(L"man_dlg_r2b", L"License \u2014 displays the product license; can require acceptance before Next is enabled.");
    bullet(L"man_dlg_r2c", L"Dependencies \u2014 shown only when the project has \u22651 external dependency defined.");
    bullet(L"man_dlg_r2d", L"For Me / All Users \u2014 shown only when AskAtInstall (install-scope choice) is enabled on the Files page.");
    bullet(L"man_dlg_r2e", L"Select Folder \u2014 lets the user choose the install location; can be disabled via Settings \u2192 DisableDirPage.");
    bullet(L"man_dlg_r2f", L"Components \u2014 shown only when the project uses component-based install.");
    bullet(L"man_dlg_r2g", L"Shortcuts \u2014 shown only when any shortcut opt-out checkbox is enabled.");
    bullet(L"man_dlg_r2h", L"Ready \u2014 summary of choices before the actual file transfer begins.");
    bullet(L"man_dlg_r2i", L"Installing \u2014 the progress screen; always present, cannot be disabled.");
    bullet(L"man_dlg_r2j", L"Finish \u2014 the final screen; optionally offers to launch the application.");

    AM(L"\r\n", false, RGB(0,0,0));
    AM(L"\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\r\n\r\n", false, RGB(100,140,180), 0, true);

    // ── Section 3: Per-row controls ──────────────────────────────────────────
    // shell32 #221 = information i.
    RegisterShell32Icon(hEdit, pd, 221);
    AM(ML(pd, L"man_dlg_h3",
          L"\u2699  Per-row controls") + L"\r\n",
       true, RGB(0,70,140), 12);

    auto ctrl = [&](const wchar_t* lblKey, const wchar_t* lblFb,
                    const wchar_t* txtKey, const wchar_t* txtFb) {
        AM(L"  ", false, RGB(0,0,0));
        AM(ML(pd, lblKey, lblFb) + L" \u2014 ", true, RGB(0,0,0));
        AM(ML(pd, txtKey, txtFb) + L"\r\n\r\n", false, RGB(60,60,60));
    };
    ctrl(L"man_dlg_l3a", L"Enable checkbox",
         L"man_dlg_p3a",
         L"Ticking this off removes the dialog from the generated installer entirely. "
         L"Available on Welcome, License, Ready, and Finish. The Installing row "
         L"cannot be disabled.");
    ctrl(L"man_dlg_l3b", L"Edit Content\u2026",
         L"man_dlg_p3b",
         L"Opens a rich-text editor where you write the body text shown inside "
         L"that dialog. Formatting (bold, italic, colors, font size) is preserved "
         L"and written as RTF to the generated Inno script.");
    ctrl(L"man_dlg_l3c", L"Preview\u2026",
         L"man_dlg_p3c",
         L"Opens the installer preview window showing all enabled dialogs in "
         L"sequence. Use Back and Next to step through them exactly as the end "
         L"user would. Close the preview to return to editing.");

    AM(L"\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\r\n\r\n", false, RGB(100,140,180), 0, true);

    // ── Section 4: License dialog extras ────────────────────────────────────
    // shell32 #152 = document / text-file icon.
    RegisterShell32Icon(hEdit, pd, 152);
    AM(ML(pd, L"man_dlg_h4",
          L"\U0001F4DD  License dialog \u2014 extra options") + L"\r\n",
       true, RGB(0,70,140), 12);
    bullet(L"man_dlg_r4a",
           L"Require acceptance \u2014 when checked the Next button stays disabled "
           L"until the user ticks \u201cI accept the agreement\u201d. Inno emits "
           L"LicenseFile + DisableReadyMemo=no.");
    bullet(L"man_dlg_r4b",
           L"License template \u2014 choose a standard template (MIT, GPL\u00a0v2, "
           L"Apache\u00a02.0, etc.) to pre-fill the editor instead of writing from scratch.");
    bullet(L"man_dlg_r4c",
           L"License source \u2014 Built-in RTF editor (content stored in the project "
           L"database) or External file (picks an .rtf or .txt file from disk; path "
           L"is embedded in the script at build time).");
    AM(L"\r\n", false, RGB(0,0,0));

    AM(L"\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\r\n\r\n", false, RGB(100,140,180), 0, true);

    // ── Section 5: Finish dialog extras ─────────────────────────────────────
    // shell32 #294 = green checkmark.
    RegisterShell32Icon(hEdit, pd, 294);
    AM(ML(pd, L"man_dlg_h5",
          L"\u2714  Finish dialog \u2014 launch app when done") + L"\r\n",
       true, RGB(0,70,140), 12);
    AM(ML(pd, L"man_dlg_p5",
          L"Enable the Launch app checkbox to add a \u201cLaunch MyApp\u201d "
          L"option on the Finish screen. The generated script emits a [Run] entry "
          L"with Flags: nowait postinstall shellexec skipifsilent. Additional options:") + L"\r\n",
       false, RGB(40,40,40));
    bullet(L"man_dlg_r5a",
           L"Description text \u2014 the label shown next to the launch checkbox "
           L"(e.g. \u201cLaunch MyApp 3.0\u201d).");
    bullet(L"man_dlg_r5b",
           L"Checked by default \u2014 when ticked the launch checkbox starts pre-checked; "
           L"uncheck to leave it unchecked and let the user opt in.");
    AM(L"\r\n", false, RGB(0,0,0));

    AM(L"\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\r\n\r\n", false, RGB(100,140,180), 0, true);

    // ── Section 6: Ready dialog extras ──────────────────────────────────────
    // shell32 #258 = badge / flag icon.
    RegisterShell32Icon(hEdit, pd, 258);
    AM(ML(pd, L"man_dlg_h6",
          L"\U0001F4CB  Ready dialog \u2014 summary options") + L"\r\n",
       true, RGB(0,70,140), 12);
    bullet(L"man_dlg_r6a",
           L"Always show install dir \u2014 maps to Inno\u2019s AlwaysShowDirOnReadyPage; "
           L"the chosen destination folder is always listed in the summary.");
    bullet(L"man_dlg_r6b",
           L"Always show group \u2014 maps to AlwaysShowGroupOnReadyPage; "
           L"the Start Menu group name is always listed in the summary.");
    AM(L"\r\n", false, RGB(0,0,0));

    AM(L"\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\r\n\r\n", false, RGB(100,140,180), 0, true);

    // ── Section 7: Preview size panel ────────────────────────────────────────
    // shell32 #3 = yellow folder — repurposed here as a "layout / window" hint.
    RegisterShell32Icon(hEdit, pd, 3);
    AM(ML(pd, L"man_dlg_h7",
          L"\U0001F5BC  Preview size and alignment") + L"\r\n",
       true, RGB(0,70,140), 12);
    AM(ML(pd, L"man_dlg_p7",
          L"When the preview is open a small floating panel appears to its left. "
          L"Use the Width and Height spinners to set the installer dialog dimensions "
          L"in logical pixels (DPI-scaled automatically). The Horizontal and Vertical "
          L"alignment combos control where the dialog appears on screen. The Reset "
          L"button clears any custom size and lets SetupCraft auto-fit. Size and "
          L"alignment are saved to the project database on the next Save.") + L"\r\n\r\n",
       false, RGB(40,40,40));

    AM(ML(pd, L"man_dlg_p7b",
          L"The Header font section in the same panel lets you override the "
          L"installer wizard\u2019s page-name label font (Inno\u2019s WizardForm.PageNameLabel). "
          L"Set a family name, point size, Bold, and Italic. Enable \u201cUse on all "
          L"dialogs\u201d to apply the font globally via CurPageChanged in [Code]; "
          L"leave it off to apply only to the currently previewed dialog.") + L"\r\n\r\n",
       false, RGB(40,40,40));

    AM(L"\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\r\n\r\n", false, RGB(100,140,180), 0, true);

    // ── Next step ─────────────────────────────────────────────────────────────
    // shell32 #294 = green checkmark.
    RegisterShell32Icon(hEdit, pd, 294);
    AM(L"\u25B6  ", true, RGB(0,120,0), 11);
    AM(ML(pd, L"man_dlg_next",
          L"Next step: go to Settings to configure the installer name, publisher, "
          L"icon, output folder, and supported languages.") + L"\r\n",
       false, RGB(0,80,0));
}

// ─── Settings page content ────────────────────────────────────────────────────

static void PopulateSettingsManual(HWND hEdit, ManualWndData* pd)
{
    // Reserve blank lines at the top for the logo image.
    int logoH = pd->pLogo ? (int)(pd->pLogo->GetHeight() * 0.75) : 0;
    int lines  = (logoH + S(10)) / S(15);
    for (int i = 0; i < lines; i++)
        AM(L"\r\n", false, RGB(0,0,0));

    // ── Page title ────────────────────────────────────────────────────────────
    // shell32 #314 = gear / settings icon — same icon used on the toolbar button.
    RegisterShell32Icon(hEdit, pd, 314);
    AM(ML(pd, L"man_sett_h1",
          L"\u2699  Settings \u2014 What does it do?") + L"\r\n\r\n",
       true, RGB(0,70,140), 14, true);

    AM(ML(pd, L"man_sett_p1",
          L"The Settings page is the global configuration hub for your installer "
          L"project. It controls how the installer is identified (name, version, "
          L"publisher), where and how it builds its output file, how it presents "
          L"itself to the user (UAC level, wizard style, language selection), what "
          L"it installs alongside your files (PATH entries), and how it handles "
          L"uninstallation and code signing. Changes on this page are saved to the "
          L"project database when you press Save.") + L"\r\n\r\n",
       false, RGB(40,40,40));

    AM(L"\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\r\n\r\n", false, RGB(100,140,180), 0, true);

    auto ctrl = [&](const wchar_t* lblKey, const wchar_t* lblFb,
                    const wchar_t* txtKey, const wchar_t* txtFb) {
        AM(L"  ", false, RGB(0,0,0));
        AM(ML(pd, lblKey, lblFb) + L" \u2014 ", true, RGB(0,0,0));
        AM(ML(pd, txtKey, txtFb) + L"\r\n\r\n", false, RGB(60,60,60));
    };
    auto flag = [&](const wchar_t* nameKey, const wchar_t* nameFb,
                    const wchar_t* descKey, const wchar_t* descFb) {
        AM(L"  ", false, RGB(0,0,0));
        AM(ML(pd, nameKey, nameFb), true, RGB(0,70,140));
        AM(L" \u2014 " + ML(pd, descKey, descFb) + L"\r\n", false, RGB(60,60,60));
    };

    // ── Section 2: Application identity ──────────────────────────────────────
    // shell32 #221 = information (i) icon.
    RegisterShell32Icon(hEdit, pd, 221);
    AM(ML(pd, L"man_sett_h2",
          L"\U0001F194  Application identity") + L"\r\n",
       true, RGB(0,70,140), 12);
    ctrl(L"man_sett_l2a", L"App Name",
         L"man_sett_p2a",
         L"The application\u2019s display name \u2014 shown in the installer title bar, "
         L"the Add/Remove Programs entry, and the Start Menu group. "
         L"This is Inno Setup\u2019s AppName directive.");
    ctrl(L"man_sett_l2b", L"Version",
         L"man_sett_p2b",
         L"The version string (e.g. 2.1.0). Written to Inno\u2019s AppVersion directive "
         L"and to the uninstaller\u2019s version record. Shown in Add/Remove Programs.");
    ctrl(L"man_sett_l2c", L"Publisher",
         L"man_sett_p2c",
         L"Your company or developer name. Used in Inno\u2019s AppPublisher directive "
         L"and in the Add/Remove Programs entry.");
    ctrl(L"man_sett_l2d", L"Publisher URL",
         L"man_sett_p2d",
         L"URL for the publisher\u2019s website. Inno emits AppPublisherURL; "
         L"shown in Add/Remove Programs as the publisher link.");
    ctrl(L"man_sett_l2e", L"Support URL",
         L"man_sett_p2e",
         L"Support or help-page URL. Inno emits AppSupportURL; "
         L"shown in Add/Remove Programs separately from the publisher URL.");
    ctrl(L"man_sett_l2f", L"App Icon",
         L"man_sett_p2f",
         L"A .ico file displayed in the installer title bar, the uninstaller, and "
         L"the Add/Remove Programs entry. Click Change Icon\u2026 to pick a file, "
         L"or drag-and-drop an .ico file onto the preview.");
    ctrl(L"man_sett_l2g", L"AppId / GUID",
         L"man_sett_p2g",
         L"A unique identifier (GUID) that Inno Setup uses to detect whether this "
         L"application is already installed and to link updates to previous versions. "
         L"Click Regenerate to create a fresh GUID. Do not change the GUID between "
         L"version updates \u2014 doing so breaks the \u201calready installed\u201d "
         L"detection and leaves orphan entries in Add/Remove Programs.");

    AM(L"\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\r\n\r\n", false, RGB(100,140,180), 0, true);

    // ── Section 3: Build output ───────────────────────────────────────────────
    // shell32 #80 = build / package icon.
    RegisterShell32Icon(hEdit, pd, 80);
    AM(ML(pd, L"man_sett_h3",
          L"\U0001F3D7  Build output") + L"\r\n",
       true, RGB(0,70,140), 12);
    ctrl(L"man_sett_l3a", L"Output Folder",
         L"man_sett_p3a",
         L"The folder where the finished installer .exe is written. "
         L"Click the browse button (\u2026) to pick a folder from disk. "
         L"The path may contain Inno constants such as {src}.");
    ctrl(L"man_sett_l3b", L"Output Filename",
         L"man_sett_p3b",
         L"The .exe filename without extension (e.g. \u201cMyApp_Setup_2.1.0\u201d). "
         L"Inno appends .exe automatically. Leave blank to use Inno\u2019s default "
         L"(usually \u201csetup\u201d).");
    ctrl(L"man_sett_l3c", L"Compression",
         L"man_sett_p3c",
         L"The compression algorithm: LZMA2 (best ratio, recommended), Zip (fast, "
         L"widely compatible), BZip2, or None (no compression, fastest build). "
         L"LZMA2 at level 9 typically reduces installer size by 50\u201370\u00a0% "
         L"compared to no compression.");
    ctrl(L"man_sett_l3d", L"Compression Level",
         L"man_sett_p3d",
         L"Level 0\u20139: higher = smaller file but slower build. Level\u00a05 "
         L"(default) is a good balance. Use 9 for release builds and 1\u20132 "
         L"for rapid test builds.");
    ctrl(L"man_sett_l3e", L"Solid Compression",
         L"man_sett_p3e",
         L"Compress all files together as one stream instead of per-file. Increases "
         L"compression ratio for projects with many similar files (source code, "
         L"locale files). Slightly slower decompression on install.");

    AM(L"\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\r\n\r\n", false, RGB(100,140,180), 0, true);

    // ── Section 4: Installation ───────────────────────────────────────────────
    // shell32 #78 = shield (UAC-style) — suits the Installation / privileges topic.
    RegisterShell32Icon(hEdit, pd, 78);
    AM(ML(pd, L"man_sett_h4",
          L"\U0001F6E1  Installation") + L"\r\n",
       true, RGB(0,70,140), 12);
    ctrl(L"man_sett_l4a", L"Default Install Folder",
         L"man_sett_p4a",
         L"The base token for the default install path \u2014 for example {autopf} "
         L"(Program Files, 32/64-bit adaptive, recommended), {pf64} (64-bit Program "
         L"Files), {pf} (32-bit), or a custom Inno constant. The user can change the "
         L"path at install time unless Hide Select Folder page is ticked.");
    ctrl(L"man_sett_l4b", L"UAC Privilege Level",
         L"man_sett_p4b",
         L"requireAdministrator \u2014 setup always runs elevated; suitable for "
         L"system-wide installs. asInvoker \u2014 runs at the invoking user\u2019s "
         L"privilege level; suitable for per-user installs. highestAvailable \u2014 "
         L"elevates if the user is an administrator, otherwise runs as standard user.");
    ctrl(L"man_sett_l4c", L"Minimum OS",
         L"man_sett_p4c",
         L"The lowest Windows version the installer will run on. If the end "
         L"user\u2019s OS is older, setup displays an error and exits. Choose from "
         L"Windows 10, 8.1, 8, 7, Vista, or XP.");
    ctrl(L"man_sett_l4d", L"Wizard Style",
         L"man_sett_p4d",
         L"modern (default since Inno\u00a06) \u2014 the Aero-glass, borderless "
         L"wizard. classic \u2014 the traditional Inno\u00a05 style with a side "
         L"banner image. Most projects should use modern.");
    ctrl(L"man_sett_l4e", L"Privileges Override",
         L"man_sett_p4e",
         L"Controls whether the user is allowed to switch between per-machine and "
         L"per-user install scopes at run time. commandline \u2014 only via "
         L"/ALLUSERS or /CURRENTUSER command-line switch. dialog \u2014 a dialog is "
         L"shown at startup. Both values require UAC to be set to highestAvailable.");

    AM(ML(pd, L"man_sett_h4b",
          L"  Installation page flags:") + L"\r\n",
       true, RGB(0,0,0));
    flag(L"man_sett_f_disabledir",   L"Hide Select Folder page",
         L"man_sett_f_disabledir_d",
         L"Skips the \u2018Select Destination Location\u2019 wizard page; the "
         L"installer uses the default folder silently. Inno emits "
         L"DisableDirPage=yes. Turn on when the install path must not be changed.");
    flag(L"man_sett_f_disablegroup", L"Hide Select Group page",
         L"man_sett_f_disablegroup_d",
         L"Skips the \u2018Select Start Menu Folder\u2019 page; uses the default "
         L"program group. Inno emits DisableProgramGroupPage=yes.");
    flag(L"man_sett_f_useprevdir",   L"Remember install folder",
         L"man_sett_f_useprevdir_d",
         L"When upgrading, pre-fills the install folder with the previous "
         L"version\u2019s location (read from the registry). "
         L"Inno emits UsePreviousAppDir=yes.");
    flag(L"man_sett_f_useprevgroup", L"Remember program group",
         L"man_sett_f_useprevgroup_d",
         L"When upgrading, pre-fills the Start Menu group with the previous "
         L"version\u2019s group. Inno emits UsePreviousGroup=yes.");
    flag(L"man_sett_f_dirwarning",   L"Warn if folder not empty",
         L"man_sett_f_dirwarning_d",
         L"Controls Inno\u2019s DirExistsWarning directive: auto (warn only when "
         L"the folder already contains files), yes (always warn), or no (never warn). "
         L"Use auto for upgrade installers to avoid unnecessary prompts.");
    AM(L"\r\n", false, RGB(0,0,0));

    AM(L"\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\r\n\r\n", false, RGB(100,140,180), 0, true);

    // ── Section 5: Installer Languages ───────────────────────────────────────
    // shell32 #294 = green checkmark — represents the language tick-boxes.
    RegisterShell32Icon(hEdit, pd, 294);
    AM(ML(pd, L"man_sett_h5",
          L"\U0001F310  Installer Languages") + L"\r\n",
       true, RGB(0,70,140), 12);
    AM(ML(pd, L"man_sett_p5",
          L"Tick one or more language checkboxes to include those translations in "
          L"the installer. English is always included and cannot be unchecked. Each "
          L"ticked language adds a Messages section entry and a bundled .isl file "
          L"to the generated script.") + L"\r\n\r\n",
       false, RGB(40,40,40));
    ctrl(L"man_sett_l5a", L"Language Detection",
         L"man_sett_p5a",
         L"How the installer picks the initial language: uilanguage (matches the "
         L"Windows UI language \u2014 recommended), locale (matches the system "
         L"locale), or none (always shows the language dialog).");
    ctrl(L"man_sett_l5b", L"Show Language Dialog",
         L"man_sett_p5b",
         L"Whether to show a language-selection dialog at startup: auto (shown only "
         L"when multiple languages are included), yes (always shown), or no (never "
         L"shown \u2014 silently uses the detected language).");

    AM(L"\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\r\n\r\n", false, RGB(100,140,180), 0, true);

    // ── Section 6: System Integration ────────────────────────────────────────
    // shell32 #166 = registry / settings icon — system-level PATH modification.
    RegisterShell32Icon(hEdit, pd, 166);
    AM(ML(pd, L"man_sett_h6",
          L"\U0001F50C  System integration \u2014 PATH folders") + L"\r\n",
       true, RGB(0,70,140), 12);
    AM(ML(pd, L"man_sett_p6",
          L"List the install-tree folders whose paths should be appended to the "
          L"system PATH environment variable. Use Add to open the VFS folder picker "
          L"and choose a folder; use Remove to delete the selected entry. The "
          L"generated script emits the necessary [Registry] entries to modify "
          L"HKLM\\SYSTEM\\CurrentControlSet\\Control\\Session Manager\\Environment\\Path "
          L"(or HKCU for per-user installs).") + L"\r\n\r\n",
       false, RGB(40,40,40));
    AM(ML(pd, L"man_sett_p6b",
          L"Only folders that exist in the VFS (Files page) can be added. Typically "
          L"this is the folder containing the application\u2019s main executable. "
          L"Remember to tick ChangesEnvironment in the Uninstall section so Windows "
          L"broadcasts the PATH change without requiring a reboot.") + L"\r\n\r\n",
       false, RGB(40,40,40));

    AM(L"\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\r\n\r\n", false, RGB(100,140,180), 0, true);

    // ── Section 7: Uninstall ──────────────────────────────────────────────────
    // shell32 #131 = red X / remove icon — matches the toolbar Close Project button.
    RegisterShell32Icon(hEdit, pd, 131);
    AM(ML(pd, L"man_sett_h7",
          L"\U0001F5D1  Uninstall") + L"\r\n",
       true, RGB(0,70,140), 12);
    flag(L"man_sett_f_uninstall",   L"Allow uninstall",
         L"man_sett_f_uninstall_d",
         L"Adds an entry to Add/Remove Programs and includes the uninstaller. "
         L"Inno emits Uninstallable=yes. Leave ticked for almost all applications.");
    flag(L"man_sett_f_closeapps",   L"Close apps before install",
         L"man_sett_f_closeapps_d",
         L"Inno emits CloseApplications=yes \u2014 setup detects processes using "
         L"files it intends to overwrite and prompts the user to close them before "
         L"proceeding. Reduces the need for a reboot.");
    flag(L"man_sett_f_changes_env", L"ChangesEnvironment",
         L"man_sett_f_changes_env_d",
         L"Inno emits ChangesEnvironment=yes \u2014 signals Windows Explorer to "
         L"broadcast WM_SETTINGCHANGE so PATH additions take effect without a "
         L"reboot. Enable whenever you add PATH entries in System Integration.");
    AM(L"\r\n", false, RGB(0,0,0));
    ctrl(L"man_sett_l7a", L"Uninstall Display Name",
         L"man_sett_p7a",
         L"Override the name shown in Add/Remove Programs. Leave blank to use the "
         L"App Name. Useful when the uninstaller should display a longer name such "
         L"as \u201cMyApp 3.0 (x64)\u201d.");
    ctrl(L"man_sett_l7b", L"Uninstall Files Dir",
         L"man_sett_p7b",
         L"The folder where the uninstaller .exe is placed. Leave blank to use "
         L"{app} (the install folder). Change only when the install folder is "
         L"read-only or shared.");

    AM(L"\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\r\n\r\n", false, RGB(100,140,180), 0, true);

    // ── Section 8: Code signing ───────────────────────────────────────────────
    // shell32 #77 = key / security icon.
    RegisterShell32Icon(hEdit, pd, 77);
    AM(ML(pd, L"man_sett_h8",
          L"\U0001F511  Code signing") + L"\r\n",
       true, RGB(0,70,140), 12);
    AM(ML(pd, L"man_sett_p8",
          L"Code-signing the installer .exe prevents Windows SmartScreen from "
          L"blocking it and shows your publisher name in the UAC dialog. Enable "
          L"signing, fill in the details below, and SetupCraft calls signtool.exe "
          L"automatically at the end of every Build (F7).") + L"\r\n\r\n",
       false, RGB(40,40,40));
    flag(L"man_sett_f_sign",   L"Enable signing",
         L"man_sett_f_sign_d",
         L"When ticked, SetupCraft invokes signtool.exe after a successful build. "
         L"All other signing fields become active.");
    AM(L"\r\n", false, RGB(0,0,0));
    ctrl(L"man_sett_l8a", L"Signtool Path",
         L"man_sett_p8a",
         L"Full path to signtool.exe (e.g. C:\\Program Files (x86)\\Windows "
         L"Kits\\10\\bin\\10.0.22621.0\\x64\\signtool.exe). "
         L"Click the browse button to navigate to it.");
    ctrl(L"man_sett_l8b", L"Certificate Thumbprint",
         L"man_sett_p8b",
         L"The SHA1 thumbprint of the code-signing certificate already installed "
         L"in your certificate store (My\\LocalMachine or My\\CurrentUser). "
         L"signtool uses /sha1 to select this certificate.");
    ctrl(L"man_sett_l8c", L"PFX File",
         L"man_sett_p8c",
         L"Path to a .pfx (PKCS#12) certificate file. Used instead of (or together "
         L"with) the thumbprint. Leave blank if you are using a certificate already "
         L"installed in the Windows certificate store.");
    ctrl(L"man_sett_l8d", L"PFX Password",
         L"man_sett_p8d",
         L"Password for the .pfx file. Stored encrypted in the project database. "
         L"Leave blank if the .pfx has no password or if you are using a "
         L"store certificate.");
    ctrl(L"man_sett_l8e", L"Timestamp URL",
         L"man_sett_p8e",
         L"URL of the timestamp server that countersigns the signature so it "
         L"remains valid after the certificate expires. Example: "
         L"http://timestamp.digicert.com (RFC\u00a03161\u00a0/\u00a0SHA-256). "
         L"Strongly recommended for release builds.");
    ctrl(L"man_sett_l8f", L"Timestamp Algorithm",
         L"man_sett_p8f",
         L"sha256 (recommended \u2014 required for Authenticode on Windows\u00a08+) "
         L"or sha1 (legacy only). Most timestamp servers accept both; sha256 is "
         L"the modern standard.");
    ctrl(L"man_sett_l8g", L"Description",
         L"man_sett_p8g",
         L"Optional /d argument passed to signtool \u2014 the string shown in the "
         L"UAC elevation dialog (e.g. \u201cMyApp 3.0 Installer\u201d). "
         L"Leave blank to omit the /d flag.");

    AM(L"\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\r\n\r\n", false, RGB(100,140,180), 0, true);

    // ── Section 9: Setup log ──────────────────────────────────────────────────
    // shell32 #152 = document with lines / text-file icon.
    RegisterShell32Icon(hEdit, pd, 152);
    AM(ML(pd, L"man_sett_h9",
          L"\U0001F4CB  Setup log") + L"\r\n",
       true, RGB(0,70,140), 12);
    AM(ML(pd, L"man_sett_p9",
          L"When enabled, Inno Setup writes a text log of every installation action "
          L"(files copied, registry entries written, errors encountered) to disk. "
          L"Useful for diagnosing post-install problems reported by end users.") + L"\r\n\r\n",
       false, RGB(40,40,40));
    flag(L"man_sett_f_log",   L"Enable setup log",
         L"man_sett_f_log_d",
         L"Activates logging. Inno emits SetupLogging=yes. The log is written at "
         L"install time, not at build time.");
    AM(L"\r\n", false, RGB(0,0,0));
    ctrl(L"man_sett_l9a", L"Log Folder",
         L"man_sett_p9a",
         L"Folder where the log file is created (e.g. {app}, {tmp}, or an absolute "
         L"path). Leave blank to use {tmp} (the system temporary folder).");
    ctrl(L"man_sett_l9b", L"Log Filename",
         L"man_sett_p9b",
         L"Name of the log file (e.g. install_log.txt). Leave blank for Inno\u2019s "
         L"default (Setup Log YYYY-MM-DD #NNN.txt in {tmp}).");
    ctrl(L"man_sett_l9c", L"Log Mode",
         L"man_sett_p9c",
         L"Overwrite \u2014 create a fresh log each time (default). "
         L"Append \u2014 add to an existing log, which is useful for "
         L"multi-installer suites.");

    AM(L"\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\r\n\r\n", false, RGB(100,140,180), 0, true);

    // ── Next step ─────────────────────────────────────────────────────────────
    // shell32 #166 = registry icon — points toward Registry (next in toolbar).
    RegisterShell32Icon(hEdit, pd, 166);
    AM(L"\u25B6  ", true, RGB(0,120,0), 11);
    AM(ML(pd, L"man_sett_next",
          L"Next step: go to Registry to add custom registry entries and configure "
          L"the Windows Add/Remove Programs entry.") + L"\r\n",
       false, RGB(0,80,0));
}

// ─── Registry page content ────────────────────────────────────────────────────

static void PopulateRegistryManual(HWND hEdit, ManualWndData* pd)
{
    int logoH = pd->pLogo ? (int)(pd->pLogo->GetHeight() * 0.75) : 0;
    int lines  = (logoH + S(10)) / S(15);
    for (int i = 0; i < lines; i++)
        AM(L"\r\n", false, RGB(0,0,0));

    RegisterShell32Icon(hEdit, pd, 166);
    AM(ML(pd, L"man_reg_h1",
          L"\U0001F5C2  Registry \u2014 What does it do?") + L"\r\n\r\n",
       true, RGB(0,70,140), 14, true);
    AM(ML(pd, L"man_reg_p1",
          L"The Registry page has two roles: it controls the Add/Remove Programs "
          L"(ARP) entry that Windows shows in Settings \u2192 Apps, and it lets you "
          L"add custom registry keys and values to the installer. The ARP section "
          L"is generated automatically from your project\u2019s Settings; the custom "
          L"entries give you full control over anything else your application needs "
          L"in the Windows registry.") + L"\r\n\r\n",
       false, RGB(40,40,40));
    AM(L"\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\r\n\r\n", false, RGB(100,140,180), 0, true);

    auto reg_ctrl = [&](const wchar_t* lblKey, const wchar_t* lblFb,
                        const wchar_t* txtKey, const wchar_t* txtFb) {
        AM(L"  ", false, RGB(0,0,0));
        AM(ML(pd, lblKey, lblFb) + L" \u2014 ", true, RGB(0,0,0));
        AM(ML(pd, txtKey, txtFb) + L"\r\n\r\n", false, RGB(60,60,60));
    };
    auto reg_flag = [&](const wchar_t* nameKey, const wchar_t* nameFb,
                        const wchar_t* descKey, const wchar_t* descFb) {
        AM(L"  ", false, RGB(0,0,0));
        AM(ML(pd, nameKey, nameFb), true, RGB(0,70,140));
        AM(L" \u2014 " + ML(pd, descKey, descFb) + L"\r\n", false, RGB(60,60,60));
    };

    // ── Section 2: Add/Remove Programs entry ─────────────────────────────────
    RegisterShell32Icon(hEdit, pd, 221);
    AM(ML(pd, L"man_reg_h2",
          L"\U0001F4CB  Add/Remove Programs entry") + L"\r\n",
       true, RGB(0,70,140), 12);
    reg_flag(L"man_reg_f_register", L"Register in Windows Installed Programs",
             L"man_reg_f_register_d",
             L"When ticked, the installer writes an entry to "
             L"HKLM\\SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Uninstall so the "
             L"application appears in Settings \u2192 Apps and Control Panel. "
             L"Untick only for hidden or embedded installers. "
             L"Inno emits CreateUninstallRegKey=yes.");
    AM(L"\r\n", false, RGB(0,0,0));
    reg_ctrl(L"man_reg_l2a", L"Display Name",
             L"man_reg_p2a",
             L"The name shown in the Apps list. Pre-filled from your project name; "
             L"change it here if you want a longer or branded display name that "
             L"differs from the App Name in Settings.");
    reg_ctrl(L"man_reg_l2b", L"Version",
             L"man_reg_p2b",
             L"The version string shown in the Apps list. Pre-filled from Settings. "
             L"Editing it here overrides the version only in the ARP registry entry, "
             L"not in the installer\u2019s own version metadata.");
    reg_ctrl(L"man_reg_l2c", L"Publisher",
             L"man_reg_p2c",
             L"The publisher name shown in the Apps list. Pre-filled from Settings. "
             L"Inno writes this as the Publisher value in the Uninstall key.");
    reg_ctrl(L"man_reg_l2d", L"App Icon (ARP)",
             L"man_reg_p2d",
             L"The .ico shown next to the entry in Settings \u2192 Apps. "
             L"Click Add Icon to browse for a .ico file. Leave blank to use "
             L"the installer\u2019s own icon (set on the Settings page).");
    reg_ctrl(L"man_reg_l2e", L"Show Regkey",
             L"man_reg_p2e",
             L"Opens a read-only preview of the exact [Registry] section that "
             L"SetupCraft will generate for this project. Useful for verifying "
             L"key paths and values before building.");
    AM(L"\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\r\n\r\n", false, RGB(100,140,180), 0, true);

    // ── Section 3: Custom registry entries ───────────────────────────────────
    RegisterShell32Icon(hEdit, pd, 166);
    AM(ML(pd, L"man_reg_h3",
          L"\U0001F5C4  Custom registry entries") + L"\r\n",
       true, RGB(0,70,140), 12);
    AM(ML(pd, L"man_reg_p3",
          L"The split pane shows the registry tree (left) and the values for the "
          L"selected key (right). Navigate the tree to find or create the key you "
          L"need, then add values in the right pane.") + L"\r\n\r\n",
       false, RGB(40,40,40));

    // ── Restore-point warning ─────────────────────────────────────────────────
    RegisterShell32Icon(hEdit, pd, 84);
    AM(L" " + ML(pd, L"man_reg_tip_restore_h",
          L"\u26A0  Create a Restore Point first!") + L"\r\n",
       true, RGB(160,80,0), 11);
    AM(ML(pd, L"man_reg_tip_restore_p",
          L"Before adding or editing any custom registry entries, click "
          L"Create Restore Point (see below). Windows will take a System "
          L"Restore snapshot of your registry. If anything goes wrong you "
          L"can roll back to this clean state in seconds \u2014 it takes only "
          L"a moment and could save you a lot of troubleshooting.") + L"\r\n\r\n",
       false, RGB(120,60,0));

    reg_ctrl(L"man_reg_l3a", L"Add Key",
             L"man_reg_p3a",
             L"Creates a new subkey under the currently selected tree node. Type the "
             L"key name in the dialog. Use backslash to create nested keys in one "
             L"step (e.g. Software\\MyApp\\Settings).");
    reg_ctrl(L"man_reg_l3b", L"Add Value",
             L"man_reg_p3b",
             L"Adds a named value to the selected key. Choose the type (REG_SZ, "
             L"REG_DWORD, REG_BINARY, REG_EXPAND_SZ, REG_MULTI_SZ) and enter the "
             L"name and data. Leave the name blank to set the default (unnamed) value.");
    reg_ctrl(L"man_reg_l3c", L"Edit / Delete",
             L"man_reg_p3c",
             L"Select a value in the right pane and click Edit to change its data, "
             L"or Delete to remove it. Double-click a value to edit it inline.");
    reg_ctrl(L"man_reg_l3d", L"Create Restore Point",
             L"man_reg_p3d",
             L"Asks Windows to create a System Restore snapshot before the installer "
             L"writes to the registry. Useful during development to quickly roll back "
             L"registry changes.");
    AM(L"\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\r\n\r\n", false, RGB(100,140,180), 0, true);

    // ── Next step ─────────────────────────────────────────────────────────────
    RegisterShell32Icon(hEdit, pd, 257);
    AM(L"\u25B6  ", true, RGB(0,120,0), 11);
    AM(ML(pd, L"man_reg_next",
          L"Next step: go to Shortcuts to create desktop, Start Menu, and "
          L"taskbar shortcuts for your application.") + L"\r\n",
       false, RGB(0,80,0));
}

// ─── Shortcuts page content ───────────────────────────────────────────────────

static void PopulateShortcutsManual(HWND hEdit, ManualWndData* pd)
{
    int logoH = pd->pLogo ? (int)(pd->pLogo->GetHeight() * 0.75) : 0;
    int lines  = (logoH + S(10)) / S(15);
    for (int i = 0; i < lines; i++)
        AM(L"\r\n", false, RGB(0,0,0));

    RegisterShell32Icon(hEdit, pd, 257);
    AM(ML(pd, L"man_sc_h1",
          L"\U0001F517  Shortcuts \u2014 What does it do?") + L"\r\n\r\n",
       true, RGB(0,70,140), 14, true);
    AM(ML(pd, L"man_sc_p1",
          L"The Shortcuts page controls every shortcut the installer creates: "
          L"desktop icons, Start Menu folder entries, and taskbar / Start screen "
          L"pins. Each shortcut is linked to an executable from your Files page "
          L"and can be configured with a custom name, icon, working directory, "
          L"arguments, hotkey, and elevation level.") + L"\r\n\r\n",
       false, RGB(40,40,40));
    AM(L"\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\r\n\r\n", false, RGB(100,140,180), 0, true);

    auto sc_ctrl = [&](const wchar_t* lblKey, const wchar_t* lblFb,
                       const wchar_t* txtKey, const wchar_t* txtFb) {
        AM(L"  ", false, RGB(0,0,0));
        AM(ML(pd, lblKey, lblFb) + L" \u2014 ", true, RGB(0,0,0));
        AM(ML(pd, txtKey, txtFb) + L"\r\n\r\n", false, RGB(60,60,60));
    };

    // ── Section 2: Shortcut locations ────────────────────────────────────────
    RegisterShell32Icon(hEdit, pd, 174);
    AM(ML(pd, L"man_sc_h2",
          L"\U0001F5A5  Shortcut locations") + L"\r\n",
       true, RGB(0,70,140), 12);
    sc_ctrl(L"man_sc_l2a", L"Desktop",
            L"man_sc_p2a",
            L"Adds a shortcut icon to the user\u2019s Desktop. Each .exe in your "
            L"Files page appears as a row button; click the button to add or remove "
            L"the shortcut. Tick \u201cAllow user to opt out\u201d to show an optional "
            L"checkbox on the installer\u2019s Finish page.");
    sc_ctrl(L"man_sc_l2b", L"Start Menu folder tree",
            L"man_sc_p2b",
            L"The tree represents the Programs folder structure the installer will "
            L"create under the Start Menu. Use Add Subfolder to nest folders, "
            L"Remove Subfolder to delete one, and Add Shortcut Here to attach a "
            L".exe shortcut to the selected folder node.");
    sc_ctrl(L"man_sc_l2c", L"Pin to Start / Pin to Taskbar",
            L"man_sc_p2c",
            L"Creates a pinned tile (Start) or pinned button (Taskbar) for the "
            L"selected executable. These use Windows shell APIs; behaviour varies "
            L"between Windows versions and may be blocked by Group Policy on "
            L"managed machines.");
    AM(L"\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\r\n\r\n", false, RGB(100,140,180), 0, true);

    // ── Section 3: Configuring a shortcut ────────────────────────────────────
    RegisterShell32Icon(hEdit, pd, 134);
    AM(ML(pd, L"man_sc_h3",
          L"\u270F  Configuring a shortcut") + L"\r\n",
       true, RGB(0,70,140), 12);
    AM(ML(pd, L"man_sc_p3",
          L"Right-click any shortcut button and choose \u201cConfigure shortcut\u2026\u201d "
          L"to open the shortcut editor:") + L"\r\n\r\n",
       false, RGB(40,40,40));
    sc_ctrl(L"man_sc_l3a", L"Name",
            L"man_sc_p3a",
            L"The display name of the shortcut (shown under the icon on the Desktop "
            L"or in the Start Menu). Defaults to the executable name without extension.");
    sc_ctrl(L"man_sc_l3b", L"Executable",
            L"man_sc_p3b",
            L"The target .exe file. Pre-filled from the .exe this shortcut was created "
            L"from; click \u2026 to change it.");
    sc_ctrl(L"man_sc_l3c", L"Working Directory",
            L"man_sc_p3c",
            L"The current directory when the shortcut launches. Defaults to the "
            L"executable\u2019s own folder.");
    sc_ctrl(L"man_sc_l3d", L"Arguments",
            L"man_sc_p3d",
            L"Command-line arguments passed to the executable. Leave blank for none.");
    sc_ctrl(L"man_sc_l3e", L"Comment",
            L"man_sc_p3e",
            L"Tooltip shown when hovering the shortcut icon. Maps to Inno\u2019s "
            L"Comment: field in the [Icons] section.");
    sc_ctrl(L"man_sc_l3f", L"Hotkey",
            L"man_sc_p3f",
            L"A global keyboard shortcut that launches the application directly "
            L"(e.g. Ctrl+Alt+H). Maps to Inno\u2019s HotKey: field. Click Clear "
            L"to remove it.");
    sc_ctrl(L"man_sc_l3g", L"Icon",
            L"man_sc_p3g",
            L"The icon displayed on the shortcut. Defaults to the first icon in the "
            L"target .exe; click Change Icon\u2026 to pick a different .ico/.exe/.dll "
            L"and icon index.");
    sc_ctrl(L"man_sc_l3h", L"Run as administrator",
            L"man_sc_p3h",
            L"When ticked, the shortcut always requests UAC elevation. Use only when "
            L"the application requires elevation to function correctly.");
    AM(L"\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\r\n\r\n", false, RGB(100,140,180), 0, true);

    // ── Next step ─────────────────────────────────────────────────────────────
    RegisterShell32Icon(hEdit, pd, 152);
    AM(L"\u25B6  ", true, RGB(0,120,0), 11);
    AM(ML(pd, L"man_sc_next",
          L"Next step: go to File Types to register file extensions that your "
          L"application can open.") + L"\r\n",
       false, RGB(0,80,0));
}

// ─── File Types page content ──────────────────────────────────────────────────

static void PopulateFileTypesManual(HWND hEdit, ManualWndData* pd)
{
    int logoH = pd->pLogo ? (int)(pd->pLogo->GetHeight() * 0.75) : 0;
    int lines  = (logoH + S(10)) / S(15);
    for (int i = 0; i < lines; i++)
        AM(L"\r\n", false, RGB(0,0,0));

    RegisterShell32Icon(hEdit, pd, 152);
    AM(ML(pd, L"man_fa_h1",
          L"\U0001F4C4  File Types \u2014 What does it do?") + L"\r\n\r\n",
       true, RGB(0,70,140), 14, true);
    AM(ML(pd, L"man_fa_p1",
          L"The File Types page lets your installer register file extension "
          L"associations in Windows. Each row is one extension (e.g. .mydata) with "
          L"its description, icon, and the command used to open it. The installer "
          L"writes the necessary registry entries so Windows Explorer launches your "
          L"application when the user double-clicks a matching file.") + L"\r\n\r\n",
       false, RGB(40,40,40));
    AM(L"\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\r\n\r\n", false, RGB(100,140,180), 0, true);

    auto fa_ctrl = [&](const wchar_t* lblKey, const wchar_t* lblFb,
                       const wchar_t* txtKey, const wchar_t* txtFb) {
        AM(L"  ", false, RGB(0,0,0));
        AM(ML(pd, lblKey, lblFb) + L" \u2014 ", true, RGB(0,0,0));
        AM(ML(pd, txtKey, txtFb) + L"\r\n\r\n", false, RGB(60,60,60));
    };

    // ── Section 2: Managing associations ─────────────────────────────────────
    RegisterShell32Icon(hEdit, pd, 221);
    AM(ML(pd, L"man_fa_h2",
          L"\U0001F4CB  Managing associations") + L"\r\n",
       true, RGB(0,70,140), 12);
    AM(ML(pd, L"man_fa_p2",
          L"The list shows all registered file types for this project. Tick or "
          L"untick the Enabled column to include or exclude individual associations "
          L"without deleting them. Use Add to create a new one, Edit to change an "
          L"existing one, or Remove to delete it.") + L"\r\n\r\n",
       false, RGB(40,40,40));
    AM(L"\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\r\n\r\n", false, RGB(100,140,180), 0, true);

    // ── Section 3: File association fields ───────────────────────────────────
    RegisterShell32Icon(hEdit, pd, 134);
    AM(ML(pd, L"man_fa_h3",
          L"\u270F  File association fields") + L"\r\n",
       true, RGB(0,70,140), 12);
    fa_ctrl(L"man_fa_l3a", L"Extension",
            L"man_fa_p3a",
            L"The file extension to register, including the leading dot (e.g. .mydata). "
            L"Windows routes double-clicks on files with this extension to your "
            L"application\u2019s Open command.");
    fa_ctrl(L"man_fa_l3b", L"Description",
            L"man_fa_p3b",
            L"Human-readable label shown in Windows Explorer for this file type "
            L"(e.g. \u201cMyApp Document\u201d). Shown in the Type column of "
            L"Explorer\u2019s detail view.");
    fa_ctrl(L"man_fa_l3c", L"ProgID",
            L"man_fa_p3c",
            L"A unique registry identifier for this file type "
            L"(e.g. \u201cMyApp.mydata\u201d). Leave blank to auto-generate as "
            L"AppName.extension. Must contain no spaces.");
    fa_ctrl(L"man_fa_l3d", L"Icon path",
            L"man_fa_p3d",
            L"Path to the .exe, .dll, or .ico containing the icon for this file "
            L"type. Use Inno constants such as {app}\\MyApp.exe. Leave blank for "
            L"the default generic document icon.");
    fa_ctrl(L"man_fa_l3e", L"Icon index",
            L"man_fa_p3e",
            L"Zero-based index of the icon inside the icon file. Use 0 for standalone "
            L".ico files, or the specific index for icons within a .dll or .exe.");
    fa_ctrl(L"man_fa_l3f", L"Open command",
            L"man_fa_p3f",
            L"The command run when the user double-clicks a file. Use %1 as a "
            L"placeholder for the file path: \u201c\\\"{app}\\MyApp.exe\\\" "
            L"\\\"%1\\\"\u201d. This is the minimum required field.");
    fa_ctrl(L"man_fa_l3g", L"Edit / Print commands",
            L"man_fa_p3g",
            L"Optional commands added to the right-click menu as Edit and Print "
            L"verbs. Leave blank to omit. Use %1 as the file path placeholder.");
    fa_ctrl(L"man_fa_l3h", L"MIME type",
            L"man_fa_p3h",
            L"The MIME content type (e.g. \u201capplication/x-mydata\u201d). "
            L"Used by browsers and servers. Leave blank if not needed.");
    AM(L"\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\r\n\r\n", false, RGB(100,140,180), 0, true);

    // ── Next step ─────────────────────────────────────────────────────────────
    RegisterShell32Icon(hEdit, pd, 310);
    AM(L"\u25B6  ", true, RGB(0,120,0), 11);
    AM(ML(pd, L"man_fa_next",
          L"Next step: go to Scripts to add custom pre-install or post-install "
          L"scripts to your installer.") + L"\r\n",
       false, RGB(0,80,0));
}

// ─── Scripts page content ─────────────────────────────────────────────────────

static void PopulateScriptsManual(HWND hEdit, ManualWndData* pd)
{
    int logoH = pd->pLogo ? (int)(pd->pLogo->GetHeight() * 0.75) : 0;
    int lines  = (logoH + S(10)) / S(15);
    for (int i = 0; i < lines; i++)
        AM(L"\r\n", false, RGB(0,0,0));

    RegisterShell32Icon(hEdit, pd, 310);
    AM(ML(pd, L"man_scr_h1",
          L"\U0001F4DC  Scripts \u2014 What does it do?") + L"\r\n\r\n",
       true, RGB(0,70,140), 14, true);
    AM(ML(pd, L"man_scr_p1",
          L"The Scripts page lets you attach .bat/.cmd or PowerShell .ps1 scripts "
          L"to your installer. Each script is tied to a specific moment in the "
          L"install (or uninstall) process. Scripts are embedded in the installer "
          L"and extracted to a temporary folder at runtime; SetupCraft generates "
          L"the appropriate Inno Setup [Run] or [UninstallRun] entries "
          L"automatically.") + L"\r\n\r\n",
       false, RGB(40,40,40));
    AM(L"\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\r\n\r\n", false, RGB(100,140,180), 0, true);

    auto scr_ctrl = [&](const wchar_t* lblKey, const wchar_t* lblFb,
                        const wchar_t* txtKey, const wchar_t* txtFb) {
        AM(L"  ", false, RGB(0,0,0));
        AM(ML(pd, lblKey, lblFb) + L" \u2014 ", true, RGB(0,0,0));
        AM(ML(pd, txtKey, txtFb) + L"\r\n\r\n", false, RGB(60,60,60));
    };

    // ── Section 2: When to run ────────────────────────────────────────────────
    RegisterShell32Icon(hEdit, pd, 138);
    AM(ML(pd, L"man_scr_h2",
          L"\u23F0  When to run") + L"\r\n",
       true, RGB(0,70,140), 12);
    scr_ctrl(L"man_scr_l2a", L"Before Files",
             L"man_scr_p2a",
             L"Runs before any files are copied. Useful for stopping services, "
             L"killing processes, or checking prerequisites. Implemented via "
             L"Inno\u2019s [Code] CurStepChanged(ssInstall).");
    scr_ctrl(L"man_scr_l2b", L"After Files",
             L"man_scr_p2b",
             L"Runs unconditionally after all files are copied. Suitable for "
             L"post-install configuration, license activation, or database migration. "
             L"Inno emits an unconditional [Run] entry.");
    scr_ctrl(L"man_scr_l2c", L"On Finish (optional)",
             L"man_scr_p2c",
             L"Runs when the user reaches the Finish page and an optional checkbox "
             L"is ticked. Suitable for \u201cLaunch MyApp now\u201d or "
             L"\u201cView release notes\u201d actions. Inno emits [Run] with "
             L"Flags: postinstall skipifsilent.");
    scr_ctrl(L"man_scr_l2d", L"On Uninstall",
             L"man_scr_p2d",
             L"Runs when the uninstaller executes. Suitable for stopping services, "
             L"removing user data, or cleaning up configuration. "
             L"Inno emits a [UninstallRun] entry.");
    AM(L"\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\r\n\r\n", false, RGB(100,140,180), 0, true);

    // ── Section 3: Script types ───────────────────────────────────────────────
    RegisterShell32Icon(hEdit, pd, 221);
    AM(ML(pd, L"man_scr_h3",
          L"\U0001F4C3  Script types") + L"\r\n",
       true, RGB(0,70,140), 12);
    scr_ctrl(L"man_scr_l3a", L".bat / .cmd",
             L"man_scr_p3a",
             L"Classic Windows batch files. Run by cmd.exe with no execution policy "
             L"restrictions. Best for simple file operations, registry tweaks via "
             L"reg.exe, or calling other executables.");
    scr_ctrl(L"man_scr_l3b", L"PowerShell (.ps1)",
             L"man_scr_p3b",
             L"PowerShell scripts. SetupCraft generates a [Run] entry that calls "
             L"powershell.exe -ExecutionPolicy Bypass -File \u201c\u2026\u201d. "
             L"More powerful for complex logic, .NET interop, or WMI queries. "
             L"Available on all supported Windows versions (7+).");
    AM(L"\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\r\n\r\n", false, RGB(100,140,180), 0, true);

    // ── Section 4: Managing scripts ───────────────────────────────────────────
    RegisterShell32Icon(hEdit, pd, 134);
    AM(ML(pd, L"man_scr_h4",
          L"\u270F  Managing scripts") + L"\r\n",
       true, RGB(0,70,140), 12);
    AM(ML(pd, L"man_scr_p4",
          L"Use the toolbar buttons at the top of the page: \u201c+ Add Script\u201d "
          L"opens the script editor to write or paste content directly; "
          L"\u201cLoad from file\u2026\u201d imports an existing .bat, .cmd, or "
          L".ps1 file from disk. Select a tile and click Edit to modify it, or "
          L"Delete to remove it. The master enable checkbox activates or disables "
          L"all scripts at once without deleting them.") + L"\r\n\r\n",
       false, RGB(40,40,40));
    AM(L"\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\r\n\r\n", false, RGB(100,140,180), 0, true);

    // ── Test option ───────────────────────────────────────────────────────────
    RegisterShell32Icon(hEdit, pd, 24);
    AM(L" " + ML(pd, L"man_scr_tip_test",
          L"Tip: before building, you can press Test (F5) to do a dry run \u2014 "
          L"SetupCraft will compile and launch the installer in test mode so you "
          L"can verify the install flow without producing a final .exe.") + L"\r\n\r\n",
       false, RGB(80,80,80));

    // ── Next step ─────────────────────────────────────────────────────────────
    RegisterShell32Icon(hEdit, pd, 80);
    AM(L"\u25B6  ", true, RGB(0,120,0), 11);
    AM(ML(pd, L"man_scr_next",
          L"Next step: press Build (F7) to compile the Inno Setup script and "
          L"create the installer .exe in your output folder.") + L"\r\n",
       false, RGB(0,80,0));
}

#undef AM

// ─── Window procedure ─────────────────────────────────────────────────────────

static LRESULT CALLBACK ManualWndProc(HWND hwnd, UINT msg,
                                      WPARAM wParam, LPARAM lParam)
{
    ManualWndData* pd = (ManualWndData*)GetWindowLongPtrW(hwnd, GWLP_USERDATA);

    switch (msg) {

    case WM_SIZE: {
        if (!pd) break;
        RECT rc; GetClientRect(hwnd, &rc);
        int cW    = rc.right;
        int cH    = rc.bottom;
        int editH = cH - 3 * pd->PAD - pd->BTN_H;
        if (editH < pd->BTN_H) editH = pd->BTN_H;
        // Resize RichEdit — EM_SETTARGETDEVICE(NULL,0) already set; text
        // reflows to the new width automatically.
        SetWindowPos(pd->hEdit, NULL,
                     pd->PAD, pd->PAD,
                     cW - 2 * pd->PAD, editH,
                     SWP_NOZORDER | SWP_NOACTIVATE);
        // Recentre Close button.
        int btnX = (cW - pd->closeW) / 2;
        SetWindowPos(pd->hCloseBtn, NULL,
                     btnX, cH - pd->PAD - pd->BTN_H,
                     pd->closeW, pd->BTN_H,
                     SWP_NOZORDER | SWP_NOACTIVATE);
        // Let the custom scrollbar recalculate its range after the reflow.
        if (pd->hScrollbar) msb_notify_content_changed(pd->hScrollbar);
        return 0;
    }

    case WM_GETMINMAXINFO: {
        // Maximise into the work area (respects the taskbar).
        MINMAXINFO* mmi = (MINMAXINFO*)lParam;
        MONITORINFO mi  = {}; mi.cbSize = sizeof(mi);
        HMONITOR hMon   = MonitorFromWindow(hwnd, MONITOR_DEFAULTTOPRIMARY);
        if (GetMonitorInfoW(hMon, &mi)) {
            mmi->ptMaxPosition.x = mi.rcWork.left;
            mmi->ptMaxPosition.y = mi.rcWork.top;
            mmi->ptMaxSize.x     = mi.rcWork.right  - mi.rcWork.left;
            mmi->ptMaxSize.y     = mi.rcWork.bottom - mi.rcWork.top;
        }
        mmi->ptMinTrackSize.x = S(420);
        mmi->ptMinTrackSize.y = S(320);
        return 0;
    }

    case WM_COMMAND:
        if (LOWORD(wParam) == IDOK || LOWORD(wParam) == IDCANCEL) {
            DestroyWindow(hwnd);
            return 0;
        }
        break;

    case WM_DRAWITEM: {
        LPDRAWITEMSTRUCT dis = (LPDRAWITEMSTRUCT)lParam;
        if (dis->CtlID == IDOK) {
            ButtonColor color = (ButtonColor)GetWindowLongPtr(dis->hwndItem, GWLP_USERDATA);
            NONCLIENTMETRICSW ncm = {}; ncm.cbSize = sizeof(ncm);
            SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(ncm), &ncm, 0);
            if (ncm.lfMessageFont.lfHeight < 0)
                ncm.lfMessageFont.lfHeight = (LONG)(ncm.lfMessageFont.lfHeight * 1.2f);
            ncm.lfMessageFont.lfWeight  = FW_BOLD;
            ncm.lfMessageFont.lfQuality = CLEARTYPE_QUALITY;
            HFONT hFont = CreateFontIndirectW(&ncm.lfMessageFont);
            LRESULT r   = DrawCustomButton(dis, color, hFont);
            if (hFont) DeleteObject(hFont);
            return r;
        }
        break;
    }

    case WM_DESTROY: {
        if (pd) {
            if (pd->hScrollbar) { msb_detach(pd->hScrollbar); pd->hScrollbar = NULL; }
            if (pd->pLogo)      { delete pd->pLogo; pd->pLogo = nullptr; }
            // Release color icons that were painted over the emoji glyphs.
            for (auto& entry : pd->sectionIcons)
                if (entry.hIcon) { DestroyIcon(entry.hIcon); entry.hIcon = NULL; }
            pd->sectionIcons.clear();
            delete pd;
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
        }
        PostQuitMessage(0);
        return 0;
    }

    case WM_CLOSE:
        DestroyWindow(hwnd);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

// ─── Public entry point ───────────────────────────────────────────────────────

void ShowPageManual(HWND parent, int pageIndex,
                    const std::map<std::wstring,std::wstring>& locale)
{
    // Load the richest available RichEdit version. Msftedit.dll (v4.1) has
    // full Unicode font-linking and renders emoji correctly via RICHEDIT50W.
    // Riched20.dll (v2.0) is a fallback on very old systems.
    static HMODULE s_hRe = nullptr;
    if (!s_hRe) {
        s_hRe = LoadLibraryW(L"Msftedit.dll");
        if (!s_hRe) s_hRe = LoadLibraryW(L"Riched20.dll");
    }

    // ── Load SC logo ────────────────────────────────────────────────────────
    wchar_t exePath[MAX_PATH];
    GetModuleFileNameW(NULL, exePath, MAX_PATH);
    Image* pLogo = nullptr;
    wchar_t* lastSlash = wcsrchr(exePath, L'\\');
    if (lastSlash) {
        *(lastSlash + 1) = 0;
        wchar_t logoPath[MAX_PATH];
        wcscpy_s(logoPath, exePath);
        wcscat_s(logoPath, L"SetupCraft.png");
        pLogo = Image::FromFile(logoPath);
        if (pLogo && pLogo->GetLastStatus() != Ok) {
            delete pLogo;
            pLogo = nullptr;
        }
    }

    // ── Window size — same as About ─────────────────────────────────────────
    const int W = S(650), H = S(560);
    RECT pr = {0,0,0,0};
    if (parent && IsWindow(parent)) GetWindowRect(parent, &pr);
    int px = (pr.right + pr.left) / 2;
    int py = (pr.bottom + pr.top) / 2;
    int x  = px ? px - W / 2 : CW_USEDEFAULT;
    int y  = py ? py - H / 2 : CW_USEDEFAULT;
    if (y != CW_USEDEFAULT && y < 30) y = 30;

    HINSTANCE hi = GetModuleHandleW(NULL);

    // ── Window class (registered once) ──────────────────────────────────────
    const wchar_t* kClassName = L"SetupCraftManualClass";
    WNDCLASSW existing = {};
    if (!GetClassInfoW(hi, kClassName, &existing)) {
        WNDCLASSW wc = {};
        wc.lpfnWndProc   = ManualWndProc;
        wc.hInstance     = hi;
        wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
        wc.hCursor       = LoadCursorW(NULL, IDC_ARROW);
        wc.lpszClassName = kClassName;
        RegisterClassW(&wc);
    }

    // ── Window title from locale ─────────────────────────────────────────────
    const wchar_t* titleKey =
        (pageIndex == 0)  ? L"man_files_window_title" :
        (pageIndex == 1)  ? L"man_reg_window_title"   :
        (pageIndex == 2)  ? L"man_sc_window_title"    :
        (pageIndex == 3)  ? L"man_deps_window_title"  :
        (pageIndex == 4)  ? L"man_dlg_window_title"   :
        (pageIndex == 5)  ? L"man_sett_window_title"  :
        (pageIndex == 8)  ? L"man_scr_window_title"   :
        (pageIndex == 9)  ? L"man_comp_window_title"  :
        (pageIndex == 10) ? L"man_fa_window_title"    : nullptr;
    auto itTitle = titleKey ? locale.find(titleKey) : locale.end();
    std::wstring title = (itTitle != locale.end()) ? itTitle->second : L"Page Manual";

    // ── Create the window ────────────────────────────────────────────────────
    // WS_THICKFRAME = resizable border; WS_MAXIMIZEBOX = the native full-screen
    // (maximise) button that lives in the upper-right corner of the title bar.
    HWND dlg = CreateWindowExW(
        WS_EX_WINDOWEDGE,
        kClassName, title.c_str(),
        WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_THICKFRAME | WS_MAXIMIZEBOX | WS_VISIBLE,
        x, y, W, H, parent, NULL, hi, NULL);
    if (!dlg) { if (pLogo) delete pLogo; return; }

    // ── App icon ─────────────────────────────────────────────────────────────
    HICON hIcon = LoadIconW(hi, MAKEINTRESOURCEW(1));
    if (!hIcon) hIcon = LoadIconW(NULL, IDI_APPLICATION);
    if (hIcon) {
        SendMessageW(dlg, WM_SETICON, ICON_SMALL, (LPARAM)hIcon);
        SendMessageW(dlg, WM_SETICON, ICON_BIG,   (LPARAM)hIcon);
    }

    // ── Layout constants ─────────────────────────────────────────────────────
    RECT rcC; GetClientRect(dlg, &rcC);
    const int cW    = rcC.right;
    const int cH    = rcC.bottom;
    const int PAD   = S(10);
    const int BTN_H = S(34);

    // ── Close button ─────────────────────────────────────────────────────────
    auto itClose = locale.find(L"man_close");
    std::wstring closeTxt = (itClose != locale.end()) ? itClose->second : L"Close";
    int closeW = MeasureButtonWidth(closeTxt, true);
    int btnX   = (cW - closeW) / 2;
    int btnY   = cH - PAD - BTN_H;
    HWND hCloseBtn = CreateCustomButtonWithIcon(
        dlg, IDOK, closeTxt, ButtonColor::Red,
        L"shell32.dll", 131,
        btnX, btnY, closeW, BTN_H, hi);

    // ── RichEdit ─────────────────────────────────────────────────────────────
    int editH = cH - 3 * PAD - BTN_H;
    // Use RICHEDIT50W if Msftedit.dll is loaded, else RichEdit20W.
    WNDCLASSEXW wce = {}; wce.cbSize = sizeof(wce);
    const wchar_t* reClass =
        (s_hRe && GetClassInfoExW(s_hRe, L"RICHEDIT50W", &wce))
        ? L"RICHEDIT50W" : L"RichEdit20W";
    HWND hEdit = CreateWindowExW(
        0, reClass, NULL,
        WS_CHILD | WS_VISIBLE | ES_MULTILINE | ES_READONLY |
        ES_AUTOVSCROLL | WS_VSCROLL,
        PAD, PAD, cW - 2 * PAD, editH,
        dlg, (HMENU)200, hi, NULL);
    if (!hEdit) { DestroyWindow(dlg); if (pLogo) delete pLogo; return; }

    // Word-wrap to control width — this is the key setting that prevents
    // horizontal scrolling and makes text reflow when the window is resized.
    SendMessageW(hEdit, EM_SETTARGETDEVICE, 0, 0);
    SendMessageW(hEdit, EM_SETEVENTMASK,    0, ENM_NONE);

    // ── Window data ──────────────────────────────────────────────────────────
    ManualWndData* pd = new ManualWndData{};
    pd->pageIndex     = pageIndex;
    pd->pLocale       = &locale;
    pd->pLogo         = pLogo;
    pd->hScrollbar    = NULL;
    pd->origEditProc  = NULL;
    pd->hEdit         = hEdit;
    pd->hCloseBtn     = hCloseBtn;
    pd->closeW        = closeW;
    pd->PAD           = PAD;
    pd->BTN_H         = BTN_H;
    pd->richFontDirty = true;
    SetWindowLongPtrW(dlg, GWLP_USERDATA, (LONG_PTR)pd);

    // ── Subclass RichEdit for logo painting ──────────────────────────────────
    SetPropW(hEdit, L"ManualData", (HANDLE)pd);
    pd->origEditProc = (WNDPROC)SetWindowLongPtrW(
        hEdit, GWLP_WNDPROC, (LONG_PTR)ManualEditSubclassProc);

    // ── Custom vertical scrollbar ─────────────────────────────────────────────
    pd->hScrollbar = msb_attach(hEdit, MSB_VERTICAL);

    // ── Populate content ──────────────────────────────────────────────────────
    if (pageIndex == 0)
        PopulateFilesManual(hEdit, pd);
    else if (pageIndex == 1)
        PopulateRegistryManual(hEdit, pd);
    else if (pageIndex == 2)
        PopulateShortcutsManual(hEdit, pd);
    else if (pageIndex == 3)
        PopulateDepsManual(hEdit, pd);
    else if (pageIndex == 4)
        PopulateDialogsManual(hEdit, pd);
    else if (pageIndex == 5)
        PopulateSettingsManual(hEdit, pd);
    else if (pageIndex == 8)
        PopulateScriptsManual(hEdit, pd);
    else if (pageIndex == 9)
        PopulateComponentsManual(hEdit, pd);
    else if (pageIndex == 10)
        PopulateFileTypesManual(hEdit, pd);

    // Scroll to top.
    SendMessageW(hEdit, EM_SETSEL, 0, 0);
    SendMessageW(hEdit, EM_SCROLLCARET, 0, 0);
    if (pd->hScrollbar) msb_notify_content_changed(pd->hScrollbar);

    // ── Modal loop ────────────────────────────────────────────────────────────
    if (parent && IsWindow(parent)) EnableWindow(parent, FALSE);
    SetFocus(hCloseBtn);

    MSG msg2;
    while (GetMessageW(&msg2, NULL, 0, 0)) {
        if (!IsDialogMessageW(dlg, &msg2)) {
            TranslateMessage(&msg2);
            DispatchMessageW(&msg2);
        }
    }
    // dlg has already been destroyed by WM_CLOSE → DestroyWindow → WM_DESTROY.

    if (parent && IsWindow(parent)) {
        EnableWindow(parent, TRUE);
        SetForegroundWindow(parent);
        BringWindowToTop(parent);
    }
}
