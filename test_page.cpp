/*
 * test_page.cpp — Test Installer page for SetupCraft (page index 7).
 *
 * Lets the user configure a separate test-output location and trigger a
 * test build of the installer without touching the production Build output.
 *
 * ── Inno Setup integration note ───────────────────────────────────────────────
 *   TEST_RunTest() is currently a stub: it computes and persists the output path,
 *   shows the resolved path in the status label, and enables the "Open folder"
 *   button if the folder already exists.  Actual Inno compile + launch is
 *   implemented in the next round.
 *
 * ── Layout (top → bottom, all via S()) ────────────────────────────────────────
 *   Page title (hPageTitleFont, S(38) high)
 *   Description text (hGuiFont, S(36) high, wraps)
 *   Gap S(20)
 *   Row: "Output folder:" label  +  edit  +  browse button
 *   Row: "Output file:"   label  +  edit  +  ".exe" suffix label
 *   Gap S(20)
 *   [▶  Run Test  (F5)] — Green, left-aligned at field column
 *   Gap S(16)
 *   Status static (hGuiFont, S(50) high, multi-line)
 *   Gap S(8)
 *   [Open test folder] — Blue, left-aligned at padH
 */

#include "test_page.h"
#include "settings.h"       // SETT_GetBuildConfig(), SETT_SetTestOutput*()
#include "mainwindow.h"     // MainWindow::GetCurrentProject(), GetProjectName(), etc.
#include "ctrlw.h"          // ExpandEscapes()
#include "db.h"
#include "dpi.h"            // S()
#include "button.h"         // CreateCustomButtonWithIcon(), MeasureButtonWidth()
#include "issgen.h"         // ISS_FindInnoDir(), ISS_GenerateIss(), IssExtraData
#include "file_assoc.h"     // FA_GetAssociations()
#include "shortcuts.h"      // SC_GetShortcutRows(), SC_GetMenuNodeRows(), SC_Get*OptOut()
#include "scripts.h"        // SCR_GetScripts()
#include <commctrl.h>       // PROGRESS_CLASS, PBM_*, PBS_*
#include <shlobj.h>         // SHBrowseForFolderW, SHGetPathFromIDListW, SHCreateDirectoryExW
#include <shellapi.h>       // ShellExecuteW
#include <algorithm>        // std::remove_if
#include <string>
#include <map>
#include <vector>

// ── Module-private state ──────────────────────────────────────────────────────
static HWND  s_hFolderEdit  = NULL;
static HWND  s_hFileEdit    = NULL;
static HWND  s_hStatus      = NULL;
static HWND  s_hOpenBtn     = NULL;
static HFONT s_hGuiFont     = NULL;
static HINSTANCE s_hInst    = NULL;
static const std::map<std::wstring, std::wstring>* s_pLocale = NULL;
static std::wstring s_lastTestFolder;

// ── Async build state ─────────────────────────────────────────────────────────
static HWND      s_hProgress    = NULL;   // PROGRESS_CLASS bar
static HWND      s_hStepLbl     = NULL;   // current step text
static HWND      s_hDetailsBtn  = NULL;   // "Details…" button
static HWND      s_hDetailsWnd  = NULL;   // modeless ISCC-output window
static HWND      s_hDetailsEdit = NULL;   // readonly edit inside details window
static HANDLE    s_hBuildThread = NULL;
static UINT_PTR  s_timerId      = 0;
static HWND      s_hwndMain     = NULL;

// Thread-shared state (protected by s_cs)
static CRITICAL_SECTION s_cs;
static bool         s_csInit      = false;
static bool         s_threadDone  = false;
static bool         s_threadOk    = false;
static std::wstring s_threadStep;
static std::wstring s_threadLog;
static std::wstring s_threadResult;

// ── Locale helper ─────────────────────────────────────────────────────────────
static std::wstring loc(const wchar_t* key, const wchar_t* fallback)
{
    if (!s_pLocale) return fallback;
    auto it = s_pLocale->find(key);
    return (it != s_pLocale->end()) ? it->second : fallback;
}

// ── Helper: compute default test output folder ────────────────────────────────
// If the production outputFolder is set, appends \test_output to it.
// Otherwise falls back to %USERPROFILE%\Documents\SetupCraft\<Name>-Setupfiles\test_output.
static std::wstring ComputeDefaultTestFolder()
{
    SBuildConfig cfg = SETT_GetBuildConfig();
    if (!cfg.outputFolder.empty())
        return cfg.outputFolder + L"\\test_output";

    std::wstring projName = MainWindow::GetProjectName();
    if (projName.empty())
        return L"";

    wchar_t userProfile[MAX_PATH] = {};
    ExpandEnvironmentStringsW(L"%USERPROFILE%", userProfile, MAX_PATH);
    return std::wstring(userProfile)
         + L"\\Documents\\SetupCraft\\"
         + projName
         + L"-Setupfiles\\test_output";
}

