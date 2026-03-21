/*
 * vfs_picker.cpp — VFS-backed file/folder picker dialog implementation.
 *
 * See vfs_picker.h for the public API and vfs_picker_API.txt for architecture
 * notes, design decisions, and usage examples.
 *
 * All pixel values go through S() (DPI-aware scale from dpi.h).
 * Root snapshots are accessed via MainWindow::TreeSnapshot_*() static methods
 * and populated on-demand via MainWindow::EnsureTreeSnapshotsFromDb().
 */

#include "vfs_picker.h"
#include "mainwindow.h"   // TreeSnapshot_*, EnsureTreeSnapshotsFromDb
#include "button.h"       // CreateCustomButtonWithIcon, DrawCustomButton
#include "dpi.h"          // S()
#include "tooltip.h"      // ShowMultilingualTooltip, HideTooltip

#include <commctrl.h>
#include <shellapi.h>     // SHGetFileInfoW

// ── Internal control IDs ──────────────────────────────────────────────────────
#define VFSP_TREE   7100
#define VFSP_LIST   7101
#define VFSP_OK     7102
#define VFSP_CANCEL 7103

// ── Internal dialog state (passed via lpCreateParams) ─────────────────────────
struct VfspState {
    const VfsPickerParams* params;
    std::vector<VfsPickerResult>* results;
    bool okClicked = false;
};
// ── ListView tooltip subclass ──────────────────────────────────────────────────────────────
// Per-picker state (only one picker can be open at a time — modal).
// The subclass proc shows the custom tooltip system from tooltip.h on hover.
static WNDPROC s_vfspOrigListProc = NULL;
static HWND    s_vfspDlgHwnd      = NULL; // top-level picker window (tooltip parent)
static int     s_vfspHoveredRow   = -1;   // last row whose tooltip is shown
static bool    s_vfspTracking     = false; // whether TME_LEAVE has been requested

static LRESULT CALLBACK VfspListSubclassProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg) {
    case WM_MOUSEMOVE: {
        // Detect which row the cursor is over (client coords from lParam).
        LVHITTESTINFO ht = {};
        ht.pt = { (short)LOWORD(lParam), (short)HIWORD(lParam) };
        int row = ListView_HitTest(hwnd, &ht);

        if (row != s_vfspHoveredRow) {
            // Row changed — hide previous tooltip and show one for the new row.
            s_vfspHoveredRow = row;
            HideTooltip();
            if (row >= 0) {
                LVITEMW lvi = {}; lvi.mask = LVIF_PARAM; lvi.iItem = row;
                ListView_GetItem(hwnd, &lvi);
                const VirtualFolderFile* vf = (const VirtualFolderFile*)lvi.lParam;
                if (vf && !vf->sourcePath.empty()) {
                    // Position tooltip just below and to the right of the cursor.
                    POINT ptScr; GetCursorPos(&ptScr);
                    std::vector<TooltipEntry> entries = {{ L"", vf->sourcePath }};
                    ShowMultilingualTooltip(entries, ptScr.x + 16, ptScr.y + 16,
                                            s_vfspDlgHwnd);
                }
            }
        }

        // Request WM_MOUSELEAVE when the mouse exits the ListView.
        if (!s_vfspTracking) {
            TRACKMOUSEEVENT tme = { sizeof(tme), TME_LEAVE, hwnd, 0 };
            TrackMouseEvent(&tme);
            s_vfspTracking = true;
        }
        break;
    }
    case WM_MOUSELEAVE:
        HideTooltip();
        s_vfspHoveredRow = -1;
        s_vfspTracking   = false;
        break;
    case WM_NCDESTROY: {
        // Restore original proc before the window is gone.
        WNDPROC orig = s_vfspOrigListProc;
        s_vfspOrigListProc = NULL;
        s_vfspDlgHwnd      = NULL;
        SetWindowLongPtrW(hwnd, GWLP_WNDPROC, (LONG_PTR)orig);
        return orig ? CallWindowProcW(orig, hwnd, msg, wParam, lParam)
                    : DefWindowProcW(hwnd, msg, wParam, lParam);
    }
    }
    return CallWindowProcW(s_vfspOrigListProc, hwnd, msg, wParam, lParam);
}
// ── VfsPicker_IsExecutable ────────────────────────────────────────────────────
bool VfsPicker_IsExecutable(const std::wstring& fileName)
{
    size_t dot = fileName.rfind(L'.');
    if (dot == std::wstring::npos) return false;

    std::wstring ext = fileName.substr(dot);
    for (auto& ch : ext) ch = towlower(ch);

    static const wchar_t* kExts[] = {
        L".exe", L".com", L".bat", L".cmd", L".msi",
        L".ps1", L".vbs", L".js",  L".wsf", L".pif", L".scr"
    };
    for (const wchar_t* e : kExts)
        if (ext == e) return true;
    return false;
}

