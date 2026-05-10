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

    AM(L"━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\r\n\r\n", false, RGB(100,140,180));

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

    AM(L"━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\r\n\r\n", false, RGB(100,140,180));

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
    AM(L"━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\r\n\r\n", false, RGB(100,140,180));

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

    AM(L"━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\r\n\r\n", false, RGB(100,140,180));

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

    AM(L"━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\r\n\r\n", false, RGB(100,140,180));

    // ── Section 6: Ask at install ────────────────────────────────────────────    // shell32 #23 = question mark / help icon.
    RegisterShell32Icon(hEdit, pd, 23);    AM(ML(pd, L"man_files_h6", L"\u2753  Ask at install") + L"\r\n",
       true, RGB(0,70,140), 12);
    AM(ML(pd, L"man_files_p6",
          L"When this checkbox is ticked the installer shows a dialog during setup "
          L"that lets the end user choose whether to install for themselves only or "
          L"for all users on the machine. An AskAtInstall root appears in the folder "
          L"tree \u2014 files placed there are handled accordingly by Inno Setup.") + L"\r\n\r\n",
       false, RGB(40,40,40));

    AM(L"━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\r\n\r\n", false, RGB(100,140,180));

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

    AM(L"━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\r\n\r\n", false, RGB(100,140,180));

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

    AM(L"\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\r\n\r\n", false, RGB(100,140,180));

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

    AM(L"\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\r\n\r\n", false, RGB(100,140,180));

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

    AM(L"\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\r\n\r\n", false, RGB(100,140,180));

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

    AM(L"\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\r\n\r\n", false, RGB(100,140,180));

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

    AM(L"\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\r\n\r\n", false, RGB(100,140,180));

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

    AM(L"\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\r\n\r\n", false, RGB(100,140,180));

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

    AM(L"\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\r\n\r\n", false, RGB(100,140,180));

    // ── Warning: save before assigning dependencies ──────────────────────────
    RegisterSystemIcon(hEdit, pd, IDI_WARNING);
    AM(L"\u26A0  ", true, RGB(160,80,0), 11);
    AM(ML(pd, L"man_comp_warn",
          L"Save the project before assigning dependencies. Components receive a "
          L"permanent database ID only after the first Save. The dependency picker "
          L"will not list components that have never been saved. After saving, "
          L"press Choose again in the edit dialog to assign dependencies.") + L"\r\n\r\n",
       false, RGB(120,50,0));

    AM(L"\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\r\n\r\n", false, RGB(100,140,180));

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
        (pageIndex == 0) ? L"man_files_window_title" :
        (pageIndex == 9) ? L"man_comp_window_title"  : nullptr;
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
    else if (pageIndex == 9)
        PopulateComponentsManual(hEdit, pd);
    // (other pages: add else-if branches here as they are written)

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