// ── Helper: compute default test output filename (no extension) ───────────────
// If the production outputFilename is set, appends _test to it.
// Otherwise: strip whitespace from project name, then "<Name>_<ver>_Setup_test".
static std::wstring ComputeDefaultTestFilename()
{
    SBuildConfig cfg = SETT_GetBuildConfig();
    if (!cfg.outputFilename.empty())
        return cfg.outputFilename + L"_test";

    std::wstring name = MainWindow::GetProjectName();
    std::wstring ver  = MainWindow::GetProjectVersion();
    name.erase(std::remove_if(name.begin(), name.end(), ::iswspace), name.end());
    if (name.empty())
        return L"Setup_test";
    return name + (ver.empty() ? L"" : L"_" + ver) + L"_Setup_test";
}

// ── Helper: uniqueness check ──────────────────────────────────────────────────
// Returns baseName (unchanged) if folder\baseName.exe does not exist.
// Otherwise tries baseName(1), baseName(2), … up to (999).
static std::wstring UniqueFilename(const std::wstring& folder, const std::wstring& baseName)
{
    auto exists = [&](const std::wstring& name) -> bool {
        std::wstring path = folder + L"\\" + name + L".exe";
        return GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES;
    };
    if (!exists(baseName))
        return baseName;
    for (int n = 1; n < 1000; ++n) {
        std::wstring candidate = baseName + L"(" + std::to_wstring(n) + L")";
        if (!exists(candidate))
            return candidate;
    }
    return baseName; // give up — caller will overwrite
}

// ── Thread-safe helpers (called from build thread) ────────────────────────────
static void ThrSetStep(const std::wstring& s) {
    EnterCriticalSection(&s_cs);
    s_threadStep = s;
    LeaveCriticalSection(&s_cs);
}
static void ThrAppendLog(const std::wstring& chunk) {
    EnterCriticalSection(&s_cs);
    s_threadLog += chunk;
    LeaveCriticalSection(&s_cs);
}
static void ThrFinish(bool ok, const std::wstring& result) {
    EnterCriticalSection(&s_cs);
    s_threadOk     = ok;
    s_threadResult = result;
    s_threadDone   = true;
    LeaveCriticalSection(&s_cs);
}

// ── Build params struct (heap-allocated, owned by thread) ─────────────────────
struct TestBuildParams {
    std::wstring templatePath;
    std::wstring outIssPath;
    ProjectRow   proj;
    SBuildConfig cfg;      // outputFolder/outputFilename set to test paths
    std::vector<InnoLangEntry>  langs;
    std::vector<FileAssocRow>   assocs;
    std::vector<InstallTypeRow> types;
    std::vector<ComponentRow>   comps;
    IssExtraData extra;    // shortcuts, scripts, registry entries
};

// ── Find ISCC.exe ─────────────────────────────────────────────────────────────
static std::wstring FindIscc() {
    const wchar_t* candidates[] = {
        L"C:\\Program Files (x86)\\Inno Setup 6\\ISCC.exe",
        L"C:\\Program Files\\Inno Setup 6\\ISCC.exe",
        L"C:\\Program Files (x86)\\Inno Setup 5\\ISCC.exe",
        L"C:\\Program Files\\Inno Setup 5\\ISCC.exe",
    };
    for (auto path : candidates)
        if (GetFileAttributesW(path) != INVALID_FILE_ATTRIBUTES)
            return path;
    return L"";
}