// ── Vfsp_AddSubtree ───────────────────────────────────────────────────────────
// Recursively inserts snapshot children into the TreeView.
// lParam of each inserted item = const TreeNodeSnapshot* (pointer into static vectors).
// Image indices: 0 = closed folder, 1 = open folder (set up in WM_CREATE).
static void Vfsp_AddSubtree(HWND hTree, HTREEITEM hParent,
                             const std::vector<TreeNodeSnapshot>& nodes)
{
    for (const auto& snap : nodes) {
        TVINSERTSTRUCTW tvis      = {};
        tvis.hParent              = hParent;
        tvis.hInsertAfter         = TVI_LAST;
        tvis.item.mask            = TVIF_TEXT | TVIF_PARAM | TVIF_IMAGE | TVIF_SELECTEDIMAGE;
        tvis.item.pszText         = (LPWSTR)snap.text.c_str();
        tvis.item.lParam          = (LPARAM)&snap;
        tvis.item.iImage          = 0; // closed folder
        tvis.item.iSelectedImage  = 1; // open folder
        HTREEITEM hNew = (HTREEITEM)SendMessageW(hTree, TVM_INSERTITEMW, 0, (LPARAM)&tvis);
        if (hNew && !snap.children.empty()) {
            Vfsp_AddSubtree(hTree, hNew, snap.children);
            if (snap.compExpanded)
                TreeView_Expand(hTree, hNew, TVE_EXPAND);
        }
    }
}

// ── Vfsp_PopulateList ─────────────────────────────────────────────────────────
// Clears the listview and fills it with the virtual files of the given snapshot.
// Applies params->fileFilter if set. No-op when hList is NULL or snap is NULL.
static void Vfsp_PopulateList(HWND hList, const TreeNodeSnapshot* snap,
                               const VfsPickerParams* params)
{
    if (!hList || !snap) return;
    // Hide any active path tooltip and reset hover state before repopulating.
    HideTooltip();
    s_vfspHoveredRow = -1;
    ListView_DeleteAllItems(hList);

    int idx = 0;
    for (const auto& vf : snap->virtualFiles) {
        // Leaf name for display
        std::wstring fname = vf.sourcePath;
        size_t sep = fname.find_last_of(L"\\/");
        if (sep != std::wstring::npos) fname = fname.substr(sep + 1);

        // Apply optional filter
        if (params->fileFilter && !params->fileFilter(fname)) continue;

        // Resolve per-file icon from the system image list
        int imageIdx = -1;
        if (!vf.sourcePath.empty()) {
            SHFILEINFOW sfi = {};
            SHGetFileInfoW(vf.sourcePath.c_str(), FILE_ATTRIBUTE_NORMAL, &sfi, sizeof(sfi),
                SHGFI_SYSICONINDEX | SHGFI_SMALLICON | SHGFI_USEFILEATTRIBUTES);
            imageIdx = sfi.iIcon;
        }

        LVITEMW lvi  = {};
        lvi.mask     = LVIF_TEXT | LVIF_PARAM | LVIF_IMAGE;
        lvi.iItem    = idx++;
        lvi.pszText  = (LPWSTR)fname.c_str();
        lvi.lParam   = (LPARAM)&vf; // pointer into snapshot's virtualFiles (stable)
        lvi.iImage   = imageIdx;
        int row = ListView_InsertItem(hList, &lvi);
        ListView_SetItemText(hList, row, 1, (LPWSTR)vf.sourcePath.c_str());
    }
}

