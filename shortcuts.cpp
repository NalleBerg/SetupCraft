/*
 * shortcuts.cpp — Shortcuts page implementation for SetupCraft.
 *
 * All Shortcuts-page state lives here as file-scope statics.
 * mainwindow.cpp routes WM_NOTIFY, WM_COMMAND, and WM_CONTEXTMENU here via
 * the public functions declared in shortcuts.h.
 *
 * Layout rules:  all pixel values through S(),  all strings through locale.
 * Persistence:   in-memory only; written to DB on IDM_FILE_SAVE.
 */

#include "shortcuts.h"
#include "mainwindow.h"    // MainWindow::MarkAsModified(), GetLocale()
#include "dpi.h"           // S() DPI-scale macro
#include "button.h"        // CreateCustomButtonWithIcon, SetButtonTooltip
#include "checkbox.h"      // CreateCustomCheckbox
#include <functional>      // std::function (recursive lambda)
#include <algorithm>       // std::remove_if

// ── Module-private state ──────────────────────────────────────────────────────

// "Allow user to opt out of the desktop shortcut at install time" toggle.
static bool s_scDesktopOptOut = false;

// Live handle to the Start Menu / Programs folder TreeView.
// Set by SC_BuildPage; cleared (window destroyed) by SC_TearDown.
static HWND s_hScStartMenuTree = NULL;

// In-memory folder tree.  Persists across page switches; cleared by SC_Reset().
static std::vector<ScMenuNode> s_scMenuNodes;
static int                     s_scNextMenuId = 2;  // 0 = Start Menu root, 1 = Programs

// In-memory shortcut definitions.  Populated by the shortcut-config dialog
// (implemented in a later session).  Cleared by SC_Reset().
static std::vector<ShortcutDef> s_scShortcuts;
static int                      s_scNextShortcutId = 1;

// ── SC_Reset ──────────────────────────────────────────────────────────────────

void SC_Reset()
{
    s_scDesktopOptOut  = false;
    s_scMenuNodes.clear();
    s_scNextMenuId     = 2;
    s_scShortcuts.clear();
    s_scNextShortcutId = 1;
}

// ── SC_BuildPage ──────────────────────────────────────────────────────────────