// ── Build thread ──────────────────────────────────────────────────────────────
static DWORD WINAPI TEST_BuildThreadProc(LPVOID param)
{
    TestBuildParams* p = reinterpret_cast<TestBuildParams*>(param);

    // Step 1: generate .iss
    ThrSetStep(L"Step 1 of 2 \u2014 Generating installer script\u2026");
    std::wstring genErr = ISS_GenerateIss(
        p->templatePath, p->outIssPath,
        p->proj, p->cfg, p->langs, p->assocs, p->types, p->comps, p->extra);
    if (!genErr.empty()) {
        ThrAppendLog(L"Error generating .iss script:\r\n" + genErr + L"\r\n");
        ThrFinish(false, L"\u2716  Failed to generate installer script \u2014 see Details");
        delete p;
        return 1;
    }

    // Step 2: compile with ISCC
    ThrSetStep(L"Step 2 of 2 \u2014 Compiling with ISCC\u2026");
    std::wstring iscc = FindIscc();
    if (iscc.empty()) {
        ThrAppendLog(L"ISCC.exe not found.\r\nInstall Inno Setup 6 from: https://jrsoftware.org/isdl.php\r\n");
        ThrFinish(false, L"\u2716  ISCC.exe not found \u2014 install Inno Setup 6");
        delete p;
        return 1;
    }

    // Ensure output directory exists
    SHCreateDirectoryExW(NULL, p->cfg.outputFolder.c_str(), NULL);

    std::wstring cmd =
        L"\"" + iscc + L"\""
        L" /O\"" + p->cfg.outputFolder + L"\""
        L" /F\"" + p->cfg.outputFilename + L"\""
        L" \"" + p->outIssPath + L"\"";

    ThrAppendLog(L"Command:\r\n  " + cmd + L"\r\n\r\n");

    SECURITY_ATTRIBUTES sa = { sizeof(sa), NULL, TRUE };
    HANDLE hRead, hWrite;
    if (!CreatePipe(&hRead, &hWrite, &sa, 0)) {
        ThrFinish(false, L"\u2716  Internal error (CreatePipe failed)");
        delete p;
        return 1;
    }
    SetHandleInformation(hRead, HANDLE_FLAG_INHERIT, 0);

    STARTUPINFOW si = {};
    si.cb          = sizeof(si);
    si.dwFlags     = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    si.hStdOutput  = hWrite;
    si.hStdError   = hWrite;

    PROCESS_INFORMATION pi = {};
    std::vector<wchar_t> cmdBuf(cmd.begin(), cmd.end());
    cmdBuf.push_back(L'\0');

    BOOL launched = CreateProcessW(
        NULL, cmdBuf.data(), NULL, NULL, TRUE,
        CREATE_NO_WINDOW, NULL, NULL, &si, &pi);
    CloseHandle(hWrite);   // close our write end so pipe EOF works

    if (!launched) {
        CloseHandle(hRead);
        ThrFinish(false, L"\u2716  Failed to launch ISCC.exe");
        delete p;
        return 1;
    }

    // Capture ISCC stdout/stderr
    char buf[4096];
    DWORD nRead;
    while (ReadFile(hRead, buf, sizeof(buf) - 1, &nRead, NULL) && nRead > 0) {
        int wLen = MultiByteToWideChar(CP_ACP, 0, buf, (int)nRead, NULL, 0);
        if (wLen > 0) {
            std::wstring chunk(wLen, L'\0');
            MultiByteToWideChar(CP_ACP, 0, buf, (int)nRead, &chunk[0], wLen);
            // Normalise to CRLF for the edit control
            std::wstring norm;
            norm.reserve(chunk.size() + 32);
            for (size_t i = 0; i < chunk.size(); ++i) {
                if (chunk[i] == L'\r') continue;
                if (chunk[i] == L'\n') norm += L"\r\n";
                else norm += chunk[i];
            }
            ThrAppendLog(norm);
        }
    }
    CloseHandle(hRead);

    DWORD exitCode = 0;
    WaitForSingleObject(pi.hProcess, INFINITE);
    GetExitCodeProcess(pi.hProcess, &exitCode);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);

    std::wstring exePath =
        p->cfg.outputFolder + L"\\" + p->cfg.outputFilename + L".exe";
    if (exitCode == 0)
        ThrFinish(true,  L"\u2714  Build complete \u2014 " + exePath);
    else
        ThrFinish(false, L"\u2716  ISCC failed (exit " + std::to_wstring(exitCode) + L") \u2014 see Details");

    delete p;
    return 0;
}

// ── Timer proc: polls thread state, updates UI (main thread) ─────────────────
static VOID CALLBACK TEST_TimerProc(HWND hwnd, UINT, UINT_PTR, DWORD)
{
    if (!s_csInit) return;

    bool done, ok;
    std::wstring step, log, result;
    EnterCriticalSection(&s_cs);
    done   = s_threadDone;
    ok     = s_threadOk;
    step   = s_threadStep;
    log    = s_threadLog;
    result = s_threadResult;
    LeaveCriticalSection(&s_cs);

    // Update step label
    if (s_hStepLbl && IsWindow(s_hStepLbl))
        SetWindowTextW(s_hStepLbl, step.c_str());

    // Live-update open details window
    if (s_hDetailsWnd  && IsWindow(s_hDetailsWnd) &&
        s_hDetailsEdit && IsWindow(s_hDetailsEdit)) {
        int curLen = GetWindowTextLengthW(s_hDetailsEdit);
        if ((int)log.size() != curLen) {
            DWORD ss = 0, se = 0;
            SendMessageW(s_hDetailsEdit, EM_GETSEL, (WPARAM)&ss, (LPARAM)&se);
            bool atEnd = (ss == (DWORD)curLen && se == (DWORD)curLen);
            SetWindowTextW(s_hDetailsEdit, log.c_str());
            if (atEnd) {
                int newLen = GetWindowTextLengthW(s_hDetailsEdit);
                SendMessageW(s_hDetailsEdit, EM_SETSEL, newLen, newLen);
                SendMessageW(s_hDetailsEdit, EM_SCROLLCARET, 0, 0);
            }
        }
    }

    if (!done) return;

    // ── Build finished ────────────────────────────────────────────────────────
    KillTimer(hwnd, s_timerId);
    s_timerId = 0;

    if (s_hProgress && IsWindow(s_hProgress)) {
        LONG style = GetWindowLongW(s_hProgress, GWL_STYLE);
        SetWindowLongW(s_hProgress, GWL_STYLE, style & ~PBS_MARQUEE);
        SendMessageW(s_hProgress, PBM_SETMARQUEE, FALSE, 0);
        SendMessageW(s_hProgress, PBM_SETRANGE32, 0, 100);
        SendMessageW(s_hProgress, PBM_SETPOS, ok ? 100 : 0, 0);
        if (!ok)
            SendMessageW(s_hProgress, PBM_SETBARCOLOR, 0, (LPARAM)RGB(200, 50, 50));
    }

    if (s_hStepLbl && IsWindow(s_hStepLbl))
        ShowWindow(s_hStepLbl, SW_HIDE);

    if (s_hStatus && IsWindow(s_hStatus))
        SetWindowTextW(s_hStatus, result.c_str());

    HWND hRun = GetDlgItem(hwnd, IDC_TEST_RUN_BTN);
    if (hRun) EnableWindow(hRun, TRUE);

    if (s_hDetailsBtn && IsWindow(s_hDetailsBtn))
        ShowWindow(s_hDetailsBtn, SW_SHOW);

    if (s_hOpenBtn && IsWindow(s_hOpenBtn)) {
        bool exists = !s_lastTestFolder.empty() &&
            GetFileAttributesW(s_lastTestFolder.c_str()) != INVALID_FILE_ATTRIBUTES;
        EnableWindow(s_hOpenBtn, exists ? TRUE : FALSE);
    }

    // Final details window sync
    if (s_hDetailsWnd  && IsWindow(s_hDetailsWnd) &&
        s_hDetailsEdit && IsWindow(s_hDetailsEdit)) {
        SetWindowTextW(s_hDetailsEdit, log.c_str());
        int newLen = GetWindowTextLengthW(s_hDetailsEdit);
        SendMessageW(s_hDetailsEdit, EM_SETSEL, newLen, newLen);
        SendMessageW(s_hDetailsEdit, EM_SCROLLCARET, 0, 0);
    }

    if (s_hBuildThread) {
        WaitForSingleObject(s_hBuildThread, 2000);
        CloseHandle(s_hBuildThread);
        s_hBuildThread = NULL;
    }
}

