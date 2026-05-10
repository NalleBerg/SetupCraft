$enc = New-Object System.Text.UTF8Encoding($false)

$newKeys = @"

# Manual page — Registry (page 1)
man_reg_window_title=Registry — How to Use
man_reg_h1=🗂  Registry — What does it do?
man_reg_p1=The Registry page has two roles: it controls the Add/Remove Programs (ARP) entry that Windows shows in Settings → Apps, and it lets you add custom registry keys and values to the installer. The ARP section is generated automatically from your project's Settings; the custom entries give you full control over anything else your application needs in the Windows registry.
man_reg_h2=📋  Add/Remove Programs entry
man_reg_f_register=Register in Windows Installed Programs
man_reg_f_register_d=When ticked, the installer writes an entry to HKLM\SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall so the application appears in Settings → Apps and Control Panel. Untick only for hidden or embedded installers. Inno emits CreateUninstallRegKey=yes.
man_reg_l2a=Display Name
man_reg_p2a=The name shown in the Apps list. Pre-filled from your project name; change it here if you want a longer or branded display name that differs from the App Name in Settings.
man_reg_l2b=Version
man_reg_p2b=The version string shown in the Apps list. Pre-filled from Settings. Editing it here overrides the version only in the ARP registry entry, not in the installer's own version metadata.
man_reg_l2c=Publisher
man_reg_p2c=The publisher name shown in the Apps list. Pre-filled from Settings. Inno writes this as the Publisher value in the Uninstall key.
man_reg_l2d=App Icon (ARP)
man_reg_p2d=The .ico shown next to the entry in Settings → Apps. Click Add Icon to browse for a .ico file. Leave blank to use the installer's own icon (set on the Settings page).
man_reg_l2e=Show Regkey
man_reg_p2e=Opens a read-only preview of the exact [Registry] section that SetupCraft will generate for this project. Useful for verifying key paths and values before building.
man_reg_h3=🗄  Custom registry entries
man_reg_p3=The split pane shows the registry tree (left) and the values for the selected key (right). Navigate the tree to find or create the key you need, then add values in the right pane.
man_reg_l3a=Add Key
man_reg_p3a=Creates a new subkey under the currently selected tree node. Type the key name in the dialog. Use backslash to create nested keys in one step (e.g. Software\MyApp\Settings).
man_reg_l3b=Add Value
man_reg_p3b=Adds a named value to the selected key. Choose the type (REG_SZ, REG_DWORD, REG_BINARY, REG_EXPAND_SZ, REG_MULTI_SZ) and enter the name and data. Leave the name blank to set the default (unnamed) value.
man_reg_l3c=Edit / Delete
man_reg_p3c=Select a value in the right pane and click Edit to change its data, or Delete to remove it. Double-click a value to edit it inline.
man_reg_l3d=Create Restore Point
man_reg_p3d=Asks Windows to create a System Restore snapshot before the installer writes to the registry. Useful during development to quickly roll back registry changes.
man_reg_next=Next step: go to Shortcuts to create desktop, Start Menu, and taskbar shortcuts for your application.