void SC_BuildPage(HWND hwnd, HINSTANCE hInst, int pageY, int clientWidth,
                  HFONT hPageTitleFont, HFONT hGuiFont,
                  const std::map<std::wstring, std::wstring>& locale)
{
    // Handy locale lookup with English fallback.
    auto loc = [&](const wchar_t* key, const wchar_t* fallback) -> std::wstring {
        auto it = locale.find(key);
        return (it != locale.end()) ? it->second : fallback;
    };

    // ── Page title ────────────────────────────────────────────────────────────
    std::wstring scTitle = loc(L"sc_page_title", L"Shortcuts");
    HWND hScTitle = CreateWindowExW(0, L"STATIC", scTitle.c_str(),
        WS_CHILD | WS_VISIBLE | SS_LEFT,
        S(20), pageY + S(15), clientWidth - S(40), S(38),
        hwnd, (HMENU)5300, hInst, NULL);
    if (hPageTitleFont) SendMessageW(hScTitle, WM_SETFONT, (WPARAM)hPageTitleFont, TRUE);

    // ── Hint label ────────────────────────────────────────────────────────────
    std::wstring scHint = loc(L"sc_page_hint",
        L"Click a shortcut type below to configure it.");
    HWND hScHint = CreateWindowExW(0, L"STATIC", scHint.c_str(),
        WS_CHILD | WS_VISIBLE | SS_LEFT,
        S(20), pageY + S(56), clientWidth - S(40), S(22),
        hwnd, NULL, hInst, NULL);
    if (hGuiFont) SendMessageW(hScHint, WM_SETFONT, (WPARAM)hGuiFont, TRUE);

    // ── Separator below hint ──────────────────────────────────────────────────
    CreateWindowExW(0, L"STATIC", L"",
        WS_CHILD | WS_VISIBLE | SS_ETCHEDHORZ,
        S(20), pageY + S(84), clientWidth - S(40), S(2),
        hwnd, NULL, hInst, NULL);

    // ── Layout constants ──────────────────────────────────────────────────────
    // Each shortcut type is a wide button (icon + label).  Width fills the
    // content area; height gives enough room for the 32 px icon + padding.
    const int btnX = S(20);
    const int btnW = clientWidth - S(40);
    const int btnH = S(48);
    const int gap  = S(10);
    int rowY = pageY + S(96);

    // Build system-directory paths once.
    wchar_t shell32Path[MAX_PATH];
    GetSystemDirectoryW(shell32Path, MAX_PATH);
    wcscat_s(shell32Path, L"\\shell32.dll");

    wchar_t imageresPath[MAX_PATH];
    GetSystemDirectoryW(imageresPath, MAX_PATH);
    wcscat_s(imageresPath, L"\\imageres.dll");

    // ── Row 1 — Desktop shortcut ──────────────────────────────────────────────
    // Icon: imageres.dll index 105 = desktop/monitor icon (verified in IconViewer).
    std::wstring scDesktop = loc(L"sc_desktop", L"Desktop");
    HWND hDesktopBtn = CreateCustomButtonWithIcon(
        hwnd, IDC_SC_DESKTOP_BTN, scDesktop.c_str(), ButtonColor::Blue,
        imageresPath, 105,
        btnX, rowY, btnW, btnH, hInst);
    {
        std::wstring tt = loc(L"sc_desktop_tooltip",
            L"Create a shortcut on the user's Desktop");
        SetButtonTooltip(hDesktopBtn, tt.c_str());
    }
    rowY += btnH + gap;

    // ── Desktop opt-out checkbox ──────────────────────────────────────────────
    // Indented to show it belongs to the Desktop row above it.
    std::wstring scOptOut = loc(L"sc_desktop_opt_out",
        L"Allow the end user to opt out of the desktop shortcut at install time");
    HWND hOptOut = CreateCustomCheckbox(
        hwnd, IDC_SC_DESKTOP_OPT, scOptOut,
        s_scDesktopOptOut,
        S(20) + S(20), rowY, btnW - S(20), S(28), hInst);
    (void)hOptOut;  // handle stored in window; state read via BM_GETCHECK later
    rowY += S(28) + S(6);

    // ── Section divider ───────────────────────────────────────────────────────
    CreateWindowExW(0, L"STATIC", L"",
        WS_CHILD | WS_VISIBLE | SS_ETCHEDHORZ,
        S(20), rowY, clientWidth - S(40), S(2),
        hwnd, NULL, hInst, NULL);
    rowY += S(2) + gap;

    // ── Start Menu & Programs — TreeView ─────────────────────────────────────
    // The developer builds the folder hierarchy here.  Clicking a node will open
    // the shortcut-config dialog (implemented in a later session).  User-added
    // subfolders can be renamed by double-clicking (TVS_EDITLABELS) and removed
    // with the Remove button.  The root and Programs nodes are fixed.

    // Box is ~17% of dialog width (half of previous 35%), centred.
    const int treeW = clientWidth * 35 / 200;
    const int treeX = (clientWidth - treeW) / 2;

    // Section header — centred + bold (hPageTitleFont).
    // SS_NOPREFIX prevents the '&' in "Start Menu & Programs" being consumed
    // as an accelerator-key prefix and rendered as a missing character.
    // ID 5301 lets WM_CTLCOLORSTATIC apply the bold page-title font to the DC.
    std::wstring scSmLabel = loc(L"sc_sm_section_label", L"Start Menu & Programs");
    HWND hSmLabel = CreateWindowExW(0, L"STATIC", scSmLabel.c_str(),
        WS_CHILD | WS_VISIBLE | SS_CENTER | SS_NOPREFIX,
        treeX, rowY, treeW, S(22),
        hwnd, (HMENU)5301, hInst, NULL);
    if (hPageTitleFont) SendMessageW(hSmLabel, WM_SETFONT, (WPARAM)hPageTitleFont, TRUE);
    rowY += S(22) + S(6);

    // TreeView — folder icons: index 0 = closed (shell32 #3), 1 = open (shell32 #5).
    // No TVS_FULLROWSELECT — highlight stays tight around icon+text, like the Files page.
    // Item height set to S(34) so 32×32 icons have a little breathing room.
    int smTreeH = S(160);
    s_hScStartMenuTree = CreateWindowExW(WS_EX_CLIENTEDGE, WC_TREEVIEWW, L"",
        WS_CHILD | WS_VISIBLE | TVS_HASLINES | TVS_LINESATROOT |
        TVS_HASBUTTONS | TVS_SHOWSELALWAYS | TVS_EDITLABELS,
        treeX, rowY, treeW, smTreeH,
        hwnd, (HMENU)IDC_SC_SM_TREE, hInst, NULL);
    {
        // Destroy any previous image list left over from a prior page visit.
        HIMAGELIST hOldIL = (HIMAGELIST)GetPropW(hwnd, L"hScSmTreeIL");
        if (hOldIL) { ImageList_Destroy(hOldIL); RemovePropW(hwnd, L"hScSmTreeIL"); }

        // 32×32 large icons, matching the project-wide icon size convention.
        HIMAGELIST hSmIL = ImageList_Create(32, 32, ILC_COLOR32 | ILC_MASK, 2, 2);
        if (hSmIL) {
            HICON hClose = NULL, hOpen = NULL;
            ExtractIconExW(shell32Path, 3, &hClose, NULL, 1);  // large closed yellow folder
            ExtractIconExW(shell32Path, 5, &hOpen,  NULL, 1);  // large open yellow folder
            if (hClose) { ImageList_AddIcon(hSmIL, hClose); DestroyIcon(hClose); }
            if (hOpen)  { ImageList_AddIcon(hSmIL, hOpen);  DestroyIcon(hOpen);  }
            TreeView_SetImageList(s_hScStartMenuTree, hSmIL, TVSIL_NORMAL);
            // No custom item height — Windows auto-sizes rows to fit icon+font,
            // giving the same compact highlight as the Files page.
            SetPropW(hwnd, L"hScSmTreeIL", (HANDLE)hSmIL);
        }
    }
    // Apply the same scaled GUI font used on the Files page so text weight and
    // size are consistent across all TreeViews in the app.
    if (hGuiFont) SendMessageW(s_hScStartMenuTree, WM_SETFONT, (WPARAM)hGuiFont, TRUE);

    // Seed default nodes on the very first visit for this project.
    if (s_scMenuNodes.empty()) {
        std::wstring rootName = loc(L"sc_startmenu", L"Start Menu");
        std::wstring progName = loc(L"sc_programs",  L"Programs");
        s_scMenuNodes.push_back({0, -1, rootName, nullptr});
        s_scMenuNodes.push_back({1,  0, progName, nullptr});
    }

    // Re-populate the TreeView from the in-memory node list each page visit,
    // recursing parent → children so the hierarchy is rebuilt correctly.
    {
        std::function<void(HTREEITEM, int)> addChildren =
            [&](HTREEITEM hParent, int parentId) {
            for (auto& node : s_scMenuNodes) {
                if (node.parentId != parentId) continue;
                TVINSERTSTRUCTW tvis     = {};
                tvis.hParent             = hParent;
                tvis.hInsertAfter        = TVI_LAST;
                tvis.item.mask           = TVIF_TEXT | TVIF_IMAGE | TVIF_SELECTEDIMAGE | TVIF_PARAM;
                tvis.item.pszText        = (LPWSTR)node.name.c_str();
                tvis.item.iImage         = 0;  // closed folder
                tvis.item.iSelectedImage = 1;  // open folder
                tvis.item.lParam         = (LPARAM)node.id;
                node.hItem = (HTREEITEM)SendMessageW(
                    s_hScStartMenuTree, TVM_INSERTITEM, 0, (LPARAM)&tvis);
                if (node.hItem)
                    addChildren(node.hItem, node.id);
            }
        };
        addChildren(TVI_ROOT, -1);
    }

    // Expand the root so Programs is immediately visible.
    HTREEITEM hSmRoot = TreeView_GetRoot(s_hScStartMenuTree);
    if (hSmRoot) {
        TreeView_Expand(s_hScStartMenuTree, hSmRoot, TVE_EXPAND);
        TreeView_SelectItem(s_hScStartMenuTree, hSmRoot);
    }
    rowY += smTreeH + S(6);

    // Action buttons: Add Subfolder (blue) and Remove (red).
    // Centred under the tree; same icons as the Files page Add Folder / Remove.
    // Remove is disabled on entry; TVN_SELCHANGED enables it for non-fixed nodes.
    const int addW  = S(160), remW = S(110), btnGap = S(6);
    const int bRowX = treeX + (treeW - addW - btnGap - remW) / 2;
    std::wstring scSmAdd = loc(L"sc_sm_add", L"Add Subfolder");
    HWND hSmAddBtn = CreateCustomButtonWithIcon(
        hwnd, IDC_SC_SM_ADD, scSmAdd.c_str(), ButtonColor::Blue,
        L"shell32.dll", 296,        // DrawCustomButton prepends the system directory path
        bRowX, rowY, addW, S(34), hInst);
    {
        std::wstring tt = loc(L"sc_sm_add_tooltip",
            L"Add a subfolder under the selected folder");
        SetButtonTooltip(hSmAddBtn, tt.c_str());
    }
    std::wstring scSmRem = loc(L"sc_sm_remove", L"Remove");
    HWND hSmRemBtn = CreateCustomButtonWithIcon(
        hwnd, IDC_SC_SM_REMOVE, scSmRem.c_str(), ButtonColor::Red,
        L"shell32.dll", 234,        // DrawCustomButton prepends the system directory path
        bRowX + addW + btnGap, rowY, remW, S(34), hInst);
    {
        std::wstring tt = loc(L"sc_sm_remove_tooltip",
            L"Remove the selected subfolder from the Start Menu structure");
        SetButtonTooltip(hSmRemBtn, tt.c_str());
    }
    EnableWindow(hSmRemBtn, FALSE);  // TVN_SELCHANGED re-enables for removable nodes
    rowY += S(34) + S(10);

    // ── Section divider ───────────────────────────────────────────────────────
    CreateWindowExW(0, L"STATIC", L"",
        WS_CHILD | WS_VISIBLE | SS_ETCHEDHORZ,
        S(20), rowY, clientWidth - S(40), S(2),
        hwnd, NULL, hInst, NULL);
    rowY += S(2) + gap;

    // ── Row 4 — Pin to Start ──────────────────────────────────────────────────
    // Icon: shell32.dll index 264 = pin/pushpin icon (verified in IconViewer).
    std::wstring scPinStart = loc(L"sc_pin_start", L"Pin to Start");
    HWND hPinStartBtn = CreateCustomButtonWithIcon(
        hwnd, IDC_SC_PINSTART_BTN, scPinStart.c_str(), ButtonColor::Blue,
        shell32Path, 264,
        btnX, rowY, btnW, btnH, hInst);
    {
        std::wstring tt = loc(L"sc_pin_start_tooltip",
            L"Pin the application to the Windows Start screen");
        SetButtonTooltip(hPinStartBtn, tt.c_str());
    }
    rowY += btnH + gap;

    // ── Row 5 — Pin to Taskbar ────────────────────────────────────────────────
    // Icon: shell32.dll index 264 (same pin icon as Pin to Start).
    std::wstring scPinTaskbar = loc(L"sc_pin_taskbar", L"Pin to Taskbar");
    HWND hPinTaskbarBtn = CreateCustomButtonWithIcon(
        hwnd, IDC_SC_PINTASKBAR_BTN, scPinTaskbar.c_str(), ButtonColor::Blue,
        shell32Path, 264,
        btnX, rowY, btnW, btnH, hInst);
    {
        std::wstring tt = loc(L"sc_pin_taskbar_tooltip",
            L"Pin the application to the Windows Taskbar");
        SetButtonTooltip(hPinTaskbarBtn, tt.c_str());
    }
    (void)rowY;  // no more rows after this
}