// ── Details window ────────────────────────────────────────────────────────────
static LRESULT CALLBACK TEST_DetailsWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg) {
    case WM_CREATE: {
        CREATESTRUCTW* cs = reinterpret_cast<CREATESTRUCTW*>(lParam);
        RECT rc; GetClientRect(hwnd, &rc);
        s_hDetailsEdit = CreateWindowExW(
            WS_EX_CLIENTEDGE, L"EDIT", L"",
            WS_CHILD | WS_VISIBLE | WS_VSCROLL | WS_HSCROLL |
            ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL,
            0, 0, rc.right, rc.bottom,
            hwnd, (HMENU)1, cs->hInstance, NULL);
        HFONT hMono = CreateFontW(
            -S(11), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, FIXED_PITCH | FF_MODERN, L"Consolas");
        if (hMono && s_hDetailsEdit)
            SendMessageW(s_hDetailsEdit, WM_SETFONT, (WPARAM)hMono, TRUE);
        return 0;
    }
    case WM_SIZE:
        if (s_hDetailsEdit && IsWindow(s_hDetailsEdit))
            MoveWindow(s_hDetailsEdit, 0, 0, LOWORD(lParam), HIWORD(lParam), TRUE);
        return 0;
    case WM_CLOSE:
        DestroyWindow(hwnd);
        return 0;
    case WM_DESTROY:
        s_hDetailsWnd  = NULL;
        s_hDetailsEdit = NULL;
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

static void TEST_ShowDetails(HWND hwndParent)
{
    static bool s_classRegistered = false;
    if (!s_classRegistered) {
        HINSTANCE hInst = (HINSTANCE)GetWindowLongPtr(hwndParent, GWLP_HINSTANCE);
        WNDCLASSEXW wc   = {};
        wc.cbSize        = sizeof(wc);
        wc.lpfnWndProc   = TEST_DetailsWndProc;
        wc.hInstance     = hInst;
        wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
        wc.lpszClassName = L"SCTestDetailsWnd";
        wc.hCursor       = LoadCursor(NULL, IDC_ARROW);
        wc.hIcon         = LoadIcon(NULL, IDI_APPLICATION);
        RegisterClassExW(&wc);
        s_classRegistered = true;
    }

    if (s_hDetailsWnd && IsWindow(s_hDetailsWnd)) {
        SetForegroundWindow(s_hDetailsWnd);
        BringWindowToTop(s_hDetailsWnd);
        return;
    }

    HINSTANCE hInst = (HINSTANCE)GetWindowLongPtr(hwndParent, GWLP_HINSTANCE);
    s_hDetailsWnd = CreateWindowExW(
        0, L"SCTestDetailsWnd",
        loc(L"test_details_title", L"ISCC Output \u2014 Test Build").c_str(),
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT, S(700), S(500),
        NULL, NULL, hInst, NULL);
    if (!s_hDetailsWnd) return;

    if (s_hDetailsEdit && IsWindow(s_hDetailsEdit)) {
        std::wstring log;
        if (s_csInit) {
            EnterCriticalSection(&s_cs);
            log = s_threadLog;
            LeaveCriticalSection(&s_cs);
        }
        SetWindowTextW(s_hDetailsEdit, log.c_str());
        int newLen = GetWindowTextLengthW(s_hDetailsEdit);
        SendMessageW(s_hDetailsEdit, EM_SETSEL, newLen, newLen);
        SendMessageW(s_hDetailsEdit, EM_SCROLLCARET, 0, 0);
    }

    ShowWindow(s_hDetailsWnd, SW_SHOW);
    UpdateWindow(s_hDetailsWnd);
}

// ── TEST_BuildPage ────────────────────────────────────────────────────────────
void TEST_BuildPage(HWND hwnd, HINSTANCE hInst,
                    int pageY, int clientWidth,
                    HFONT hPageTitleFont, HFONT hGuiFont,
                    const std::map<std::wstring, std::wstring>& locale)
{
    s_hInst    = hInst;
    s_hGuiFont = hGuiFont;
    s_pLocale  = &locale;

    const int padH    = S(20);
    const int lblW    = S(120);
    const int lblGap  = S(8);
    const int rowH    = S(22);
    const int rowStep = S(32);
    const int btnH    = S(28);

    // Field column starts here
    const int fldX = padH + lblW + lblGap;
    const int fldW = clientWidth - fldX - padH - S(btnH + 6); // leave room for browse btn

    int y = pageY + S(20);

    // ── Page title ────────────────────────────────────────────────────────────
    HWND hTitle = CreateWindowExW(0, L"STATIC",
        loc(L"test_page_title", L"Test Installer").c_str(),
        WS_CHILD | WS_VISIBLE | SS_LEFT,
        padH, y, clientWidth - padH * 2, S(38),
        hwnd, (HMENU)(UINT_PTR)IDC_TEST_PAGE_TITLE, hInst, NULL);
    if (hPageTitleFont) SendMessageW(hTitle, WM_SETFONT, (WPARAM)hPageTitleFont, TRUE);
    y += S(38) + S(8);

    // ── Description ───────────────────────────────────────────────────────────
    HWND hDesc = CreateWindowExW(0, L"STATIC",
        ExpandEscapes(loc(L"test_page_desc",
            L"Generate and run a test build of the installer.\r\n"
            L"Output goes to the test folder and never overwrites your production build.")).c_str(),
        WS_CHILD | WS_VISIBLE | SS_LEFT,
        padH, y, clientWidth - padH * 2, S(36),
        hwnd, NULL, hInst, NULL);
    if (hGuiFont) SendMessageW(hDesc, WM_SETFONT, (WPARAM)hGuiFont, TRUE);
    y += S(36) + S(20);

    // ── "Output folder:" row ──────────────────────────────────────────────────
    HWND hFolderLbl = CreateWindowExW(0, L"STATIC",
        loc(L"test_output_folder_lbl", L"Output folder:").c_str(),
        WS_CHILD | WS_VISIBLE | SS_RIGHT | SS_CENTERIMAGE,
        padH, y, lblW, rowH,
        hwnd, NULL, hInst, NULL);
    if (hGuiFont) SendMessageW(hFolderLbl, WM_SETFONT, (WPARAM)hGuiFont, TRUE);

    SBuildConfig cfg = SETT_GetBuildConfig();
    std::wstring initFolder = cfg.testOutputFolder;
    if (initFolder.empty()) initFolder = ComputeDefaultTestFolder();

    s_hFolderEdit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT",
        initFolder.c_str(),
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
        fldX, y, fldW, rowH,
        hwnd, (HMENU)(UINT_PTR)IDC_TEST_OUTPUT_FOLDER, hInst, NULL);
    if (hGuiFont) SendMessageW(s_hFolderEdit, WM_SETFONT, (WPARAM)hGuiFont, TRUE);

    CreateCustomButtonWithIcon(hwnd, IDC_TEST_OUTPUT_FOLDER_BTN,
        L"", ButtonColor::Blue, L"shell32.dll", 3,
        fldX + fldW + S(4), y, S(btnH), S(btnH), hInst);
    y += rowStep;

    // ── "Output file:" row ────────────────────────────────────────────────────
    HWND hFileLbl = CreateWindowExW(0, L"STATIC",
        loc(L"test_output_file_lbl", L"Output file:").c_str(),
        WS_CHILD | WS_VISIBLE | SS_RIGHT | SS_CENTERIMAGE,
        padH, y, lblW, rowH,
        hwnd, NULL, hInst, NULL);
    if (hGuiFont) SendMessageW(hFileLbl, WM_SETFONT, (WPARAM)hGuiFont, TRUE);

    std::wstring initFile = cfg.testOutputFilename;
    if (initFile.empty()) initFile = ComputeDefaultTestFilename();
    // Shorten the file edit to leave room for ".exe" suffix label
    const int extLblW = S(36);
    const int fileEditW = fldW - extLblW - S(4);

    s_hFileEdit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT",
        initFile.c_str(),
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
        fldX, y, fileEditW, rowH,
        hwnd, (HMENU)(UINT_PTR)IDC_TEST_OUTPUT_FILE, hInst, NULL);
    if (hGuiFont) SendMessageW(s_hFileEdit, WM_SETFONT, (WPARAM)hGuiFont, TRUE);

    HWND hExtLbl = CreateWindowExW(0, L"STATIC", L".exe",
        WS_CHILD | WS_VISIBLE | SS_LEFT | SS_CENTERIMAGE,
        fldX + fileEditW + S(4), y, extLblW, rowH,
        hwnd, NULL, hInst, NULL);
    if (hGuiFont) SendMessageW(hExtLbl, WM_SETFONT, (WPARAM)hGuiFont, TRUE);
    y += rowStep;

    y += S(20);

    // ── Run Test button (Green) + Details button (Blue, hidden until first run) ─
    std::wstring runTxt = loc(L"test_run_btn", L"Run Test  (F5)");
    int wRun = MeasureButtonWidth(runTxt, true);
    CreateCustomButtonWithIcon(hwnd, IDC_TEST_RUN_BTN,
        runTxt.c_str(), ButtonColor::Green, L"shell32.dll", 137,
        fldX, y, wRun, btnH, hInst);

    std::wstring detTxt = loc(L"test_details_btn", L"Details\u2026");
    int wDet = MeasureButtonWidth(detTxt, false);
    s_hDetailsBtn = CreateCustomButtonWithIcon(hwnd, IDC_TEST_DETAILS_BTN,
        detTxt.c_str(), ButtonColor::Blue, L"shell32.dll", 23,
        fldX + wRun + S(8), y, wDet, btnH, hInst);
    ShowWindow(s_hDetailsBtn, SW_HIDE);   // shown after first build

    y += btnH + S(8);

    // ── Progress bar (hidden until build is running) ───────────────────────────
    s_hProgress = CreateWindowExW(0, PROGRESS_CLASS, NULL,
        WS_CHILD | PBS_SMOOTH,
        padH, y, clientWidth - padH * 2, S(12),
        hwnd, (HMENU)(UINT_PTR)IDC_TEST_PROGRESS, hInst, NULL);
    ShowWindow(s_hProgress, SW_HIDE);
    y += S(12) + S(4);

    // ── Step label (hidden until build is running) ─────────────────────────────
    s_hStepLbl = CreateWindowExW(0, L"STATIC", L"",
        WS_CHILD | SS_LEFT,
        padH, y, clientWidth - padH * 2, S(18),
        hwnd, (HMENU)(UINT_PTR)IDC_TEST_STEP_LBL, hInst, NULL);
    if (hGuiFont) SendMessageW(s_hStepLbl, WM_SETFONT, (WPARAM)hGuiFont, TRUE);
    ShowWindow(s_hStepLbl, SW_HIDE);
    y += S(18) + S(10);

    // ── Status ────────────────────────────────────────────────────────────────
    s_hStatus = CreateWindowExW(0, L"STATIC",
        loc(L"test_status_idle",
            L"Ready \u2014 press \u00ABRun Test\u00BB or F5 to generate the test installer.").c_str(),
        WS_CHILD | WS_VISIBLE | SS_LEFT,
        padH, y, clientWidth - padH * 2, S(40),
        hwnd, (HMENU)(UINT_PTR)IDC_TEST_STATUS, hInst, NULL);
    if (hGuiFont) SendMessageW(s_hStatus, WM_SETFONT, (WPARAM)hGuiFont, TRUE);
    y += S(40) + S(10);

    // ── Open test folder button (Blue, initially disabled) ────────────────────
    std::wstring openTxt = loc(L"test_open_folder_btn", L"Open test folder");
    int wOpen = MeasureButtonWidth(openTxt, true);
    s_hOpenBtn = CreateCustomButtonWithIcon(hwnd, IDC_TEST_OPEN_FOLDER_BTN,
        openTxt.c_str(), ButtonColor::Blue, L"shell32.dll", 3,
        padH, y, wOpen, btnH, hInst);
    {
        std::wstring chk = cfg.testOutputFolder.empty()
            ? ComputeDefaultTestFolder() : cfg.testOutputFolder;
        bool exists = !chk.empty() &&
            GetFileAttributesW(chk.c_str()) != INVALID_FILE_ATTRIBUTES;
        EnableWindow(s_hOpenBtn, exists ? TRUE : FALSE);
        if (exists) s_lastTestFolder = chk;
    }

    // Show "no project" status if appropriate
    if (MainWindow::GetProjectName().empty())
        SetWindowTextW(s_hStatus,
            loc(L"test_status_no_project",
                L"No project open. Create or open a project first.").c_str());

    // Re-attach monitoring if a build is still running from a prior page switch
    if (s_hBuildThread) {
        DWORD xc = 0;
        if (GetExitCodeThread(s_hBuildThread, &xc) && xc == STILL_ACTIVE) {
            s_hwndMain = hwnd;
            s_timerId  = SetTimer(hwnd, 8901, 150, TEST_TimerProc);
            HWND hRun2 = GetDlgItem(hwnd, IDC_TEST_RUN_BTN);
            if (hRun2) EnableWindow(hRun2, FALSE);
            LONG sty = GetWindowLongW(s_hProgress, GWL_STYLE);
            SetWindowLongW(s_hProgress, GWL_STYLE, (sty & ~PBS_SMOOTH) | PBS_MARQUEE);
            SendMessageW(s_hProgress, PBM_SETMARQUEE, TRUE, 60);
            ShowWindow(s_hProgress, SW_SHOW);
            ShowWindow(s_hStepLbl,  SW_SHOW);
        }
    }
}