# Manual page — Shortcuts (page 2)
man_sc_window_title=Shortcuts — How to Use
man_sc_h1=🔗  Shortcuts — What does it do?
man_sc_p1=The Shortcuts page controls every shortcut the installer creates: desktop icons, Start Menu folder entries, and taskbar / Start screen pins. Each shortcut is linked to an executable from your Files page and can be configured with a custom name, icon, working directory, arguments, hotkey, and elevation level.
man_sc_h2=🖥  Shortcut locations
man_sc_l2a=Desktop
man_sc_p2a=Adds a shortcut icon to the user's Desktop. Each .exe in your Files page appears as a row button; click the button to add or remove the shortcut. Tick "Allow user to opt out" to show an optional checkbox on the installer's Finish page.
man_sc_l2b=Start Menu folder tree
man_sc_p2b=The tree represents the Programs folder structure the installer will create under the Start Menu. Use Add Subfolder to nest folders, Remove Subfolder to delete one, and Add Shortcut Here to attach a .exe shortcut to the selected folder node.
man_sc_l2c=Pin to Start / Pin to Taskbar
man_sc_p2c=Creates a pinned tile (Start) or pinned button (Taskbar) for the selected executable. These use Windows shell APIs; behaviour varies between Windows versions and may be blocked by Group Policy on managed machines.
man_sc_h3=✏  Configuring a shortcut
man_sc_p3=Right-click any shortcut button and choose "Configure shortcut…" to open the shortcut editor:
man_sc_l3a=Name
man_sc_p3a=The display name of the shortcut (shown under the icon on the Desktop or in the Start Menu). Defaults to the executable name without extension.
man_sc_l3b=Executable
man_sc_p3b=The target .exe file. Pre-filled from the .exe this shortcut was created from; click … to change it.
man_sc_l3c=Working Directory
man_sc_p3c=The current directory when the shortcut launches. Defaults to the executable's own folder.
man_sc_l3d=Arguments
man_sc_p3d=Command-line arguments passed to the executable. Leave blank for none.
man_sc_l3e=Comment
man_sc_p3e=Tooltip shown when hovering the shortcut icon. Maps to Inno's Comment: field in the [Icons] section.
man_sc_l3f=Hotkey
man_sc_p3f=A global keyboard shortcut that launches the application directly (e.g. Ctrl+Alt+H). Maps to Inno's HotKey: field. Click Clear to remove it.
man_sc_l3g=Icon
man_sc_p3g=The icon displayed on the shortcut. Defaults to the first icon in the target .exe; click Change Icon… to pick a different .ico/.exe/.dll and icon index.
man_sc_l3h=Run as administrator
man_sc_p3h=When ticked, the shortcut always requests UAC elevation. Use only when the application requires elevation to function correctly.
man_sc_next=Next step: go to File Types to register file extensions that your application can open.

# Manual page — File Types (page 10)
man_fa_window_title=File Types — How to Use
man_fa_h1=📄  File Types — What does it do?
man_fa_p1=The File Types page lets your installer register file extension associations in Windows. Each row is one extension (e.g. .mydata) with its description, icon, and the command used to open it. The installer writes the necessary registry entries so Windows Explorer launches your application when the user double-clicks a matching file.
man_fa_h2=📋  Managing associations
man_fa_p2=The list shows all registered file types for this project. Tick or untick the Enabled column to include or exclude individual associations without deleting them. Use Add to create a new one, Edit to change an existing one, or Remove to delete it.
man_fa_h3=✏  File association fields
man_fa_l3a=Extension
man_fa_p3a=The file extension to register, including the leading dot (e.g. .mydata). Windows routes double-clicks on files with this extension to your application's Open command.
man_fa_l3b=Description
man_fa_p3b=Human-readable label shown in Windows Explorer for this file type (e.g. "MyApp Document"). Shown in the Type column of Explorer's detail view.
man_fa_l3c=ProgID
man_fa_p3c=A unique registry identifier for this file type (e.g. "MyApp.mydata"). Leave blank to auto-generate as AppName.extension. Must contain no spaces.
man_fa_l3d=Icon path
man_fa_p3d=Path to the .exe, .dll, or .ico containing the icon for this file type. Use Inno constants such as {app}\MyApp.exe. Leave blank for the default generic document icon.
man_fa_l3e=Icon index
man_fa_p3e=Zero-based index of the icon inside the icon file. Use 0 for standalone .ico files, or the specific index for icons within a .dll or .exe.
man_fa_l3f=Open command
man_fa_p3f=The command run when the user double-clicks a file. Use %1 as a placeholder for the file path: "{app}\MyApp.exe" "%1". This is the minimum required field.
man_fa_l3g=Edit / Print commands
man_fa_p3g=Optional commands added to the right-click menu as Edit and Print verbs. Leave blank to omit. Use %1 as the file path placeholder.
man_fa_l3h=MIME type
man_fa_p3h=The MIME content type (e.g. "application/x-mydata"). Used by browsers and servers. Leave blank if not needed.
man_fa_next=Next step: go to Scripts to add custom pre-install or post-install scripts to your installer.