// ── SC_TearDown ───────────────────────────────────────────────────────────────

void SC_TearDown(HWND hwnd)
{
    // Destroy the live TreeView window.
    if (s_hScStartMenuTree && IsWindow(s_hScStartMenuTree)) {
        DestroyWindow(s_hScStartMenuTree);
    }
    s_hScStartMenuTree = NULL;

    // Clear transient HTREEITEM handles; node names and structure persist.
    for (auto& n : s_scMenuNodes) n.hItem = nullptr;

    // Destroy the image list stored as a window property on the main window.
    HIMAGELIST hOldIL = (HIMAGELIST)GetPropW(hwnd, L"hScSmTreeIL");
    if (hOldIL) { ImageList_Destroy(hOldIL); RemovePropW(hwnd, L"hScSmTreeIL"); }
}

// ── SC_OnNotify ───────────────────────────────────────────────────────────────

LRESULT SC_OnNotify(HWND hwnd, LPNMHDR nmhdr, bool* handled)
{
    *handled = false;

    // Only handle notifications from the Start Menu TreeView.
    if (nmhdr->idFrom != IDC_SC_SM_TREE)
        return 0;

    // Block label editing on the fixed "Start Menu" root node (id == 0).
    // Returning TRUE from TVN_BEGINLABELEDIT cancels the in-place label edit.
    if (nmhdr->code == TVN_BEGINLABELEDIT) {
        LPNMTVDISPINFO ptvdi = (LPNMTVDISPINFO)nmhdr;
        TVITEMW tvi = {}; tvi.mask = TVIF_PARAM; tvi.hItem = ptvdi->item.hItem;
        if (s_hScStartMenuTree) TreeView_GetItem(s_hScStartMenuTree, &tvi);
        *handled = true;
        return (tvi.lParam == 0) ? TRUE : FALSE;  // TRUE = cancel edit for root
    }

    // Persist the renamed label into s_scMenuNodes on label-edit commit.
    // Returning TRUE accepts the new label; FALSE rejects it.
    if (nmhdr->code == TVN_ENDLABELEDIT) {
        LPNMTVDISPINFO ptvdi = (LPNMTVDISPINFO)nmhdr;
        if (!ptvdi->item.pszText || ptvdi->item.pszText[0] == L'\0') {
            *handled = true;
            return FALSE;  // reject empty names
        }
        int nodeId = (int)ptvdi->item.lParam;
        for (auto& node : s_scMenuNodes) {
            if (node.id == nodeId) {
                node.name = ptvdi->item.pszText;
                MainWindow::MarkAsModified();
                break;
            }
        }
        *handled = true;
        return TRUE;  // accept the new label
    }

    // Enable/disable the Remove button based on which node is selected.
    // Nodes 0 (Start Menu root) and 1 (Programs) are fixed and cannot be removed.
    if (nmhdr->code == TVN_SELCHANGED) {
        HWND hRemBtn = GetDlgItem(hwnd, IDC_SC_SM_REMOVE);
        if (hRemBtn && s_hScStartMenuTree) {
            HTREEITEM hSel = TreeView_GetSelection(s_hScStartMenuTree);
            BOOL canRemove = FALSE;
            if (hSel) {
                TVITEMW tvi = {}; tvi.mask = TVIF_PARAM; tvi.hItem = hSel;
                TreeView_GetItem(s_hScStartMenuTree, &tvi);
                canRemove = (tvi.lParam > 1) ? TRUE : FALSE;
            }
            EnableWindow(hRemBtn, canRemove);
        }
        *handled = true;
        return 0;
    }

    return 0;
}