// ── TEST_TearDown ─────────────────────────────────────────────────────────────
void TEST_TearDown()
{
    // Stop polling timer if running
    if (s_timerId && s_hwndMain) {
        KillTimer(s_hwndMain, s_timerId);
        s_timerId = 0;
    }
    // Close details window
    if (s_hDetailsWnd && IsWindow(s_hDetailsWnd)) {
        DestroyWindow(s_hDetailsWnd);
        s_hDetailsWnd  = NULL;
        s_hDetailsEdit = NULL;
    }
    s_hFolderEdit  = NULL;
    s_hFileEdit    = NULL;
    s_hStatus      = NULL;
    s_hOpenBtn     = NULL;
    s_hProgress    = NULL;
    s_hStepLbl     = NULL;
    s_hDetailsBtn  = NULL;
}

// ── TEST_OnCommand ────────────────────────────────────────────────────────────
bool TEST_OnCommand(HWND hwnd, int wmId, int wmEvent)
{
    // Folder edit changes
    if (wmId == IDC_TEST_OUTPUT_FOLDER && wmEvent == EN_CHANGE) {
        if (s_hFolderEdit && IsWindow(s_hFolderEdit)) {
            wchar_t buf[MAX_PATH] = {};
            GetWindowTextW(s_hFolderEdit, buf, MAX_PATH);
            SETT_SetTestOutputFolder(buf);
        }
        return true;
    }

    // File edit changes
    if (wmId == IDC_TEST_OUTPUT_FILE && wmEvent == EN_CHANGE) {
        if (s_hFileEdit && IsWindow(s_hFileEdit)) {
            wchar_t buf[512] = {};
            GetWindowTextW(s_hFileEdit, buf, 512);
            SETT_SetTestOutputFilename(buf);
        }
        return true;
    }

    // Browse for folder
    if (wmId == IDC_TEST_OUTPUT_FOLDER_BTN && wmEvent == BN_CLICKED) {
        BROWSEINFOW bi = {};
        bi.hwndOwner = hwnd;
        bi.lpszTitle = loc(L"test_output_folder_picker_title", L"Select test output folder").c_str();
        bi.ulFlags   = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE | BIF_USENEWUI;
        LPITEMIDLIST pidl = SHBrowseForFolderW(&bi);
        if (pidl) {
            wchar_t path[MAX_PATH] = {};
            if (SHGetPathFromIDListW(pidl, path)) {
                SETT_SetTestOutputFolder(path);
                if (s_hFolderEdit && IsWindow(s_hFolderEdit))
                    SetWindowTextW(s_hFolderEdit, path);
            }
            CoTaskMemFree(pidl);
        }
        return true;
    }

    // Run Test button
    if (wmId == IDC_TEST_RUN_BTN && wmEvent == BN_CLICKED) {
        TEST_RunTest(hwnd);
        return true;
    }

    // Details button
    if (wmId == IDC_TEST_DETAILS_BTN && wmEvent == BN_CLICKED) {
        TEST_ShowDetails(hwnd);
        return true;
    }

    // Open test folder button
    if (wmId == IDC_TEST_OPEN_FOLDER_BTN && wmEvent == BN_CLICKED) {
        if (!s_lastTestFolder.empty()) {
            ShellExecuteW(hwnd, L"explore", s_lastTestFolder.c_str(), NULL, NULL, SW_SHOWNORMAL);
        }
        return true;
    }

    return false;
}