// ── VfspDlgProc ──────────────────────────────────────────────────────────────
static LRESULT CALLBACK VfspDlgProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg) {

    case WM_CREATE: {
        CREATESTRUCTW* cs    = (CREATESTRUCTW*)lParam;
        HINSTANCE      hInst = cs->hInstance;
        VfspState*     pS    = (VfspState*)cs->lpCreateParams;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)pS);
        const VfsPickerParams* p = pS->params;

        RECT rcC; GetClientRect(hwnd, &rcC);
        int W = rcC.right, H = rcC.bottom;

        int pad   = S(10);
        int btnH  = S(36);
        int btnY  = H - pad - btnH;
        int labelH = S(18);
        int ctrlH = btnY - pad * 2 - labelH - pad; // tree/list height

        int treeW, listX, listW;
        if (p->showFilePane) {
            treeW  = S(320);
            listX  = treeW + pad * 2;
            listW  = W - listX - pad;
        } else {
            treeW  = W - pad * 2;
            listX  = 0;
            listW  = 0;
        }

        // "Folders" label
        std::wstring foldLbl = p->foldersLabel.empty()
            ? std::wstring(L"Folders") : p->foldersLabel;
        CreateWindowExW(0, L"STATIC", foldLbl.c_str(),
            WS_CHILD | WS_VISIBLE | SS_LEFT,
            pad, pad, treeW, labelH, hwnd, NULL, hInst, NULL);

        // TreeView
        HWND hTree = CreateWindowExW(WS_EX_CLIENTEDGE, WC_TREEVIEWW, L"",
            WS_CHILD | WS_VISIBLE | TVS_HASLINES | TVS_LINESATROOT |
                TVS_HASBUTTONS | TVS_SHOWSELALWAYS,
            pad, pad + labelH, treeW, ctrlH,
            hwnd, (HMENU)VFSP_TREE, hInst, NULL);

        // TreeView image list: index 0 = closed folder (shell32 #4), 1 = open (#5)
        // Use 32×32 icons with a 4-px left-pad in a 36-wide slot — same technique
        // as the Files page — so the icon clears the expand button.
        {
            HIMAGELIST hIL = ImageList_Create(S(36), S(32), ILC_COLOR32 | ILC_MASK, 2, 0);
            if (hIL) {
                auto AddPadded = [&](HICON hIco) {
                    if (!hIco) return;
                    HDC hScreen = GetDC(NULL);
                    BITMAPINFO bmi = {};
                    bmi.bmiHeader.biSize        = sizeof(BITMAPINFOHEADER);
                    bmi.bmiHeader.biWidth       = S(36);
                    bmi.bmiHeader.biHeight      = -S(32);
                    bmi.bmiHeader.biPlanes      = 1;
                    bmi.bmiHeader.biBitCount    = 32;
                    bmi.bmiHeader.biCompression = BI_RGB;
                    void* pBits = nullptr;
                    HBITMAP hPad = CreateDIBSection(hScreen, &bmi, DIB_RGB_COLORS, &pBits, NULL, 0);
                    if (hPad && pBits) {
                        memset(pBits, 0, S(36) * S(32) * 4);
                        HDC hMem = CreateCompatibleDC(hScreen);
                        HBITMAP hOld = (HBITMAP)SelectObject(hMem, hPad);
                        DrawIconEx(hMem, S(4), 0, hIco, S(32), S(32), 0, NULL, DI_NORMAL);
                        SelectObject(hMem, hOld);
                        DeleteDC(hMem);
                        ImageList_Add(hIL, hPad, NULL);
                        DeleteObject(hPad);
                    }
                    ReleaseDC(NULL, hScreen);
                };
                HICON hClosed = (HICON)LoadImageW(LoadLibraryW(L"shell32.dll"),
                    MAKEINTRESOURCEW(4), IMAGE_ICON, S(32), S(32), 0);
                HICON hOpen   = (HICON)LoadImageW(LoadLibraryW(L"shell32.dll"),
                    MAKEINTRESOURCEW(5), IMAGE_ICON, S(32), S(32), 0);
                AddPadded(hClosed);
                AddPadded(hOpen);
                if (hClosed) DestroyIcon(hClosed);
                if (hOpen)   DestroyIcon(hOpen);
                TreeView_SetImageList(hTree, hIL, TVSIL_NORMAL);
                SetPropW(hwnd, L"VfspTreeIL", (HANDLE)hIL);
            }
        }

        if (p->showFilePane) {
            // "Files in selected folder" label
            std::wstring fileLbl = p->filesLabel.empty()
                ? std::wstring(L"Files in selected folder") : p->filesLabel;
            CreateWindowExW(0, L"STATIC", fileLbl.c_str(),
                WS_CHILD | WS_VISIBLE | SS_LEFT,
                listX, pad, listW, labelH, hwnd, NULL, hInst, NULL);

            // ListView
            DWORD lvStyle = LVS_REPORT | LVS_SHOWSELALWAYS;
            if (p->singleSelect) lvStyle |= LVS_SINGLESEL;
            HWND hList = CreateWindowExW(WS_EX_CLIENTEDGE, WC_LISTVIEWW, L"",
                WS_CHILD | WS_VISIBLE | lvStyle,
                listX, pad + labelH, listW, ctrlH,
                hwnd, (HMENU)VFSP_LIST, hInst, NULL);
            ListView_SetExtendedListViewStyle(hList,
                LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES);

            // Attach system small-icon image list so each file row shows its icon.
            SHFILEINFOW sfiIL = {};
            HIMAGELIST hSysIL = (HIMAGELIST)SHGetFileInfoW(L"C:\\", 0, &sfiIL, sizeof(sfiIL),
                SHGFI_SYSICONINDEX | SHGFI_SMALLICON);
            if (hSysIL)
                ListView_SetImageList(hList, hSysIL, LVSIL_SMALL);

            // Subclass the ListView to drive the custom path tooltip on hover.
            s_vfspDlgHwnd    = hwnd;
            s_vfspHoveredRow = -1;
            s_vfspTracking   = false;
            s_vfspOrigListProc = (WNDPROC)SetWindowLongPtrW(
                hList, GWLP_WNDPROC, (LONG_PTR)VfspListSubclassProc);

            std::wstring colName = p->colFileName.empty() ? std::wstring(L"Name") : p->colFileName;
            std::wstring colPath = p->colFilePath.empty() ? std::wstring(L"Path") : p->colFilePath;
            LVCOLUMNW col = {};
            col.mask    = LVCF_TEXT | LVCF_WIDTH;
            col.cx      = S(180);
            col.pszText = (LPWSTR)colName.c_str();
            ListView_InsertColumn(hList, 0, &col);
            col.cx      = listW - S(190);
            col.pszText = (LPWSTR)colPath.c_str();
            ListView_InsertColumn(hList, 1, &col);
        }

        // OK / Cancel buttons
        std::wstring okTxt = p->okText.empty() ? std::wstring(L"OK") : p->okText;
        std::wstring cnTxt = p->cancelText.empty() ? std::wstring(L"Cancel") : p->cancelText;
        int wOK_v  = MeasureButtonWidth(okTxt, true);
        int wCnl_v = MeasureButtonWidth(cnTxt, true);
        int btnGap = S(10);
        int totalBW = wOK_v + btnGap + wCnl_v;
        int btnX1 = (W - totalBW) / 2;
        int btnX2 = btnX1 + wOK_v + btnGap;
        CreateCustomButtonWithIcon(hwnd, VFSP_OK,
            okTxt.c_str(), ButtonColor::Green,
            L"imageres.dll", 89, btnX1, btnY, wOK_v, btnH, hInst);
        CreateCustomButtonWithIcon(hwnd, VFSP_CANCEL,
            cnTxt.c_str(), ButtonColor::Red,
            L"shell32.dll", 131, btnX2, btnY, wCnl_v, btnH, hInst);

        // Apply system font (scaled ×1.2 like the rest of the app dialogs)
        {
            NONCLIENTMETRICSW ncm = {}; ncm.cbSize = sizeof(ncm);
            SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(ncm), &ncm, 0);
            if (ncm.lfMessageFont.lfHeight < 0)
                ncm.lfMessageFont.lfHeight = (LONG)(ncm.lfMessageFont.lfHeight * 1.2f);
            ncm.lfMessageFont.lfQuality = CLEARTYPE_QUALITY;
            HFONT hF = CreateFontIndirectW(&ncm.lfMessageFont);
            if (hF) {
                EnumChildWindows(hwnd, [](HWND hC, LPARAM lp) -> BOOL {
                    SendMessageW(hC, WM_SETFONT, (WPARAM)(HFONT)lp, TRUE);
                    return TRUE;
                }, (LPARAM)hF);
                SetPropW(hwnd, L"VfspFont", (HANDLE)hF);
            }
        }

        // Populate tree from VFS snapshots
        MainWindow::EnsureTreeSnapshotsFromDb();

        auto addRoot = [&](const std::wstring& label,
                           const std::vector<TreeNodeSnapshot>& snaps)
        {
            if (snaps.empty()) return;
            TVINSERTSTRUCTW tvis  = {};
            tvis.hParent          = TVI_ROOT;
            tvis.hInsertAfter     = TVI_LAST;
            tvis.item.mask        = TVIF_TEXT | TVIF_PARAM;
            tvis.item.pszText     = (LPWSTR)label.c_str();
            tvis.item.lParam      = 0; // section root — not directly pickable
            HTREEITEM hR = (HTREEITEM)SendMessageW(hTree, TVM_INSERTITEMW, 0, (LPARAM)&tvis);
            if (hR) {
                Vfsp_AddSubtree(hTree, hR, snaps);
                TreeView_Expand(hTree, hR, TVE_EXPAND);
            }
        };

        std::wstring lbl_pf  = p->rootLabel_ProgramFiles.empty()  ? L"Program Files"    : p->rootLabel_ProgramFiles;
        std::wstring lbl_pd  = p->rootLabel_ProgramData.empty()   ? L"ProgramData"       : p->rootLabel_ProgramData;
        std::wstring lbl_ad  = p->rootLabel_AppData.empty()       ? L"AppData (Roaming)" : p->rootLabel_AppData;
        std::wstring lbl_ai  = p->rootLabel_AskAtInstall.empty()  ? L"Ask at install"    : p->rootLabel_AskAtInstall;

        addRoot(lbl_pf, MainWindow::TreeSnapshot_ProgramFiles());
        addRoot(lbl_pd, MainWindow::TreeSnapshot_ProgramData());
        addRoot(lbl_ad, MainWindow::TreeSnapshot_AppData());
        addRoot(lbl_ai, MainWindow::TreeSnapshot_AskAtInstall());

        return 0;
    }

    case WM_NOTIFY: {
        NMHDR* nmhdr = (NMHDR*)lParam;
        if (nmhdr->idFrom == VFSP_TREE && nmhdr->code == TVN_SELCHANGED) {
            HWND hList = GetDlgItem(hwnd, VFSP_LIST);
            if (hList) {
                LPNMTREEVIEWW pnmtv = (LPNMTREEVIEWW)lParam;
                const TreeNodeSnapshot* snap =
                    (const TreeNodeSnapshot*)pnmtv->itemNew.lParam;
                VfspState* pS = (VfspState*)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
                Vfsp_PopulateList(hList, snap, pS->params);
            }
        }
        // Double-clicking a file in the list commits the selection (same as OK).
        if (nmhdr->idFrom == VFSP_LIST && nmhdr->code == NM_DBLCLK) {
            HWND hList = GetDlgItem(hwnd, VFSP_LIST);
            if (hList && ListView_GetNextItem(hList, -1, LVNI_SELECTED) >= 0)
                SendMessageW(hwnd, WM_COMMAND, MAKEWPARAM(VFSP_OK, 0), 0);
            return 0;
        }
        return 0;
    }

    case WM_COMMAND: {
        int wmId  = LOWORD(wParam);
        VfspState* pS = (VfspState*)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
        if (!pS) break;
        const VfsPickerParams* p = pS->params;

        if (wmId == VFSP_CANCEL || wmId == IDCANCEL) {
            pS->okClicked = false;
            DestroyWindow(hwnd);
            return 0;
        }

        if (wmId == VFSP_OK) {
            HWND hList = GetDlgItem(hwnd, VFSP_LIST);
            HWND hTree = GetDlgItem(hwnd, VFSP_TREE);

            // Collect file selections from the listview (if shown)
            if (hList) {
                int lvSel = -1;
                while ((lvSel = ListView_GetNextItem(hList, lvSel, LVNI_SELECTED)) != -1) {
                    LVITEMW lvi = {}; lvi.mask = LVIF_PARAM; lvi.iItem = lvSel;
                    ListView_GetItem(hList, &lvi);
                    const VirtualFolderFile* vf = (const VirtualFolderFile*)lvi.lParam;
                    if (!vf) continue;
                    std::wstring fname = vf->sourcePath;
                    size_t sep = fname.find_last_of(L"\\/");
                    if (sep != std::wstring::npos) fname = fname.substr(sep + 1);
                    VfsPickerResult r;
                    r.sourcePath  = vf->sourcePath;
                    r.displayName = fname;
                    pS->results->push_back(r);
                }
            }

            // If nothing from listview, try tree selection as folder (if allowed)
            if (pS->results->empty() && p->allowFolderPick && hTree) {
                HTREEITEM hSel = TreeView_GetSelection(hTree);
                if (hSel) {
                    TVITEMW tvi      = {};
                    tvi.hItem        = hSel;
                    tvi.mask         = TVIF_PARAM | TVIF_TEXT;
                    wchar_t buf[260] = {};
                    tvi.pszText      = buf;
                    tvi.cchTextMax   = 260;
                    TreeView_GetItem(hTree, &tvi);
                    const TreeNodeSnapshot* snap = (const TreeNodeSnapshot*)tvi.lParam;
                    if (snap && !snap->fullPath.empty()) {
                        VfsPickerResult r;
                        r.sourcePath  = snap->fullPath;
                        r.displayName = buf;
                        pS->results->push_back(r);
                    }
                }
            }

            if (pS->results->empty()) {
                std::wstring noSel = p->noSelMessage.empty()
                    ? std::wstring(L"Please select an item.") : p->noSelMessage;
                MessageBoxW(hwnd, noSel.c_str(), L"", MB_OK | MB_ICONINFORMATION);
                return 0;
            }

            pS->okClicked = true;
            DestroyWindow(hwnd);
            return 0;
        }
        break;
    }

    case WM_DRAWITEM: {
        LPDRAWITEMSTRUCT dis = (LPDRAWITEMSTRUCT)lParam;
        if (dis->CtlID == VFSP_OK || dis->CtlID == VFSP_CANCEL) {
            ButtonColor color = (ButtonColor)GetWindowLongPtr(dis->hwndItem, GWLP_USERDATA);
            NONCLIENTMETRICSW ncm = {}; ncm.cbSize = sizeof(ncm);
            SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(ncm), &ncm, 0);
            if (ncm.lfMessageFont.lfHeight < 0)
                ncm.lfMessageFont.lfHeight = (LONG)(ncm.lfMessageFont.lfHeight * 1.2f);
            ncm.lfMessageFont.lfWeight  = FW_BOLD;
            ncm.lfMessageFont.lfQuality = CLEARTYPE_QUALITY;
            HFONT hFont = CreateFontIndirectW(&ncm.lfMessageFont);
            LRESULT res = DrawCustomButton(dis, color, hFont);
            if (hFont) DeleteObject(hFont);
            return res;
        }
        break;
    }

    case WM_CTLCOLORSTATIC:
    case WM_CTLCOLORDLG: {
        HDC hdc = (HDC)wParam;
        SetBkColor(hdc, GetSysColor(COLOR_BTNFACE));
        SetTextColor(hdc, GetSysColor(COLOR_BTNTEXT));
        return (LRESULT)GetSysColorBrush(COLOR_BTNFACE);
    }

    case WM_DESTROY: {
        HideTooltip();
        HFONT hF = (HFONT)GetPropW(hwnd, L"VfspFont");
        if (hF) { DeleteObject(hF); RemovePropW(hwnd, L"VfspFont"); }
        HIMAGELIST hIL = (HIMAGELIST)GetPropW(hwnd, L"VfspTreeIL");
        if (hIL) { ImageList_Destroy(hIL); RemovePropW(hwnd, L"VfspTreeIL"); }
        break;
    }
    case WM_CLOSE:
        DestroyWindow(hwnd);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