# Manual page — Scripts (page 8)
man_scr_window_title=Scripts — How to Use
man_scr_h1=📜  Scripts — What does it do?
man_scr_p1=The Scripts page lets you attach .bat/.cmd or PowerShell .ps1 scripts to your installer. Each script is tied to a specific moment in the install (or uninstall) process. Scripts are embedded in the installer and extracted to a temporary folder at runtime; SetupCraft generates the appropriate Inno Setup [Run] or [UninstallRun] entries automatically.
man_scr_h2=⏰  When to run
man_scr_l2a=Before Files
man_scr_p2a=Runs before any files are copied. Useful for stopping services, killing processes, or checking prerequisites. Implemented via Inno's [Code] CurStepChanged(ssInstall).
man_scr_l2b=After Files
man_scr_p2b=Runs unconditionally after all files are copied. Suitable for post-install configuration, license activation, or database migration. Inno emits an unconditional [Run] entry.
man_scr_l2c=On Finish (optional)
man_scr_p2c=Runs when the user reaches the Finish page and an optional checkbox is ticked. Suitable for "Launch MyApp now" or "View release notes" actions. Inno emits [Run] with Flags: postinstall skipifsilent.
man_scr_l2d=On Uninstall
man_scr_p2d=Runs when the uninstaller executes. Suitable for stopping services, removing user data, or cleaning up configuration. Inno emits a [UninstallRun] entry.
man_scr_h3=📃  Script types
man_scr_l3a=.bat / .cmd
man_scr_p3a=Classic Windows batch files. Run by cmd.exe with no execution policy restrictions. Best for simple file operations, registry tweaks via reg.exe, or calling other executables.
man_scr_l3b=PowerShell (.ps1)
man_scr_p3b=PowerShell scripts. SetupCraft generates a [Run] entry that calls powershell.exe -ExecutionPolicy Bypass -File "…". More powerful for complex logic, .NET interop, or WMI queries. Available on all supported Windows versions (7+).
man_scr_h4=✏  Managing scripts
man_scr_p4=Use the toolbar buttons at the top of the page: "+ Add Script" opens the script editor to write or paste content directly; "Load from file…" imports an existing .bat, .cmd, or .ps1 file from disk. Select a tile and click Edit to modify it, or Delete to remove it. The master enable checkbox activates or disables all scripts at once without deleting them.
man_scr_next=Next step: press Build (F7) to compile the Inno Setup script and create the installer .exe in your output folder.
"@

# Update man_sett_next in both locale files
foreach ($path in @(
    "C:\Users\NalleBerg\Documents\C++\Workspace\SetupCraft\locale\en_GB.txt",
    "C:\Users\NalleBerg\Documents\C++\Workspace\SetupCraft\SetupCraft\locale\en_GB.txt"
)) {
    $text = [System.IO.File]::ReadAllText($path, $enc)

    # Fix the Settings next step text
    $oldNext = "man_sett_next=Next step: press Build (F7) to compile the Inno Setup script and create the installer .exe in your output folder."
    $newNext = "man_sett_next=Next step: go to Registry to add custom registry entries and configure the Windows Add/Remove Programs entry."
    $text = $text.Replace($oldNext, $newNext)

    # Insert new keys after man_sett_next line
    $insertAfter = $newNext
    $text = $text.Replace($insertAfter, $insertAfter + "`r`n" + $newKeys)

    [System.IO.File]::WriteAllText($path, $text, $enc)
    Write-Host "Updated: $path"
}
