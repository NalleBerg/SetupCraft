/*
 * scripts.cpp — Scripts page implementation for SetupCraft (page index 8).
 *
 * Two script slots: pre-install (slot 0) and post-install (slot 1).
 * Each slot has: enabled flag, type (bat/ps1), free-text content.
 * A master checkbox gates the whole feature.
 *
 * Layout (top → bottom):
 *   Page title
 *   Master enable checkbox  +  hint label
 *   [separator]
 *   Section label "Before installation"
 *     Per-slot enable checkbox
 *     bat / ps1 radio pair
 *     "Script content:" label
 *     Multiline edit
 *     [Load from file…]  [Test in terminal]  row
 *   [separator]
 *   Section label "After installation"
 *     (same structure)
 *
 * Persistence: settings table, project-scoped keys.
 * Test button writes a temp file and opens it in cmd.exe or powershell.exe.
 */

#include "scripts.h"
#include "mainwindow.h"   // MarkAsModified()
#include "db.h"
#include "dpi.h"          // S()
#include "button.h"       // CreateCustomButtonWithIcon(), MeasureButtonWidth()
#include "checkbox.h"     // CreateCustomCheckbox()
#include <shellapi.h>     // ShellExecuteW()
#include <commdlg.h>      // GetOpenFileNameW()
#include <fstream>
#include <sstream>

// ── Module-private state ──────────────────────────────────────────────────────

// Master enable
static bool s_scrEnabled = false;

// Per-slot state
struct ScrSlot {
    bool         enabled = false;
    int          type    = SCR_TYPE_PS1;  // default PowerShell
    std::wstring content;
};
static ScrSlot s_slots[SCR_SLOT_COUNT];

// Scroll offset for the page
static int s_scrScrollOffset = 0;

// Saved font / locale pointers (valid between BuildPage and TearDown)
static HFONT       s_hGuiFont   = NULL;
static HINSTANCE   s_hInst      = NULL;
static const std::map<std::wstring, std::wstring>* s_pLocale = NULL;

// ── Locale helper ─────────────────────────────────────────────────────────────
static std::wstring loc(const wchar_t* key, const wchar_t* fallback)
{
    if (!s_pLocale) return fallback;
    auto it = s_pLocale->find(key);
    return (it != s_pLocale->end()) ? it->second : fallback;
}

// ── Enable/disable helper ─────────────────────────────────────────────────────
// Enable or disable all controls for one slot.
static void ApplySlotEnable(HWND hwnd, int slot)
{
    bool masterOn = s_scrEnabled;
    bool slotOn   = masterOn && s_slots[slot].enabled;

    int baseEnable = (slot == SCR_SLOT_PRE) ? IDC_SCR_PRE_ENABLE : IDC_SCR_POST_ENABLE;
    int baseBat    = (slot == SCR_SLOT_PRE) ? IDC_SCR_PRE_BAT    : IDC_SCR_POST_BAT;
    int basePs1    = (slot == SCR_SLOT_PRE) ? IDC_SCR_PRE_PS1    : IDC_SCR_POST_PS1;
    int baseLabel  = (slot == SCR_SLOT_PRE) ? IDC_SCR_PRE_EDIT_LABEL : IDC_SCR_POST_EDIT_LABEL;
    int baseEdit   = (slot == SCR_SLOT_PRE) ? IDC_SCR_PRE_EDIT   : IDC_SCR_POST_EDIT;
    int baseLoad   = (slot == SCR_SLOT_PRE) ? IDC_SCR_PRE_LOAD   : IDC_SCR_POST_LOAD;
    int baseTest   = (slot == SCR_SLOT_PRE) ? IDC_SCR_PRE_TEST   : IDC_SCR_POST_TEST;

    HWND hSlotEn = GetDlgItem(hwnd, baseEnable);
    if (hSlotEn) EnableWindow(hSlotEn, masterOn ? TRUE : FALSE);

    HWND hBat   = GetDlgItem(hwnd, baseBat);
    HWND hPs1   = GetDlgItem(hwnd, basePs1);
    HWND hLabel = GetDlgItem(hwnd, baseLabel);
    HWND hEdit  = GetDlgItem(hwnd, baseEdit);
    HWND hLoad  = GetDlgItem(hwnd, baseLoad);
    HWND hTest  = GetDlgItem(hwnd, baseTest);

    BOOL en = slotOn ? TRUE : FALSE;
    if (hBat)   EnableWindow(hBat,   en);
    if (hPs1)   EnableWindow(hPs1,   en);
    if (hLabel) EnableWindow(hLabel, en);
    if (hEdit)  EnableWindow(hEdit,  en);
    if (hLoad)  EnableWindow(hLoad,  en);
    if (hTest)  EnableWindow(hTest,  en);
}