// ── SC_OnCommand ──────────────────────────────────────────────────────────────

bool SC_OnCommand(HWND hwnd, int id)
{
    switch (id) {

    // ── Desktop shortcut button ───────────────────────────────────────────────
    case IDC_SC_DESKTOP_BTN:
        // TODO: open Desktop shortcut config dialog (implemented in a later session).
        return true;

    // ── Start Menu / Programs buttons (reserved, not yet standalone) ──────────
    case IDC_SC_STARTMENU_BTN:
        // TODO: open Start Menu shortcut config dialog.
        return true;

    case IDC_SC_PROGRAMS_BTN:
        // TODO: open Programs shortcut config dialog.
        return true;

    // ── Pin to Start button ───────────────────────────────────────────────────
    case IDC_SC_PINSTART_BTN:
        // TODO: open Pin to Start config dialog.
        return true;

    // ── Pin to Taskbar button ─────────────────────────────────────────────────
    case IDC_SC_PINTASKBAR_BTN:
        // TODO: open Pin to Taskbar config dialog.
        return true;

    // ── Desktop opt-out checkbox ──────────────────────────────────────────────
    case IDC_SC_DESKTOP_OPT: {
        HWND hCb = GetDlgItem(hwnd, IDC_SC_DESKTOP_OPT);
        if (hCb) {
            s_scDesktopOptOut = (SendMessageW(hCb, BM_GETCHECK, 0, 0) == BST_CHECKED);
            MainWindow::MarkAsModified();
        }
        return true;
    }

    // ── Add Subfolder button (also invoked from right-click context menu) ─────
    case IDC_SC_SM_ADD: {
        if (!s_hScStartMenuTree || !IsWindow(s_hScStartMenuTree)) return true;

        HTREEITEM hSel = TreeView_GetSelection(s_hScStartMenuTree);
        if (!hSel) hSel = TreeView_GetRoot(s_hScStartMenuTree);

        // Determine the parent node id from the selected HTREEITEM.
        int parentId = 0;  // default: child of Start Menu root
        for (const auto& n : s_scMenuNodes)
            if (n.hItem == hSel) { parentId = n.id; break; }

        // Resolve the default "New Subfolder" name from the current locale.
        const auto& locMap = MainWindow::GetLocale();
        auto itNF = locMap.find(L"sc_sm_new_folder");
        std::wstring newName = (itNF != locMap.end()) ? itNF->second : L"New Subfolder";

        int newId = s_scNextMenuId++;
        s_scMenuNodes.push_back({newId, parentId, newName, nullptr});
        ScMenuNode& newNode = s_scMenuNodes.back();

        TVINSERTSTRUCTW tvis     = {};
        tvis.hParent             = hSel;
        tvis.hInsertAfter        = TVI_LAST;
        tvis.item.mask           = TVIF_TEXT | TVIF_IMAGE | TVIF_SELECTEDIMAGE | TVIF_PARAM;
        tvis.item.pszText        = (LPWSTR)newName.c_str();
        tvis.item.iImage         = 0;
        tvis.item.iSelectedImage = 1;
        tvis.item.lParam         = (LPARAM)newId;
        newNode.hItem = (HTREEITEM)SendMessageW(
            s_hScStartMenuTree, TVM_INSERTITEM, 0, (LPARAM)&tvis);
        if (newNode.hItem) {
            TreeView_Expand(s_hScStartMenuTree, hSel, TVE_EXPAND);
            TreeView_SelectItem(s_hScStartMenuTree, newNode.hItem);
            TreeView_EditLabel(s_hScStartMenuTree, newNode.hItem);
            MainWindow::MarkAsModified();
        }
        return true;
    }

    // ── Remove Subfolder button (also invoked from right-click context menu) ──
    case IDC_SC_SM_REMOVE: {
        if (!s_hScStartMenuTree || !IsWindow(s_hScStartMenuTree)) return true;

        HTREEITEM hSel = TreeView_GetSelection(s_hScStartMenuTree);
        if (!hSel) return true;

        TVITEMW tvi = {}; tvi.mask = TVIF_PARAM; tvi.hItem = hSel;
        TreeView_GetItem(s_hScStartMenuTree, &tvi);
        int selId = (int)tvi.lParam;
        if (selId <= 1) return true;  // Start Menu root and Programs are fixed

        // Recursively erase the selected subtree from s_scMenuNodes.
        std::function<void(int)> removeSubtree = [&](int nodeId) {
            for (int i = (int)s_scMenuNodes.size() - 1; i >= 0; --i)
                if (s_scMenuNodes[i].parentId == nodeId)
                    removeSubtree(s_scMenuNodes[i].id);
            s_scMenuNodes.erase(
                std::remove_if(s_scMenuNodes.begin(), s_scMenuNodes.end(),
                    [nodeId](const ScMenuNode& n){ return n.id == nodeId; }),
                s_scMenuNodes.end());
        };
        removeSubtree(selId);

        // Removing the HTREEITEM also removes all its visual children.
        TreeView_DeleteItem(s_hScStartMenuTree, hSel);
        MainWindow::MarkAsModified();

        // Nothing is selected after the deletion — disable the Remove button.
        HWND hRemBtn = GetDlgItem(hwnd, IDC_SC_SM_REMOVE);
        if (hRemBtn) EnableWindow(hRemBtn, FALSE);
        return true;
    }

    default:
        return false;
    }
}

