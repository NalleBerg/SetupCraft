$cpp = "C:\Users\NalleBerg\Documents\C++\Workspace\SetupCraft\page_manual.cpp"
$content = [System.IO.File]::ReadAllText($cpp)

$old = @'
    // ── Next step ─────────────────────────────────────────────────────────────
    // shell32 #80 = build icon — points toward the Build step.
    RegisterShell32Icon(hEdit, pd, 80);
    AM(L"\u25B6  ", true, RGB(0,120,0), 11);
    AM(ML(pd, L"man_sett_next",
          L"Next step: press Build (F7) to compile the Inno Setup script and "
          L"create the installer .exe in your output folder.") + L"\r\n",
       false, RGB(0,80,0));
}

#undef AM
'@

$new = @'
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

    // ── Page title ────────────────────────────────────────────────────────────
    // shell32 #166 = registry icon.
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
    AM(L"\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\r\n\r\n", false, RGB(100,140,180));

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

    // ── Section 2: Add/Remove Programs entry ─────────────────────────────────
    // shell32 #221 = information (i) icon.
    RegisterShell32Icon(hEdit, pd, 221);
    AM(ML(pd, L"man_reg_h2",
          L"\U0001F4CB  Add/Remove Programs entry") + L"\r\n",
       true, RGB(0,70,140), 12);
    flag(L"man_reg_f_register", L"Register in Windows Installed Programs",
         L"man_reg_f_register_d",
         L"When ticked, the installer writes an entry to "
         L"HKLM\\SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Uninstall so the "
         L"application appears in Settings \u2192 Apps and in Control Panel. "
         L"Untick only for hidden or embedded installers. "
         L"Inno emits CreateUninstallRegKey=yes.");
    AM(L"\r\n", false, RGB(0,0,0));
    ctrl(L"man_reg_l2a", L"Display Name",
         L"man_reg_p2a",
         L"The name shown in the Apps list. Pre-filled from your project name; "
         L"change it here if you want a longer or branded display name that differs "
         L"from the App Name in Settings.");
    ctrl(L"man_reg_l2b", L"Version",
         L"man_reg_p2b",
         L"The version string shown in the Apps list. Pre-filled from Settings. "
         L"Editing it here overrides the version only in the registry entry, not "
         L"in the installer\u2019s own version metadata.");
    ctrl(L"man_reg_l2c", L"Publisher",
         L"man_reg_p2c",
         L"The publisher name shown in the Apps list. Pre-filled from Settings. "
         L"Inno writes this as Publisher in the Uninstall key.");
    ctrl(L"man_reg_l2d", L"App Icon (ARP)",
         L"man_reg_p2d",
         L"The .ico file shown next to the entry in Settings \u2192 Apps. "
         L"Click Add Icon to browse for a .ico file. Leave blank to use the "
         L"installer\u2019s own icon (set on the Settings page).");
    ctrl(L"man_reg_l2e", L"Show Regkey",
         L"man_reg_p2e",
         L"Opens a read-only preview of the exact [Registry] section that "
         L"SetupCraft will generate for this project. Useful for verifying the "
         L"key paths and values before building.");
    AM(L"\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\r\n\r\n", false, RGB(100,140,180));

    // ── Section 3: Custom registry entries ───────────────────────────────────
    // shell32 #166 = registry icon.
    RegisterShell32Icon(hEdit, pd, 166);
    AM(ML(pd, L"man_reg_h3",
          L"\U0001F5C4  Custom registry entries") + L"\r\n",
       true, RGB(0,70,140), 12);
    AM(ML(pd, L"man_reg_p3",
          L"The split pane shows the registry tree (left) and the values for the "
          L"selected key (right). Navigate the tree to find or create the key you "
          L"need, then add values in the right pane.") + L"\r\n\r\n",
       false, RGB(40,40,40));
    ctrl(L"man_reg_l3a", L"Add Key",
         L"man_reg_p3a",
         L"Creates a new subkey under the currently selected tree node. Type the "
         L"key name in the dialog that appears. Use backslash to create nested keys "
         L"in one step (e.g. Software\\MyApp\\Settings).");
    ctrl(L"man_reg_l3b", L"Add Value",
         L"man_reg_p3b",
         L"Adds a new named value to the selected key. Choose the type (REG_SZ, "
         L"REG_DWORD, REG_BINARY, REG_EXPAND_SZ, REG_MULTI_SZ) and enter the value "
         L"name and data. Leave the name blank to set the default (unnamed) value.");
    ctrl(L"man_reg_l3c", L"Edit / Delete",
         L"man_reg_p3c",
         L"Select a value in the right pane and click Edit to change its data, or "
         L"Delete to remove it. Double-click a value to edit it inline.");
    ctrl(L"man_reg_l3d", L"Create Restore Point",
         L"man_reg_p3d",
         L"Asks Windows to create a System Restore snapshot before the installer "
         L"writes to the registry. Useful during development to quickly roll back "
         L"registry changes.");
    AM(L"\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\r\n\r\n", false, RGB(100,140,180));

    // ── Next step ─────────────────────────────────────────────────────────────
    // shell32 #257 = shortcut arrow — points toward Shortcuts (next in toolbar).
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

    // ── Page title ────────────────────────────────────────────────────────────
    // shell32 #257 = shortcut arrow icon.
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
    AM(L"\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\r\n\r\n", false, RGB(100,140,180));

    auto ctrl = [&](const wchar_t* lblKey, const wchar_t* lblFb,
                    const wchar_t* txtKey, const wchar_t* txtFb) {
        AM(L"  ", false, RGB(0,0,0));
        AM(ML(pd, lblKey, lblFb) + L" \u2014 ", true, RGB(0,0,0));
        AM(ML(pd, txtKey, txtFb) + L"\r\n\r\n", false, RGB(60,60,60));
    };

    // ── Section 2: Shortcut locations ────────────────────────────────────────
    // shell32 #174 = monitor / display icon.
    RegisterShell32Icon(hEdit, pd, 174);
    AM(ML(pd, L"man_sc_h2",
          L"\U0001F5A5  Shortcut locations") + L"\r\n",
       true, RGB(0,70,140), 12);
    ctrl(L"man_sc_l2a", L"Desktop",
         L"man_sc_p2a",
         L"Adds a shortcut icon to the user\u2019s Desktop. Each .exe in your "
         L"Files page appears as a row button; click the button to add or remove "
         L"the shortcut. Tick \u201cAllow user to opt out\u201d to show an optional "
         L"checkbox on the installer\u2019s Finish page.");
    ctrl(L"man_sc_l2b", L"Start Menu folder tree",
         L"man_sc_p2b",
         L"The tree on the left represents the Programs folder structure that the "
         L"installer will create under the user\u2019s Start Menu. Use Add Subfolder "
         L"to nest folders, Remove Subfolder to delete one, and Add Shortcut Here "
         L"to attach a .exe shortcut to the selected folder node.");
    ctrl(L"man_sc_l2c", L"Pin to Start / Pin to Taskbar",
         L"man_sc_p2c",
         L"Creates a pinned tile (Start) or a pinned button (Taskbar) for the "
         L"selected executable. These use Windows shell APIs; behaviour varies "
         L"between Windows versions and may be blocked by Group Policy on managed "
         L"machines.");
    AM(L"\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\r\n\r\n", false, RGB(100,140,180));

    // ── Section 3: Configuring a shortcut ────────────────────────────────────
    // shell32 #134 = properties icon.
    RegisterShell32Icon(hEdit, pd, 134);
    AM(ML(pd, L"man_sc_h3",
          L"\u270F  Configuring a shortcut") + L"\r\n",
       true, RGB(0,70,140), 12);
    AM(ML(pd, L"man_sc_p3",
          L"Right-click any shortcut button and choose \u201cConfigure shortcut\u2026\u201d "
          L"to open the shortcut editor. The fields below are available:") + L"\r\n\r\n",
       false, RGB(40,40,40));
    ctrl(L"man_sc_l3a", L"Name",
         L"man_sc_p3a",
         L"The display name of the shortcut (shown under the icon on the Desktop "
         L"or in the Start Menu). Defaults to the executable name without extension.");
    ctrl(L"man_sc_l3b", L"Executable",
         L"man_sc_p3b",
         L"The target .exe file. Pre-filled with the .exe this shortcut was created "
         L"from; click the browse (\u2026) button to change it.");
    ctrl(L"man_sc_l3c", L"Working Directory",
         L"man_sc_p3c",
         L"The directory that will be the current directory when the shortcut is "
         L"launched. Defaults to the executable\u2019s own directory.");
    ctrl(L"man_sc_l3d", L"Arguments",
         L"man_sc_p3d",
         L"Command-line arguments passed to the executable whenever the shortcut "
         L"is invoked. Leave blank for no arguments.");
    ctrl(L"man_sc_l3e", L"Comment",
         L"man_sc_p3e",
         L"The tooltip shown when the user hovers over the shortcut icon. Maps to "
         L"Inno\u2019s Comment: field in the [Icons] section.");
    ctrl(L"man_sc_l3f", L"Hotkey",
         L"man_sc_p3f",
         L"A global keyboard shortcut that launches the application directly "
         L"(e.g. Ctrl+Alt+H). Use the HOTKEY control to record the key combination; "
         L"click Clear to remove it. Maps to Inno\u2019s HotKey: field.");
    ctrl(L"man_sc_l3g", L"Icon",
         L"man_sc_p3g",
         L"The icon displayed on the shortcut. Defaults to the first icon in the "
         L"target .exe; click Change Icon\u2026 to pick a different .ico/.exe/.dll "
         L"and icon index.");
    ctrl(L"man_sc_l3h", L"Run as administrator",
         L"man_sc_p3h",
         L"When ticked, the shortcut always requests UAC elevation when launched. "
         L"Use only when the application requires elevation to function correctly.");
    AM(L"\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\r\n\r\n", false, RGB(100,140,180));

    // ── Next step ─────────────────────────────────────────────────────────────
    // shell32 #152 = document icon — points toward File Types (next in toolbar).
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

    // ── Page title ────────────────────────────────────────────────────────────
    // shell32 #152 = document / file icon.
    RegisterShell32Icon(hEdit, pd, 152);
    AM(ML(pd, L"man_fa_h1",
          L"\U0001F4C4  File Types \u2014 What does it do?") + L"\r\n\r\n",
       true, RGB(0,70,140), 14, true);
    AM(ML(pd, L"man_fa_p1",
          L"The File Types page lets your installer register file extension "
          L"associations in Windows. When the installer runs, it writes the "
          L"necessary registry entries so that Windows Explorer opens files with "
          L"your application when the user double-clicks them. Each row in the "
          L"list is one file extension (e.g. .mydata) with its description, icon, "
          L"and the command used to open it.") + L"\r\n\r\n",
       false, RGB(40,40,40));
    AM(L"\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\r\n\r\n", false, RGB(100,140,180));

    auto ctrl = [&](const wchar_t* lblKey, const wchar_t* lblFb,
                    const wchar_t* txtKey, const wchar_t* txtFb) {
        AM(L"  ", false, RGB(0,0,0));
        AM(ML(pd, lblKey, lblFb) + L" \u2014 ", true, RGB(0,0,0));
        AM(ML(pd, txtKey, txtFb) + L"\r\n\r\n", false, RGB(60,60,60));
    };

    // ── Section 2: Managing associations ─────────────────────────────────────
    // shell32 #221 = info icon.
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

    // ── Section 3: File association fields ───────────────────────────────────
    // shell32 #134 = properties icon.
    RegisterShell32Icon(hEdit, pd, 134);
    AM(ML(pd, L"man_fa_h3",
          L"\u270F  File association fields") + L"\r\n",
       true, RGB(0,70,140), 12);
    ctrl(L"man_fa_l3a", L"Extension",
         L"man_fa_p3a",
         L"The file extension to register, including the leading dot (e.g. .mydata). "
         L"Windows will route double-clicks on files with this extension to your "
         L"application\u2019s Open command.");
    ctrl(L"man_fa_l3b", L"Description",
         L"man_fa_p3b",
         L"Human-readable label shown in Windows Explorer for files of this type "
         L"(e.g. \u201cMyApp Document\u201d). Displayed in the \u201cType\u201d "
         L"column of Explorer\u2019s detail view.");
    ctrl(L"man_fa_l3c", L"ProgID",
         L"man_fa_p3c",
         L"A unique registry identifier for this file type (e.g. \u201cMyApp.mydata\u201d). "
         L"Leave blank to auto-generate as AppName.extension. Must contain no spaces.");
    ctrl(L"man_fa_l3d", L"Icon path",
         L"man_fa_p3d",
         L"Path to the .exe, .dll, or .ico file containing the icon for this file "
         L"type. Use Inno constants such as {app}\\MyApp.exe. Leave blank to use "
         L"the default generic document icon.");
    ctrl(L"man_fa_l3e", L"Icon index",
         L"man_fa_p3e",
         L"Zero-based index of the icon inside the icon file (0 = first icon). "
         L"Relevant for .exe and .dll files that contain multiple icons; use 0 for "
         L"standalone .ico files.");
    ctrl(L"man_fa_l3f", L"Open command",
         L"man_fa_p3f",
         L"The command run when the user double-clicks a file. Use %1 as a "
         L"placeholder for the file path: \u201c\\\"{app}\\MyApp.exe\\\" \\\"%1\\\"\u201d. "
         L"This is the minimum required field for a working file association.");
    ctrl(L"man_fa_l3g", L"Edit / Print commands",
         L"man_fa_p3g",
         L"Optional commands added to the right-click context menu as \u201cEdit\u201d "
         L"and \u201cPrint\u201d verbs. Leave blank to omit those menu entries. "
         L"Use %1 as the file path placeholder, same as the Open command.");
    ctrl(L"man_fa_l3h", L"MIME type",
         L"man_fa_p3h",
         L"The MIME content type for this extension (e.g. \u201capplication/x-mydata\u201d). "
         L"Used by browsers and web servers. Leave blank if not needed.");
    AM(L"\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\r\n\r\n", false, RGB(100,140,180));

    // ── Next step ─────────────────────────────────────────────────────────────
    // shell32 #310 = script icon — points toward Scripts (next in toolbar row 2).
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

    // ── Page title ────────────────────────────────────────────────────────────
    // shell32 #310 = script / command-prompt icon.
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
    AM(L"\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\r\n\r\n", false, RGB(100,140,180));

    auto ctrl = [&](const wchar_t* lblKey, const wchar_t* lblFb,
                    const wchar_t* txtKey, const wchar_t* txtFb) {
        AM(L"  ", false, RGB(0,0,0));
        AM(ML(pd, lblKey, lblFb) + L" \u2014 ", true, RGB(0,0,0));
        AM(ML(pd, txtKey, txtFb) + L"\r\n\r\n", false, RGB(60,60,60));
    };

    // ── Section 2: When to run ────────────────────────────────────────────────
    // shell32 #138 = clock / timer icon.
    RegisterShell32Icon(hEdit, pd, 138);
    AM(ML(pd, L"man_scr_h2",
          L"\u23F0  When to run") + L"\r\n",
       true, RGB(0,70,140), 12);
    ctrl(L"man_scr_l2a", L"Before Files (ssInstall)",
         L"man_scr_p2a",
         L"Runs before any files are copied to the target machine. Useful for "
         L"stopping services, killing processes, or checking prerequisites. "
         L"Implemented via Inno\u2019s [Code] CurStepChanged(ssInstall).");
    ctrl(L"man_scr_l2b", L"After Files",
         L"man_scr_p2b",
         L"Runs unconditionally after all files have been copied. Suitable for "
         L"post-install configuration, license activation, or database migration. "
         L"Inno emits an unconditional [Run] entry.");
    ctrl(L"man_scr_l2c", L"On Finish (optional)",
         L"man_scr_p2c",
         L"Runs when the user reaches the Finish page of the wizard and an optional "
         L"checkbox is ticked. Suitable for \u201cLaunch MyApp now\u201d or "
         L"\u201cView release notes\u201d actions. Inno emits [Run] with "
         L"Flags: postinstall skipifsilent.");
    ctrl(L"man_scr_l2d", L"On Uninstall",
         L"man_scr_p2d",
         L"Runs when the uninstaller executes. Suitable for stopping services, "
         L"removing user data, or cleaning up configuration. "
         L"Inno emits a [UninstallRun] entry.");
    AM(L"\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\r\n\r\n", false, RGB(100,140,180));

    // ── Section 3: Script types ───────────────────────────────────────────────
    // shell32 #221 = info icon.
    RegisterShell32Icon(hEdit, pd, 221);
    AM(ML(pd, L"man_scr_h3",
          L"\U0001F4C3  Script types") + L"\r\n",
       true, RGB(0,70,140), 12);
    ctrl(L"man_scr_l3a", L".bat / .cmd",
         L"man_scr_p3a",
         L"Classic Windows batch files. Run directly by cmd.exe with no execution "
         L"policy restrictions. Best for simple file operations, registry tweaks "
         L"via reg.exe, or calling other executables.");
    ctrl(L"man_scr_l3b", L"PowerShell (.ps1)",
         L"man_scr_p3b",
         L"PowerShell scripts. SetupCraft generates a [Run] entry that calls "
         L"powershell.exe -ExecutionPolicy Bypass -File \u201c\u2026\u201d. "
         L"More powerful than batch for complex logic, .NET interop, or WMI "
         L"queries. Available on all supported Windows versions (7+).");
    AM(L"\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\r\n\r\n", false, RGB(100,140,180));

    // ── Section 4: Managing scripts ───────────────────────────────────────────
    // shell32 #134 = properties icon.
    RegisterShell32Icon(hEdit, pd, 134);
    AM(ML(pd, L"man_scr_h4",
          L"\u270F  Managing scripts") + L"\r\n",
       true, RGB(0,70,140), 12);
    AM(ML(pd, L"man_scr_p4",
          L"Use the toolbar buttons at the top of the page to manage scripts: "
          L"\u201c+ Add Script\u201d opens the script editor to write or paste "
          L"script content directly; \u201cLoad from file\u2026\u201d imports an "
          L"existing .bat, .cmd, or .ps1 file from disk. Select a script tile and "
          L"click Edit to modify it, or Delete to remove it. The master enable "
          L"checkbox activates or disables all scripts at once without deleting "
          L"them.") + L"\r\n\r\n",
       false, RGB(40,40,40));
    AM(L"\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\u2501\r\n\r\n", false, RGB(100,140,180));

    // ── Next step ─────────────────────────────────────────────────────────────
    // shell32 #80 = build icon — points toward Build (F7).
    RegisterShell32Icon(hEdit, pd, 80);
    AM(L"\u25B6  ", true, RGB(0,120,0), 11);
    AM(ML(pd, L"man_scr_next",
          L"Next step: press Build (F7) to compile the Inno Setup script and "
          L"create the installer .exe in your output folder.") + L"\r\n",
       false, RGB(0,80,0));
}

#undef AM
'@

if ($content.Contains($old)) {
    $content = $content.Replace($old, $new)
    [System.IO.File]::WriteAllText($cpp, $content)
    Write-Host "SUCCESS: page_manual.cpp updated."
} else {
    Write-Host "ERROR: old text not found in page_manual.cpp"
    # Debug: show what we're looking for
    $snippet = '    // shell32 #80 = build icon'
    Write-Host "Contains snippet: $($content.Contains($snippet))"
}