static void ApplyMasterEnable(HWND hwnd)
{
    for (int s = 0; s < SCR_SLOT_COUNT; s++)
        ApplySlotEnable(hwnd, s);

    // Hint label: show when master is off
    HWND hHint = GetDlgItem(hwnd, IDC_SCR_ENABLE_HINT);
    if (hHint) ShowWindow(hHint, s_scrEnabled ? SW_HIDE : SW_SHOW);
}

// ── SCR_Reset ─────────────────────────────────────────────────────────────────
void SCR_Reset()
{
    s_scrEnabled = false;
    for (int s = 0; s < SCR_SLOT_COUNT; s++) {
        s_slots[s].enabled = false;
        s_slots[s].type    = SCR_TYPE_PS1;
        s_slots[s].content.clear();
    }
    s_scrScrollOffset = 0;
    s_hGuiFont  = NULL;
    s_hInst     = NULL;
    s_pLocale   = NULL;
}

// ── SCR_BuildPage ─────────────────────────────────────────────────────────────
int SCR_BuildPage(HWND hwnd, HINSTANCE hInst,
                  int pageY, int clientWidth,
                  HFONT hPageTitleFont, HFONT hGuiFont,
                  const std::map<std::wstring, std::wstring>& locale)
{
    s_hInst   = hInst;
    s_hGuiFont = hGuiFont;
    s_pLocale  = &locale;

    const int padH  = S(20);
    const int gap   = S(10);
    const int titleH = S(38);
    const int cbH   = S(22);

    int y = pageY + S(20);

    // ── Page title ────────────────────────────────────────────────────────────
    HWND hTitle = CreateWindowExW(0, L"STATIC",
        loc(L"scr_page_title", L"Run Scripts").c_str(),
        WS_CHILD | WS_VISIBLE | SS_LEFT,
        padH, y, clientWidth - padH * 2, titleH,
        hwnd, (HMENU)(UINT_PTR)IDC_SCR_PAGE_TITLE, hInst, NULL);
    if (hPageTitleFont) SendMessageW(hTitle, WM_SETFONT, (WPARAM)hPageTitleFont, TRUE);
    y += titleH + gap;

    // ── Master enable checkbox ────────────────────────────────────────────────
    CreateCustomCheckbox(hwnd, IDC_SCR_MASTER_ENABLE,
        loc(L"scr_enable", L"Enable scripts for this project"),
        s_scrEnabled, padH, y, clientWidth - padH * 2, cbH, hInst);
    y += cbH + S(4);

    // Hint label (visible when master is off)
    HWND hHint = CreateWindowExW(0, L"STATIC",
        loc(L"scr_enable_hint", L"When unchecked, no scripts will be packaged or run by the installer.").c_str(),
        WS_CHILD | SS_LEFT,
        padH + S(24), y, clientWidth - padH * 2 - S(24), cbH,
        hwnd, (HMENU)(UINT_PTR)IDC_SCR_ENABLE_HINT, hInst, NULL);
    if (hGuiFont) SendMessageW(hHint, WM_SETFONT, (WPARAM)hGuiFont, TRUE);
    ShowWindow(hHint, s_scrEnabled ? SW_HIDE : SW_SHOW);
    y += cbH + gap;

    // ── Separator ─────────────────────────────────────────────────────────────
    CreateWindowExW(0, L"STATIC", L"",
        WS_CHILD | WS_VISIBLE | SS_ETCHEDHORZ,
        padH, y, clientWidth - padH * 2, S(2),
        hwnd, NULL, hInst, NULL);
    y += S(2) + gap;

    // ── Build one section for each slot ──────────────────────────────────────
    const wchar_t* kSectionKeys[SCR_SLOT_COUNT]    = { L"scr_pre_section",         L"scr_post_section" };
    const wchar_t* kSectionFalls[SCR_SLOT_COUNT]   = { L"Before installation",     L"After installation" };
    const wchar_t* kEnableKeys[SCR_SLOT_COUNT]     = { L"scr_script_enable_pre",   L"scr_script_enable_post" };
    const wchar_t* kEnableFalls[SCR_SLOT_COUNT]    = { L"Run a script before installation begins",
                                                        L"Run a script after installation completes" };
    const int kEnableIds[SCR_SLOT_COUNT]   = { IDC_SCR_PRE_ENABLE,  IDC_SCR_POST_ENABLE };
    const int kBatIds[SCR_SLOT_COUNT]      = { IDC_SCR_PRE_BAT,     IDC_SCR_POST_BAT    };
    const int kPs1Ids[SCR_SLOT_COUNT]      = { IDC_SCR_PRE_PS1,     IDC_SCR_POST_PS1    };
    const int kLabelIds[SCR_SLOT_COUNT]    = { IDC_SCR_PRE_EDIT_LABEL, IDC_SCR_POST_EDIT_LABEL };
    const int kEditIds[SCR_SLOT_COUNT]     = { IDC_SCR_PRE_EDIT,    IDC_SCR_POST_EDIT   };
    const int kLoadIds[SCR_SLOT_COUNT]     = { IDC_SCR_PRE_LOAD,    IDC_SCR_POST_LOAD   };
    const int kTestIds[SCR_SLOT_COUNT]     = { IDC_SCR_PRE_TEST,    IDC_SCR_POST_TEST   };

    for (int slot = 0; slot < SCR_SLOT_COUNT; slot++) {
        bool masterOn = s_scrEnabled;
        bool slotOn   = masterOn && s_slots[slot].enabled;

        // Section label
        HWND hSection = CreateWindowExW(0, L"STATIC",
            loc(kSectionKeys[slot], kSectionFalls[slot]).c_str(),
            WS_CHILD | WS_VISIBLE | SS_LEFT,
            padH, y, clientWidth - padH * 2, S(24),
            hwnd, NULL, hInst, NULL);
        if (hPageTitleFont) SendMessageW(hSection, WM_SETFONT, (WPARAM)hPageTitleFont, TRUE);
        y += S(24) + S(6);

        // Per-slot enable checkbox
        HWND hSlotEn = CreateCustomCheckbox(hwnd, kEnableIds[slot],
            loc(kEnableKeys[slot], kEnableFalls[slot]),
            s_slots[slot].enabled,
            padH, y, clientWidth - padH * 2, cbH, hInst);
        EnableWindow(hSlotEn, masterOn ? TRUE : FALSE);
        y += cbH + S(6);

        // Radio buttons: bat (WS_GROUP) then ps1
        std::wstring batTxt = loc(L"scr_type_bat", L"Batch (.bat)");
        std::wstring ps1Txt = loc(L"scr_type_ps1", L"PowerShell (.ps1)");

        HWND hBat = CreateWindowExW(0, L"BUTTON", batTxt.c_str(),
            WS_CHILD | WS_VISIBLE | BS_AUTORADIOBUTTON | WS_GROUP,
            padH, y, S(140), cbH,
            hwnd, (HMENU)(UINT_PTR)kBatIds[slot], hInst, NULL);
        if (hGuiFont) SendMessageW(hBat, WM_SETFONT, (WPARAM)hGuiFont, TRUE);
        SendMessageW(hBat, BM_SETCHECK,
            (s_slots[slot].type == SCR_TYPE_BAT) ? BST_CHECKED : BST_UNCHECKED, 0);
        EnableWindow(hBat, slotOn ? TRUE : FALSE);

        HWND hPs1 = CreateWindowExW(0, L"BUTTON", ps1Txt.c_str(),
            WS_CHILD | WS_VISIBLE | BS_AUTORADIOBUTTON,
            padH + S(150), y, S(180), cbH,
            hwnd, (HMENU)(UINT_PTR)kPs1Ids[slot], hInst, NULL);
        if (hGuiFont) SendMessageW(hPs1, WM_SETFONT, (WPARAM)hGuiFont, TRUE);
        SendMessageW(hPs1, BM_SETCHECK,
            (s_slots[slot].type == SCR_TYPE_PS1) ? BST_CHECKED : BST_UNCHECKED, 0);
        EnableWindow(hPs1, slotOn ? TRUE : FALSE);
        y += cbH + S(6);

        // "Script content:" label
        HWND hLabel = CreateWindowExW(0, L"STATIC",
            loc(L"scr_edit_label", L"Script content (paste or type here):").c_str(),
            WS_CHILD | WS_VISIBLE | SS_LEFT,
            padH, y, clientWidth - padH * 2, S(18),
            hwnd, (HMENU)(UINT_PTR)kLabelIds[slot], hInst, NULL);
        if (hGuiFont) SendMessageW(hLabel, WM_SETFONT, (WPARAM)hGuiFont, TRUE);
        EnableWindow(hLabel, slotOn ? TRUE : FALSE);
        y += S(18) + S(4);

        // Multiline edit — fixed height
        const int editH = S(120);
        HWND hEdit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT",
            s_slots[slot].content.c_str(),
            WS_CHILD | WS_VISIBLE | WS_VSCROLL |
            ES_MULTILINE | ES_AUTOVSCROLL | ES_WANTRETURN | ES_LEFT,
            padH, y, clientWidth - padH * 2, editH,
            hwnd, (HMENU)(UINT_PTR)kEditIds[slot], hInst, NULL);
        if (hGuiFont) SendMessageW(hEdit, WM_SETFONT, (WPARAM)hGuiFont, TRUE);
        EnableWindow(hEdit, slotOn ? TRUE : FALSE);
        // Use a monospace font for scripts if available
        HFONT hMono = (HFONT)GetStockObject(SYSTEM_FIXED_FONT);
        if (hMono) SendMessageW(hEdit, WM_SETFONT, (WPARAM)hMono, TRUE);
        y += editH + gap;

        // "Load from file…" and "Test in terminal" buttons
        std::wstring loadTxt = loc(L"scr_load_file", L"Load from file\u2026");
        std::wstring testTxt = loc(L"scr_test",      L"Test in terminal");
        int wLoad = MeasureButtonWidth(loadTxt, true);
        int wTest = MeasureButtonWidth(testTxt, true);
        const int btnH = S(30);

        HWND hLoad = CreateCustomButtonWithIcon(
            hwnd, kLoadIds[slot], loadTxt.c_str(), ButtonColor::Blue,
            L"shell32.dll", 3,   // folder icon
            padH, y, wLoad, btnH, hInst);
        SetButtonTooltip(hLoad, loc(L"scr_load_tip",
            L"Load script content from a .bat or .ps1 file on disk").c_str());
        EnableWindow(hLoad, slotOn ? TRUE : FALSE);

        HWND hTest = CreateCustomButtonWithIcon(
            hwnd, kTestIds[slot], testTxt.c_str(), ButtonColor::Green,
            L"shell32.dll", 137,   // run / execute icon
            padH + wLoad + gap, y, wTest, btnH, hInst);
        SetButtonTooltip(hTest, loc(L"scr_test_tip",
            L"Write the script to a temporary file and run it in a console window to test it").c_str());
        EnableWindow(hTest, slotOn ? TRUE : FALSE);
        y += btnH + gap;

        // Separator between sections (only after slot 0)
        if (slot == SCR_SLOT_PRE) {
            y += gap;
            CreateWindowExW(0, L"STATIC", L"",
                WS_CHILD | WS_VISIBLE | SS_ETCHEDHORZ,
                padH, y, clientWidth - padH * 2, S(2),
                hwnd, NULL, hInst, NULL);
            y += S(2) + gap;
        }
    }

    y += S(15);  // bottom breathing room
    return y;
}