// ── ShowVfsPicker ─────────────────────────────────────────────────────────────
bool ShowVfsPicker(
    HWND                                        hwndParent,
    HINSTANCE                                   hInst,
    const VfsPickerParams&                      params,
    const std::map<std::wstring, std::wstring>& /*locale*/,
    std::vector<VfsPickerResult>&               results)
{
    results.clear();

    // Register window class once per process.
    static bool s_classRegistered = false;
    if (!s_classRegistered) {
        WNDCLASSEXW wc  = {};
        wc.cbSize        = sizeof(wc);
        wc.lpfnWndProc   = VfspDlgProc;
        wc.hInstance     = hInst;
        wc.lpszClassName = L"VfsPickerDlgClass";
        wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
        wc.hCursor       = LoadCursorW(NULL, IDC_ARROW);
        RegisterClassExW(&wc);
        s_classRegistered = true;
    }

    // Compute window size: content area + non-client decorations.
    int clientW = params.showFilePane ? S(960) : S(460);
    int clientH = S(620);
    RECT rcAdj  = { 0, 0, clientW, clientH };
    AdjustWindowRectEx(&rcAdj, WS_POPUP | WS_CAPTION | WS_SYSMENU, FALSE,
                       WS_EX_DLGMODALFRAME);
    int wW = rcAdj.right  - rcAdj.left;
    int wH = rcAdj.bottom - rcAdj.top;

    // Centre over parent (or screen if no parent).
    int wx = 0, wy = 0;
    if (hwndParent) {
        RECT rcP; GetWindowRect(hwndParent, &rcP);
        wx = rcP.left + ((rcP.right  - rcP.left) - wW) / 2;
        wy = rcP.top  + ((rcP.bottom - rcP.top)  - wH) / 2;
    } else {
        wx = (GetSystemMetrics(SM_CXSCREEN) - wW) / 2;
        wy = (GetSystemMetrics(SM_CYSCREEN) - wH) / 2;
    }

    VfspState state;
    state.params   = &params;
    state.results  = &results;
    state.okClicked = false;

    std::wstring title = params.title;
    HWND hDlg = CreateWindowExW(
        WS_EX_DLGMODALFRAME,
        L"VfsPickerDlgClass",
        title.c_str(),
        WS_POPUP | WS_CAPTION | WS_SYSMENU,
        wx, wy, wW, wH,
        hwndParent, NULL, hInst, &state);

    if (!hDlg) return false;

    if (hwndParent) EnableWindow(hwndParent, FALSE);
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

    if (hwndParent) { EnableWindow(hwndParent, TRUE); SetForegroundWindow(hwndParent); }
    return state.okClicked;
}