// ── SC_OnContextMenu ──────────────────────────────────────────────────────────

bool SC_OnContextMenu(HWND hwnd, HWND hCtrl, int x, int y)
{
    // Right-click on the Start Menu folder TreeView.
    if (hCtrl == s_hScStartMenuTree && s_hScStartMenuTree) {
        // Hit-test to find the item under the cursor and select it.
        TVHITTESTINFO ht = {};
        ht.pt.x = x; ht.pt.y = y;
        ScreenToClient(s_hScStartMenuTree, &ht.pt);
        HTREEITEM hItem = TreeView_HitTest(s_hScStartMenuTree, &ht);
        if (hItem)
            TreeView_SelectItem(s_hScStartMenuTree, hItem);
        else
            hItem = TreeView_GetSelection(s_hScStartMenuTree);

        // Decide whether Remove is applicable (fixed nodes 0 and 1 cannot be removed).
        BOOL canRemove = FALSE;
        if (hItem) {
            TVITEMW tvi = {}; tvi.mask = TVIF_PARAM; tvi.hItem = hItem;
            TreeView_GetItem(s_hScStartMenuTree, &tvi);
            canRemove = (tvi.lParam > 1) ? TRUE : FALSE;
        }

        const auto& locMap = MainWindow::GetLocale();
        auto locS = [&](const wchar_t* k, const wchar_t* fb) -> std::wstring {
            auto it = locMap.find(k); return (it != locMap.end()) ? it->second : fb;
        };

        HMENU hMenu = CreatePopupMenu();
        AppendMenuW(hMenu, MF_STRING, IDM_SC_CTX_ADD_SUBFOLDER,
            locS(L"sc_sm_add", L"Add Subfolder").c_str());
        AppendMenuW(hMenu, MF_SEPARATOR, 0, NULL);
        AppendMenuW(hMenu,
            canRemove ? MF_STRING : (MF_STRING | MF_GRAYED),
            IDM_SC_CTX_REMOVE_FOLDER,
            locS(L"sc_sm_remove", L"Remove").c_str());

        // TPM_RETURNCMD lets us manually dispatch so SC_OnCommand handles it.
        int cmd = (int)TrackPopupMenu(hMenu, TPM_RIGHTBUTTON | TPM_RETURNCMD,
            x, y, 0, hwnd, NULL);
        DestroyMenu(hMenu);

        if (cmd == IDM_SC_CTX_ADD_SUBFOLDER)
            SendMessageW(hwnd, WM_COMMAND, MAKEWPARAM(IDC_SC_SM_ADD, 0), 0);
        else if (cmd == IDM_SC_CTX_REMOVE_FOLDER)
            SendMessageW(hwnd, WM_COMMAND, MAKEWPARAM(IDC_SC_SM_REMOVE, 0), 0);
        return true;
    }

    // Right-click on any of the shortcut row buttons (Desktop, Pin to Start/Taskbar).
    // The shortcut config dialog is not yet implemented; show a greyed-out stub so
    // the right-click infrastructure is in place for the next development session.
    HWND hDesktopBtn    = GetDlgItem(hwnd, IDC_SC_DESKTOP_BTN);
    HWND hPinStartBtn   = GetDlgItem(hwnd, IDC_SC_PINSTART_BTN);
    HWND hPinTaskbarBtn = GetDlgItem(hwnd, IDC_SC_PINTASKBAR_BTN);

    if ((hDesktopBtn    && hCtrl == hDesktopBtn)    ||
        (hPinStartBtn   && hCtrl == hPinStartBtn)   ||
        (hPinTaskbarBtn && hCtrl == hPinTaskbarBtn))
    {
        const auto& locMap = MainWindow::GetLocale();
        auto locS = [&](const wchar_t* k, const wchar_t* fb) -> std::wstring {
            auto it = locMap.find(k); return (it != locMap.end()) ? it->second : fb;
        };
        HMENU hMenu = CreatePopupMenu();
        // Greyed until the shortcut-config dialog is implemented.
        AppendMenuW(hMenu, MF_STRING | MF_GRAYED, IDM_SC_CTX_EDIT_SC,
            locS(L"sc_ctx_configure", L"Configure shortcut\u2026").c_str());
        TrackPopupMenu(hMenu, TPM_RIGHTBUTTON, x, y, 0, hwnd, NULL);
        DestroyMenu(hMenu);
        return true;
    }

    return false;
}