// ── SCR_TearDown ──────────────────────────────────────────────────────────────
void SCR_TearDown(HWND /*hwnd*/)
{
    // All controls are direct children of hwnd and are destroyed by the
    // generic SwitchPage child-enumeration loop in mainwindow.cpp.
    // We only need to reset the scroll offset here.
    s_scrScrollOffset = 0;
    s_hGuiFont  = NULL;
    s_hInst     = NULL;
    s_pLocale   = NULL;
}

// ── SCR_OnCommand ─────────────────────────────────────────────────────────────
bool SCR_OnCommand(HWND hwnd, int wmId, int wmEvent, HWND /*hCtrl*/)
{
    // ── Master enable checkbox ────────────────────────────────────────────────
    if (wmId == IDC_SCR_MASTER_ENABLE && wmEvent == BN_CLICKED) {
        HWND hCb = GetDlgItem(hwnd, IDC_SCR_MASTER_ENABLE);
        s_scrEnabled = (hCb && SendMessageW(hCb, BM_GETCHECK, 0, 0) == BST_CHECKED);
        ApplyMasterEnable(hwnd);
        MainWindow::MarkAsModified();
        return true;
    }

    // ── Per-slot enable checkboxes ────────────────────────────────────────────
    for (int slot = 0; slot < SCR_SLOT_COUNT; slot++) {
        int enId = (slot == SCR_SLOT_PRE) ? IDC_SCR_PRE_ENABLE : IDC_SCR_POST_ENABLE;
        if (wmId == enId && wmEvent == BN_CLICKED) {
            HWND hCb = GetDlgItem(hwnd, enId);
            s_slots[slot].enabled = (hCb && SendMessageW(hCb, BM_GETCHECK, 0, 0) == BST_CHECKED);
            ApplySlotEnable(hwnd, slot);
            MainWindow::MarkAsModified();
            return true;
        }
    }

    // ── Type radio buttons ────────────────────────────────────────────────────
    for (int slot = 0; slot < SCR_SLOT_COUNT; slot++) {
        int batId = (slot == SCR_SLOT_PRE) ? IDC_SCR_PRE_BAT : IDC_SCR_POST_BAT;
        int ps1Id = (slot == SCR_SLOT_PRE) ? IDC_SCR_PRE_PS1 : IDC_SCR_POST_PS1;
        if (wmId == batId && wmEvent == BN_CLICKED) {
            s_slots[slot].type = SCR_TYPE_BAT;
            MainWindow::MarkAsModified();
            return true;
        }
        if (wmId == ps1Id && wmEvent == BN_CLICKED) {
            s_slots[slot].type = SCR_TYPE_PS1;
            MainWindow::MarkAsModified();
            return true;
        }
    }

    // ── Edit change notification — mirror to in-memory state ─────────────────
    for (int slot = 0; slot < SCR_SLOT_COUNT; slot++) {
        int editId = (slot == SCR_SLOT_PRE) ? IDC_SCR_PRE_EDIT : IDC_SCR_POST_EDIT;
        if (wmId == editId && wmEvent == EN_CHANGE) {
            HWND hEdit = GetDlgItem(hwnd, editId);
            if (hEdit) {
                int len = GetWindowTextLengthW(hEdit);
                if (len > 0) {
                    std::wstring buf(len + 1, L'\0');
                    GetWindowTextW(hEdit, &buf[0], len + 1);
                    buf.resize(len);
                    s_slots[slot].content = buf;
                } else {
                    s_slots[slot].content.clear();
                }
            }
            MainWindow::MarkAsModified();
            return true;
        }
    }

    // ── Load from file buttons ────────────────────────────────────────────────
    for (int slot = 0; slot < SCR_SLOT_COUNT; slot++) {
        int loadId = (slot == SCR_SLOT_PRE) ? IDC_SCR_PRE_LOAD : IDC_SCR_POST_LOAD;
        if (wmId == loadId && wmEvent == BN_CLICKED) {
            OPENFILENAMEW ofn = {};
            wchar_t szFile[MAX_PATH] = {};
            ofn.lStructSize  = sizeof(OPENFILENAMEW);
            ofn.hwndOwner    = hwnd;
            ofn.lpstrFile    = szFile;
            ofn.nMaxFile     = MAX_PATH;
            ofn.lpstrFilter  = L"Script Files (*.bat;*.cmd;*.ps1)\0*.bat;*.cmd;*.ps1\0All Files (*.*)\0*.*\0";
            ofn.nFilterIndex = 1;
            ofn.Flags = OFN_EXPLORER | OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST | OFN_HIDEREADONLY;

            if (GetOpenFileNameW(&ofn)) {
                // Read file as UTF-8 or Latin-1; display in edit box
                std::ifstream ifs(szFile, std::ios::binary);
                if (ifs) {
                    std::string bytes((std::istreambuf_iterator<char>(ifs)),
                                       std::istreambuf_iterator<char>());
                    // Try UTF-8 conversion first; fallback to Latin-1
                    int wlen = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
                        bytes.c_str(), (int)bytes.size(), NULL, 0);
                    std::wstring wtext;
                    if (wlen > 0) {
                        wtext.resize(wlen);
                        MultiByteToWideChar(CP_UTF8, 0,
                            bytes.c_str(), (int)bytes.size(), &wtext[0], wlen);
                    } else {
                        // Fallback: ANSI / system code page
                        int alen = MultiByteToWideChar(CP_ACP, 0,
                            bytes.c_str(), (int)bytes.size(), NULL, 0);
                        wtext.resize(alen);
                        MultiByteToWideChar(CP_ACP, 0,
                            bytes.c_str(), (int)bytes.size(), &wtext[0], alen);
                    }
                    // Normalise line endings to CR+LF for the Edit control
                    std::wstring normalised;
                    normalised.reserve(wtext.size() + 64);
                    for (size_t i = 0; i < wtext.size(); ++i) {
                        if (wtext[i] == L'\n' &&
                            (i == 0 || wtext[i - 1] != L'\r'))
                            normalised += L'\r';
                        normalised += wtext[i];
                    }
                    s_slots[slot].content = normalised;
                    int editId = (slot == SCR_SLOT_PRE) ? IDC_SCR_PRE_EDIT : IDC_SCR_POST_EDIT;
                    HWND hEd = GetDlgItem(hwnd, editId);
                    if (hEd) SetWindowTextW(hEd, normalised.c_str());
                    MainWindow::MarkAsModified();
                }
            }
            return true;
        }
    }

    // ── Test in terminal buttons ──────────────────────────────────────────────
    for (int slot = 0; slot < SCR_SLOT_COUNT; slot++) {
        int testId = (slot == SCR_SLOT_PRE) ? IDC_SCR_PRE_TEST : IDC_SCR_POST_TEST;
        if (wmId == testId && wmEvent == BN_CLICKED) {
            if (s_slots[slot].content.empty()) {
                MessageBoxW(hwnd,
                    loc(L"scr_test_no_content",
                        L"The script editor is empty. Type or paste a script first.").c_str(),
                    loc(L"scr_test_title", L"Test Script").c_str(),
                    MB_OK | MB_ICONINFORMATION);
                return true;
            }

            // Build a temp file path
            wchar_t tmpDir[MAX_PATH] = {};
            GetTempPathW(MAX_PATH, tmpDir);
            wchar_t tmpFile[MAX_PATH] = {};
            UINT ret = GetTempFileNameW(tmpDir,
                L"sc_",   // prefix
                0,        // unique = auto
                tmpFile);
            if (ret == 0) return true;  // could not create temp file path

            bool isPs1 = (s_slots[slot].type == SCR_TYPE_PS1);
            // Rename to correct extension
            std::wstring scriptPath(tmpFile);
            scriptPath += isPs1 ? L".ps1" : L".bat";

            // Write content as UTF-8 with BOM for PowerShell compatibility
            {
                std::ofstream ofs(scriptPath, std::ios::binary);
                if (!ofs) return true;
                if (isPs1) {
                    // UTF-8 BOM so PowerShell respects the encoding
                    const unsigned char bom[] = { 0xEF, 0xBB, 0xBF };
                    ofs.write(reinterpret_cast<const char*>(bom), 3);
                }
                // Convert wstring → UTF-8
                int utf8len = WideCharToMultiByte(CP_UTF8, 0,
                    s_slots[slot].content.c_str(), -1, NULL, 0, NULL, NULL);
                if (utf8len > 1) {
                    std::string utf8(utf8len - 1, '\0');
                    WideCharToMultiByte(CP_UTF8, 0,
                        s_slots[slot].content.c_str(), -1,
                        &utf8[0], utf8len, NULL, NULL);
                    ofs.write(utf8.c_str(), utf8.size());
                }
            }

            // Delete the placeholder file created by GetTempFileName
            DeleteFileW(tmpFile);

            if (isPs1) {
                // powershell.exe -NoExit -ExecutionPolicy Bypass -File "<path>"
                std::wstring args = L"-NoExit -ExecutionPolicy Bypass -File \"" + scriptPath + L"\"";
                ShellExecuteW(hwnd, L"open", L"powershell.exe",
                              args.c_str(), NULL, SW_SHOW);
            } else {
                // cmd.exe /K ""<path>""
                std::wstring args = L"/K \"\"" + scriptPath + L"\"\"";
                ShellExecuteW(hwnd, L"open", L"cmd.exe",
                              args.c_str(), NULL, SW_SHOW);
            }
            return true;
        }
    }

    return false;
}