// ── TEST_RunTest ──────────────────────────────────────────────────────────────
void TEST_RunTest(HWND hwnd)
{
    // Guard: need an open project
    if (MainWindow::GetProjectName().empty()) {
        if (s_hStatus && IsWindow(s_hStatus))
            SetWindowTextW(s_hStatus,
                loc(L"test_status_no_project",
                    L"No project open. Create or open a project first.").c_str());
        return;
    }

    // Guard: unsaved changes — test must use the saved DB state, not in-memory edits
    if (MainWindow::HasUnsavedChanges()) {
        if (s_hStatus && IsWindow(s_hStatus))
            SetWindowTextW(s_hStatus,
                loc(L"test_status_unsaved",
                    L"Project has unsaved changes. Save first (Ctrl+S).").c_str());
        return;
    }

    // Guard: don't start a second build while one is running
    if (s_timerId != 0) return;

    if (!s_csInit) {
        InitializeCriticalSection(&s_cs);
        s_csInit = true;
    }

    // ── Resolve output paths ──────────────────────────────────────────────────
    std::wstring folder, baseName;
    if (s_hFolderEdit && IsWindow(s_hFolderEdit)) {
        wchar_t buf[MAX_PATH] = {};
        GetWindowTextW(s_hFolderEdit, buf, MAX_PATH);
        folder = buf;
    } else {
        SBuildConfig cfg2 = SETT_GetBuildConfig();
        folder = cfg2.testOutputFolder;
    }
    if (s_hFileEdit && IsWindow(s_hFileEdit)) {
        wchar_t buf[512] = {};
        GetWindowTextW(s_hFileEdit, buf, 512);
        baseName = buf;
    } else {
        SBuildConfig cfg2 = SETT_GetBuildConfig();
        baseName = cfg2.testOutputFilename;
    }

    if (folder.empty())   folder   = ComputeDefaultTestFolder();
    if (baseName.empty()) baseName = ComputeDefaultTestFilename();

    // Push defaults back into edit controls if they were blank
    if (s_hFolderEdit && IsWindow(s_hFolderEdit)) {
        wchar_t cur[MAX_PATH] = {};
        GetWindowTextW(s_hFolderEdit, cur, MAX_PATH);
        if (cur[0] == L'\0') SetWindowTextW(s_hFolderEdit, folder.c_str());
    }
    if (s_hFileEdit && IsWindow(s_hFileEdit)) {
        wchar_t cur[512] = {};
        GetWindowTextW(s_hFileEdit, cur, 512);
        if (cur[0] == L'\0') SetWindowTextW(s_hFileEdit, baseName.c_str());
    }

    SETT_SetTestOutputFolder(folder);
    SETT_SetTestOutputFilename(baseName);
    baseName = UniqueFilename(folder, baseName);
    s_lastTestFolder = folder;

    // ── Locate inno/ directory ────────────────────────────────────────────────
    std::wstring innoDir = ISS_FindInnoDir();
    if (innoDir.empty()) {
        if (s_hStatus && IsWindow(s_hStatus))
            SetWindowTextW(s_hStatus,
                L"\u2716  Cannot find inno\\ directory next to SetupCraft.exe");
        return;
    }

    // ── Build thread parameters ───────────────────────────────────────────────
    TestBuildParams* p = new TestBuildParams();
    p->templatePath = innoDir + L"\\template.iss";
    p->outIssPath   = innoDir + L"\\" + MainWindow::GetProjectName() + L"_test.iss";
    p->proj   = MainWindow::GetCurrentProject();
    p->cfg    = SETT_GetBuildConfig();
    p->cfg.outputFolder   = folder;     // override with test paths
    p->cfg.outputFilename = baseName;
    p->langs  = SETT_GetInstallerLanguages();
    p->assocs = FA_GetAssociations();
    if (p->proj.use_components && p->proj.id > 0) {
        p->types = DB::GetInstallTypesForProject(p->proj.id);
        p->comps = MainWindow::GetComponents();
    }

    // ── Populate extra data for ISS generation ───────────────────────────────
    p->extra.shortcuts      = SC_GetShortcutRows();
    p->extra.menuNodes      = SC_GetMenuNodeRows();
    p->extra.desktopOptOut  = SC_GetDesktopOptOut();
    p->extra.smPinOptOut    = SC_GetSmPinOptOut();
    p->extra.tbPinOptOut    = SC_GetTbPinOptOut();
    p->extra.scripts        = SCR_GetScripts();
    p->extra.registryEntries = MainWindow::GetCustomRegistryEntries();

    // ── Reset shared thread state ─────────────────────────────────────────────
    EnterCriticalSection(&s_cs);
    s_threadDone   = false;
    s_threadOk     = false;
    s_threadStep   = L"";
    s_threadLog    = L"";
    s_threadResult = L"";
    LeaveCriticalSection(&s_cs);

    // ── Update UI: running state ──────────────────────────────────────────────
    if (s_hStatus && IsWindow(s_hStatus))
        SetWindowTextW(s_hStatus,
            loc(L"test_status_running", L"Build in progress\u2026").c_str());

    if (s_hProgress && IsWindow(s_hProgress)) {
        LONG sty = GetWindowLongW(s_hProgress, GWL_STYLE);
        SetWindowLongW(s_hProgress, GWL_STYLE, (sty & ~PBS_SMOOTH) | PBS_MARQUEE);
        SendMessageW(s_hProgress, PBM_SETBARCOLOR, 0, (LPARAM)CLR_DEFAULT);
        ShowWindow(s_hProgress, SW_SHOW);
        SendMessageW(s_hProgress, PBM_SETMARQUEE, TRUE, 60);
    }
    if (s_hStepLbl && IsWindow(s_hStepLbl)) {
        SetWindowTextW(s_hStepLbl, L"");
        ShowWindow(s_hStepLbl, SW_SHOW);
    }
    // Clear details window if open
    if (s_hDetailsEdit && IsWindow(s_hDetailsEdit))
        SetWindowTextW(s_hDetailsEdit, L"");

    HWND hRun = GetDlgItem(hwnd, IDC_TEST_RUN_BTN);
    if (hRun) EnableWindow(hRun, FALSE);
    if (s_hDetailsBtn && IsWindow(s_hDetailsBtn))
        ShowWindow(s_hDetailsBtn, SW_HIDE);

    // ── Launch build thread + polling timer ───────────────────────────────────
    s_hwndMain     = hwnd;
    s_hBuildThread = CreateThread(NULL, 0, TEST_BuildThreadProc, p, 0, NULL);
    if (!s_hBuildThread) {
        delete p;
        if (s_hStatus && IsWindow(s_hStatus))
            SetWindowTextW(s_hStatus, L"\u2716  Failed to start build thread");
        if (s_hProgress && IsWindow(s_hProgress)) ShowWindow(s_hProgress, SW_HIDE);
        if (s_hStepLbl  && IsWindow(s_hStepLbl))  ShowWindow(s_hStepLbl,  SW_HIDE);
        if (hRun) EnableWindow(hRun, TRUE);
        return;
    }
    s_timerId = SetTimer(hwnd, 8901, 150, TEST_TimerProc);
}