// ── SCR_SaveToDb ──────────────────────────────────────────────────────────────
void SCR_SaveToDb(int projectId)
{
    std::wstring pid = std::to_wstring(projectId);

    DB::SetSetting(L"scr_enabled_"      + pid, s_scrEnabled       ? L"1" : L"0");

    // Pre slot
    DB::SetSetting(L"scr_pre_enabled_"  + pid, s_slots[SCR_SLOT_PRE].enabled ? L"1" : L"0");
    DB::SetSetting(L"scr_pre_type_"     + pid, s_slots[SCR_SLOT_PRE].type == SCR_TYPE_BAT ? L"bat" : L"ps1");
    DB::SetSetting(L"scr_pre_content_"  + pid, s_slots[SCR_SLOT_PRE].content);

    // Post slot
    DB::SetSetting(L"scr_post_enabled_" + pid, s_slots[SCR_SLOT_POST].enabled ? L"1" : L"0");
    DB::SetSetting(L"scr_post_type_"    + pid, s_slots[SCR_SLOT_POST].type == SCR_TYPE_BAT ? L"bat" : L"ps1");
    DB::SetSetting(L"scr_post_content_" + pid, s_slots[SCR_SLOT_POST].content);
}

// ── SCR_LoadFromDb ────────────────────────────────────────────────────────────
void SCR_LoadFromDb(int projectId)
{
    std::wstring pid = std::to_wstring(projectId);
    std::wstring val;

    if (DB::GetSetting(L"scr_enabled_" + pid, val))
        s_scrEnabled = (val == L"1");

    // Pre slot
    if (DB::GetSetting(L"scr_pre_enabled_" + pid, val))
        s_slots[SCR_SLOT_PRE].enabled = (val == L"1");
    if (DB::GetSetting(L"scr_pre_type_" + pid, val))
        s_slots[SCR_SLOT_PRE].type = (val == L"bat") ? SCR_TYPE_BAT : SCR_TYPE_PS1;
    if (DB::GetSetting(L"scr_pre_content_" + pid, val))
        s_slots[SCR_SLOT_PRE].content = val;

    // Post slot
    if (DB::GetSetting(L"scr_post_enabled_" + pid, val))
        s_slots[SCR_SLOT_POST].enabled = (val == L"1");
    if (DB::GetSetting(L"scr_post_type_" + pid, val))
        s_slots[SCR_SLOT_POST].type = (val == L"bat") ? SCR_TYPE_BAT : SCR_TYPE_PS1;
    if (DB::GetSetting(L"scr_post_content_" + pid, val))
        s_slots[SCR_SLOT_POST].content = val;
}
