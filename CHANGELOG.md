# Changelog

All notable changes to SetupCraft will be documented in this file.

## [2026.05.06.13] - 2026-05-06

### file_assoc — File Associations page (page 10) + per-field help tooltips
- **file_assoc — new "File Types" toolbar button and page 10 (`IDC_TB_FILE_ASSOC = 5084`)**: Dedicated page for registering Windows file type associations. ListView (`LVS_EX_CHECKBOXES`) with Extension, Description, ProgID, Icon, Open Command columns. Per-row enabled checkbox — individual file types can be toggled on or off without removing them. Add / Edit / Remove buttons (`IDC_FA_ADD = 8101`, `IDC_FA_EDIT = 8102`, `IDC_FA_REMOVE = 8103`) with right-click context menu (`IDM_FA_CTX_EDIT = 6400`, `IDM_FA_CTX_REMOVE = 6401`); double-click to edit. `s_refreshing` guard prevents spurious `LVN_ITEMCHANGED` during `ListView_SetCheckState` calls inside `RefreshList()`. Full module in `file_assoc.h` / `file_assoc.cpp`; fully i18n'd via locale keys.
- **file_assoc_dialog — Add/Edit File Association modal (`file_assoc_dialog.h/.cpp`)**: Flat form with fields: Extension (validated, required, including the leading dot), Description, ProgID (auto-derives to `AppName.ext` if blank), Icon path + `…` browse (`GetOpenFileNameW` for `*.exe;*.dll;*.ico`), Icon index (zero-based), Open command, Edit command, Print command, MIME type, Enabled checkbox. All fields fully i18n'd. Validation: `ShowValidationDialog` on empty extension. Dialog class `"SetupCraftFaDialog"`. Both `WM_DRAWITEM` paths wired: `DrawCustomCheckbox` first, then `DrawCustomButton`.
- **file_assoc_dialog — per-field help icons with descriptive tooltips**: Every field (including the Enabled checkbox) has a `shell32.dll` index 23 (ℹ) button appended to the right of the control. Hovering shows a `SetButtonTooltip`-based tooltip describing exactly what the field expects — format, example value, and what happens when left blank. `MakeRow()` helper extended with `hasHelp` / `helpId` / `helpTip` parameters (no-op when not passed). Help button IDs `FADLG_IDC_HELP_ENABLED` through `FADLG_IDC_HELP_MIME` (3220–3229). 10 tooltip locale keys: `fa_tip_enabled` through `fa_tip_mime`.
- **db — `file_associations` table + `FileAssocRow` struct**: New table created by `InitDb()`. Columns: `id`, `project_id`, `enabled`, `extension`, `description`, `prog_id`, `icon_path`, `icon_index`, `open_cmd`, `edit_cmd`, `print_cmd`, `content_type`. CRUD: `DB::InsertFileAssoc()`, `DB::DeleteFileAssocsForProject()`, `DB::GetFileAssocsForProject()`. `FileAssocRow` struct declared in `db.h`.
- **settings / issgen — `ChangesAssociations` auto-derived**: `SBuildConfig` gains `changesAssociations` (bool); set to `FA_HasAnyEnabled()` in `SETT_GetBuildConfig()`. Emitted as `ChangesAssociations=yes/no` via `{#ChangesAssociations}` token. Both `template.iss` copies updated. The `; <<FILE_ASSOCIATIONS>>` marker in `[Registry]` expands to HKCR entries per enabled `FileAssocRow`: extension → ProgID, optional `Content Type` (MIME), `DefaultIcon`, `shell\open\command`; optional `shell\edit\command` and `shell\print\command` when their fields are non-empty. ProgID defaults to `AppName.ext` when blank.
- **locale — 43 locale keys in `en_GB.txt`** (both copies): `tb_file_assoc`, `fa_page_title`, `fa_btn_add/edit/remove`, `fa_btn_*_tip` (button tooltips), `fa_col_*` (5 column headers), `fa_progid_auto`, `fa_ctx_edit/remove`, `fa_enabled`, `fa_dlg_title_add/edit`, `fa_lbl_*` (all field labels), `fa_err_ext_empty`, `fa_confirm_remove`, `fa_confirm_remove_named`, `fa_tip_*` (10 per-field description tooltips).

## [2026.05.06.11] - 2026-05-06

### settings / issgen — AddToPath checkbox (11:55)
- **settings — `AddToPath` checkbox (`IDC_SETT_ADD_TO_PATH = 8033`)**: New *Add install directory to system PATH* checkbox in the Uninstall section, immediately before *Broadcast environment change*. Off by default. When ticked, the generated `.iss` includes a `[Registry]` entry appending `{app}` to `HKLM\SYSTEM\CurrentControlSet\Control\Session Manager\Environment` → `Path` (`expandsz` + `preservestringtype`). The *Broadcast environment change* checkbox is enabled and auto-checked when *Add to PATH* is ticked, then unchecked and disabled when unticked. Persisted as `add_to_path`. The `; <<PATH_REGISTRY>>` marker in `template.iss` expands to the registry line when enabled, or empty when disabled. `SBuildConfig` gains `addToPath`.

### settings / issgen — ChangesEnvironment checkbox (11:44)
- **settings — `ChangesEnvironment` checkbox (`IDC_SETT_CHANGES_ENV = 8032`)**: New *Broadcast environment change (required when modifying PATH)* checkbox in the Uninstall section, below *Close running applications*. When ticked, Inno Setup broadcasts `WM_SETTINGCHANGE` with `lparam = "Environment"` after installation completes — notifying the Windows shell and all running applications that `PATH` or other environment variables have changed, so users don't need to log out for new entries to take effect. Off by default; enable whenever the installer writes to the system or per-user `Environment` registry key. Persisted as `changes_env`. Emitted as `ChangesEnvironment=yes/no` via `{#ChangesEnvironment}` token. Both `template.iss` copies updated. `SBuildConfig` gains `changesEnvironment`.

### settings — Settings page combo width auto-fit (11:41)
- **settings — `LabelCombo` auto-sizes to the widest item**: All combo boxes on the Settings page were previously capped at `S(240)` logical pixels, causing longer option strings (e.g. *Auto (warn only when not upgrading)*) to be clipped in the closed combo control. `LabelCombo` now measures every item's text width at creation time via `GetTextExtentPoint32W` (using the GUI font) and adds the scroll-arrow width plus a small padding allowance. Width is clamped between `S(120)` (minimum readable) and the available space to the right edge. All combo boxes on the page benefit automatically.

### ctrlw / inno / locale — "installer already running" detection (11:20)
- **ctrlw — `ShowMutexRunningDialog(hwndParent, locale) → bool`**: New custom modal dialog shown when a duplicate installer instance is detected (mutex already owned). Three owner-drawn buttons: Green IDYES *"Close the running installer"*, Blue IDNO *"Close this installer"*, Red IDCANCEL *"Cancel"*. Returns `true` only when the user chose IDYES so the caller can attempt to close the running instance and retry the mutex. Fully i18n via the locale map; locale keys: `mutex_title`, `mutex_message`, `mutex_close_btn`, `mutex_close_this_btn`, `cancel`. Layout follows the standard `ctrlw` measure-then-create pattern; button row is centred and auto-widens to fit the widest label at any DPI.
- **inno/template.iss — `InitializeSetup()` mutex guard (`[Code]`)**: Inno Pascal `InitializeSetup` creates a named mutex via `CreateMutexW`. If `GetLastError = ERROR_ALREADY_EXISTS` a `MB_YESNOCANCEL` message box is shown with localized text from `[CustomMessages]`. IDYES: `FindWindowW` (class `'TSetupForm'`) locates the running installer and `PostMessageW WM_CLOSE` requests it to exit; after a 1.5 s pause the mutex is retried — if acquired the setup continues, otherwise the user is prompted again. IDNO and IDCANCEL both set `Result := False` (abort this installer). External function declarations (`CreateMutexW`, `CloseHandle`, `GetLastError` from `kernel32.dll`; `FindWindowW`, `PostMessageW` from `user32.dll`) and Pascal constants (`ERROR_ALREADY_EXISTS = 183`, `WM_CLOSE = 16`, `MB_YESNOCANCEL = 3`, `IDYES = 6`, `IDNO = 7`) declared in `template.iss`.
- **locale — mutex dialog strings in all 20 locale files**: Four keys added to every `locale/*.txt` file: `mutex_title`, `mutex_message` (neutral "What would you like to do?" form), `mutex_close_btn` ("Close the running installer"), `mutex_close_this_btn` ("Close this installer"). All 20 languages covered (da DK, de CH, de DE, el GR, en GB, es ES, fr CH, fr FR, is IS, it CH, it IT, nl BE, nl NL, no NB, no NN, pl PL, pt PT, rm CH, ro RO, uk UA).
- **inno/template.iss — `[CustomMessages]` in 22 installer languages**: Four `[CustomMessages]` keys (`mutex_title`, `mutex_message`, `mutex_close_btn`, `mutex_close_this_btn`) for all 22 supported Inno installer languages. The Inno `MB_YESNOCANCEL` dialog uses `CustomMessage('mutex_*')` so text matches whatever language the end user selected.

## [2026.05.06.09] - 2026-05-06

### settings / issgen — DirExistsWarning combo (09:15)
- **settings — `DirExistsWarning` combo (`IDC_SETT_DIR_EXISTS_WARNING = 8074`)**: New *Dir-exists warning:* combo in the Installation section. Three choices: *Auto (warn only when not upgrading)* (Inno default), *Yes (always warn)*, *No (never warn)*. Default: *Auto*. Setting to *No* suppresses the "Folder already exists" dialog entirely — useful for silent upgrade installers. Persisted as `dir_exists_warning`. Emitted as `DirExistsWarning=auto/yes/no` via `{#DirExistsWarning}` token. New `DirExistsWarningStr()` helper in `issgen.cpp`. Both `template.iss` copies updated. `SBuildConfig` gains `dirExistsWarning`.

### settings / issgen — UsePreviousAppDir & UsePreviousGroup toggles (09:11)
- **settings — `UsePreviousAppDir` checkbox (`IDC_SETT_USE_PREV_APP_DIR = 8072`)**: New *Remember last install path across upgrades* checkbox in the Installation section. Checked by default (mirrors Inno's own default). When ticked, Inno reads the previously chosen directory from the registry and pre-fills the *Where to install?* page on upgrades — users keep their custom path instead of reverting to `DefaultDirName`. Persisted as `use_prev_app_dir`. Emitted as `UsePreviousAppDir=yes/no` via `{#UsePreviousAppDir}` token.
- **settings — `UsePreviousGroup` checkbox (`IDC_SETT_USE_PREV_GROUP = 8073`)**: New *Remember last Start Menu folder across upgrades* checkbox, also checked by default. Pre-fills the Start Menu page with the previously chosen group name. Persisted as `use_prev_group`. Emitted as `UsePreviousGroup=yes/no` via `{#UsePreviousGroup}` token. Both `template.iss` copies updated. `SBuildConfig` gains `usePreviousAppDir` and `usePreviousGroup`.

### settings / issgen — DisableProgramGroupPage toggle (09:00)
- **settings — `DisableProgramGroupPage` checkbox (`IDC_SETT_DISABLE_PROG_GROUP_PAGE = 8071`)**: New *Hide "Select Start Menu folder" wizard page* checkbox in the Installation section, below the DisableDirPage checkbox. When ticked, the Inno wizard skips the Start Menu group selection page — shortcut always placed under `DefaultGroupName` without prompting. Useful for kiosk/enterprise deployments. Persisted as `disable_prog_group_page`. Emitted as `DisableProgramGroupPage=yes/no` via `{#DisableProgramGroupPage}` token. Both `template.iss` copies updated. `SBuildConfig` gains `disableProgramGroupPage`.

## [2026.05.06.08] - 2026-05-06

### settings / issgen — DisableDirPage toggle (08:57)
- **settings — `DisableDirPage` checkbox (`IDC_SETT_DISABLE_DIR_PAGE = 8070`)**: New *Hide "Where to install?" wizard page (fixed location)* checkbox in the Installation section, below the Default dir base combo. When ticked, the Inno wizard skips directory selection — app always installs to `Default dir base\AppName`. Persisted as `disable_dir_page`. Emitted as `DisableDirPage=yes/no` via `{#DisableDirPage}` token. Both `template.iss` copies updated. `SBuildConfig` gains `disableDirPage`.

### settings / issgen — Code Signing section (08:47)
- **settings — Section 6: Code Signing**: New section appended to the Settings page. Enable checkbox (`IDC_SETT_SIGN_ENABLE = 8060`) — all fields below are disabled while unchecked. Fields: signtool.exe path + browse (`IDC_SETT_SIGNTOOL_PATH = 8061`, `IDC_SETT_SIGNTOOL_BTN = 8062`; blank = use PATH); cert thumbprint (`IDC_SETT_SIGN_THUMBPRINT = 8063`); PFX file + browse (`IDC_SETT_SIGN_PFX_PATH = 8064`, `IDC_SETT_SIGN_PFX_BTN = 8065`); PFX password masked (`IDC_SETT_SIGN_PFX_PASS = 8066`); timestamp URL (`IDC_SETT_SIGN_TS_URL = 8067`, default: `http://timestamp.digicert.com`); timestamp digest combo SHA-1/SHA-256 (`IDC_SETT_SIGN_TS_ALGO = 8068`, default: SHA-256); description (`IDC_SETT_SIGN_DESC = 8069`). Persisted as `sign_*` DB keys. `SBuildConfig` gains nine fields.
- **issgen — `BuildSignToolLine()` + `SignToolLine` token**: Assembles Inno's `SignTool=sc … $f` directive; ISCC calls signtool after linking (eliminates SmartScreen "Unknown Publisher"). Thumbprint: `/sha1 <thumb>`; PFX: `/f <pfx> /p <pass>`. SHA-1 timestamp: `/t <url>`; SHA-256: `/tr <url> /td sha256 /fd sha256`. Disabled → empty string (blank line, silently ignored). Both `template.iss` copies updated with `{#SignToolLine}` after `VersionInfoCopyright`.

### settings / issgen — PrivilegesRequiredOverridesAllowed and WizardStyle (08:38)
- **settings — `PrivilegesRequiredOverridesAllowed` combo (`IDC_SETT_PRIV_OVERRIDES = 8024`)**: New *Allow override:* combo in the Installation section, below the UAC radios. Three choices: *None*, *Command line only*, *Dialog (For Me / All Users page)*. Default: *Dialog* — enables the wizard page already on the Dialogs page. Persisted as `priv_overrides`. Emitted as `PrivilegesRequiredOverridesAllowed=` in the generated `.iss`.
- **settings — `WizardStyle` combo (`IDC_SETT_WIZARD_STYLE = 8025`)**: New *Wizard style:* combo in the Installation section. *Modern* (Inno 6 sidebar bitmap) or *Classic* (top banner, Inno 5 style). Default: *Classic*. Persisted as `wizard_style`. Emitted as `WizardStyle=` in the generated `.iss`. `SBuildConfig` gains `privOverridesAllowed` and `wizardStyle` fields. Both `template.iss` copies updated with `{#PrivilegesRequiredOverridesAllowed}` and `{#WizardStyle}` placeholders.

### inno/template.iss — installer .exe version resource and AppCopyright (08:22)
- **inno/template.iss — installer .exe version resource (VersionInfo* directives)**: Seven Inno Setup `VersionInfo*` directives added to `template.iss` (both copies) and to the `issgen` token table: `VersionInfoVersion`, `VersionInfoTextVersion`, `VersionInfoDescription`, `VersionInfoProductName`, `VersionInfoProductVersion`, `VersionInfoCompany`, `VersionInfoCopyright`. All derived from existing project data — no new UI fields. `VersionInfoVersion`/`TextVersion`/`ProductVersion` → `proj.version`; `VersionInfoDescription` → `"<AppName> Setup"`; `VersionInfoProductName` → `proj.name`; `VersionInfoCompany` → `proj.app_publisher`; `VersionInfoCopyright` → `"© YYYY Publisher"` (year from `GetLocalTime()` at generation time). Without these, the installer's Properties → Details tab is blank — looks unprofessional and breaks enterprise deployment tools (SCCM, Intune) that inspect file version resources to detect installed versions.
- **inno/template.iss — `AppCopyright` directive**: `AppCopyright={#AppCopyright}` added alongside `VersionInfoCopyright`. Feeds the installer's own About box. Same `© YYYY Publisher` computed string as `VersionInfoCopyright`.

### build — .iss generator and Build > Compile Installer wired up
- **issgen — `ISS_GenerateIss()` .iss script generator**: New module `issgen.h` / `issgen.cpp`. `ISS_GenerateIss(templatePath, outPath, proj, cfg, langs)` reads `inno/template.iss` via Win32 (`CreateFileW` — wide paths on MinGW), strips UTF-8 BOM if present, expands every `{#Token}` placeholder with per-project values, replaces the `; <<LANGUAGES>>` marker with correct Inno `[Languages]` entries derived from `langs`, and writes the result as UTF-8 with BOM. `ISS_FindInnoDir()` locates the `inno/` directory relative to the running exe (exe-adjacent first, then `../inno/` for the development build layout).
- **issgen — Language section generation**: Each `InnoLangEntry` emits one `[Languages]` line. `local=false` → `Name: "english"; MessagesFile: "compiler:Default.isl"`. `local=true` (community file in `inno/`) → `Name: "swedish"; MessagesFile: "Swedish.isl"` (relative path resolved from the `.iss` location). `Name:` identifier = ISL base name lowercased; `Default` → `english`.
- **issgen — Token table**: Tokens substituted: `AppName`, `AppVersion`, `Publisher`, `PublisherURL`, `SupportURL`, `AppId` (braces escaped to `{{…}}` for Inno), `DefaultDirBase` (from `SETT_GetInstallBasePath()`), `OutputDir`, `OutputBase`, `Compression`, `SolidCompression`, `PrivilegesRequired`, `Uninstallable`, `CloseApplications`, `MinVersion`, `ExeName`, `SourceDir`. `OutputDir` falls back to the project directory when no output folder is configured.
- **settings — `SBuildConfig` struct and `SETT_GetBuildConfig()`**: New struct in `settings.h` with a snapshot of all build/install settings (`publisherUrl`, `supportUrl`, `outputFolder`, `outputFilename`, `compressionType`, `compressionLevel`, `solidCompression`, `uacLevel`, `minOsVersion`, `allowUninstall`, `closeApps`). `SETT_GetBuildConfig()` returns a populated snapshot from current statics.
- **inno/template.iss — fully parameterized `[Setup]` section**: All hardcoded `[Setup]` values replaced with `{#Token}` placeholders: `AppPublisher`, `AppPublisherURL`, `AppSupportURL`, `AppId`, `OutputDir`, `Compression`, `SolidCompression`, `PrivilegesRequired`, `Uninstallable`, `CloseApplications`. Hardcoded `[Languages]` entry replaced with `; <<LANGUAGES>>` marker. Both copies updated (`inno/template.iss` and `SetupCraft/inno/template.iss`).
- **mainwindow — `IDM_BUILD_COMPILE` / Build > Compile Installer (F7) wired up**: Was a stub `MessageBoxW`. Now calls `ISS_FindInnoDir()`, generates `<AppName>_generated.iss` in `inno/` via `ISS_GenerateIss()`, then launches `compile_inno.bat <generated.iss>` in a visible cmd window via `ShellExecuteW`. All error conditions surface via `MessageBoxW`.

## [2026.05.05.15] - 2026-05-05

### settings — Installer Languages section
- **settings — "Installer Languages" section in Settings page**: New section (Section 4, above Uninstall) with a 3-column checkbox grid of all 22 supported Inno Setup installer languages. English always on and its checkbox permanently disabled. Ticking additional languages causes the installer to show a language-selection dialog. Static `kInnoLangs[]` table of `InnoLang { isl, displayName, local }` structs; `local` flag distinguishes standard Inno files (`compiler:X.isl`) from community files shipped in `inno/` (relative path reference). State in `s_installerLangs` (vector<bool>); persisted as comma-separated ISL names via `DB::SetSetting` key `installer_langs`. English never stored — always present. `IDC_SETT_LANG_BASE = 8050`; checkbox IDs 8050–8071.
- **settings — `SETT_GetInstallerLanguages()` public accessor**: Returns `std::vector<InnoLangEntry>` (isl name + local flag). English (`isl=L"Default"`, `local=false`) always first; fallback to `{L"Default", false}` if empty. `InnoLangEntry` struct in `settings.h`. Compile pipeline uses `local` to emit `MessagesFile: "compiler:X.isl"` vs `MessagesFile: "X.isl"`.
- **inno/Swedish.isl — community Swedish translation**: Full Inno Setup 6.5.0+ Swedish message file (`LanguageID=$041D`, `LanguageCodePage=1252`) added to `inno/` and packaged by `makeit.bat`. `local=true` in the language table — referenced by relative path. Russian is intentionally absent.

### settings — DefaultDirName base path selector
- **settings — "Default dir base:" combo (`IDC_SETT_INSTALL_BASE = 8040`)**: New row in the Installation section of the Settings page. A drop-down combo lets the developer choose the Inno constant that forms the left side of `DefaultDirName`. Seven choices: `{pf}` (default, Program Files), `{pf64}`, `{pf32}`, `{localappdata}`, `{commonappdata}`, `{userdocs}`, `Custom…`. Stored as `s_installBase` (int 0–6); persisted via `DB::SetSetting` key `install_base`.
- **settings — Custom base edit (`IDC_SETT_INSTALL_BASE_CUSTOM = 8041`)**: Inline edit appearing to the right of the combo only when `Custom…` (index 6) is selected; hidden otherwise. Developer types any valid Inno constant or path string (e.g. `{autopf}`, `{code:GetDir}`). Falls back to `{pf}` when empty. Stored as `s_installBaseCustom`; persisted as `install_base_custom`.
- **settings — `SETT_GetInstallBasePath()` public accessor**: New function in `settings.h` returning the effective Inno token string, e.g. `L"{localappdata}"` or the custom string. For use by the compile pipeline to inject `#define DefaultDirBase <token>` into the generated `.iss`.
- **inno/template.iss — `{#DefaultDirBase}` placeholder**: `DefaultDirName` changed from hardcoded `{pf}\{#AppName}` to `{#DefaultDirBase}\{#AppName}` so the compile step can supply the correct token without touching the template.

### dialogs — Preview word-wrap; license image seeding; sizer width; PD logo filename
- **dialogs — Preview RichEdit word-wrap to window**: `WS_HSCROLL` removed from the preview RichEdit creation style. `WS_HSCROLL` was silently setting an infinite wrap target, causing content to overflow the right edge. Fix: remove the style flag and call `EM_SETTARGETDEVICE(NULL, 0)` after every `EM_STREAMIN`. Custom horizontal MSB scrollbar on the preview RichEdit removed (no longer needed).
- **dialogs — License default seeded with logo image**: `IDLG_ApplyDefaults` now re-seeds the License slot from the active template whenever the slot has no embedded image (`\pict`) but the template does. Fixes existing projects previously seeded from the image-less `dialog_defaults` row. Slots that already contain a custom image are left untouched.
- **dialogs — Sizer panel width `S(180)`**: Widened from `S(165)` to give all controls a comfortable right-side margin.
- **db — Unlicense logo filename corrected**: Template entry `img_file` and `LtMake` call corrected from `public-domain-logo-streamlined.png` to `public-domain-logo-cut.png` (matching the actual file in `LicenseImg\`). Picked up automatically on next startup via UPSERT seeding.

## [2026.05.05.14] - 2026-05-05

### settings — AppId (GUID) moved from Registry page to Settings page
- **settings — AppId field in Application section (`IDC_SETT_APP_ID = 8007`)**: Moved the read-only GUID display from Registry page to Settings page, Application section, after Support URL. Correct location: AppId goes in Inno `[Setup]` alongside `AppName`/`AppVersion`/`AppPublisher`, not with custom registry entries.
- **settings — Regenerate button (`IDC_SETT_REGEN_GUID = 8008`)**: Moved alongside the field. Same behaviour — confirmation dialog, `CoCreateGuid`, tooltip via subclass.
- **registry — AppId row removed**: `IDC_REG_APP_ID`/`IDC_REG_REGEN_GUID` controls removed from Registry page. Divider, backup button, warning icon, and split pane shift up by S(40).

## [2026.05.05.13] - 2026-05-05

### dialogs — Preview sizer: header font & color controls
- **dialogs — Header font section in sizer panel (IDC_IDLG_SZR_FONT_GLOBAL 7128 – IDC_IDLG_SZR_FONT_ITALIC 7133)**: Font family name edit, size spinner (0–72 pt), Bold/Italic checkboxes below existing size/alignment controls. Empty name and size 0 = Inno default. Live preview update via `RefreshPreviewHeader()`.
- **dialogs — Header color section in sizer panel (IDC_IDLG_SZR_CLR_GLOBAL 7134 – IDC_IDLG_SZR_CLR_BG 7136)**: Title fg and header bg color swatch buttons. Left-click = system color picker; right-click = reset to `IDLG_NOCOLOR`. Live preview via `WM_ERASEBKGND` (bg) and `WM_CTLCOLORSTATIC` (fg).
- **dialogs — "Use on all dialogs" toggles**: Each group (font, color) has an independent global checkbox. Per-dialog and global modes interoperate with full value-seeding semantics.
- **dialogs — Public accessors**: `IDLG_GetHeaderFont(t)`, `IDLG_GetHeaderFgColor(t)`, `IDLG_GetHeaderBgColor(t)`, `IDLG_IsHeaderFontGlobal()`, `IDLG_IsHeaderColorGlobal()`. `IdlgHeaderFont` struct and `IDLG_NOCOLOR` in `dialogs.h`.
- **dialogs — Persistence**: Global flags + global values + per-dialog values persisted via `DB::SetSetting` (keys: `installer_hdr_font_*` and `installer_hdr_clr_*`).

## [2026.05.05.12] - 2026-05-05

### dialogs — Ready page: “Show summary of choices” section
- **dialogs — “Always show destination directory” checkbox (`IDC_IDLG_READY_SHOW_DIR = 7074`)**: New sub-section below the Ready to Install row. When checked (the default, matching Inno’s own default), emits `AlwaysShowDirOnReadyPage=yes` in Inno `[Setup]` so the Ready page always lists the chosen destination directory even when the directory-selection page is hidden. When unchecked, the directive is omitted. Persisted as `installer_ready_show_dir_<pid>` via `DB::SetSetting`.
- **dialogs — “Always show Start Menu group” checkbox (`IDC_IDLG_READY_SHOW_GROUP = 7075`)**: Same pattern for the Start Menu group name. Emits `AlwaysShowGroupOnReadyPage=yes` when checked (default). Persisted as `installer_ready_show_group_<pid>`.
- **dialogs — Public accessors**: `IDLG_GetReadyShowDir()` and `IDLG_GetReadyShowGroup()` in `dialogs.h` for script generation.

### dialogs — Finish page: “Launch app when done” section
- **dialogs — “Launch app when the installer finishes” checkbox (`IDC_IDLG_FINISH_LAUNCH = 7070`)**: New sub-section below the Finish row on the Dialogs page. When enabled, emits a `[Run]` entry in the Inno script: `Filename: "{app}\{#MyAppExeName}"; Description: "..."; Flags: nowait postinstall shellexec skipifsilent`. This is the standard Inno mechanism for the “Launch AppName now” checkbox on the Finish page — surfaced directly on the Dialogs page rather than requiring the developer to know about `SWR_FINISH_OPTOUT` on the Scripts page.
- **dialogs — Description text edit (`IDC_IDLG_FINISH_LAUNCH_DESC = 7072`)**: Editable label shown next to the checkbox in the installer. Defaults to `Launch {#MyAppName}`. Inno macros expanded at compile time. Disabled when main toggle is off. Persisted as `installer_finish_launch_desc_<pid>` via `DB::SetSetting`.
- **dialogs — “Default checked” toggle (`IDC_IDLG_FINISH_LAUNCH_DEFCHK = 7073`)**: When checked (default), the launch checkbox is pre-ticked. When unchecked, Inno adds `Flags: unchecked` so the end user must opt in. Disabled when main toggle is off. Persisted as `installer_finish_launch_defchk_<pid>` via `DB::SetSetting`.
- **dialogs — Preview integration**: Previewing the Finish dialog when launch is enabled shows the launch checkbox (with current desc text and default-checked state) in the extras panel.
- **dialogs — Public accessors**: `IDLG_GetFinishLaunchEnabled()`, `IDLG_GetFinishLaunchDesc()`, `IDLG_GetFinishLaunchDefaultChecked()` in `dialogs.h` for script generation.

### dialogs — Per-dialog enabled toggle for always-visible installer pages
- **dialogs — Per-dialog enable checkbox (`IDC_IDLG_ROW_ENABLE_BASE = 7060`)**: The four always-present installer dialog rows — Welcome, License, Ready to Install, and Finish — now each carry a custom checkbox in place of the plain name label. When unchecked, the dialog is excluded from the generated installer script and skipped during preview Back/Next navigation. The Install (progress bar) dialog cannot be disabled. Conditional rows (Dependencies, For Me/All Users, Components, Shortcuts) retain their existing label. Control IDs: `IDC_IDLG_ROW_ENABLE_BASE + InstallerDialogType` (range 7060–7068; only four created). Tooltip key: `idlg_enable_tip`.
- **dialogs — `s_dialogEnabled[IDLG_COUNT]` state array**: New `bool s_dialogEnabled[IDLG_COUNT]`, all `true` by default. Reset to all-`true` in `IDLG_Reset()`. Persisted per-project via `DB::SetSetting` key `installer_dialog_enabled_<pid>` as a compact 9-char string. Restored in `IDLG_LoadFromDb()`.
- **dialogs — `IsDialogActiveInInstaller()` helper**: New module-private predicate combining `IsDialogVisible()` with `s_dialogEnabled[]`. `NextVisibleType()` and `PrevVisibleType()` updated to use it so disabled pages are skipped in preview navigation. `IDLG_INSTALL` is always active.
- **dialogs — `IDLG_IsDialogEnabled(InstallerDialogType)` public accessor**: New public function in `dialogs.h`. Returns `true` when the dialog is developer-enabled. Always `true` for `IDLG_INSTALL`. For script generation: emit `DisableWelcomePage=yes`, `DisableReadyPage=yes`, or `DisableFinishedPage=yes` in the Inno `[Setup]` section when the corresponding type is disabled.

## [2026.05.05.11] - 2026-05-05

### edit_rtf — Word-wrap-to-window mode; license editor uses it
- **edit_rtf — `RtfEditorData::wrapToWindow` flag**: New `bool wrapToWindow = false` field. When `true`, the editor calls `EM_SETTARGETDEVICE(NULL, 0)` so text wraps to the window width and no horizontal scrollbar is needed. `false` (default) keeps the existing 32767-px page-layout canvas.
- **dialogs — License editor word-wraps to window**: The “Edit Content…” RTF editor for the License dialog row now sets `ed.wrapToWindow = true`. Long license template paragraphs previously required horizontal scrolling; they now wrap to fit the viewport.

## [2026.05.05.10] - 2026-05-05

### db — License template content & images fully embedded
- **db — License template canonical RTF content**: All 19 license templates now carry complete, canonical, display-ready RTF text in the `license_templates` table. Each template uses the shared RTF colour palette (`\cf1` blue titles/headers, `\cf2` dark-grey body, `\cf4` dark-red warranty/liability disclaimers) and begins with the logo image embedded as a `\pict\pngblip` RTF block (PNG hex-encoded inline). Copyright placeholder lines and the `LT_CREDIT` italic credit line removed from all templates where the license text does not require owner attribution — the developer is not prompted to add a copyright notice they do not need. Developer can still freely edit any template after selecting it.
- **db — UPSERT seeding (`INSERT … ON CONFLICT … DO UPDATE`)**: Changed from `INSERT OR IGNORE` to a full UPSERT so all template content (RTF text, image) is refreshed on every app startup. Any fix or improvement to a built-in template is picked up automatically without a manual DB wipe.
- **db — GNU logo switched to PNG (`GnuLogo.png`)**: All five GNU license entries (GPL v2, GPL v3, LGPL v2.1, LGPL v3, AGPL v3) now reference `GnuLogo.png` instead of `GnuLogo.bmp`. The RTF `\dibitmap0` blip type for BMP is not rendered by the Windows RichEdit control; `\pngblip` is. `GnuLogo.png` is included in `LicenseImg\`.
- **makeit.bat — `LicenseImg\` always copied**: Build script now copies `LicenseImg\` to both `build\` (test runs) and `SetupCraft\` (package) on every build. Previously missing, causing all license logo images to be absent from the DB after a fresh package run.
- **apache.png — white background**: Apache logo PNG reprocessed to replace near-black background with white via `System.Drawing` flood-fill, so the logo displays correctly on the white RTF background.

## [2026.05.05.08] - 2026-05-05

### Dialogs — License file type selector
- **dialogs — License source selector (`IDC_IDLG_LICENSE_SRC = 7050`)**: New "License source:" label and combo at the top of the License row sub-section. Two options: *Built-in RTF editor* (default — unchanged behaviour) and *External file (.rtf or .txt)* (developer points at an existing file on disk). Switching to External hides the template combo and shows a "File path:" label (`IDC_IDLG_LICENSE_FILE_LBL = 7051`), a read-only path edit (`IDC_IDLG_LICENSE_FILE_EDIT = 7052`), and a "Browse…" button (`IDC_IDLG_LICENSE_FILE_BROWSE = 7053`) filtered to `*.rtf;*.txt`. "Edit Content…" is disabled when External is selected. State in `s_licenseSource` (int) + `s_licenseFilePath` (wstring); persisted as `installer_license_source_<pid>` / `installer_license_file_<pid>`. New public accessors: `IDLG_GetLicenseSource()` / `IDLG_GetLicenseFilePath()`. Locale keys: `idlg_license_src_label`, `idlg_license_src_builtin`, `idlg_license_src_external`, `idlg_license_file_label`, `idlg_license_file_browse`.

## [2026.05.04.14] - 2026-05-04

### Dialogs — License template selector
- **dialogs / db — License template selector (`IDC_IDLG_LICENSE_TEMPLATE = 7047`)**: New "License template:" label and drop-down combo added above the "Require license acceptance" checkbox on the License row. Selecting a template loads the full canonical RTF text into the License dialog content slot in memory (`<<AppName>>` and `<<Year>>` substituted at load time using the project name and current year). Selected template ID persisted per-project via `DB::SetSetting` key `installer_license_template_<pid>`. Static label: `IDC_IDLG_LICENSE_TEMPLATE_LBL = 7048`. Locale key: `idlg_license_template_label`.
- **db — `license_templates` table**: New read-only catalogue table seeded in `InitDb()` with `INSERT OR IGNORE`. Columns: `id` (PK), `name`, `spdx_id`, `img_file` (filename in `LicenseImg\`, empty = none), `content_rtf`. 19 licences seeded at id 0–18: The Unlicense (0, default), MIT (1), Apache 2.0 (2), GNU GPL v2 (3), GNU GPL v3 (4), GNU LGPL v2.1 (5), GNU LGPL v3 (6), GNU AGPL v3 (7), BSD 2-Clause (8), BSD 3-Clause (9), ISC (10), MPL 2.0 (11), Boost 1.0 (12), EUPL 1.2 (13), CC0 1.0 (14), CC-BY 4.0 (15), CC-BY-SA 4.0 (16), Artistic 2.0 (17), WTFPL (18). All use the shared RTF colour palette (\cf1 blue title, \cf2 dark body, \cf3 grey credit, \cf4 dark-red disclaimer). New DB accessors: `DB::GetAllLicenseTemplates()` → `vector<LicenseTemplateInfo>` (id, name, img_file); `DB::GetLicenseTemplateRtf(int id)` → `wstring`.
- **LicenseImg\ folder**: New deployment folder with logo images for each template — `apache.png` (converted from ASF SVG via ImageMagick, 300×157 px), `BSD.png`, `BSL-1.0 (Boost).png`, `CC-BY-4.0.png`, `CC-BY-SA-4.0.png`, `CC0-1.0.png`, `Logo_EUPL.svg.png`, `Mozilla.png`, `OpeenSourceInitiative.png`, `wtfpl-badge-1.png`; GNU licences use `GnuLogo.png` (converted from `GnuLogo.bmp`). The `img_file` column links each template to its logo; preview rendering is the next step.

## [2026.05.04.11] - 2026-05-04

### Dependencies — Version check source; UAC fix; makeit kill fix; Dialogs — license must-accept toggle
- **dep_edit_dialog / deps / db — Version check source field (`IDC_DEPDLG_VER_SOURCE = 428`)**: New combo in the Detection section (between "File path to detect" and "Minimum required version") that disambiguates where the version string is read for `min_version`/`max_version` comparison. Options: *(no version check)* (default), *Registry key value*, *File version resource*. Stored in `ExternalDep::detect_version_source` (`DepVersionSource` enum: `DVS_NONE=0`, `DVS_REGISTRY=1`, `DVS_FILE=2`). DB migration: `ALTER TABLE external_deps ADD COLUMN detect_version_source INTEGER DEFAULT 0` (idempotent). `InsertExternalDep` extended to 22 bound params; `GetExternalDepsForProject` SELECT extended to column 21. Reflow guard raised from `>=9` to `>=11`; combo at indices [5–6], min/max shifted to [7–10]. Locale keys: `dep_dlg_ver_source`, `dep_ver_source_none`, `dep_ver_source_registry`, `dep_ver_source_file`.
- **app.manifest — UAC elevation fixed (`requestedExecutionLevel level="asInvoker"`)**: Windows' installer-detection heuristic auto-elevates executables whose name contains "Setup". Added an explicit `<trustInfo>` block with `level="asInvoker" uiAccess="false"` to opt out. SetupCraft.exe now launches without a UAC prompt.
- **makeit.bat — process-kill timing fix**: Added `ping -n 2 127.0.0.1` delay after `taskkill` to give Windows time to release file handles before the package folder is wiped and the new exe copied in.
- **dialogs — License must-accept toggle (`IDC_IDLG_LICENSE_ACCEPT = 7046`)**: New custom checkbox below the License row on the Dialogs page: "Require license acceptance (end user must choose 'I accept' before proceeding)". Defaults to checked. When checked the License preview enters split layout with a radio-button pair — *"I accept the license agreement"* (default unchecked) and *"I do not accept the license agreement"* (default checked). Reuses `hRadioMe`/`hRadioAll` in `PopulateExtras`; `SetWindowTextW` updates labels when navigating between License and For Me / All Users. State in `s_licenseMustAccept` (bool). Persisted via `DB::SetSetting` key `installer_license_must_accept_<pid>`. Public accessor `IDLG_GetLicenseMustAccept()` for script generation. Locale keys: `idlg_license_must_accept`, `idlg_lic_accept`, `idlg_lic_no_accept`.

## [2026.05.03.14] - 2026-05-03

### Dependencies — Per-dep component linkage
- **dep_edit_dialog / deps / db — Component linkage field (`IDC_DEPDLG_COMP_EDIT = 426`, `IDC_DEPDLG_COMP_PICK = 427`)**: New optional row visible for **all** delivery types whenever the project has at least one component defined. Specifies which components must be selected for this dep to be installed; empty = install unconditionally (default — zero behavior change for existing projects). The read-only edit shows the current space-separated component names; the adjacent `…` button opens a self-contained modal picker (`PickCompDlgProc`, `LBS_EXTENDEDSEL` listbox). Section absent when no components exist, preserving simplicity. Stored in `ExternalDep::required_components` (`std::wstring`). DB migration: `ALTER TABLE external_deps ADD COLUMN required_components TEXT DEFAULT ''` (idempotent). `InsertExternalDep` extended to 21 bound params; `GetExternalDepsForProject` SELECT extended to column 20. Locale keys: `dep_dlg_components`, `dep_dlg_comp_pick_title`. Component names sourced from `MainWindow::GetComponents()` at dialog-open time; sorted + deduplicated in `deps.cpp`.

## [2026.05.03.13] - 2026-05-03

### Dependencies — Download timeout; exit codes; max version; help button
- **dep_edit_dialog / deps / db — Download timeout field (`IDC_DEPDLG_TIMEOUT = 422`)**: New integer edit below "If offline:" in the Edit Dependency dialog, visible only for **Auto-download** delivery mode. Accepts seconds to wait before giving up on a download; `0` = no timeout (wait forever). Created with `ES_NUMBER` via direct `CreateWindowExW` (bypasses the `MkEdit` lambda which lacks `ES_NUMBER`). Stored in `ExternalDep::download_timeout_sec` (default 0). DB migration: `ALTER TABLE external_deps ADD COLUMN download_timeout_sec INTEGER DEFAULT 0` (idempotent). `InsertExternalDep` extended to 18 bound params; `GetExternalDepsForProject` SELECT extended to column 17. Locale key: `dep_dlg_timeout`.
- **dep_edit_dialog / deps / db — Acceptable exit codes field (`IDC_DEPDLG_EXIT_CODES = 423`)**: New plain-text edit visible for **Bundled** and **Auto-download** modes (the two modes where an installer exe is actually executed). Accepts a space-separated list of exit codes treated as success in addition to 0 — e.g. `3010 1641`. 3010 = `ERROR_SUCCESS_REBOOT_REQUIRED`, 1641 = `ERROR_SUCCESS_REBOOT_INITIATED`; SetupCraft detects these at install-time and prompts the user before any reboot. Stored in `ExternalDep::extra_exit_codes` (`std::wstring`). DB migration: `ALTER TABLE external_deps ADD COLUMN extra_exit_codes TEXT DEFAULT ''` (idempotent). `InsertExternalDep` extended to 19 bound params; `GetExternalDepsForProject` SELECT extended to column 18. Locale key: `dep_dlg_exit_codes`.
- **dep_edit_dialog — Exit codes help button (`IDC_DEPDLG_EXIT_CODES_HELP = 425`)**: Square icon-only button (shell32.dll #23, the "?" help glyph) placed to the right of the exit-codes edit. Hover tooltip "Common exit codes reference" via `SetButtonTooltip`. Click opens `ShowValidationDialog` with a reference list: reboot codes 3010/1641 (safe to add) + seven other common codes (1 general error, 2 file not found, 5 access denied, 1602 user cancelled, 1603 fatal install error, 1618 another install running, 1638 another version installed). All strings locale-backed. Locale keys: `dep_dlg_exit_codes_help_tip`, `dep_dlg_exit_codes_ref_title`, `dep_dlg_exit_codes_ref_body`.
- **dep_edit_dialog / deps / db — Maximum allowed version field (`IDC_DEPDLG_MAX_VER = 424`)**: Optional plain-text edit below "Minimum required version" in the Detection section. Upper version bound for deps incompatible with versions that are too new (e.g. targeting a specific runtime range). Empty = no upper bound. Stored in `ExternalDep::max_version` (`std::wstring`). DB migration: `ALTER TABLE external_deps ADD COLUMN max_version TEXT DEFAULT ''` (idempotent). `InsertExternalDep` extended to 20 bound params; `GetExternalDepsForProject` SELECT extended to column 19. Locale key: `dep_dlg_max_version`.
- **dep_edit_dialog — Reflow detection-section guard fixed (7 → 9)**: After adding `max_version` as a 4th label + edit pair, the Reflow guard was still `ctrls.size() >= 7`, leaving the two new controls unpositioned at y = 0 and overlapping earlier content. Guard raised to `>= 9`; all four rows (registry key, file path, min version, max version) now correctly placed.

## [2026.05.03.12] - 2026-05-03

### Shortcuts — Arguments and Comment fields; Pin button context-menu cleanup
- **sc_shortcut_dialog / shortcuts — Arguments field (`IDC_SCDLG_ARGUMENTS = 5230`)**: New full-width text edit below "Run in folder:" in the Configure Shortcut dialog. Maps to Inno's `Parameters:` field — command-line arguments passed to the target executable at launch. Stored in `ShortcutDef::arguments` / `ScShortcutRow::arguments`. DB migration: `ALTER TABLE sc_shortcuts ADD COLUMN arguments TEXT DEFAULT ''` (idempotent). All 4 call sites in shortcuts.cpp wired; save/load paths updated. Locale keys: `scdlg_arguments_label`, `scdlg_arguments_tooltip`.
- **sc_shortcut_dialog / shortcuts — Comment field (`IDC_SCDLG_COMMENT = 5231`)**: New full-width text edit below Arguments. Maps to Inno's `Comment:` field — sets the tooltip shown when the user hovers the shortcut in Windows Explorer or the Start Menu. Stored in `ShortcutDef::comment` / `ScShortcutRow::comment`. DB migration: `ALTER TABLE sc_shortcuts ADD COLUMN comment TEXT DEFAULT ''` (idempotent). All 4 call sites wired; save/load paths updated. Locale keys: `scdlg_comment_label`, `scdlg_comment_tooltip`.
- **sc_shortcut_dialog / shortcuts — Hot key field (`IDC_SCDLG_HOTKEY = 5232`, `IDC_SCDLG_HOTKEY_CLEAR = 5233`)**: New row below Comment with a `msctls_hotkey32` (HOTKEY_CLASS) control and a red Clear button. Captures a key combination directly; converts to/from Inno's `HotKey:` string format (e.g. `ctrl+alt+h`, `ctrl+shift+F5`) via `InnoHotKeyToWord`/`WordToInnoHotKey`. Stored in `ShortcutDef::hotkey` / `ScShortcutRow::hotkey`. DB migration: `ALTER TABLE sc_shortcuts ADD COLUMN hotkey TEXT DEFAULT ''` (idempotent). All 4 call sites wired; save/load paths updated. Locale keys: `scdlg_hotkey_label`, `scdlg_hotkey_tooltip`, `scdlg_hotkey_clear_tooltip`.
- **shortcuts — Pin to Start / Taskbar buttons context-menu stub removed**: Right-clicking the Pin to Start or Pin to Taskbar icon buttons previously showed a greyed-out "Configure shortcut…" context-menu stub left over from the initial right-click infrastructure. The stub is removed; the right-click context menu now appears only on the Desktop icon button.

## [2026.05.03.11] - 2026-05-03

### Registry page — Add/Edit Value dialog layout fixes + HKA auto-root in TreeView
- **mainwindow — syntax hint label no longer clipped (`AV_HINT_H` 14 → 20)**: The "Any text (Unicode string)" hint below the Data field was rendered at only 14 design-px — too short for the 1.2× scaled NONCLIENTMETRICS font; descenders (g, p, y) were clipped. `AV_HINT_H` raised to 20 px; both Add and Edit dialog height formulas inherit the change automatically.
- **mainwindow — checkbox row height increased (`AV_CHK_H` 24 → 28) + separate `AV_RAD_H = 24` for radio rows**: Extra 4 px margin ensures descenders are never clipped and adds visual breathing room between flag rows. Radio-button rows in the Registry view section keep 24 px via new `AV_RAD_H` constant (short labels, never need extra space), keeping the overall dialog compact.
- **mainwindow — HKA pseudo-root added to registry TreeView**: New **HKA** root node (first in list, expanded with `SOFTWARE → [Publisher] → [AppName]`) exposes Inno's auto-root: resolves to `HKCU` for per-user installs, `HKLM` for all-users. Eliminates the need to duplicate entries under both hives. Hover tooltip via `TVN_GETINFOTIP` (`TVS_INFOTIP` added). All persistence paths (custom keys/values, hive/path split) handle HKA transparently.
- **Locale key added** (both `locale/en_GB.txt` files): `reg_hka_tooltip`.

## [2026.05.03.10] - 2026-05-03

### Components dialogs — centred dependency buttons + full RTF editor
- **mainwindow — Edit Folder dialog: Remove / Choose buttons centred**: `IDC_FOLDER_DLG_REMOVE_DEPS` (Remove) and `IDC_FOLDER_DLG_CHOOSE_DEPS` (Choose…) buttons below the dependencies list in `CompFolderEditDlgProc` were right-aligned. Both are now centred in the content area, consistent with the OK / Cancel row below them.
- **mainwindow — Edit Component dialog: Remove / Choose buttons centred**: Same fix applied to `CompEditDlgProc` — `IDC_COMPDLG_REMOVE_DEPS` / `IDC_COMPDLG_CHOOSE_DEPS` pair is now centred rather than right-aligned.
- **mainwindow — Notes editor upgraded to full RTF editor**: Both dialogs previously called `OpenNotesEditor` (the lightweight fixed-size popup). The Notes / Description button now opens `OpenRtfEditor` (full-featured resizable RTF editor with 19-control toolbar: Bold / Italic / Underline / Strikethrough / Sub / Super / Font Face / Font Size / Align L/C/R/J / Bullet / Numbered / Text colour / Highlight / Insert Image / Open file / Insert table). `#include "edit_rtf.h"` added to `mainwindow.cpp`; `pLocale` wired to the application locale map so all editor tooltip strings are localised.

## [2026.05.03.09] - 2026-05-03

### Registry page — Components picker redesigned: multi-select tree with real file icons
- **mainwindow — `PickCompDialogProc` redesigned**: `TVS_CHECKBOXES` removed in favour of `TVS_FULLROWSELECT | TVS_SHOWSELALWAYS`. System small-icon image list (`SHGetFileInfoW SHGFI_SYSICONINDEX`) attached — every item shows its real Windows file/folder icon (actual path tried first, extension fallback). Multi-select driven by `PickMultiCtx` heap struct + `PickTree_SubclassProc` subclass: plain click → single; Ctrl+click → toggle; Shift+click → contiguous range from anchor; Ctrl+A → all non-header visible items. Highlight drawn via `NM_CUSTOMDRAW / CDDS_ITEMPREPAINT` with `COLOR_HIGHLIGHT`. Previously-selected items pre-highlighted. `PickComp_CollectSelected` collects in tree order.
- **mainwindow — custom icon buttons**: Plain `BS_PUSHBUTTON` OK/Cancel replaced with `CreateCustomButtonWithIcon` (Green OK / Red Cancel) + `WM_DRAWITEM` handler. Widths `S(110)` with `S()`-scaled gap.
- **mainwindow — localized picker title**: `pick_comp_title` locale key looked up in `IDC_ADDVAL_COMP_PICK` handler; window title localized.
- **Locale keys added** (both `locale/en_GB.txt` files): `pick_comp_title`.

## [2026.05.02.15] - 2026-05-02

### Registry page — Add/Edit Value dialog: Components field UX
- **mainwindow — hint-to-components spacing**: `AV_GAP_RD` increased from 10 px to 18 px so the data-type syntax hint label (`IDC_ADDVAL_HINT`) is no longer clipped by the Components row. Both `clientH` calculations automatically inherit the larger gap.
- **mainwindow — Components field changed to read-only + picker button**: `IDC_ADDVAL_COMPONENTS` is now `ES_READONLY` (narrowed). `IDC_ADDVAL_COMP_PICK = 5080` "…" button added to the right. `AV_PICK_W = 30` layout constant added.
- **mainwindow — `PickCompDialogProc` (TVS_CHECKBOXES tree picker)**: Opens a 400×500 px `PickCompDialog` with a `TVS_CHECKBOXES | TVS_HASLINES | TVS_HASBUTTONS` tree view. Section roots from `dest_path`, folder-type components as parent nodes, file-type components nested by `source_path` prefix. Items already in the field are pre-checked. `PickComp_CollectChecked` recurses on OK to produce the space-separated result. Silent if no components defined.

## [2026.05.02.14] - 2026-05-02

### Registry page — Components hint spacing fix and multiselect picker
- **mainwindow — hint-to-components spacing**: `AV_GAP_RD` increased from 10 px to 18 px so the data-type syntax hint label (`IDC_ADDVAL_HINT`) is no longer clipped by the Components row immediately below it. Both `clientH` calculations automatically inherit the larger gap.
- **mainwindow — Components field changed to read-only + picker button**: `IDC_ADDVAL_COMPONENTS` is now an `ES_READONLY` edit (narrowed to leave room for the button). A new `IDC_ADDVAL_COMP_PICK = 5080` "…" button sits immediately to its right. `AV_PICK_W = 30` layout constant added for the button width.
- **mainwindow — `PickCompDialogProc` redesigned as checkbox tree view**: The flat `LBS_MULTIPLESEL` listbox is replaced with a `TVS_CHECKBOXES | TVS_HASLINES | TVS_HASBUTTONS` tree view. The tree mirrors the Components page hierarchy: section roots (*Program Files*, *ProgramData*, …) derived from `dest_path`, folder-type components as expandable parents, and file-type components nested under their containing folder (matched by `source_path` prefix). Items already in the Components field are pre-checked. `PickComp_CollectChecked` recursively walks the tree on OK to produce the space-separated result. Dialog resized to 400×500 px.

## [2026.05.02.13] - 2026-05-02

### Registry page — per-entry flags, registry view selector, GUID field protection, data syntax hints, and per-component linkage
- **db.h / db.cpp — `flags` on `RegistryEntryRow`**: New `std::wstring flags` field. `ALTER TABLE registry_entries ADD COLUMN flags TEXT DEFAULT ''` migration in `InitDb` (idempotent). `InsertRegistryEntry` extended with `const std::wstring& flags = L""` parameter; SQL binds it as parameter 7. `GetRegistryEntriesForProject` SELECTs and reads column 6 as `r.flags`.
- **mainwindow — Add/Edit Value dialog expanded**: `AddValueDialogData` gains `std::wstring valueFlags` and `std::vector<RECT> sectionRects`. Two new sections appear below the Data field with 1 px `COLOR_BTNSHADOW` border rectangles: **Uninstall behaviour** (7 checkboxes in two columns: `deletevalue`, `uninsdeletevalue`, `dontcreatekey`, `preservestringtype` on the left; `deletekey`, `uninsdeletekey`, `uninsdeletekeyifempty` on the right) and **Registry view** (3 `BS_AUTORADIOBUTTON`s: Default / 32-bit / 64-bit). Dialog class uses `WHITE_BRUSH`; `WM_CTLCOLORSTATIC` and `WM_CTLCOLORBTN` return `WHITE_BRUSH`. Section headers use a bold NONCLIENTMETRICS font stored as `hHdrFont`. Pre-population restores all 7 checkbox states and the correct radio on Edit.
- **mainwindow — flags collected on OK**: All 7 checkbox IDs and 2 radio IDs read; flags assembled into a space-separated string (e.g. `uninsdeletevalue 64bit`) and stored in `pData->valueFlags`. IDC constants `IDC_ADDVAL_F_DELETEVALUE = 5065` … `IDC_ADDVAL_F_ARCH_64BIT = 5077`.
- **mainwindow — ListView Flags column**: 4th column "Flags" (`reg_col_flags` locale key, 20 % width) added. All 4 populate spots call `ListView_SetItemText(..., 3, entry.flags.c_str())`.
- **mainwindow — load / save / add / edit paths wired**: `RegistryEntry.flags` field added. Loaded from DB on project open, passed into Add/Edit dialog, written back after OK, persisted via updated `InsertRegistryEntry`.
- **mainwindow — hover tooltips on all flag controls**: Each checkbox and radio subclassed with `AVFlag_SubclassProc` — calls `ShowMultilingualTooltip` (custom yellow tooltip) on `WM_MOUSEMOVE`, `HideTooltip` on `WM_MOUSELEAVE`. Tooltip text (2–3 sentences per flag) stored as window property `avTip`. Win32 `TOOLTIPS_CLASS` not used — it does not fire over owner-drawn checkboxes.
- **mainwindow — GUID field made read-only static**: `IDC_REG_APP_ID` changed from an `ES_READONLY` EDIT to a plain STATIC (`SS_LEFT | SS_CENTERIMAGE | SS_NOPREFIX`), matching the install-folder field. Text rendered dark blue (`RGB(0, 51, 153)`) via `WM_CTLCOLORSTATIC`. Regenerate button and save path continue to use `SetWindowTextW` / `GetWindowTextW`, which work on STATIC controls.
- **mainwindow — Add/Edit Value dialog syntax hint**: A gray `STATIC` label (`IDC_ADDVAL_HINT = 5078`) sits immediately below the Data edit field and shows the expected format for the selected type — e.g. `"Decimal: 0–4294967295   or   Hex: 0x00000000"` for `REG_DWORD`. Updates live on `CBN_SELCHANGE` via `GetRegTypeHint()`. Rendered in `RGB(120, 120, 120)` gray via `WM_CTLCOLORSTATIC` ID check. Both `clientH` calculations include `AV_GAP_DH + AV_HINT_H`.
- **mainwindow — per-entry component linkage**: `IDC_ADDVAL_COMPONENTS = 5079` — a new "Components:" EDIT row in the Add/Edit Value dialog, below the hint label. User types space-separated component names (e.g. `main extra`) matching those defined in the Components page. Stored in `RegistryEntry::components` / `AddValueDialogData::valueComponents`. DB: `ALTER TABLE registry_entries ADD COLUMN components TEXT DEFAULT ''` migration; `InsertRegistryEntry` takes an 8th `components` parameter; `GetRegistryEntriesForProject` reads column 7. 5th ListView column "Components" (`reg_col_components`, 17 % width). Column widths: Name 22 %, Type 14 %, Data 27 %, Flags 20 %, Components 17 %. `AV_GAP_CF = 14` layout constant; both `clientH` calculations extended with `AV_ROW_H + AV_GAP_CF`. All 4 populate spots and add/edit/delete/load paths wired.
- **Locale keys added** (both `locale/en_GB.txt` files): `reg_col_flags`, `reg_col_components`, `reg_add_value_components`, `reg_flg_sec_uninstall`, `reg_flg_sec_view`, `reg_flg_deletevalue`, `reg_flg_uninsdeletevalue`, `reg_flg_dontcreatekey`, `reg_flg_preservestringtype`, `reg_flg_deletekey`, `reg_flg_uninsdeletekey`, `reg_flg_uninsdeletekeyifempty`, `reg_flg_view_default`, `reg_flg_view_32bit`, `reg_flg_view_64bit`.

## [2026.05.02.12] - 2026-05-02

### Registry page — AppId (GUID) feature + field alignment
- **db.h / db.cpp — `app_id` field on `ProjectRow`**: New `std::wstring app_id` field added to the `ProjectRow` struct. `ALTER TABLE projects ADD COLUMN app_id TEXT DEFAULT ''` migration in `InitDb` (idempotent — existing databases are upgraded silently). `InsertProject` auto-generates a GUID via `CoCreateGuid` + `StringFromGUID2` and binds it as parameter 8. `UpdateProject` persists it as parameter 11. `ListProjects` and `GetProject` auto-generate and immediately persist a GUID if the loaded row has an empty `app_id` — so every existing project gets a stable GUID on first open, with no developer action required.
- **Registry page — AppId row**: A new "AppId (GUID):" row appears below Publisher. The label uses the `reg_app_id` locale key; the edit field (`IDC_REG_APP_ID = 5059`) is read-only (`ES_READONLY`) and 300 px wide — the GUID is always machine-generated and never typed by hand. A compact Blue icon-only button (`IDC_REG_REGEN_GUID = 5070`, shell32 #238, `S(30)` wide) sits immediately to the right and regenerates the GUID on click.
- **Regenerate confirmation dialog**: Clicking the Regenerate button shows a custom `ShowConfirmDeleteDialog` modal (not `MessageBox`) with a styled "Yes, regenerate" / "Cancel" button pair and a properly expanded multi-line warning (via `ExpandEscapes`). New locale keys: `reg_regen_warn_title`, `reg_regen_warn_msg`, `reg_regen_warn_yes`, `reg_regen_warn_no`.
- **Custom tooltip on Regenerate button**: `RegenBtn_SubclassProc` subclasses the button HWND and calls `ShowMultilingualTooltip` with the `reg_regen_guid` locale text on `WM_MOUSEMOVE`, hides on `WM_MOUSELEAVE`. Uses `TrackMouseEvent(TME_LEAVE)` — same pattern as other icon tooltips. `TTM_ADDTOOL` is not used because it does not work reliably on owner-drawn icon-only buttons. Statics: `s_prevRegenBtnProc`, `s_regenBtnTooltipTracking`.
- **Registry page — field alignment**: Display Name, Version, and Publisher labels moved from `S(220)` to `S(370)`; their edit fields moved from `S(325)` (300 px wide) to `S(470)` with width `rc.right - S(540)` so all four rows (Display Name / Version / Publisher / AppId) share the same label and field X positions. WM_SIZE updated to match. AppId field stays fixed-width (`S(300)`) since the GUID length is constant.
- **Locale keys added** (both `locale/en_GB.txt` files): `reg_app_id`, `reg_regen_guid`, `reg_regen_warn_title`, `reg_regen_warn_msg`, `reg_regen_warn_yes`, `reg_regen_warn_no`.
- **`#include <objbase.h>`** added to both `mainwindow.cpp` and `db.cpp` for `CoCreateGuid` / `StringFromGUID2`.

## [2026.05.02.11] - 2026-05-02

### H-scroll Step 3 complete: custom H-bar now shown; Dialogs→Preview H-scroll added
- **my_scrollbar — H-bar now visible (Step 3 done)**: `MSB_HORIZONTAL` early-return guards removed from `Msb_UpdateVisibility` and `Msb_UpdateVisibilityGuarded`. The custom H-bar now fades in when content overflows and fades out when it does not, identical to the V-bar behaviour.
- **my_scrollbar — proximity-expand for H-bar**: `WM_MOUSEMOVE` now triggers `FADE_EXPANDING` for the H-bar when the cursor moves within `MSB_WIDTH_FULL` px of the target's bottom edge and content overflows — matching the V-bar proximity logic.
- **my_scrollbar — `Msb_HideNativeBar(H)` reverted to `ShowScrollBar(FALSE)`**: The Step-2 fix used `SetWindowLongPtr(GWL_STYLE, style & ~WS_HSCROLL)` to preserve the ListView internal scroll counter between ticks. With Step 3 showing the custom bar, this approach caused a problem: `SetWindowLongPtr` only clears the style bit; the NC area slot remains painted as a 3-px strip behind the custom bar. `ShowScrollBar(FALSE)` actually collapses the NC area. The `inHDeliver` guard already in place in `WM_NCPAINT` and `WM_SIZE` prevents `Msb_HideNativeBar(H)` from being called during `LVM_SCROLL` delivery, so the internal counter is never zeroed while a scroll is in flight — making `ShowScrollBar(FALSE)` safe to use here.
- **my_scrollbar — `Msb_UpdateVisibilityGuarded` guard restricted to V-bars**: The "keep showing" guard (prevents premature hide of a bar that was just visible) now checks `vert &&` before applying. For H-bars, content genuinely not overflowing must immediately hide the bar; the guard was incorrectly keeping a 3-px strip visible.
- **my_scrollbar — empty-ListView guard in `Msb_ContentOverflows`**: For the H-axis on a ListView, `Msb_ContentOverflows` now returns `FALSE` immediately when `ListView_GetItemCount == 0`, regardless of column widths. Prevents a phantom H-bar from appearing on empty ListViews.
- **dialogs.cpp — H-scroll on Dialogs→Preview content RichEdit**: `WS_HSCROLL` added to the RichEdit creation style; `hContentSBH` handle added to `PreviewData`; `msb_attach(MSB_HORIZONTAL)` called in `SyncContentScrollbar` on the first layout pass and `msb_sync` on subsequent passes; `msb_detach` in `WM_DESTROY`. Both `my_scrollbar_vscroll.h` and `my_scrollbar_hscroll.h` included. Resize the preview small enough to truncate wide content and the H-bar appears automatically.

## [2026.05.02.10] - 2026-05-02

### H-scroll Step 2: tilt-wheel left+right fully working on ListView, TreeView, and RichEdit
- **my_scrollbar — ListView H tilt-wheel fixed (both directions)**: `LVM_SCROLL`'s internal horizontal scroll counter was being zeroed between tilt-wheel ticks by `ShowScrollBar(SB_HORZ, FALSE)` inside `Msb_HideNativeBar` — called from the post-delivery `WM_NCPAINT` intercept (when `inHDeliver` is already `FALSE`). Once the counter reached 0, any leftward delta failed the `counter + dx >= 0` clamp check and was silently rejected. Fix: `Msb_HideNativeBar` for the H-axis now clears `WS_HSCROLL` via `SetWindowLongPtrW(GWL_STYLE, style & ~WS_HSCROLL)` instead of `ShowScrollBar(FALSE)`. This removes the bar visually without touching the internal counter. Counter persists at `lvHPos` between ticks; `lvHPos + delta >= 0` always passes after clamping. V-scroll is completely unaffected (separate code path, unchanged).
- **my_scrollbar — counter pre-seed**: `SetScrollInfo(nPos=lvHPos, FALSE)` is called immediately before `LVM_SCROLL` so that any internal `ShowScrollBar(TRUE)` call within `LVM_SCROLL` re-seeds the counter from the correct baseline (`lvHPos`, not a stale 0).
- **my_scrollbar — NCPAINT restoreH guarded**: The `restoreH` capture/restore path in the `WM_NCPAINT` intercept is now skipped while `inHDeliver` is `TRUE`, preventing a stale `SCROLLINFO.nPos` snapshot from overwriting the pre-seeded value during delivery.
- **my_scrollbar — WM_SIZE guard extended**: The `WM_SIZE` handler now applies the same `!ctxH->inHDeliver` guard to `Msb_HideNativeBar(H)` as the `WM_NCPAINT` intercept. `LVM_SCROLL` fires `WM_SIZE` synchronously (via an internal `ShowScrollBar(TRUE)`) before the scroll completes; without the guard the handler called `Msb_HideNativeBar(H)` and zeroed the counter before the scroll ran.

## [2026.04.29.09] - 2026-04-29

### H-scroll Step 2 (tilt-wheel): inHDeliver guard + SetScrollInfo-before-LVM_SCROLL attempt
- **my_scrollbar — inHDeliver guard**: Added `inHDeliver` flag to `MsbCtx`. Set `TRUE` by `MsbH_DeliverScroll` around `LVM_SCROLL`; the `WM_NCPAINT` intercept skips `Msb_HideNativeBar` for H while the flag is set, preventing `ShowScrollBar(FALSE)` from firing during the scroll itself.
- **my_scrollbar — SetScrollInfo before LVM_SCROLL**: `SetScrollInfo(SIF_POS, nPos=lvHPos, FALSE)` is now called immediately before `LVM_SCROLL` in `MsbH_DeliverScroll`, restoring `SCROLLINFO.nPos` (zeroed by `ShowScrollBar(FALSE)` between deliveries) as the correct left-boundary clamp baseline. Tilt-wheel **right** works. Tilt-wheel **left** is still broken — `LVM_SCROLL` uses an internal scroll counter separate from `SCROLLINFO.nPos`; `SetScrollInfo` updates the struct but not the counter. See `here.txt` for next direction.

## [2026.04.27.10] - 2026-04-27

### H-scroll Step 2 (tilt-wheel, partial) + left-scroll root cause identified
- **my_scrollbar — H-scroll Step 2 (tilt-wheel, partial)**: `MsbH_DeliverScroll` now sends `LVM_SCROLL` for ListView H-axis. `ShowScrollBar(SB_HORZ, TRUE)` re-enables `WS_HSCROLL` (stripped by the `WM_NCPAINT` intercept); the full `SCROLLINFO` (range + `nPos=lvHPos`) is set before the call so the NCPAINT capture/restore cycle sees correct data; then `LVM_SCROLL(delta, 0)` is sent. Tilt-wheel **right** works. Tilt-wheel **left** is still broken.
- **my_scrollbar — left-scroll root cause identified**: `LVM_SCROLL` uses the ListView's internal `SCROLLINFO.nPos` for left-boundary clamping. `ShowScrollBar(FALSE)` zeroes `nPos`; the ListView's own `WM_NCPAINT` handler (called first inside our intercept via `origProc`) reads the zero value back into its internal state, overwriting our `SetScrollInfo(nPos=lvHPos)`. `LVM_SCROLL` therefore always sees `nPos=0` and rejects any negative delta. Fix direction: replace `LVM_SCROLL(delta)` with `WM_HSCROLL(SB_THUMBPOSITION, newPos)` + `SIF_TRACKPOS=newPos` — an absolute-position message that bypasses delta clamping.

### H-scroll clean-room rewrite (Step 1): total V/H separation, H-bars suppressed
- **my_scrollbar — V/H separation**: Scrollbar module split into two fully independent units: `my_scrollbar_vscroll.cpp`/`my_scrollbar_vscroll.h` (vertical, working, untouched) and `my_scrollbar_hscroll.cpp`/`my_scrollbar_hscroll.h` (horizontal, clean-room rewrite in progress). `my_scrollbar_hscroll.cpp` is `#include`d directly inside `my_scrollbar_vscroll.cpp` so it shares all internal types (`MsbCtx`, `S()`, helpers) without being a separate compilation unit. Public API unchanged — all callers continue to use `msb_attach`/`msb_detach`/`msb_sync` etc.
- **my_scrollbar — H-scroll Step 1 (suppressed)**: All H-scroll delivery routes through `MsbH_DeliverScroll` (in `my_scrollbar_hscroll.cpp`), currently a no-op. `Msb_UpdateVisibility` and `Msb_UpdateVisibilityGuarded` return immediately for `MSB_HORIZONTAL` bars; proximity-expand in `WM_MOUSEMOVE` also suppressed. No custom H-bar appears anywhere in the application. The native Windows H-bar continues to be suppressed by the existing `WM_NCPAINT` intercept. Intentional — Step 1 verifies a clean baseline before scroll delivery is added incrementally.
- **my_scrollbar — lvHPos drag-bug fix**: Removed stale `lvHPos` re-read from `WM_MOUSEMOVE` drag path. Previous code called `Msb_GetListViewHPos()` on every mouse-move; because `LVM_SCROLL` fires `WM_NCPAINT` → `ShowScrollBar(FALSE)` before the header repositions, the re-read always returned the old position — making every drag delta compute from zero and scroll never accumulate.
- **my_scrollbar — debug logging removed**: `MsbDbg()` function, `MsbDbgInit` struct, `#include <stdio.h>`, and all eight `MsbDbg(...)` call sites removed.
- **API docs — split**: `my_scrollbar_API.txt` renamed to `my_scrollbar_vscroll_API.txt`; new `my_scrollbar_hscroll_API.txt` documents H-scroll Golden Rules, step plan, and architecture. `API_list.txt` updated.

## [2026.04.19.09] - 2026-04-19

### Internal housekeeping (boilerplate removal, my_scrollbar auto-syncs, makeit timestamp)
- **mainwindow.cpp — TVN_ITEMEXPANDED handlers**: The Files, Components, and Registry pages now handle `TVN_ITEMEXPANDED` on their TreeViews; calls `msb_reposition` so the custom scrollbar thumb resizes correctly after subtree expand/collapse.
- **mainwindow.cpp / deps.cpp — boilerplate removal**: All remaining placeholder comments, stub handlers, and dead code removed from the Files, Components, Registry, and Dependencies page sections. No functional changes.
- **my_scrollbar — LVM_DELETEALLITEMS auto-sync**: Library now intercepts `LVM_DELETEALLITEMS` on ListView targets; resets `lvHPos` to 0 and redraws the H-bar after items are cleared. Callers no longer need a post-`ListView_DeleteAllItems` `ShowScrollBar`+`msb_sync` sequence.
- **my_scrollbar — LVM_SETCOLUMNWIDTH auto-sync**: Library now intercepts `LVM_SETCOLUMNWIDTH`; re-reads `lvHPos` from the header and redraws the bar after `origProc` commits the new width. Callers no longer need a post-`ListView_SetColumnWidth` `msb_reposition` call.
- **my_scrollbar — HDN_ENDTRACK auto-sync**: Library now intercepts `WM_NOTIFY`/`HDN_ENDTRACK` from the ListView header; re-reads `lvHPos` and redraws after the user finishes dragging a column divider. Callers no longer need a manual `msb_reposition` call from `HDN_ENDTRACK`.
- **my_scrollbar — inNcPaintChain WM_SIZE guard**: `WM_SIZE` now skips the `Msb_GetListViewHPos` re-read when `inNcPaint` is set on either attached bar, preventing a stale-read corruption that could corrupt `lvHPos` when `LVM_SCROLL` fires `WM_NCPAINT` synchronously before the header repositions.
- **makeit.bat — exe timestamp**: Build script now prints the `SetupCraft.exe` file timestamp after packaging.

## [2026.04.18.07] - 2026-04-18

### Added / Fixed (Files page — exclude filter for Add Folder; tree redraw fix)
- **Files page — Add Folder exclude filter**: New `FolderFilterDialogProc` modal appears before the folder is ingested. Presents a wildcard-pattern editor (listbox + edit + Add/Remove) pre-loaded with patterns saved for the current project. Patterns matched against file and subfolder names via `PathMatchSpecW` (`*` and `?` supported). Matching files skipped in `IngestRealPathFiles`; matching subdirs skipped in `AddTreeNodeRecursive`. When patterns are active, `IngestRealPathFiles` is called eagerly for both root and every subfolder during `AddTreeNodeRecursive` so the lazy `TVN_SELCHANGED` path never re-reads disk without the filter. ListView refreshed via `ForceRefreshListView` (uses pre-filtered VFS data) instead of `PopulateListView`. Patterns persisted to DB via `DB::GetSetting`/`DB::SetSetting` with key `folder_exclude_patterns_<projectId>` (newline-separated); recalled on next Add Folder for the same project. Cancelling aborts the entire Add Folder operation. Dialog style: `my_scrollbar` on listbox, NONCLIENTMETRICS font, `CreateCustomButtonWithIcon` Add (Blue, shell32 #264) / Remove (Red, shell32 #234) / Add Folder OK (Green, imageres #89) / Cancel (Red, shell32 #131), `WM_DRAWITEM` + `WM_CTLCOLORSTATIC` + `WM_DESTROY`. New locale keys: `ffilter_title`, `ffilter_label`, `ffilter_hint`, `ffilter_add_btn`, `ffilter_remove_btn`, `ffilter_ok`, `ffilter_cancel`. Control ID range 9030–9035 (`IDC_FFILTER_*`).
- **Files page — TreeView redraw fix after Add Folder**: TreeView failed to repaint after `FolderFilterDialogProc` (and `FileCompDialogProc`) modal closed, until the user clicked on it. Root cause: `GetMessageW` loop lacked an `IsWindow` guard, and a `PeekMessage(PM_REMOVE)` drain consumed the `WM_PAINT` messages queued by `TreeView_Expand`/`TreeView_SelectItem`. Fix: loop condition changed to `IsWindow(hDlgFF) && GetMessageW(...) > 0`; drain removed; `InvalidateRect(s_hTreeView, NULL, TRUE)` + `UpdateWindow` called after all tree operations.

## [2026.04.17.17] - 2026-04-17

### Added / Fixed (Files page — per-file component assignment; FileCompDialogProc visual polish)
- **Files page — per-file component assignment**: "Component…" context menu item in the Files-page ListView right-click menu (shown only when exactly one file is selected and components are enabled). Opens `FileCompDialogProc` — a modal listing all known component names from `s_components` in a scrollable listbox, with "(Unassigned)" at the top. An edit + Add button row lets the developer coin new names inline. OK commits to `s_components` (finding an existing `ComponentRow` by `source_path` or creating one); Cancel discards. Choosing "(Unassigned)" removes all `ComponentRow` entries for that file. Persisted to DB on Save. New "Component" ListView column (col 3, `files_col_component`, 130 px) via `GetCompNameForFile()`. New locale keys: `files_col_component`, `files_ctx_component`, `fcomp_title`, `fcomp_unassigned`, `fcomp_new_label`, `fcomp_add_btn`. Control ID range 9020–9024 (`IDC_FCOMP_*`).
- **FileCompDialogProc — app-style visual polish**: `my_scrollbar` (`MSB_VERTICAL`) on the listbox; `ShowScrollBar` + `msb_sync` after population; `msb_detach` in `WM_DESTROY`. NONCLIENTMETRICS font (×1.2, `CLEARTYPE_QUALITY`) via `EnumChildWindows`; stored as `"hCtrlFont"` prop, deleted in `WM_DESTROY`. Add button: `CreateCustomButtonWithIcon` (Blue, shell32 #264, `MeasureButtonWidth`). OK / Cancel: `CreateCustomButtonWithIcon` (Green imageres #89 / Red shell32 #131), centred. `WM_DRAWITEM` → `DrawCustomButton` for all three. `WM_CTLCOLORSTATIC` returns `WHITE_BRUSH`. `WS_EX_TOPMOST` removed — ownership provides Z-order.

## [2026.04.17.14] - 2026-04-17

### Added / Fixed (Files page — per-file Inno Setup flags)
- **Files page — per-file Inno Setup flags**: All eleven Inno Setup `[Files]` `Flags:` attributes exposed per file. Right-click a single file in the Files-page ListView → "File Flags…" opens `FileFlagsDialogProc` — a modal with four sections: *Overwrite behaviour* (`ignoreversion`, `onlyifdoesntexist`, `confirmoverwrite`), *Post-install actions* (`isreadme`, `deleteafterinstall`, `restartreplace`, `sign`), *Registration* (`regserver`, `regtypelib`, `sharedfile`), and *Architecture* (three mutually-exclusive radio buttons: Default / 32-bit only / 64-bit only). White background with 1 px `COLOR_BTNSHADOW` border lines around each section group. OK commits to `VirtualFolderFile.inno_flags` and refreshes ListView column 2; Cancel discards. Persisted to DB on Save.
- **Files page — "Flags" ListView column**: Third column (`files_col_flags`, default "Flags", 150 px) added to the Files-page ListView. `TVN_SELCHANGED` and `ForceRefreshListView` both populate column 2 from `inno_flags`.
- **DB migration — `inno_flags` column**: `ALTER TABLE files ADD COLUMN inno_flags TEXT DEFAULT ''` added to `InitDb` (idempotent). `InsertFile` extended with optional `innoFlags` param (defaults to `L""`). `GetFilesForProject` SELECT and bindings updated. `sql/setupcraft_schema.sql` updated.
- **Locale fixes — raw `\u` sequences**: `LoadLocaleFile` is a raw UTF-8 reader; it does not expand `\u` escapes. Three entries containing literal `\u2026`, `\u201c`/`\u201d`, and `\u00bb` corrected to actual UTF-8 characters.

## [2026.04.17.10] - 2026-04-17

### Added / Fixed (About Credits dialog; entry-page tooltip fix; Settings checkbox font)
- **About — Credits dialog**: New `ShowCreditsDialog` modal, opened by a third button "Credits" (Green, shell32 #294) placed between *View License* and *Close*. Two sections: **Inno Setup** (orange heading, description, clickable URL `https://jrsoftware.org/isinfo.php`) and **Scintilla** (teal heading, description, clickable URL `https://www.scintilla.org/`), separated by a double-line rule. Custom scrollbar via `msb_attach`. 100% i18n — all strings via `Loc()`; seven new locale keys in both `en_GB.txt` files (`about_credits_btn`, `about_credits_title`, `credits_inno_heading`, `credits_inno_desc`, `credits_scintilla_heading`, `credits_scintilla_desc`). URLs hardcoded (not translatable). `ShowCreditsDialog` added to `about.h`; wired in `AboutWndProc` as command 1002.
- **About Credits — clickable URLs with hand cursor**: `EM_AUTOURLDETECT TRUE` auto-styles URLs with `CFE_LINK`. `EN_LINK` handles `WM_LBUTTONDOWN`/`WM_LBUTTONUP` — extracts URL via `EM_GETTEXTRANGE`, opens via `ShellExecuteW`. Hand cursor via `CreditsEditSubclassProc` intercepting `WM_SETCURSOR`: `EM_CHARFROMPOS` finds the char under cursor, `EM_GETCHARFORMAT` reads `CFE_LINK`, `IDC_HAND` set before Windows can override. `s_origCreditsEditProc` stored for clean fallthrough.
- **About tooltip (entry page) — not hiding on click**: Entry-page `WM_LBUTTONDOWN` in `main.cpp` now calls `HideTooltip()` and clears `g_currentTooltipIcon` / `g_mouseTracking` before `ShowAboutDialog`. Same fix as `about_icon.cpp` (v2026.04.14.09) applied to the entry screen's own hit-test path.
- **Settings — checkbox font uniformity**: `FieldCheckbox()` in `settings.cpp` gained `HFONT hFont` parameter; sends `WM_SETFONT` to the checkbox HWND after creation. All three call sites pass `hGuiFont`: *Solid compression*, *Allow users to uninstall*, *Close running applications before installing*. Previously rendered at the system default (smaller) font while all adjacent controls used the project's 120% scaled font.

## [2026.04.17.08] - 2026-04-17

### Added / Fixed (Scripts page complete)
- **Scripts — checkbox font uniformity**: All three custom checkboxes in the Add/Edit Script dialog (`IDC_SCRDLG_RUN_HIDDEN`, `IDC_SCRDLG_WAIT`, `IDC_SCRDLG_ALSO_UNINSTALL`) were missing `WM_SETFONT`, causing text to render smaller than adjacent radio buttons. Fixed: `HWND` returned by `CreateCustomCheckbox` captured for each control; `WM_SETFONT` sent with the same `hFont` as the radio buttons.
- **Scripts — editor layout**: Removed the "Script content:" static label row and the reserved finish-label height block. Finish-label controls (`IDC_SCRDLG_FINISH_LABEL_LBL` / `IDC_SCRDLG_FINISH_LABEL`) now overlay the top of the Scintilla editor, only visible for `SWR_FINISH_OPTOUT`. Editor starts directly below the "When to run" combo.
- **Scripts — duplicate-name rename sub-dialog**: `IsDupName()` helper added to detect conflicts against `pExisting`. On conflict, `ShowRenameDialog()` opens a compact sub-dialog (conflict message, pre-filled all-selected name edit, Rename / Cancel). Empty-name and still-taken validations both use `ShowValidationDialog`. `ScriptDlgData` gains `pExisting` field; new control IDs 7365–7368 (`IDC_SCRDLG_RENAME_MSG` through `IDC_SCRDLG_RENAME_CANCEL`).
- **Scripts — i18n audit**: `\u201c`/`\u201d` escape sequences in `en_GB.txt` replaced with real Unicode curly-quote characters (`LoadLocaleFile` does not expand `\u` escapes). Two `MessageBoxW` calls in `RenameDlgProc` replaced with `ShowValidationDialog` (handles `\n` expansion via `ExpandEscapes`). Scripts page is now fully i18n-clean.

## [2026.04.16.09] - 2026-04-16

### Fixed
- **Preview — dirty flag on open (two causes)**: (1) `UDM_SETPOS32` fires `EN_CHANGE` on the Height edit during sizer creation; the `EN_CHANGE` handler did not check `sd->ignoring`, so `MarkAsModified()` was called on every preview open. Fixed: early `if (sd->ignoring) return 0;` guard added. (2) The `openLogW/H/openUserSized` Cancel-guard snapshot was captured *before* `AutoFitPreview`, so the saved sizes never matched the actual fitted sizes and `TryCancelPreview` always thought the developer had changed something. Fixed: snapshot moved to *after* `AutoFitPreview` and the re-centre block.
- **Preview — wrong height after Next/Back navigation (image-heavy dialogs)**: The temporary measurement `RichEdit` in `AutoFitPreview` was created without `WS_VISIBLE` and without a forced paint pass. `GetScrollInfo.nMax` therefore always returned 0 and the code fell back to `EM_FORMATRANGE`, which underestimates height for embedded images/OLE. Fixed: added `WS_VISIBLE` + `RedrawWindow(RDW_ERASE|RDW_INVALIDATE|RDW_UPDATENOW)` after streaming RTF into the measurement window. Both single-layout and split-layout paths updated.
- **Preview — wrong position after Next/Back navigation**: `AutoFitPreview` uses `SWP_NOMOVE`, so the window was never repositioned after navigation changed its size. Fixed: added monitor-work-area re-centre in `NavigateTo` after `AutoFitPreview`; both the preview window and the sizer panel are moved.
- **Preview — centre on monitor instead of parent**: Initial preview position and all post-AutoFit re-centre sites now use `MonitorFromWindow → GetMonitorInfoW → rcWork` to place the window at the exact centre of the monitor work area.
- **Preview — Reset button wrongly marks project modified**: Reset called `MarkAsModified()` unconditionally. If the dialog was already auto-fitted (never manually resized), pressing Reset still dirtied the project. Fixed: `MarkAsModified()` is now only called when `wasSized` (the pre-clear value of `s_previewUserSized[type]`) is true.

## [2026.04.15.10] - 2026-04-15

### Changed / Fixed
- **Global dialog delay — spurious-leave guard removed**: `ButtonSubclassProc WM_MOUSELEAVE` had a guard calling `GetCursorPos` + `GetWindowRect` + `PtInRect` on every mouse leave and re-arming `TrackMouseEvent`. Fired on every custom button app-wide, starving the message pump during dialog open (4–12 sec blank-window delays). Guard removed entirely — `WM_MOUSELEAVE` now unconditionally clears hover state, hides tooltip, and exits. See `button_INTERNALS.txt §9`.
- **Deferred dialog population (`WM_POPULATE_LIST`)**: Open Project and Delete Project dialogs post a custom `WM_POPULATE_LIST` message from `WM_CREATE` instead of populating the ListView synchronously. Window frame appears instantly; project list loads on next dispatch.
- **Persistent database connection**: `InitDb()` keeps the SQLite connection open. All DB functions use `GetDb()` instead of per-call open/close — eliminates file-open latency on each query.
- **Tooltip above-anchor (`aboveAnchorY`)**: `ShowMultilingualTooltip` gains `int aboveAnchorY = -1`. When ≥ 0, tooltip is placed above the anchor (`aboveAnchorY − tooltipHeight − 4`) instead of below. Fixes Finish-button hover blink in the preview dialog: tooltip was flipping above the button when near screen bottom, overlapping it, triggering spurious `WM_MOUSELEAVE` → blink loop.
- **"Please select a project" converted to `ShowValidationDialog`**: Both Open Project and Delete Project procs replaced `MessageBoxW` with `ShowValidationDialog` — custom button, i18n OK label, consistent app style.
- **ValidationDialog — info icon**: Shell32 #221 (white-on-blue round "i") added centered above message text. `WM_CTLCOLORSTATIC` updated to `WHITE_BRUSH` for clean white icon background.
- **ValidationDialog — narrower width**: New `VAL_CONT_W = 260` constant (was `DLG_CONT_W = 380`) for a more compact info dialog.

## [2026.04.14.09] - 2026-04-14

### Changed / Fixed (About dialog overhaul)
- **About dialog — i18n buttons**: All buttons in About and License dialogs converted to `CreateCustomButtonWithIcon` + `MeasureButtonWidth`. View License: Blue, shell32 #221. Close: Red, shell32 #131. License OK: Green, shell32 #294. Widths measured at runtime by locale string — correct in all 20 languages.
- **About dialog — custom scrollbars**: `msb_attach(MSB_VERTICAL)` added to both About and License RichEdits. `WM_DRAWITEM` handlers added to both wndprocs; `msb_detach` on `WM_DESTROY`.
- **About dialog — system font**: Body text now uses `SPI_GETNONCLIENTMETRICS` at ×1.2 scale, matching `g_guiFont` in main.cpp and every other label in the project. Font face name dynamically resolved (no hardcoded "Segoe UI"). `s_richFontDirty` flag ensures the font is re-measured on every dialog open.
- **About dialog — width**: Both About and License dialogs widened to `S(650)` for a consistent, comfortable reading width.
- **About dialog — logo spacing**: Blank-line prefix uses `S(15)` (DPI-scaled) instead of hardcoded `15` — eliminates excess white space at high DPI.
- **About icon tooltip — not hiding on click**: `EnableWindow(parent, FALSE)` (the modal loop) fires before `WM_MOUSELEAVE` reaches the icon control. Fixed by calling `HideTooltip()` explicitly in `WM_LBUTTONDOWN` before `ShowAboutDialog`.
- **About dialog — locale propagation**: Both `ShowAboutDialog` and `ShowLicenseDialog` accept a `const std::map<std::wstring, std::wstring>& locale` parameter. Five locale keys added to `en_GB.txt` (`about_title`, `about_license_btn`, `about_close_btn`, `about_license_title`, `about_license_ok_btn`). All call sites in `mainwindow.cpp`, `main.cpp`, and `about_icon.cpp` updated.

## [2026.04.12.10] - 2026-04-12

### Changed / Fixed (GlyphPicker)
- **Glyph coverage filter**: Each codepoint is now checked against `IDWriteFontFace::GetGlyphIndices` before being added to the display list — index 0 (`.notdef`) means no glyph and the slot is silently skipped. Blocks no longer show empty squares for unrenderable codepoints.
- **Segoe UI fallback font**: A second `IDWriteFontFace` (Segoe UI) is acquired at startup. Codepoints absent from Segoe UI Emoji are tested against the fallback; if found they are rendered with the Segoe UI text format. Currency Symbols, Letterlike Symbols, Latin Extended, and similar blocks are now fully populated.
- **Flags block removed**: The 258 ISO 3166-1 regional-indicator pairs displayed as bold two-letter codes (Windows Segoe UI Emoji has no flag images). Removed rather than showing misleading placeholders; the codepoints still paste correctly as flag emoji in apps that render them.
- **Block list pruned and alphabetised**: Blocks rendering zero glyphs after filtering (Box Drawing, Block Elements, Hiragana, Katakana, Symbols Extended-A, CJK Symbols & Punctuation) removed. The two surviving CJK glyphs (U+3030, U+303D) moved as hard-coded extras to Misc Symbols. Remaining blocks sorted A→Z in the dropdown with **— All —** pinned first.
- **Currency Symbols — prepend extras**: `$` (U+0024), `¢` (U+00A2), `£` (U+00A3), `¤` (U+00A4), `¥` (U+00A5) prepended as hard-coded extras so all common currency signs appear at the top of the block.
- **Status bar no longer clears on empty-space click**: Clicking outside the glyph grid previously reset the selection and wiped the status text. Empty-space clicks are now ignored.
- **Clock on status bar**: Live `HH:MM:SS` clock in the right section of the status bar, updated every second via `WM_TIMER`. Parts set in `WM_SIZE` (not `WM_CREATE` where width is 0).
- **makeit.bat**: New build script in `glyphpicker/` — kills any running instance via `taskkill`, configures CMake if needed, builds. `POST_BUILD` in `CMakeLists.txt` copies the exe to the source folder automatically.

## [2026.04.11.10] - 2026-04-11

### Added
- **GlyphPicker companion tool (`glyphpicker/`)**: Standalone Unicode glyph and emoji picker. Scrollable grid across 30 Unicode blocks (Smileys, Animals, Food, Travel, Symbols, Latin, Greek, Cyrillic, Box Drawing, and more). Click once to select, click again or press Enter/Space to copy to clipboard. U+hex search filters across all blocks. Block dropdown defaults to **— All —** so the full catalogue is visible immediately; individual blocks narrow the view. Rendered with **Direct2D + DirectWrite** (`D2D1_DRAW_TEXT_OPTIONS_ENABLE_COLOR_FONT`) for full-color Segoe UI Emoji — GDI `DrawTextW` only renders the monochrome outline and does not support OpenType COLR color tables. Keyboard navigation (arrows, Page Up/Down). Status bar shows glyph, U+ code, and copy confirmation. Icon from `imageres.dll #81`. Statically linked MinGW binary (~670 KB), DPI-aware, Common Controls v6.

## [2026.04.10.07] - 2026-04-10

### Fixed
- **Preview — Finish button unreliable click and hover blink**: `IsDialogMessageW(hPreview, &m)` was called in the preview's outer message loop on a `WS_POPUP | WS_CAPTION` window that was never created via `DialogBox`/`CreateDialog`. `IsDialogMessageW` on non-dialog windows consumes the first `WM_LBUTTONDOWN` on any non-focused child control to set keyboard focus — returning `TRUE` (handled) without forwarding the click — so `BN_CLICKED` was only generated on the second click attempt. It also sent spurious `WM_SETFOCUS`/`WM_KILLFOCUS` messages to track the "default button" state, causing the Finish button to blink on hover. Fixed by removing `IsDialogMessageW(hPreview, &m)` from the preview message loop entirely. `Escape` is now handled via an explicit `WM_KEYDOWN VK_ESCAPE → Cancel` case in `PreviewWndProc`.

## [2026.04.09.12] - 2026-04-09

### Fixed
- **Preview auto-fit — License page: false scrollbar from 1-px rounding tolerance**: `MeasureRichEditLogHeight` (via `GetScrollInfo.nMax`) and `Msb_MeasureRichVertMax` (via `EM_POSFROMCHAR`) differ by ≤ 1 physical px due to integer rounding. When the window was sized exactly to `rtfLogH`, `Msb_ContentOverflows` saw a 1-px overflow and kept the custom scrollbar visible on the License page despite content fitting perfectly. Fixed by adding `rtfLogH += 1` (1 logical-px tolerance) in `AutoFitPreview` single-layout non-capped path so the window is always 1 logical px taller than the measured content height.
- **Preview auto-fit — FOR_ME_ALL split layout: "Install for all users" too close to nav buttons**: `AutoFitPreview` split layout used `n × 28` for all extras types. For `IDLG_FOR_ME_ALL` (no checkboxes, just two radio buttons), `n = max(0, 1) = 1` → 28 px allocated; the actual radio-button area requires `rH(24) + S(6) + rH(24) + breathing(8) = 62 px`. Window was 34 px too short, jamming the "Install for all users" radio button against the nav row. Fixed by computing `extrasContentH` per type: checkboxes → `n × 28`; radio buttons (when `hRadioMe` is visible) → `62`. Capped path updated to use `extrasContentH` directly.

### Added
- **Preview — Finish button feedback dialog**: Clicking Finish now shows a small modal "End of Preview" dialog before closing. Caption is `{installer title} — End of Preview`; body shows a short message and a disabled, checked "Open \<AppName\>" checkbox (substituted with the real app name from `s_previewAppName`); OK dismisses the dialog and the preview closes. New function `ShowFinishFeedback(HWND)` + `FinishFeedbackWndProc` in `dialogs.cpp`. Locale keys: `idlg_preview_done_title`, `idlg_preview_done_msg`, `idlg_preview_done_open`.

## [2026.04.09.09] - 2026-04-09

### Fixed
- **Preview auto-fit: window 4 px too short (`WS_EX_CLIENTEDGE` height not accounted for)**: `kChromeLogH = 114` did not include the `2×SM_CYEDGE` border that `WS_EX_CLIENTEDGE` subtracts from `hContent`'s viewport. `logH` was therefore `2×SM_CYEDGE` (≈ 4 px at 96 dpi) too small and the content overflowed by that amount. Fixed in both single layout (`logH = rtfLogH + kChromeLogH + edgeHLog`) and split layout (`logH = 160 + rtfLogH + n×28 + edgeHLog`) paths of `AutoFitPreview`. `edgeHLog = round(2×SM_CYEDGE / g_dpiScale)` is computed once per call; the cap path subtracts it symmetrically so split-layout scrolling still triggers at the right threshold.
- **Preview scrollbar never shown / falsely detached after `EM_SHOWSCROLLBAR(FALSE)`**: `SyncContentScrollbar` re-read `GetScrollInfo` on every layout pass to decide whether to keep or remove the custom bar. After `msb_attach` calls `EM_SHOWSCROLLBAR(FALSE)`, the RichEdit stops maintaining `SCROLLINFO` and `nMax` reads back as 0 — causing a false "no overflow" verdict and immediate detach. Fixed by splitting the branch: when `hContentSB` is already attached, `msb_sync` is called directly (which uses `Msb_ContentOverflows` → direct document-height measurement, bypassing `SCROLLINFO`); the `GetScrollInfo` path is used only for first-attach when no bar exists yet.
- **`richVertMax` cache stale across content changes and window resizes**: Added `msb_notify_content_changed(pd->hContentSB)` in `NavigateTo` before `StreamRtfIn` so the cached document height is invalidated when RTF content is replaced. Added `ctxV->richVertMax = 0` reset in `my_scrollbar.cpp`'s `WM_SIZE` subclass handler (after `origProc`) so text-reflow on width change always triggers a fresh measurement on the next `Msb_ContentOverflows` call.

## [2026.04.08.11] - 2026-04-08

### Added
- **Installer dialog defaults — `dialog_defaults` table**: New DB table `dialog_defaults(dialog_type PK, content_rtf)` seeded in `DB::InitDb` with `INSERT OR IGNORE` for all 9 dialog types. RTF templates include `<<AppName>>`, `<<AppVersion>>`, `<<AppNameAndVersion>>` placeholders (`<<AppNameAndVersion>>` = "Name, Version" or "Name" when version empty). Developer edits in DB are never overwritten. `DB::GetAllDialogDefaults()` added to `db.cpp`/`db.h`.
- **`IDLG_ApplyDefaults(appName, appVersion)`**: New function in `dialogs.cpp`/`dialogs.h`. Called once in `mainwindow.cpp Create()` after `IDLG_LoadFromDb`. Fills each empty in-memory dialog slot with the substituted default RTF. Both new projects (all slots empty) and existing projects with partially-filled dialogs get sensible starter content without developer effort. `SubstitutePlaceholders` processes `<<AppNameAndVersion>>` before `<<AppName>>` to avoid partial-match collisions.
- **`SyncContentScrollbar`**: Block at end of `LayoutPreviewControls`. Attaches/detaches a `my_scrollbar` (`hContentSB` in `PreviewData`) on the content RichEdit when content overflows. Triggered by both the 75% auto-fit cap and a manually-set too-small window. `WS_VSCROLL` always set on `hContent` so `SCROLLINFO` stays valid.
- **`preview_autofit_INTERNALS.txt`**: New documentation file (project root) covering `AutoFitPreview`, `ScanRtfNaturalWidthTwips`, `SyncContentScrollbar`, layout constants, cap values, and per-type `s_previewUserSized` suppression. Entry added to `API_list.txt`.

### Changed
- **Preview dialog auto-fit — all 9 types**: `AutoFitPreview(hPreview, pd)` replaces `AutoFitComponentHeight`. Old function was limited to `IDLG_COMPONENTS`; all other types were stuck at the default 214 px height. New function uses a 1 px WS_CHILD of the live preview (not broken `GetDesktopWindow` parent), handles both single and split layouts, caps at 75% screen height. Called from `ShowPreviewDialog` (post-creation) and `NavigateTo` for every type.
- **`s_previewUserSized` per-type**: Static upgraded from `bool` to `bool[IDLG_COUNT]`. Resizing the preview on one dialog type no longer suppresses auto-fit on all others.
- **Preview minimum width 400 px**: Raised from 200 px. The GNU logo's `\picwgoal` is narrow; 200 px caused word-wrap breakdown in the License dialog.
- **`kPreviewMaxWidthPct` 0.95**: Widened from 0.90 (95% of screen width cap).

### Removed
- **`MeasureRtfPreviewSize`**: Removed. Used `GetDesktopWindow()` as measurement parent; no WM_PAINT pass ran so content height was always wrong (stuck at 1 px). Replaced by live-parent measurement in `AutoFitPreview`.
- **Pre-creation auto-fit block in `ShowPreviewDialog`**: Removed. Auto-fit now runs post-show via `AutoFitPreview` for all types, after the window and RichEdit are fully created and painted.

## [2026.04.08.08] - 2026-04-08

### Fixed
- **Dependencies page H-bar — thumb never moved visually**: `Msb_Scroll` ListView H returned early after `Msb_ScrollToPos` but before the `Msb_Layout + InvalidateRect + UpdateWindow` block at the function bottom. Content was scrolled but the bar was never repainted. Fixed by adding those three calls before the early `return`.
- **Dependencies page H-bar — `lvHPos` set to stale header position after `LVM_SCROLL`**: `Msb_ScrollToPos` called `Msb_GetListViewHPos()` *after* `LVM_SCROLL` to update `lvHPos`. `LVM_SCROLL` fires `WM_NCPAINT` synchronously before returning; `WM_NCPAINT` fires before the header repositions, so the header still reflected the old offset — `lvHPos` was never advanced. Fixed: `ctx->lvHPos = newPos` (clamped target), not a header re-read.
- **Dependencies page H-bar — cannot scroll back left (delta always 0)**: Delta computed as `newPos − Msb_GetListViewHPos()`. On leftward scrolls the header returned the old position (same WM_NCPAINT timing), making delta always non-negative. Fixed: `curPos = ctx->lvHPos`; delta is now correct in both directions.
- **Dependencies page H-bar — blink and capture/restore mess on populate**: `msb_attach` was called *after* `RefreshList` so the WM_NCPAINT subclass was absent during row insertion. Four code-paths used a fragile capture/restore wrapper. Fixed: move `msb_attach` (V + H + edge gap) before `RefreshList`; replace all four capture/restore blocks with `ShowScrollBar(FALSE) + msb_sync` (the Components page pattern).
- **Dependencies page H-bar — arrow buttons and tilt-wheel jumped a full column width**: Line step was `ListView_GetColumnWidth(col0)` ≈ 280 px — felt like track-click. Replaced with `S(ctx, 20)` (20 logical px, DPI-scaled) in both `Msb_Scroll` and `WM_MOUSEHWHEEL`.

### Documentation
- **my_scrollbar_API.txt**: Updated status table for ListView V+H with 8 golden rules for the `LVM_SCROLL` hot-path; added LISTVIEW H-BAR CHECKLIST box to section 4d alongside the existing TreeView checklist.

## [2026.04.07.11] - 2026-04-07

### Fixed
- **Dependencies page MSB H-bar — WM_NCPAINT capture/restore**: `ShowScrollBar(SB_HORZ, FALSE)` inside `WM_NCPAINT` was zeroing `SCROLLINFO.nPos` after every `LVM_SCROLL`. Fixed by capturing `SB_HORZ` SCROLLINFO after `origProc` but before `HideNativeBar`, restoring afterward.
- **Dependencies page — deps.cpp capture/restore pattern**: All six `ShowScrollBar(SB_HORZ, FALSE)` calls now capture/restore SCROLLINFO so `msb_sync` sees valid range and position.
- **Msb_Scroll ListView H — routed through Msb_ScrollToPos**: Arrow/page commands now use column-width stepping via `Msb_ScrollToPos` (LVM_SCROLL + lvHPos) instead of `CallWindowProcW(WM_HSCROLL)` which does not reliably update `SCROLLINFO.nPos` when `nMax=0`.
- **WM_MOUSEHWHEEL ListView H — routed through Msb_ScrollToPos**: Tilt-wheel now accumulates steps × column-width via `Msb_ScrollToPos`, bypassing `WM_HSCROLL` entirely for ListView H.
- **WM_HSCROLL interceptor — lvHPos no longer updated**: Removed unreliable `ctxH->lvHPos = siPre.nPos`. `lvHPos` managed exclusively by `Msb_ScrollToPos` and `Msb_GetListViewHPos()` readbacks.
- **HDN_ENDTRACK in DEP_OnNotify**: Column resize now calls `msb_reposition` which syncs `lvHPos` from the header before hiding native bar.
- **WM_NCPAINT: no longer syncs lvHPos**: Removed sync — fires before header repositions; would read stale position and corrupt delta.

### Added
- **Msb_GetListViewHPos()**: New helper reads `-Header_GetItemRect(col0).left` — the only reliable source for true visual H scroll offset for ListView. Used by `Msb_ScrollToPos` (before/after `LVM_SCROLL`), `msb_sync`, `msb_reposition`, drag-start, and `WM_SIZE`.

### Known issues
- **Dependencies H-bar thumb drag**: Content moves when dragging right but thumb does not follow; cannot drag back left; blinks at right end. Arrow buttons, track-click, tilt-wheel are working. Fix deferred.

## [2026.04.05.15] - 2026-04-05

### Changed
- **Shortcuts page — native WS_VSCROLL replaced with my_scrollbar**: `s_hMsbSc` (HMSB) attached to the main window after page build (`msb_attach` / `msb_set_insets(pageY, statusH)` / `msb_set_edge_gap(4)`) — identical pattern to the Dialogs page. No native gutter; bar fades in/out gracefully. Detached in SwitchPage teardown. `SC_TearDown` simplified. `WM_SIZE` Shortcuts block extended to mirror Dialogs page: update SCROLLINFO, clamp position, move children, `msb_sync`. MSB bar HWND excluded from child-enum loops in `WM_MOUSEWHEEL` and `WM_VSCROLL`.
- **Documentation restructure — API_INTERNALS directory**: All `*_API.txt` and `*_INTERNALS.txt` moved from project root into `API_INTERNALS\API\` (reusable toolbox modules) and `API_INTERNALS\INTERNALS\` (project-internal architecture). `API_list.txt` updated with full paths, two-section layout, and a living-document maintenance rule. `scrollbar_INTERNALS.txt` updated to document the MSB migration.

### Added
- **skeleton_idea.txt**: New planning document in `API_INTERNALS\API\` — checklist for a future reusable Win32/C++ project skeleton (build system, sqlite3, UI modules, localisation stub, DPI, icons, Inno Setup stub, doc seed) and open decisions for publish time.

### Fixed
- **Track-click and thumb drag broken on Shortcuts and Dialogs page scrollbars**: Both `WM_VSCROLL` handlers used `si.nPos` from `GetScrollInfo` as `oldPos`. MSB pre-calls `SetScrollInfo(nPos=newPos)` before sending `SB_THUMBTRACK`/`SB_THUMBPOSITION`, so `si.nPos == newPos` at read time → `dy=0` → children never move. Fixed by using `SC_GetScrollOffset()` / `IDLG_GetScrollOffset()` as `oldPos`. `SIF_TRACKPOS` also unreliable — MSB encodes position in `HIWORD(wParam)`. Both pitfalls documented in `my_scrollbar_API.txt`.

## [2026.04.05.14] - 2026-04-05

### Added
- **Registry page — DB persistence**: Custom registry entries and user-added keys are now saved to `registry_entries` on Save and restored on project open. `DB::InsertRegistryEntry` / `DB::GetRegistryEntriesForProject` / `DB::DeleteRegistryEntriesForProject` fully wired. Custom keys stored as `name="__KEY__"` sentinel rows so tree nodes are recreated on reload. The three project-level fields (`register_in_windows`, `app_publisher`, `app_icon_path`) now synced to `s_currentProject` before `DB::UpdateProject`.
- **Registry page — data consistency on page switch**: `s_customRegistryEntries` and `s_customRegistryKeys` persist across page switches. On page build a full `path → HTREEITEM` map is built from the template tree; custom keys are re-inserted and custom values re-populated into `s_registryValues`.
- **Registry page — custom hidden scrollbars**: Four `my_scrollbar` handles (`s_hMsbRegTreeV/H`, `s_hMsbRegListV/H`) attached after build with `SM_CYEDGE + SM_CYBORDER + 6` edge gap. Repositioned via `msb_reposition` in `WM_SIZE`. Proportional ListView column resize removed — H-bar appears instead of truncating.
- **Registry page — i18n**: Four hardcoded strings replaced with locale lookups: `reg_select_icon_title`, `reg_no_key_selected`, `reg_select_key_first`, `reg_cannot_delete_root`. Keys added to `locale/en_GB.txt`.

### Fixed
- **Crash (white screen then terminate) when switching Registry → Components**: `IDC_REG_TREEVIEW` and `IDC_REG_LISTVIEW` are in the `controlIds[]` loop; their `WM_DESTROY` fired `Msb_TargetSubclassProc → msb_detach`, freeing MSB contexts before the explicit detach calls ran → double-free → heap corruption → crash. Fixed by moving all four reg MSB detach calls to before the `controlIds[]` loop, mirroring the existing Components fix. Documented in `scrollbar_reopen_INTERNALS.txt`.

## [2026.04.05.13] - 2026-04-05

### Fixed
- **H-bar hint strip obscured by V-bar bottom gap**: `Msb_PositionBar` reserved full 12 px for the H-bar at the V-bar bottom even when H was in 3 px hint-strip state. Reserve now tracks live `fadeWidth`: `max(fadeWidth, MSB_WIDTH_HIDDEN) + edgeGap + insetFar`.
- **V-bar height did not update when H-bar appeared/disappeared**: `Msb_UpdateVisibility` state transitions and the 16 ms fade timer now reposition the V-bar peer after every H-bar width change.
- **TreeView H-bar invisible on first attach**: `msb_attach` sampled scroll range after `ShowScrollBar(FALSE)`, which resets the TreeView H-range to zero. Fixed by sampling `GetScrollInfo` before the hide call (`preHideOverflows`); bar starts `FADE_HIDDEN` if content was already overflowing.
- **Three scrollbars stuck visible (infinite expand–contract loop)**: Target `WM_MOUSEMOVE` proximity expanded from both `FADE_HIDDEN` and `FADE_INVISIBLE`. When the bar contracted to 3 px and the cursor was still within the 12 px proximity zone of the target, it immediately re-expanded. Fixed: proximity expansion limited to `FADE_INVISIBLE` only.
- **Scrollbar contracted immediately on mouse leave**: Contraction now delayed 200 ms (timer ID 4). Re-entry, click, or drag end cancels the timer.
- **Edge gap inconsistent across H-bars**: All H-bars use `SM_CYEDGE + SM_CYBORDER` as base; CompTreeView H-bar adds +2 px extra.

### Changed
- **Files ListView — column order and no truncation**: Columns reordered to Destination → Source Path. Both columns auto-sized (`LVSCW_AUTOSIZE` / `LVSCW_AUTOSIZE_USEHEADER`) after every populate/add. `LVS_EX_INFOTIP` and `LVN_GETINFOTIP` handler removed. `WM_SIZE` no longer forces proportional column widths — narrow windows show the H-scrollbar instead of truncating.
- **Components ListView — Source Path auto-sized**: `LVSCW_AUTOSIZE_USEHEADER` applied after every populate. `LVS_EX_INFOTIP` removed.

## [2026.04.05.10] - 2026-04-05

### Fixed
- **Main window opened in the background**: After `ShowWindow`/`UpdateWindow`, the window is now brought to the foreground via a `HWND_TOPMOST`/`HWND_NOTOPMOST` pair followed by `SetForegroundWindow`. A long-standing annoyance where the project window opened behind other running applications.
- **Fresh-process architecture — crash on second project open and background-opening root cause**: `--new` and `--open=<id>` command-line modes added to `wWinMain`. The entry screen now calls `SpawnSelf` and posts quit instead of creating the main window in-process. Every project window runs in its own fresh `SetupCraft.exe` process with a clean Win32 class registration and COM state. Eliminates the heap-corruption crash that occurred when closing one project and opening another in the same process.
- **`SwitchPage` silently destroyed the status bar on every non-initial page switch**: `s_hStatus` and `s_hAboutButton` have IDs outside the toolbar-button range (`IDC_TB_FILES`–`IDC_TB_ABOUT`), so the `isToolbarBtn` guard in the child-window enumeration missed them. After the first switch away from and back to any page the status bar was destroyed and `s_hStatus` became a dangling handle (blank status strip, potential crash on `SetWindowText`). Fixed by adding both HWNDs to the exclusion list.
- **`WM_DESTROY` did not post quit message**: With the fresh-process architecture, each project window runs in its own process. `WM_DESTROY` now calls `PostQuitMessage(0)` so the message loop exits cleanly when the window is closed.
- **Stale HWND/HMSB pointers caused heap corruption on second `MainWindow::Create`**: Static HWND and HMSB pointers were left non-NULL from the previous session after `DestroyWindow`. On the next `Create`, `WM_SIZE` called `msb_reposition` on already-freed `MsbContext` memory, corrupting the heap. All static handles are now nulled at the top of `Create()` before `CreateWindowExW`.
- **Tooltip popup leaked between sessions**: The `WS_POPUP` tooltip window (`s_hTooltip`) is not automatically destroyed when its owner is destroyed. Added explicit `DestroyWindow(s_hTooltip)` + null assignment in `WM_DESTROY`. Without this, `IsWindow(s_hTooltip)` returned TRUE on the next open, causing the stale popup to be reused with garbage state.
- **`Msb_TargetSubclassProc` `WM_DESTROY` did not chain to the original WndProc**: After `msb_detach` freed the contexts, the handler returned 0 without calling `CallWindowProcW(origProc, ...)`. The original control's own `WM_DESTROY` was never invoked. Fixed by saving `origProc` before the detach calls and then chaining through.

## [2026.04.03.09] - 2026-04-03

### Added
- **`msb_reposition(HMSB)`** — new public `my_scrollbar` API. Re-derives the bar position from the target's current client-rect corners at call time, runs the unguarded visibility check (hides the bar if content fits, shows it if not), then calls `Msb_PositionBar` + `Msb_Layout` + redraw. Designed for explicit resize events where content may genuinely fit after the resize.
- **Components page — custom hidden scrollbars on TreeView and ListView**: Attached four `my_scrollbar` handles (`s_hMsbCompTreeV/H`, `s_hMsbCompListV/H`) in hidden/fade mode. ListView bars attached after column creation; TreeView bars attached after full tree population (required — `ShowScrollBar(FALSE)` must run after all `TreeView_InsertItem` calls). Edge gap set on both H-bars. ListView native bars re-suppressed after every `TVN_SELCHANGED` fill via `ShowScrollBar` + `msb_sync`. All four repositioned in `WM_SIZE` via `msb_reposition`.

### Fixed
- **Registry page did not resize with the window**: Extended `WM_SIZE` to immediately reposition all variable-width controls (title, edit fields, divider, right-anchored backup button and warning icon) and resize the TreeView/ListView split pane via `DeferWindowPos`. Bottom action buttons follow the pane bottom. ListView column widths updated proportionally.
- **Dependencies page did not resize with the window**: Extended `WM_SIZE` to reposition the page title and stretch the ListView to fill the available height and width. All four ListView column widths updated proportionally on every resize.
- **Shortcuts page did not resize with the window**: Added `SC_OnResize(hwnd, newClientWidth)`, called from `WM_SIZE`. Re-centres the three column headings and icon buttons, repositions opt-out checkboxes, start-menu label, TreeView, and action buttons to the new column boundaries. Updates the column-width state variables used by the desktop and pin-strip refresh paths.
- **Components page crash on page switch (use-after-free in scrollbar teardown)**: `controlIds[]` contains `IDC_COMP_TREEVIEW` and `IDC_COMP_LISTVIEW`. When the loop called `DestroyWindow` on these, `WM_DESTROY` fired `Msb_TargetSubclassProc`, which internally called `msb_detach` and freed the context. The four explicit `msb_detach` calls that followed then re-used the freed memory, causing heap corruption and a crash ~3 s later. Fixed by moving all four `msb_detach` calls to before the `controlIds[]` loop, and adding `s_hCompTreeView` to the HWND exclusion list in the child-window enumeration.
- **Files page H-bar visible with an empty or fully-visible ListView**: Column widths were based on the window-space pane width, slightly exceeding the ListView client area (narrower by the border thickness), producing a permanent false overflow. Fixed by using `GetClientRect(s_hListView).right` for column sizing in both the initial build and every `WM_SIZE` resize.
- **Files page H-bar and V-bar at different heights after resize**: In the target's subclassed `WM_SIZE`, `Msb_HideNativeBar` was called *after* `Msb_PositionBar`. If the control had re-enabled its native scrollbar during `CallWindowProcW`, the native gutter reduced the client height seen by `Msb_PositionBar`, placing the H-bar lower. Fixed by calling `Msb_HideNativeBar` first so `GetClientRect` returns the full client dimensions.
- **Files page scrollbars did not reposition after window resize**: `msb_reposition` is now called from `WM_SIZE` for all four Files page bars after the `DeferWindowPos` block.
- **Dialogs page scrollbar stayed visible after maximising; did not return when downsizing**: `WM_SIZE` never updated the `SCROLLINFO` for the Dialogs bar, so `Msb_ContentOverflows` read stale values from page build time. Added a Dialogs `WM_SIZE` block that recalculates viewport/content height, updates `SetScrollInfo`, clamps the scroll position (slides controls back if the window grew past the content bottom), and calls `msb_sync` to hide or show the bar immediately.
- **SwitchPage leaked scrolled-away controls into the next page**: Child-window teardown filtered with `rcChild.top > s_toolbarHeight`. Controls scrolled above the toolbar had `rcChild.top < s_toolbarHeight` and were never destroyed, surviving into the next page. Fixed by changing the condition to `rcChild.bottom > s_toolbarHeight`.

## [2026.04.03.07] - 2026-04-03

### Fixed
- **Files page H-bar disappeared after first hover cycle**: `Msb_UpdateVisibility` was called unconditionally in `WM_VSCROLL`/`WM_HSCROLL`. When the TreeView auto-scrolls to a selection it transiently reports a zero scroll range; `Msb_UpdateVisibility` saw no overflow and hid the bar (`SW_HIDE`). Introduced `Msb_UpdateVisibilityGuarded`: for TreeView/ListView targets it allows `FADE_INVISIBLE → FADE_HIDDEN` (bar appears when content first overflows) but blocks `FADE_HIDDEN → FADE_INVISIBLE` (transient zero-range cannot hide a bar already showing). Guard applied in `WM_SIZE`, `WM_VSCROLL`, `WM_HSCROLL`, `WM_MOUSEWHEEL`, and `WM_MOUSEHWHEEL`.
- **Files page H-bar sat 2–3 px too low (flush against nc border)**: `Msb_PositionBar` placed the H-bar flush against the `WS_EX_CLIENTEDGE` non-client border, partially hiding the bar. Fixed by adding `msb_set_edge_gap(h, GetSystemMetrics(SM_CYEDGE) + 2)` (4 px at 96 DPI) for both the TreeView and ListView H-bars, so the full 3 px hint strip sits visibly within the client area.
- **Files page V-bar thumb drag not working on TreeView**: During live drag, `SB_THUMBPOSITION + SB_ENDSCROLL` was sent on every mousemove frame; TreeView treated `SB_ENDSCROLL` as finalization and discarded intermediate positions, causing the thumb to snap back. Fixed so live drag sends `SetScrollInfo + SB_THUMBTRACK` for non-RichEdit targets; `SB_THUMBPOSITION` is only sent once on `WM_LBUTTONUP`.
- **Files page ListView thumb drag worked only one way**: `SetScrollInfo(nPos)` was called before `WM_HSCROLL/VSCROLL(SB_THUMBTRACK)` during drag; ListView computed a zero delta and content did not move. Fixed by routing ListView live drag through `Msb_ScrollToPos` (`LVM_SCROLL` pixel delta), same path as track-clicks and arrow buttons.
- **Files Management and Components pages did not resize with the window**: Both pages build TreeView/ListView as direct children of the main window; `WM_SIZE` only resized the `s_hCurrentPage` container (which is NULL for both pages). Extended `WM_SIZE` to recompute and reposition all variable-width controls and resize the split panes via `DeferWindowPos`. ListView column widths updated proportionally on resize.

## [2026.04.02.19] - 2026-04-02

### Fixed
- **Files page TreeView — native scrollbars still showing**: `msb_attach` was called before tree population. `TreeView_InsertItem` internally re-enables the native scrollbar, so the native bars were always visible after population finished. Fixed by moving the TreeView `msb_attach` calls to after all item insertion (RestoreTreeSnapshot / DB rebuild / PopulateTreeView + expansion) — `ShowScrollBar(FALSE)` inside `msb_attach` now runs with content already present and correctly suppresses the native bars.
- **Files page ListView — native bars shown after folder selection**: Selecting a tree node fills the ListView (either inline via TVN_SELCHANGED or via `PopulateListView`), which re-enables native scroll bars. Fixed by calling `ShowScrollBar + msb_sync` after every list-fill path: TVN_SELCHANGED inline item loop, `PopulateTreeView` exit, and IDC_FILES_ADD_DIR handler.

## [2026.04.02.16] - 2026-04-02

### Added
- **Files page — custom hidden scrollbars on both panes**: The TreeView (left pane) now has vertical and horizontal custom bars; the ListView (right pane) has vertical and horizontal custom bars. All four bars use hidden/fade mode (3 px hint strip when idle, expand on hover). Bars are attached on page build and detached on page teardown in `SwitchPage`.

### Fixed
- **ListView thumb drag snapped back; text did not scroll**: `Msb_ScrollToPos` computed `delta = newPos - si.nPos` and passed it directly to `LVM_SCROLL` as pixels. ListView vertical `SCROLLINFO.nPos` is in rows, not pixels, so the pixel delta was always near zero — the thumb moved visually but content barely shifted, then snapped back on mouse release. Fixed by multiplying the vertical row delta by the item row height (measured via `ListView_GetItemRect`). Horizontal is already in pixels and was correct.
- **ListView track click was dead**: Same root cause — pixel delta too small to advance even one row. Resolved by the same row-height multiplication in `Msb_ScrollToPos`.
- **TreeView track click could not reach the bottom**: Track-click position cap was `nMin + scrollRange - 1` = `nMax - nPage` — one row short of the true maximum (`nMax - nPage + 1`). Fixed to match the drag path.

## [2026.04.02.15] - 2026-04-02

### Added
- **Dialogs page — custom hidden scrollbar**: The Dialogs page now has a custom `my_scrollbar` vertical bar attached to the main window (not a native `WS_VSCROLL`). Bar is clamped to the page area via `msb_set_insets` (skips toolbar at top and status bar at bottom) and sits 4 px inward from the right edge via `msb_set_edge_gap` so the 3 px hint strip is clearly visible against the page background. Wheel scrolling, thumb drag, track click, and arrow buttons all work correctly.
- **`msb_set_insets(h, insetNear, insetFar)`**: New API — restricts the bar window to a sub-range of the target's edge. For a vertical bar, `insetNear` is the top exclusion zone and `insetFar` the bottom (e.g. toolbar height and status bar height). `Msb_PositionBar` uses these values to size and place the bar correctly.
- **`msb_get_bar_hwnd(h)`**: New API — returns the bar's HWND so the caller can exclude it from child-moving loops (the Dialogs page scroll handler moves all children by `-dy`; skipping the bar prevents it from drifting on every scroll event).
- **`msb_set_edge_gap(h, gap)`**: New API — shifts the bar inward from the window edge by `gap` unscaled px. Moves a vertical bar left so the hint strip is separated from the frame and easier to see.

### Changed
- **`MSB_WIDTH_HIDDEN` reduced 5 → 3 px**: The idle hint strip is now 3 px instead of 5 px — narrower and less intrusive, while still showing the thumb position.
- **RTF editor scrollbars — removed `MSB_NOHIDE`**: Both V and H bars on the RTF editor now use hidden/fade mode. They collapse to a 3 px hint strip when the cursor is away and expand on hover, matching the rest of the app.

### Fixed
- **Bar invisible on top-level target windows**: `Msb_PositionBar` was calling `GetParent(ctx->hTarget)` for coordinate mapping; for a top-level target (main window) the parent is `NULL` and all coordinates mapped to screen space, placing the bar off-screen. Fixed by using `GetParent(ctx->hBar)` which always returns the correct parent.
- **Bar drifts on every scroll event**: The Dialogs page `WM_MOUSEWHEEL` and `WM_VSCROLL` handlers iterate all child windows and move them by the scroll delta. The bar HWND was not excluded, so it shifted with every scroll. Fixed by retrieving the bar HWND via `msb_get_bar_hwnd` and skipping it in both child loops.
- **Thumb drag broken on Dialogs page (`SB_THUMBTRACK`)**: The thumb drag handler read the new position via `GetScrollInfo(SIF_TRACKPOS)`, which only works with the native bar — the custom bar never sets `SIF_TRACKPOS`. Fixed: position is now read directly from `(int)(short)HIWORD(wParam)`, which is always correct.
- **Contraction stops at hint strip, not fully invisible**: The fade-out timer previously animated to 0 px (`FADE_INVISIBLE`). Changed to stop at `MSB_WIDTH_HIDDEN` (`FADE_HIDDEN`) so the 3 px position indicator is always visible while content overflows.
- **Hint-strip background is white**: In hint mode the track background is painted with `COLOR_WINDOW` so only the thumb is visible — no gray track box behind the 3 px strip.
- **Click on hint strip expands before hit-test**: `WM_LBUTTONDOWN` when the bar is in `FADE_HIDDEN` now snaps to full width and `FADE_VISIBLE` before performing the hit-test, so the first click always lands correctly on an arrow, track, or thumb rather than being swallowed.

## [2026.04.02.14] - 2026-04-02

### Fixed
- **Vertical scrollbar — no longer overlaps H bar**: The V bar window is now shortened by the H bar height so the down-arrow button and the H bar's right-arrow button no longer collide. A peer-notification mechanism in `msb_attach` re-positions the V bar the instant the H bar registers itself, fixing the ordering problem that made the initial layout always full-height.
- **Vertical scrollbar — thumb correctly proportional**: `EM_SHOWSCROLLBAR(FALSE)` stops RichEdit from maintaining `GetScrollInfo.nMax`, so the thumb was always filling the full track. Fixed by measuring document height directly via `Msb_MeasureRichVertMax` (`EM_POSFROMCHAR` + `EM_GETSCROLLPOS`, the same pattern as `Msb_MeasureRichHorzMax`). Result is cached in `richVertMax` and cleared by `msb_notify_content_changed`.
- **Vertical scrollbar — instant update on paste**: `msb_notify_content_changed` previously only synced the bar it was called on (H bar). It now clears both caches and calls `msb_sync` on the peer bar as well, so both bars reflect new content immediately after paste.
- **Last line always scrollable above H bar**: Added `Msb_ApplyRichFormatRect` — sends `EM_SETRECT` to shrink the RichEdit formatting rectangle by the H bar height plus `MSB_VERT_MARGIN` (6 logical px). RichEdit's own scroll clamp is tied to the format rect height, so the last line now scrolls fully above the bar. The format rect is re-applied after every scroll event because `EM_SHOWSCROLLBAR` causes RichEdit to reset it.
- **Breathing room below last line**: `MSB_VERT_MARGIN` (6 logical px, DPI-scaled) of white space is always visible below the last line of text in the RTF editor, giving a clean bottom margin regardless of scroll position.
- **Active/drag scrollbar color — pink → mid-gray**: Thumb drag and arrow-press highlight color changed from bleach pink (`RGB(250,215,220)`) to mid-gray (`RGB(140,140,148)`) for a more professional appearance.
- **RTF editor — app no longer goes to background on close**: `SetForegroundWindow(owner)` is now called before `DestroyWindow` in all three exit paths (Save, Cancel, ×). Previously only `EnableWindow(owner, TRUE)` was called, which re-enabled the owner but did not activate it; when the editor window was then destroyed Windows picked its own Z-order candidate, often sending the app to the background.

## [2026.04.02.11] - 2026-04-02

### Fixed
- **RTF editor — no longer always on top**: `OpenRtfEditor` was created with `WS_EX_TOPMOST` and never disabled the parent, making the editor float above all other applications. Fixed: removed `WS_EX_TOPMOST`; added `EnableWindow(hwndParent, FALSE)` before creation; added `EnableWindow(owner, TRUE)` in the OK, Cancel, and WM_CLOSE handlers; restored foreground via TOPMOST flash after the modal loop (same proven pattern as `CompNotesEditor`).
- **Horizontal scrollbar — track-click wrong position**: Clicking the track (above/below thumb) used `GetScrollInfo.nMax` = 32767 (the `EM_SETTARGETDEVICE` dummy line width) to compute the target position, so every click jumped to the wrong place. Fixed: track-click handler now overrides `si.nMax` / `si.nMin` with the real measured content width (`richHorzMax + S(24)`), matching what `Msb_Layout` already did.
- **Horizontal scrollbar — thumb drag wrong position**: The thumb drag handler had the same `nMax=32767` bug — `range` was computed from the raw SCROLLINFO, making large drag deltas produce tiny position changes and small ones jump wildly. Fixed with the same `richHorzMax` override applied in `WM_MOUSEMOVE` while dragging.
- **Horizontal scrollbar — last character clipped**: `Msb_MeasureRichHorzMax` used `EM_POSFROMCHAR(lastChar)` = the *left* edge of the last character, so the last character's width was never counted and text was clipped by roughly one character width. Fixed: now uses `EM_POSFROMCHAR(firstChar + lineLen)` = the position *after* the last character = its right edge.
- **Horizontal scrollbar — right-edge margin too tight**: Right-edge margin increased from `S(8)` to `S(24)` in all five locations (layout `nMax`, overflow check, `Msb_ClampRichHorzPos`, track-click, drag) so the last character shows fully with room for one more.

## [2026.04.01.11] - 2026-04-01

### Added
- **Hidden-mode scrollbar (Phase 4)**: By default the custom bar collapses to a 5 px strip when the cursor is away. Moving the cursor to the bar's edge smoothly expands it to its full 12 px width; leaving contracts it back. The strip always shows the thumb so document position is visible at all times.
- **Fade animation**: ~60 fps (16 ms timer, 9 steps ≈ 150 ms). `FadeState` enum (`FADE_HIDDEN → FADE_EXPANDING → FADE_VISIBLE → FADE_CONTRACTING`) + `fadeWidth` float in `MsbCtx` drives both bar width and paint.
- **Proximity trigger**: `WM_MOUSEMOVE` on the target window starts expansion when the cursor enters the outermost `MSB_WIDTH_FULL` px of the client edge — bar starts expanding before cursor physically reaches it.
- **Arrow buttons fade with bar**: Hidden while bar is < 5/8 full width during animation — appear and disappear cleanly, no clipping artifact.
- **Mousewheel stays hidden**: Wheel scroll does not pop the bar out — thumb position already visible on the 5 px strip.
- **`MSB_NOHIDE` unchanged**: Starts at full width, no animation.
- **`MSB_WIDTH_HIDDEN` = 5 px** (raised from 3 px during testing).
- **Thumb color states**: Idle = bleach blue (`RGB(160,196,222)`), hover = bleach green (`RGB(128,208,130)`), active scrolling = bleach pink (`RGB(250,215,220)`). Arrow buttons tinted to match. All colors are macros in `my_scrollbar.h`.
- **Hover tracking**: `WM_MOUSEMOVE` updates arrow/thumb hover states live. `TrackMouseEvent(TME_LEAVE)` + `WM_MOUSELEAVE` resets all states when cursor leaves the bar.
- **Click-to-position**: Clicking the track jumps content so the thumb centres on the click point. Uses `EM_SETSCROLLPOS` for RichEdit targets. No more page-up/page-down on track click.

### Fixed
- **Thumb proportional size**: RichEdit stops updating `SCROLLINFO.nPage` when native bar is hidden. Fixed by using the visible client pixel height as `nPage` directly.
- **Thumb position tracking**: RichEdit stops updating `SCROLLINFO.nPos` when native bar is hidden. Fixed by reading `EM_GETSCROLLPOS` for both layout and drag-start position.
- **Horizontal wheel direction**: `WM_MOUSEHWHEEL` delta sign was inverted — scroll-right now moves right.
- **Bars visible on attach**: Added `WS_CLIPSIBLINGS` to target so RichEdit does not overdraw the bar. Added `UpdateWindow` after attach for immediate paint.
- **Wheel pink color reset**: Timer tuned to 220 ms — thumb stays pink just long enough to be visible without blinking.

## [2026.03.31.08] - 2026-03-31

### Added
- **Custom scrollbar module** (`my_scrollbar.h` / `my_scrollbar.cpp`): Full Win32 custom scrollbar, no SetupCraft dependencies. `msb_attach(hWnd, flags)` / `msb_detach` / `msb_sync` public API. Supports `MSB_VERTICAL`, `MSB_HORIZONTAL`, `MSB_NOHIDE`. Double-buffered GDI painting, DPI-aware layout, rounded rectangles, triangle arrow glyphs, proportional thumb from `GetScrollInfo`.
- **Phase 2 mouse interaction**: Arrow clicks (`SB_LINEUP`/`SB_LINEDOWN`), track page clicks (`SB_PAGEUP`/`SB_PAGEDOWN`), auto-repeat timer (350 ms initial / 50 ms repeat), full thumb drag with `SetCapture` + `SB_THUMBTRACK` + `SB_THUMBPOSITION` + `SB_ENDSCROLL`.
- **Own mousewheel handler**: Intercepts `WM_MOUSEWHEEL`/`WM_MOUSEHWHEEL` in the target subclass; sends explicit `SB_LINEUP`/`SB_LINEDOWN` commands (honouring `SPI_GETWHEELSCROLLLINES`). Suppresses RichEdit's native wheel to ensure `SCROLLINFO.nPos` stays correct. Sub-tick delta accumulation for smooth high-precision scrolling.
- **Standalone test app** (`test_scrollbar/`): Three-pane test — top-left read-only, top-right writable, bottom full-width with vertical + horizontal bars and wide text for horizontal scroll testing.

### Fixed
- **Dual-attach**: Two bars (V+H) can share a target. Separate `MSB_Target_V` / `MSB_Target_H` property keys prevent the second attach from capturing `Msb_TargetSubclassProc` as `origProc` (would cause infinite recursion on scroll).
- **Native bar suppression on RichEdit**: `ShowScrollBar(FALSE)` does not stick on RichEdit. Fixed by using `EM_SHOWSCROLLBAR` (0x0460) and re-suppressing after every `WM_SIZE`, `WM_VSCROLL`, and `WM_HSCROLL`.
- **Bar scrolls with content**: Bar was a child of the target, so it moved with scrolled content. Fixed by making the bar a child of the target's parent (sibling), positioned via `ClientToScreen` + `ScreenToClient`.
- **Horizontal bar moves on vertical scroll**: `WM_VSCROLL` and `WM_HSCROLL` handlers now update only the relevant axis bar, not both.

## [2026.03.30.09] - 2026-03-30

### Fixed
- **Preview — auto-fit measurement window**: Removed `WS_EX_CLIENTEDGE` from the hidden measurement RichEdit. The 2 px top+bottom border caused a negative client area on the 1 px-tall window so the RichEdit never laid out content and `GetScrollInfo` always returned 0. Without the extended style the client height is exactly 1 px, content always overflows, and the scroll range is populated correctly.
- **Preview — buttons/checkboxes never truncated**: When the 75 % screen-height cap is applied, `contentFitH` is now updated to the capped viewport height so `LayoutPreviewControls` always leaves enough room for the extras panel and button row below the RTF viewport.
- **Preview — forced scrollbar removed from layout**: `LayoutPreviewControls` no longer unconditionally adds `WS_VSCROLL` to the split-layout RichEdit. The scrollbar is only added/removed by `AutoFitComponentHeight` when content genuinely overflows the 75 % cap. This eliminates the spurious always-visible scrollbar when content fits comfortably.
- **Preview — scroll wheel**: Replaced `WM_MOUSEWHEEL` forward with direct `EM_SCROLL` calls (one `SB_LINEUP`/`SB_LINEDOWN` per scroll-lines tick). The forwarded message was silently dropped when the RichEdit did not have keyboard focus; `EM_SCROLL` works unconditionally.

## [2026.03.30.08] - 2026-03-30

### Fixed
- **Preview — auto-fit height for Components page**: Replaced the unreliable live-RichEdit 1 px shrink approach with a dedicated hidden off-screen measurement window (1 px tall, off-screen position). Content always overflows the tiny viewport so `GetScrollInfo` reliably returns the true total height — including embedded images that `EM_FORMATRANGE` measure-only mode routinely undercounts.
- **Preview — V-align removed from split layout**: The V-align offset code incorrectly capped the RichEdit height to `contentFitH` in split layout, preventing the viewport from growing when the developer increased the window height (dead space appeared above the image). V-align now applies only in single-layout dialogs; split layout always anchors the RichEdit flush to the top and fills the full allocated `contentH`.
- **Preview — scroll wheel**: `WM_MOUSEWHEEL` forwarded from the preview window to the RichEdit so the developer can scroll content without clicking inside the RichEdit first.
- **Preview — scrollbar range sync**: `UpdateWindow(hContent)` called after each `SetWindowPos` so the scrollbar reflects the current content/viewport ratio before the user interacts.
- **Preview — control flicker on 1 px resize**: `InvalidateRect(hwnd, NULL, FALSE)` added at the end of `LayoutPreviewControls` to repaint vacated areas when controls shift position.
- **WinProgramManager RC version**: `FILEVERSION`/`ProductVersion` in `winprogrammanager.rc` corrected from `1.0.0.0` to `2026.3.26.7` / `"2026.03.26.07"` to match the application `ABOUT_VERSION`.

## [2026.03.28.10] - 2026-03-28

### Added
- **RTF editor — table column width: unit picker button**: The two separate "Column width (px)" and "Column width (%)" spinners are replaced by a single edit + spinner + **px ▼** / **% ▼** unit picker button (`IDC_RTFE_TD_COLWUNIT`). Selecting a unit converts the current value. Defaults to **50 %** for new tables so a 2-column table fills 100 % of the editor by default.
- **RTF editor — table: DPI-aware twip calculation**: Cell widths computed as `physPx × 15 / g_dpiScale` (correct at any DPI). `EM_GETRECT` used instead of `GetClientRect` to get the formatting-rectangle width (excludes RichEdit internal margin) — eliminates the prior ~5 % overflow.
- **RTF editor — table: proportional resize after window resize**: `RtfEd_RescalePctTables()` rescales all pct-mode table rows via `EM_SETTABLEPARMS` 80 ms after the editor window is resized (debounced timer on parent window `WM_TIMER` id 9901). Px-mode columns keep their fixed width.
- **RTF editor — table: mutually exclusive colWidthPx / colWidthPct**: `RtfTableParams` carries both `colWidthPx` (fixed logical px) and `colWidthPct` (% of editor at apply time). Only one can be active; pct mode takes priority in both `RtfEd_InsertTableNative` and `RtfEd_ApplyTableProps`.

### Fixed
- **RTF editor — table: right-click context menu**: Switched from `WM_CONTEXTMENU` (never sent by Msftedit.dll for mouse clicks) to `WM_RBUTTONUP` in the subclass proc. If caret is in a table the custom menu is shown; otherwise Msftedit's cut/copy/paste menu appears. Keyboard context menus (`lParam == -1`) still handled via `WM_CONTEXTMENU`.

## [2026.03.27.13] - 2026-03-27

### Added
- **RTF editor — table dialog: column width fields**: "Column width (px, 0=auto)" spinner (0–9999) and "Column width (%, 0=auto)" spinner (0–100) added; changing one automatically syncs the other. Both default to 0 (auto). `colWidthPx` stored in `RtfTableParams`; applied as `colWidthPx×15` twips in both `RtfEd_InsertTableNative` and `RtfEd_ApplyTableProps` when > 0. `edWidthPx` threaded into the dialog via `RtfEd_ShowTableDialog` for accurate px↔pct conversion.
- **RTF editor — table dialog: cell H alignment**: "Cell H alignment" combo (Left / Centre / Right) added. `cellHAlign` stored in `RtfTableParams`; applied via `EM_SETPARAFORMAT PFM_ALIGNMENT` on the full table selection in both insert and edit paths.
- **RTF editor — table dialog: label clarifications**: "H alignment" renamed to "Table alignment"; "V alignment" renamed to "Cell V alignment".
- **RTF editor — right-click: cell alignment submenu**: Right-click inside a table now shows "Table properties…" + "Cell alignment ▶" submenu (Align left / Align centre / Align right). Each item calls `RtfEd_SetAlignment(hwnd, PFA_*)` via `EM_SETPARAFORMAT PFM_ALIGNMENT`.
- **RTF editor — right-click: `WM_CONTEXTMENU` approach**: Replaced `WM_RBUTTONUP` with `WM_RBUTTONDOWN` (pre-position caret) + `WM_CONTEXTMENU` (correct Win32 pattern). Keyboard-invoked context menu (`lParam == -1`) handled via `EM_POSFROMCHAR`.
- **RTF editor — toolbar tooltip fix**: Insert table tooltip changed from `"Insert table  (2 × 2)"` to `"Insert table…"`.

### Fixed
- **RTF editor — main window foreground after editor closes**: `OpenRtfEditor` now calls `SetForegroundWindow` + `BringWindowToTop(hwndParent)` after the modal loop, preventing the main window from going behind other apps when the editor is closed.
- **RTF editor — column width auto-override bug**: The old code computed `initPx` from `editorWidth × widthPct ÷ cols` in `WM_CREATE`; that value was saved on OK as an explicit column-width override, causing column widths to change unintentionally when adding columns. Column width spinners now initialise to the stored value only (0 for new tables).

## [2026.03.26.16] - 2026-03-26

### Added
- **Preview — developer-sized lock**: Once the developer manually resizes the preview via the sizer spinners, automatic height adjustments (`AutoFitComponentHeight`) are suppressed for that project. `s_previewUserSized` is set on the first sizer interaction, reset when a new project is opened, and persisted via `installer_preview_user_sized_<id>` so the preference survives across sessions.
- **Preview — `MeasureRichEditLogHeight()` helper**: Uses `EM_FORMATRANGE` (measure-only, `wParam=FALSE`) to determine the exact natural content height of a RichEdit in logical pixels — works for RTF, plain text, and embedded images.
- **Preview — smart auto-fit for Components page (both layouts)**: `AutoFitComponentHeight` now handles both the no-RTF layout (`contentHidden=true`: `144 + n×28` logical px) and the split RTF+items layout (`contentHidden=false`: `max(2×rtfH+124, n×56+190)` logical px), deriving window height from the measured RTF content so the full content is visible without a scrollbar.

### Fixed
- **Preview — app disappears on close**: The main window was going behind other windows after the preview closed. Root cause: `EnableWindow(hwndParent, TRUE)` was called after `DestroyWindow`, by which point Windows had already activated a different window. Fix: `WM_DESTROY` on the preview re-enables the owner immediately; `ShowPreviewDialog` then calls `SetActiveWindow` + `SetForegroundWindow` as a fallback.

## [2026.03.26.14] - 2026-03-26

### Added
- **Toolbar active-page highlight**: The active toolbar button now displays a light blue background (`RGB(196, 224, 246)`) so the current page is immediately visible. All ten page-linked buttons participate (Files, Registry, Shortcuts, Dependencies, Dialogs, Settings, Build, Test, Scripts, Components). The three non-page buttons (Save, Close Project, Exit) are unaffected. `SwitchPage()` sets an `IsActivePage` window property on the matching button and removes it from all others, then triggers redraws. `DrawCustomButton` checks `IsActivePage` and overrides the base colour when the button is neither pressed nor hovered — hover and pressed states still use their own colours on top of the active background.

## [2026.03.26.09] - 2026-03-26

### Added
- **Dialogs page — vertical scrollbar**: The Dialogs page now adds `WS_VSCROLL` to the main window when its content height exceeds the available view area. `IDLG_BuildPage` returns the absolute Y of the last content row (used by `SwitchPage` for `SCROLLINFO` sizing). `IDLG_SetScrollOffset()` / `IDLG_GetScrollOffset()` expose the current scroll offset. `IDLG_TearDown` now resets the offset to 0 and removes `WS_VSCROLL` on page switch. Mousewheel scrolling wired in `mainwindow.cpp` `WM_MOUSEWHEEL` for page index 4. `WS_CLIPSIBLINGS` stamped on all non-toolbar page controls on `SwitchPage(4)` to prevent scrolled controls from overdrawing the status bar. Follows the same scroll pattern as the Shortcuts page (documented in `scrollbar_INTERNALS.txt`).

## [2026.03.25.15] - 2026-03-25

### Added
- **Preview dialog overhaul**: Style is now `WS_POPUP | WS_CAPTION` — no `WS_SYSMENU`, no × button (Alt+F4 still cancels). Title bar shows `{Installer title} — Preview — {Dialog name}`, updated on every navigation.
- **Sizer panel (`IDLGSizerClass`)**: Floating tool window left of the preview with Width / Height spinners (200–1400 / 150–1000 logical px). Changes applied live: `EN_CHANGE` → `SetWindowPos` on preview → `WM_SIZE` → `LayoutPreviewControls`. Default 460×360 logical px; DPI-scaled via `S()` + `AdjustWindowRectEx`. Owned by the preview window so both sink behind other apps together; `WS_EX_TOOLWINDOW` keeps it off Alt+Tab.
- **`LayoutPreviewControls()`**: New helper called from `WM_CREATE` and `WM_SIZE`. Positions type-title STATIC, RichEdit, and all three buttons from the live client rect; button widths remeasured on every call.
- **Back / Next navigation**: `NextVisibleType()` / `PrevVisibleType()` skip invisible conditional types. Back disabled at first visible; Next becomes "Finish ✔" at last and closes on click. `NavigateTo()` re-streams RTF, updates title bar and heading.
- **DB persistence for preview size**: `s_previewLogW` / `s_previewLogH` saved/loaded via `installer_preview_w_<id>` / `installer_preview_h_<id>` settings keys.
- **New locale keys** (both `en_GB.txt` copies): `idlg_preview_finish`, `idlg_sizer_title`, `idlg_sizer_w_label`, `idlg_sizer_h_label`, `idlg_sizer_w_tip`, `idlg_sizer_h_tip`.
- **New control IDs**: `IDC_IDLG_SZR_W_EDIT` / `W_SPIN` / `H_EDIT` / `H_SPIN` (7120–7123) in `dialogs.h`.
- **Z-order / always-on-top policy**: New section in `dialog_INTERNALS.txt` — rule against `WS_EX_TOPMOST`, ownership-based Z-order pattern, `WS_EX_TOOLWINDOW` guidance, and audit checklist for the translation pass.

### Fixed
- **Back button had no text**: `LayoutPreviewControls` was missing `SetWindowTextW` for the Back button; label was blank after creation.
- **Sizer was globally always-on-top**: Removed `WS_EX_TOPMOST`; owner changed from `hwndParent` to `hPreview` so the sizer stays above the preview without floating over unrelated apps.

## [2026.03.25.09] - 2026-03-25

### Added
- **Dialogs page — new module (`dialogs.h` / `dialogs.cpp`)**: Fully implemented installer-dialogs page at toolbar index 4. Renders one row per dialog type (Welcome, License, Dependencies, For Me/All Users, Components, Shortcuts, Ready to Install, Install, Finish) with a 32×32 shell32.dll icon, type name, "Edit Content…" button, and "Preview…" button per row. Conditional rows (Dependencies, For Me/All Users, Components, Shortcuts) appear only when the corresponding feature is enabled in the project. Control IDs 7000–7045.
- **Installer title & icon section**: Panel at the top of the Dialogs page — 48×48 icon preview (default: shell32 #2), "Change Icon…" button (OFN_EXPLORER shell picker, *.ico only), and "Installer title:" edit field auto-filled from the project name. `IDLG_SetInstallerInfo()`, `IDLG_GetInstallerTitle()`, `IDLG_GetInstallerIconPath()` accessors for mainwindow—module seeding and persistence. Etched `SS_ETCHEDHORZ` divider separates the section from the dialog rows below.
- **Preview dialog**: Read-only facsimile of an installer page — page title label, scrollable read-only RichEdit (pre-loaded from saved RTF or placeholder), and plain `BS_PUSHBUTTON` Back (`◀  Back`) / Next (`Next  ▶`) / Cancel buttons sized via `MeasureButtonWidth`. Back is disabled on the Welcome dialog.
- **`installer_dialogs` DB table**: `(id PK, project_id, dialog_type INTEGER, content_rtf TEXT, UNIQUE(project_id, dialog_type))`. Three new `db.cpp` functions: `UpsertInstallerDialog`, `DeleteInstallerDialogsForProject`, `GetInstallerDialogsForProject`.
- **New accessors**: `MainWindow::UseComponents()`, `MainWindow::AskAtInstallEnabled()`, `DEP_HasAny()`, `SC_HasOptOut()` — used by `IDLG_BuildPage` to compute row visibility.
- **`dialogs_INTERNALS.txt`**: Architecture reference for the Dialogs page module. Registered in `API_list.txt`.
- **Locale keys** (both `locale/en_GB.txt` copies): `idlg_page_title`, `idlg_btn_edit`, `idlg_btn_preview`, `idlg_btn_edit_tip`, `idlg_btn_preview_tip`, `idlg_change_icon`, `idlg_change_icon_tip`, `idlg_inst_title_label`, `idlg_name_*` (9 dialog type names), `idlg_edit_title`, `idlg_edit_save`, `idlg_edit_cancel`, `idlg_preview_caption`, `idlg_preview_no_content`, `idlg_preview_back`, `idlg_preview_next`, `idlg_preview_cancel`.

### Fixed
- **WM_DRAWITEM range for Dialogs page**: The main WndProc `WM_DRAWITEM` condition now covers `IDC_IDLG_ROW_BASE`…`+IDLG_COUNT*4` and `IDC_IDLG_INST_CHANGE_ICON`. Previously all row buttons rendered as blank grey rectangles.
- **IDLG_OnCommand not dispatched**: `IDLG_OnCommand()` was never called from the main `WM_COMMAND` handler — all row buttons and the Change Icon button were silently dropped. Added the call alongside `SC_OnCommand` / `DEP_OnCommand`.
- **Installer-title layout**: Label and edit field x-positions are now computed from the actual measured button width (`btnX + wChange + gap`) instead of hardcoded `S(220)` / `S(335)`, preventing truncation at any DPI or locale.

## [2026.03.24.10] - 2026-03-24

### Added
- **`dep_instructions` DB table** — `(id PK, dep_id, project_id, sort_order, rtf_text)` created by `InitDb`. `InsertExternalDep` inserts each `instructions_list` item; `GetExternalDepsForProject` loads pages per dep ordered by `sort_order`; `DeleteExternalDepsForProject` deletes from `dep_instructions` before the main rows (no FK cascade).
- **`ExternalDep::instructions_list`** — `std::vector<std::wstring>` replaces the single `instructions` wstring, allowing any number of ordered RTF pages per dependency.
- **`InstrIconCtx` struct** — heap-allocated per icon, stored in `GWLP_USERDATA`; fields: `HICON hIco`, `WNDPROC prevProc`, `bool tracking`. Freed via `WM_NCDESTROY` so each icon is fully self-contained.
- **`WM_DEPINSTR_REMOVE` custom message** (`WM_USER + 42`) — posted by the icon subclass right-click handler and handled by `DepDlgProc default:` to remove a page, destroy its controls, renumber remaining labels, and re-Reflow.
- **New locale keys** — `dep_dlg_add_instr`, `dep_dlg_instr_icon_tip`, `dep_dlg_instr_remove_tip`, `dep_dlg_instr_remove`, `dep_instr_none`, `dep_instr_has_content` added to `locale/en_GB.txt`.

### Changed
- **Instructions section redesigned — icon grid**: Each page shown as a shell32.dll icon #70 (document, 40×40 px) with a **bold** page number centred below. Icons row-wrap when the row fills. Double-click opens the RTF editor for that page; right-click → context menu removes it. An "Add Instructions…" button (shell32 #87, Blue) always appears below the grid.
- **`DIO_BEFORE_WELCOME` restricted for Instructions Only**: When the delivery type is switched to `DD_INSTRUCTIONS_ONLY`, the "Before the Welcome screen (silent)" item is removed from the Install step combo (silent pre-welcome install is incompatible with a manual-steps workflow). Switching to any other delivery type restores it at index 1.
- **Bold number labels**: The numeric label below each instruction icon now uses `FW_BOLD` (derived from `SPI_GETNONCLIENTMETRICS`) in both the initial creation path and the dynamic Add path.

## [2026.03.24.08] - 2026-03-24

### Added
- **`ShowValidationDialog()`** — new styled single-button OK dialog in `ctrlw.cpp` / `ctrlw.h`. Custom `ValidationDialogProc` / `ValidationDlgData`, `SPI_GETNONCLIENTMETRICS` font (×1.2), `ButtonColor::Blue` OK button, proper modal loop, DPI-aware, centred on parent. Uses `ok` locale key for button label.
- **`DD_TITLE_H = 28`** — new layout constant for the Edit Dependency dialog title headline. Replaces the reuse of `DD_LABEL_H = 18`; prevents text clipping at high DPI.
- **`i18n_INTERNALS.txt`** — new documentation: English-first policy (only `locale/en_GB.txt` maintained during development), locale file format, key naming conventions, safe-fallback lookup pattern, `ExpandEscapes` usage, guide for adding new keys. Registered in `API_list.txt`.
- **`dep_dlg_delivery_bundled` locale key** — long-form "Bundled (included in installer)" for the dialog combo, separate from `dep_delivery_bundled` (short form for ListView cells). Eliminates a key collision.
- **Add/Edit title awareness** — dialog title bar and headline show "Add Dependency" for new records (`dep.id == 0`) and "Edit Dependency" for existing ones. New locale key `dep_dialog_add_title` in `locale/en_GB.txt`.

### Changed
- **dep_edit_dialog — progressive disclosure complete for Bundled** — Bundled delivery type now shows only: Required · Install step · License · Credits. Full map: Bundled → Required · Order · License · Credits; Auto-download → Required · Order · Detection (optional) · Network(all) · License · Credits; Redirect URL → Required · Order · Detection (optional) · Network(URL+offline) · License · Credits; Instructions only → Required · Order.
- **dep_edit_dialog — fonts match main app** — body label font changed from hardcoded `CreateFontW(-S(11), ...)` to `SPI_GETNONCLIENTMETRICS lfMessageFont × 1.2` + `CLEARTYPE_QUALITY` (matches `s_scaledFont`). Title headline changed to `× 1.5 + FW_SEMIBOLD` (matches `s_hPageTitleFont`). Section headers use a bold variant of the same NCM font.
- **dep_edit_dialog — RTF editor width** — `preferredW` on both `OpenRtfEditor` calls increased from `S(820)` to `S(880)`. Toolbar now fits in a single row at default sizes.
- **dep_edit_dialog — edit buttons centred** — "Edit License…" button centred under its indicator field (`bx = s_ddLX + (s_ddEW - bw) / 2`).
- **dep_edit_dialog — dialog placement** — dialog opens with its title bar `S(3)` px below the main window's title bar (`dlgY = rcParent.top + S(3)`), work-area clamped.
- **dep_edit_dialog — validation errors** — both `MessageBoxW` calls replaced with `ShowValidationDialog()`. Title from `validation_error` locale key; messages from `dep_err_no_name` / `dep_err_no_delivery`.
- **dep_edit_dialog — optional field labels** — five fields now labelled explicitly as optional: Silent install arguments, SHA-256 hash, Minimum required version, License section header, Credits / attribution.

### Removed
- **dep_edit_dialog — Architecture field** — the architecture combo and label removed from the dialog. The dependency's architecture always matches the main application.
- **dep_edit_dialog — Instructions section** — the Manual install instructions RTF section (indicator + "Edit Instructions…" button) removed. The dependency's own installer handles its documentation.

## [2026.03.23.09] - 2026-03-23

### Added
- **`DepInstallOrder::DIO_UNSPECIFIED (−1)`** — new sentinel value meaning "not yet chosen"; valid at OK (no validation error); stored in DB as `−1`; generated installer will apply a sensible default. `ExternalDep::install_order` default changed from `0` to `DIO_UNSPECIFIED`.
- **`DepInstallOrder::DIO_CUSTOM_DIALOG (4)`** — new install stage for a developer-defined custom dialog step anywhere in the installer wizard.
- **Install order — named dropdown** — the install-order field in the Edit Dependency dialog is now a combo box with six named choices: "Choose install step…" / "Before the Welcome screen (silent)" / "After the Welcome dialog" / "Before install (after License page)" / "After the main program installs" / "At a custom dialog step". Previously a free numeric text edit.
- **Delivery combo — OK validation** — pressing OK without choosing a delivery type (leaving "Choose type…" selected) now shows a validation error (`dep_err_no_delivery`). Previously the sentinel index was silently coerced to `DD_BUNDLED (0)`.
- **Locale keys** — `dep_install_order_choose` and `dep_install_order_custom_dialog` added to `locale/en_GB.txt`.

### Changed
- **Install order label** — "Install order:" renamed to "Install step:" (`dep_dlg_install_order`).
- **Install order locale strings** — four existing `dep_install_order_*` keys reworded for clarity: "Before the Welcome screen (silent)" / "After the Welcome dialog" / "Before install (after License page)" / "After the main program installs".

## [2026.03.22.10] - 2026-03-22

### Fixed
- **`InsertExternalDep` — dangling-pointer bug** — integer-valued fields (`is_required`, `delivery`, `install_order`, `architecture`, `offline_behavior`) were bound via `p_bind_text(stmt, n, std::to_string(...).c_str(), -1, NULL)`. The temporary `std::string` from `std::to_string` is destroyed at the semicolon; the raw pointer passed to SQLite was dangling when `p_step` read it. The stored value was effectively always 0, causing delivery type to silently revert to `DD_BUNDLED` after every Save. Fixed by giving each integer field a named `std::string` local variable (`sIsReq`, `sDelivery`, `sOrder`, `sArch`, `sOffline`) whose lifetime spans from `p_bind_text` through `p_step`.

## [2026.03.22.09] - 2026-03-22

### Added
- **RTF editor — Open file button (`IDC_RTFE_OPEN`)** — new button on the toolbar (shell32.dll icon 38). Opens a file picker for RTF / TXT / `.md` files (filter assembled from the locale map). RTF streamed via `SF_RTF`; plain-text and Markdown decoded from UTF-8, streamed via `SF_TEXT|SF_UNICODE`. Toolbar syncs after load.
- **RTF editor — responsive toolbar layout (`RtfEd_LayoutToolbar`)** — all 18 controls repositioned on every `WM_SIZE` and at `WM_CREATE`; switches between one row and two rows based on available width. `editY` updated accordingly so the RichEdit always fills the remaining space.
- **RTF editor — `pLocale` field in `RtfEditorData`** — optional `const std::map<std::wstring,std::wstring>*` for the application locale map. All 27 internal strings (toolbar tooltips, image-picker filter/title, Open dialog filter/title) looked up via `RtfLS()`; English fallbacks used when null. All 27 keys in `edit_rtf_API.txt` §14 and `locale/en_GB.txt`.
- **dep_edit_dialog — inline RTF editor for License** — file-path + Browse replaced by read-only status indicator + "Edit License…" button that opens `OpenRtfEditor`. License stored as RTF string (`s_depLicRtf`).
- **dep_edit_dialog — inline RTF editor for Instructions** — plain multiline edit replaced by read-only indicator + "Edit Instructions…" button. Instructions now RTF (`s_depInstrRtf`).

### Changed
- **dep_edit_dialog — content column widened** from 400 px to 560 px.

### Fixed
- **RTF editor — Ctrl+B / Ctrl+I / Ctrl+U** — intercepted in `OpenRtfEditor` message loop before `TranslateMessage` while the RichEdit has focus; forwarded to `WM_COMMAND` handlers so toolbar state stays in sync.
- **RTF editor — Open button painted** — `WM_DRAWITEM` condition now includes `IDC_RTFE_OPEN`; previously the `BS_OWNERDRAW` button was never painted.
- **RTF editor — tooltip text no longer garbled** — `RtfEd_SetToolTip` heap-allocates a copy (`new wchar_t[len]`); `WM_NCDESTROY` in `RtfEd_ToolbarBtnProc` calls `delete[]`. Previous code stored a pointer into a `std::wstring` temporary's buffer, which became a dangling pointer immediately.

### Removed
- **`rtf_editor_test.cpp` / `RtfEditorTest` CMake target** — standalone test harness deleted; references removed from `edit_rtf_API.txt` and `API_list.txt`.

## [2026.03.21.15] - 2026-03-21

### Added
- **NSBEdit — Print support (File → Print..., Ctrl+P)** — standard `PrintDlgW` dialog; multi-page rendering via `EM_FORMATRANGE`; page geometry computed in twips from printer DC device capabilities (`PHYSICALWIDTH` / `PHYSICALOFFSETX` / `LOGPIXELSX`). Selection restored after printing; printer DC cleaned up. NSBEdit .exe (with print) extracted to author's toolbox; source not included in the SetupCraft distribution.

## [2026.03.21.11] - 2026-03-21

### Added
- **New RTF editor component (`edit_rtf.h` / `edit_rtf.cpp`)** — general-purpose resizable rich-text editor modal (`WS_OVERLAPPEDWINDOW`, default S(660)×S(520)). Two-row toolbar: Bold/Italic/Underline/Strikethrough/Subscript/Superscript/FontFace/FontSize (row 1); Align L/C/R/J / Bullet / Numbered / Text colour / Highlight / Insert Image (row 2). `BS_AUTOCHECKBOX|BS_PUSHLIKE` toggle buttons synced from caret on `EN_SELCHANGE`. RTF streamed in/out via `EM_STREAMIN`/`EM_STREAMOUT`. All `notes_editor` bug-fixes carried forward. Control IDs 4500–4530.
- **Image insertion (PNG / JPEG)** — 🖼 toolbar button: file picked via `GetOpenFileNameW`, classified by header bytes (PNG 8-byte signature / JPEG SOF marker scan), hex-encoded, streamed into caret as `\pict\pngblip`/`\jpegblip` via `SF_RTF|SFF_SELECTION`. Fully embedded in the RTF string. Zero extra library dependencies.
- **Toolbar tooltips** — all 17 toolbar controls subclassed via `RtfEd_SetToolTip` / `RtfEd_ToolbarBtnProc`; shows the project's `ShowMultilingualTooltip` on hover. `WM_MOUSELEAVE` / `TrackMouseEvent` for reliable hide.
- **`rtf_editor_test.cpp`** — standalone WinMain test harness; CMake target `RtfEditorTest`, output `build\rtf_editor_test.exe`.
- **`edit_rtf_API.txt`** / **`API_list.txt`** — full component reference and index entry.
- **NSBEdit — standalone RTF notepad** — self-contained, statically-linked RTF notepad built on the `edit_rtf` component patterns: File menu (New / Open / Save / Save As / Exit with unsaved-changes prompt), same two-row formatting toolbar, PNG/JPEG image insertion, English-only hover tooltips, shell32.dll index 70 icon on title bar/taskbar/status bar, command-line file opening. No MinGW runtime DLL dependencies (`-static -static-libgcc -static-libstdc++`). Extracted to author's toolbox for reuse; source not included in the SetupCraft distribution.

### Fixed
- **`makeit.bat` packaging** — searches `build\SetupCraft.exe` specifically instead of the first `*.exe` found (alphabetically `rtf_editor_test.exe` was always matched first, leaving `SetupCraft.exe` out of the package).

## [2026.03.21.09] - 2026-03-21

### Fixed
- **Files page button mutual width reconciliation** — All three Files-page buttons (Add Folder, Add Files, Remove) are now measured before any button is created. `topRowW = wFDir + fBtnGap + wFFiles` is computed; if `wRemove > topRowW`, the extra pixels are added to `wFDir` so Add Folder grows and the visual block stays flush-left. Remove then uses `topRowW` as its width. Previously Remove's width was derived one-way from the top row, which would clip Remove's text in any locale where "Remove" translates wider than the two buttons combined.

## [2026.03.20.11] - 2026-03-20

### Added
- **Dependencies page** — new modular page (`deps.h` / `deps.cpp` / `deps_INTERNALS.txt`, page index 3). ListView with Name / Delivery / Required / Detection columns, three action buttons (Add / Edit / Remove), right-click context menu, double-click to edit. State held in `s_deps` (in-memory vector); persisted to new `external_deps` DB table on Save. DB API: `InsertExternalDep`, `GetExternalDepsForProject`, `DeleteExternalDepsForProject`.
- **Edit Dependency dialog** (`dep_edit_dialog.h` / `dep_edit_dialog.cpp`) — modal measure-then-create dialog with all 18 `ExternalDep` fields: delivery type (Bundled / Auto-download / Redirect URL / Instructions only), Required checkbox, architecture (Any / x64 / ARM64), install order, Detection section (registry key, file path, min version), conditional Network section (URL, SHA-256, silent args, offline behaviour), License section (path + Browse… picker), credits, multi-line instructions.
- **Scrollable dialog** — dialog uses `WS_VSCROLL`, height-clamped to work area. `WM_VSCROLL` and `WM_MOUSEWHEEL` handlers move all child controls. Centers over parent; clamped to work area on all four sides.

### Changed
- **`DepArch` enum** — `DA_X86` removed; `DA_ARM64` renumbered 3 → 2. App is 64-bit only. Arch combo uses `CB_SETITEMDATA`/`CB_GETITEMDATA`. Default architecture changed `DA_ANY` → `DA_X64`.

### Fixed
- **Dependencies page title font** — title STATIC now carries `IDC_DEP_PAGE_TITLE (6100)`; `WM_CTLCOLORSTATIC` selects `s_hPageTitleFont`, matching Files and Shortcuts headings.
- **`DepDlgProc` signature** — corrected from `INT_PTR CALLBACK` to `LRESULT CALLBACK`. Added `WM_CTLCOLORSTATIC`, `WM_KEYDOWN` Escape, `DefWindowProcW` fallthrough. Modal loop aligned with `sc_shortcut_dialog.cpp` pattern.

## [2026.03.20.09] - 2026-03-20

### Changed
- **Pin status labels ("Not Pinned / Pinned / Multi Pinned") removed** — the checkboxes make pin state self-evident; removing them gives the page a cleaner layout. `IDC_SC_SM_PIN_LABEL` and `IDC_SC_TB_PIN_LABEL` controls removed; `SC_RefreshPinLabels` now only updates pin-button enable state.
- **Select-all removed from 64×64 pin icons** — clicking the Pin to Start / Pin to Taskbar icon no longer bulk-toggles checkboxes. Each shortcut must be selected individually.

### Added
- **64×64 pin icon hover tooltip** — when a pin icon is enabled, hovering shows a multiline tooltip: "Use the checkboxes below to select which shortcuts to pin to Start" / "…to pin to the Taskbar". Added `WM_MOUSEMOVE`/`WM_MOUSELEAVE` to `SC_DesktopIconSubclassProc`. The disabled-state "Add shortcuts to pin first" tooltip (mainwindow.cpp timer callback) is unchanged.
- **`SC_TooltipSubclassProc` / `AttachTooltip` helper** — generic reusable hover-tooltip subclass in `shortcuts.cpp`. Text stored as heap `std::wstring` in property `L"TtText"`; `WM_NCDESTROY` frees it. Subclass id 2 (coexists with `CheckboxSubclassProc` id 1).

### Fixed
- **Pin-strip write-back fix** — `SC_OnCommand` now receives the real `HWND` from `lParam` and the `wmEvent` code. Range handlers use `hCtrl` directly with a `wmEvent != 0` guard that blocks phantom writes during page rebuilds. Pin state now survives page switches.
- **DB persistence for `pinToStart`/`pinToTaskbar`** — `SC_SaveToDb`/`SC_LoadFromDb` now map the `pin_to_start`/`pin_to_taskbar` columns. Existing databases are migrated via `ALTER TABLE` in `DB::InitDb`.

**Shortcuts page is now feature-complete.**

## [2026.03.19.15] - 2026-03-19

### Fixed
- **Pin-strip checkboxes now persist state across page switches** — clicking an individual "Pin to Start" or "Pin to Taskbar" strip checkbox now immediately writes `pinToStart`/`pinToTaskbar` back to the matching `ShortcutDef` in `s_scShortcuts`. Previously no write-back handler existed: the custom checkbox subclass toggled the visual state but never updated the data model, so state was discarded every time the page was torn down and rebuilt.
- **Pin-strip cross-talk fix (UB → `SC_RefreshPinLabels`)** — `SC_RefreshPinStrips` destroys and recreates every pin checkbox. Calling it from `WM_COMMAND` while the triggering checkbox's `WM_LBUTTONUP` is still on the call stack is UB: the button's post-`WM_LBUTTONUP` cleanup ran against an already-destroyed HWND, causing spurious check/uncheck on adjacent strip entries. Added `SC_RefreshPinLabels()` — a lightweight helper that updates only the "Not Pinned / Pinned / Multi Pinned" status labels and bulk-pin button enable state, without touching checkbox HWNDs. Individual-click handlers now call this instead of `SC_RefreshPinStrips`.
- **Entry screen font scaling corrected** — `g_guiFont` was created from the raw `NONCLIENTMETRICS` `lfHeight` without the `× 1.2f` scale applied to `s_scaledFont` in the main window, making entry-screen labels and the new-project dialog visually smaller. Fixed by adding `if (lf.lfHeight < 0) lf.lfHeight = (LONG)(lf.lfHeight * 1.2f)` before `CreateFontIndirectW`. Same fix applied to `g_tooltipFont` in `tooltip.cpp`.
- **New-project dialog title and label fonts corrected** — dialog title used hardcoded `CreateFontW(-18, …)` instead of `NONCLIENTMETRICS × 1.5 + FW_SEMIBOLD`. All five static labels (Name, Directory, Description, Language, Version) were never sent `WM_SETFONT` and fell back to the system default font. Both now use the correct scaled body/title fonts.

## [2026.03.19.14] - 2026-03-19

### Added
- **Shortcuts page vertical scrollbar** — `SC_BuildPage` now returns `int` (absolute Y of last content row). `SwitchPage` case 2 uses this to set up `SCROLLINFO` (`nMax = contentH-1`, `nPage = viewH`) and add `WS_VSCROLL` when content exceeds the view. `WM_VSCROLL` and `WM_MOUSEWHEEL` handlers added to the main window. `SC_SetScrollOffset`/`SC_GetScrollOffset` in `shortcuts.cpp`; `SC_RefreshDesktopStrip` and `SC_RefreshPinStrips` subtract the offset when positioning controls. `SC_TearDown` removes `WS_VSCROLL` and resets the offset on page switch. Status-bar height always measured via `GetWindowRect(s_hStatus)`, not hardcoded.
- **`make_ico.ps1`** — PowerShell script wrapping ImageMagick `magick` to repack any source `.ico` into a multi-frame icon (16, 24, 32, 40, 48, 64, 96, 128 px). `icons/trashcan_empty.ico` generated from a 128 px source and embedded in `SetupCraft.rc` as resource ID 2 (`IDI_TRASHCAN = 2` in `shortcuts.h`).
- **`scrollbar_INTERNALS.txt`** — architectural doc covering SCROLLINFO setup, why `SW_SCROLLCHILDREN` fails for below-viewport controls, the correct manual child-enumeration pattern, controls to exclude, `SC_SetScrollOffset` integration, teardown protocol, and status-bar `HWND_TOP` anti-overlap technique. Entry added to `API_list.txt`.

### Fixed
- **Scrollbar mechanism — `SW_SCROLLCHILDREN` replaced with manual child enumeration** — `ScrollWindowEx(..., SW_SCROLLCHILDREN)` only moves children intersecting the scroll rect at call time; controls created below the viewport are never moved and stay permanently unreachable. Both `WM_MOUSEWHEEL` and `WM_VSCROLL` handlers now enumerate all direct children of `hwnd` via `GetWindow(hwnd, GW_CHILD)` and call `SetWindowPos` on each page control (skipping toolbar buttons, the About button, and the status bar). Status bar is pinned to `HWND_TOP` after each scroll step.
- **Page controls painting over status bar** — added `WS_CLIPSIBLINGS` to all page controls after `SC_BuildPage`. Each control now excludes sibling-covered pixels from its own paint region, so the status bar (always at `HWND_TOP`) is never overdrawn by a scrolled page control.
- **Instant pin-strip refresh on all mutation paths** — `SC_RefreshPinStrips` now called from all 8 mutation sites, including `IDC_SC_SM_REMOVE` (folder Remove button and context-menu equivalent) and `IDM_SC_CTX_EDIT_SM` (SM shortcut Edit, where an exe-path change may alter pin eligibility). Previously those two paths refreshed the SM tree but left pin-strip checkboxes stale.

## [2026.03.19.12] - 2026-03-19

### Added
- **VFS picker double-click to select** — `NM_DBLCLK` on `VFSP_LIST` now sends `WM_COMMAND(VFSP_OK)` when a row is selected, identical to clicking OK.
- **SM tree per-shortcut program icons** — `SC_RebuildSmTree()` now rebuilds the image list on every call. Indices 0/1 = folder icons; index 2 = fallback link; indices 3+ = per-shortcut icon loaded via `PrivateExtractIconsW` from `iconPath` or `exePath`. Image list creation removed from `SC_BuildPage`.

### Changed
- **SM tree width = button row width** — `treeW = addW + btnGap + scW + btnGap + remW`; `bRowX = treeX`. Tree and buttons now share the same horizontal extent and are centred together. Button constants moved before tree creation.



### Added
- **SM tree shortcut child items (Phase 2)** — `SCT_STARTMENU` shortcuts now appear as leaf items in their parent folder node in the Start Menu TreeView. Image-list index 2 (`shell32.dll` #17 link icon) distinguishes them from folder nodes (indices 0/1). `lParam` scheme: positive = folder `node.id`, negative = `-(sc.id)`. New `SC_RebuildSmTree(selectLParam)` replaces the inline population block and is called on every page visit, shortcut add/edit/remove, and folder remove. Double-click opens Edit dialog; right-click shows Edit/Remove shortcut menu (`IDM_SC_CTX_EDIT_SM` 6307, `IDM_SC_CTX_REMOVE_SM` 6308). TVN_BEGINLABELEDIT cancelled for shortcut items. TVN_SELCHANGED disables Add Subfolder and Add Shortcut Here for shortcut items. Folder remove also cleans up shortcuts in the removed subtree. "Add Shortcut Here" always adds new — multiple shortcuts per SM folder now supported.
- **Multiple Desktop shortcuts** — clicking the 64×64 Desktop icon always adds a new shortcut. Existing shortcuts appear as 16×16 mini-icons (centred strip below the big icon). `SC_DskMiniSubclassProc` subclass handles paint, tooltip, double-click edit, and right-click Edit/Remove. `SC_RefreshDesktopStrip()` rebuilds the strip from `s_scShortcuts`. ID range `IDC_SC_DSK_STRIP_BASE` 5400–5449; `IDM_SC_CTX_EDIT_DSK` 6305 / `IDM_SC_CTX_REMOVE_DSK` 6306.
- **SM tree custom hover tooltip** — `TVS_NOTOOLTIPS` added to suppress system truncation tooltip; `SC_SmTreeSubclassProc` shows the project's custom tooltip on `WM_MOUSEMOVE` via `TreeView_HitTest` + `ShowMultilingualTooltip`.
- **"Add Shortcut Here" button** (`IDC_SC_SM_ADDSC` / 5213) — green button in the SM section; composite icon (shell32.dll 257 + 29); tooltip via `sc_sm_addsc_tooltip`.
- **"Add shortcut here…" context menu item** — first item in the SM tree right-click menu, dispatches to `IDC_SC_SM_ADDSC`.
- **`BuildSmPath()` helper** — builds breadcrumb `Start Menu › Programs › Folder` from a node id.
- **DB persistence for shortcuts** — two new tables: `sc_menu_nodes (id, project_id, parent_id, name)` and `sc_shortcuts (id, project_id, type, sm_node_id, name, exe_path, working_dir, icon_path, icon_index, run_as_admin)`. New DB functions: `InsertScMenuNode`, `DeleteScMenuNodesForProject`, `GetScMenuNodesForProject`, `InsertScShortcut`, `DeleteScShortcutsForProject`, `GetScShortcutsForProject`. `SC_SaveToDb(projectId)` called on `IDM_FILE_SAVE`; `SC_LoadFromDb(projectId)` called on project open. Covers all shortcut types and opt-out flags.
- **New locale keys**: `sc_ctx_edit`, `sc_ctx_remove_sc` added to `locale/en_GB.txt`.

### Fixed
- **Desktop mini-icon tooltip off-by-one** — hover and right-click Remove used `ctrlId − IDC_SC_DSK_STRIP_BASE` to look up the shortcut but `ShortcutDef::id` starts at 1 (not 0). Fixed to read the `L"ScId"` window property set at creation time.
- **Tooltip single-line width** — removed hardcoded `S(500)` cap; width is now `sz.cx + S(32)`, clamped to monitor width. Updated `tooltip_API.txt` §3 and §7.

## [2026.03.19.08] - 2026-03-19

### Changed
- **Balanced two-line opt-out labels** — new `MidBreak()` helper in `shortcuts.cpp` inserts a hard `L'\n'` at the word boundary nearest the string midpoint so each sentence splits into two roughly equal lines. Adapts to translated strings automatically.
- **Opt-out checkboxes centred in columns** — each control is measured with `GetTextExtentPoint32W` (per line, taking the wider), sized to fit exactly, then centred within its column via `cbX = colX + (colW - cbW) / 2`, matching the headings and status labels above.

## [2026.03.18.10] - 2026-03-18

### Added
- **"Pin to Start" column heading** — renamed from "Start Menu" (`sc_sm_pin_section`) to eliminate the duplicate label also used by the Start Menu tree section below.
- **SM pin opt-out checkbox** (`IDC_SC_SM_PIN_OPT` / 5211) — developer toggle allowing end-users to opt out of the Start Menu pin at install time. State stored in `s_scSmPinOptOut`, reset by `SC_Reset()`.
- **Taskbar pin opt-out checkbox** (`IDC_SC_TB_PIN_OPT` / 5212) — same pattern for the Taskbar pin column.
- **New locale keys**: `sc_sm_pin_opt_out`, `sc_tb_pin_opt_out` in `locale/en_GB.txt`.

### Changed
- **Vertical alignment of all opt-out checkboxes** — Desktop, Start Menu pin, and Taskbar pin checkboxes all now start at `rowY + statusH + S(4)`, one row below the status labels.
- **`DrawCustomCheckbox` word-wrap** — label rendering switched from `DT_SINGLELINE | DT_VCENTER` to `DT_WORDBREAK`; box is now top-aligned at `+S(2)`. Checkbox height is S(34) to accommodate two lines of text at 9pt bold.

## [2026.03.18.09] - 2026-03-18

### Fixed
- **Start Menu tree centred** — box is 17% of dialog width, horizontally centred using content-area maths.
- **Section label ampersand** — `SS_NOPREFIX` added; "Start Menu & Programs" now shows the `&` literally.
- **Row height** — removed `TreeView_SetItemHeight(S(34))`; Windows auto-sizes rows from image list + font, giving the same compact highlight as the Files page.
- **TreeView font** — `WM_SETFONT` with `hGuiFont` applied, matching Files/Components trees.
- **Icon extraction** — large 32×32 icons extracted via `ExtractIconExW` large-icon param, consistent with project convention.
- **Bug A** — tree X position uses content-area maths (not raw `rc.right`).
- **Bug B** — Add Subfolder/Remove buttons pass bare `L"shell32.dll"` so `DrawCustomButton` builds the correct full path.
- **Bug C** — section label assigned ID 5301 and added to `WM_CTLCOLORSTATIC` so bold page-title font is applied.

## [2026.03.17.11] - 2026-03-17

### Added
- **Shortcuts page module** — all Shortcuts-page code extracted from `mainwindow.cpp` into `shortcuts.h`, `shortcuts.cpp`, and `shortcuts_INTERNALS.txt`. `mainwindow.cpp` routes `WM_NOTIFY` / `WM_COMMAND` / `WM_CONTEXTMENU` via `SC_OnNotify` / `SC_OnCommand` / `SC_OnContextMenu`.
- **Right-click context menu on Start Menu tree** — Add Subfolder and Remove (grayed for fixed root/Programs nodes). Uses `TPM_RETURNCMD` + `SendMessageW(WM_COMMAND)` to reuse the existing button handlers.
- **Right-click stub on shortcut row buttons** — grayed «Configure shortcut…» item in place; handler infrastructure ready for the config dialog next session.
- **Shortcut config dialog spec** — full specification added to `ToDo.txt` Item 1 (name field, run-as-admin checkbox, icon picker, OK/Cancel).
- **APPDATA/home install location spec** — added to `ToDo.txt` Item 5.

### Changed
- **Shortcuts page title uses `WM_CTLCOLORSTATIC`** — control ID 5300 added alongside 5100 so the page headline renders in `s_hPageTitleFont` (bold, large) matching the Files page headline.
- **“Start Menu & Programs” section label** — `SS_CENTER` + `hPageTitleFont`, horizontally centred above the tree.
- **TreeView and buttons centred at 40% width** — folder tree and its action buttons are horizontally centred and limited to 40% of the content area.
- **Button icons** — Add Subfolder: `shell32.dll` index 296; Remove: `shell32.dll` index 234 (matching the Files page).

### Fixed
- **Components Required-icon state** — marked complete in `ToDo.txt`.

## [2026.03.16.11] - 2026-03-16

### Fixed
- **Dep picker: excluded folder's children now visible** — `addVFS` previously used a bare `continue` when it hit the excluded node, which skipped the node's entire subtree. Changed to `addVFS(hParent, snap.children, secLabel); continue;` so the excluded folder itself is not selectable (correct) but all its subfolders and files remain visible and checkable as dependencies.
- **Dep init always reads from in-memory state** — `IDM_COMP_TREE_CTX_EDIT` previously used a DB fallback (`DB::GetDependenciesForComponent`) when `cmp.dependencies` was empty but `cmp.id > 0`. This discarded any in-memory edits made since the last save. Changed to always use `cmp.dependencies` directly.
- **"Choose" after auto-save opens picker immediately** — the `anyUnsaved` path in `IDC_FOLDER_DLG_CHOOSE_DEPS` previously returned 0 after saving and rebuilding `otherComponents`, requiring the user to press "Choose" a second time. Now falls through to open the dep picker in the same gesture. `outDependencyIds` is cleared before opening so stale pre-save IDs (which would be invalid after the Save ID-remap) are not carried over.

## [2026.03.16.09] - 2026-03-16

### Added
- **Component deps list: two columns (Name | Type)** — the dependency summary in the folder-edit dialog is now a proper `WC_LISTVIEW` with two columns (locale keys `comp_deps_col_name` / `comp_deps_col_type`). Type shows "Folder" or "File". An empty list shows a `(none)` placeholder row. The list supports multiselect.
- **Dep list: custom hover tooltip** — `DepListSubclassProc` subclasses the ListView and uses the project's `ShowMultilingualTooltip` / `HideTooltip` system (`TME_LEAVE` tracking via window properties). Hovering a file row shows its full virtual path; hovering a folder row shows the locale string `comp_deps_folder_dblclick` ("Double-click to see files").
- **Dep list: double-click opens file-tree popup** — double-clicking a folder dep row opens `ShowDepsFileListPopup`, a blocking popup with a `WC_TREEVIEW` showing the VFS contents of that folder dependency. The tree uses an ImageList built from shell32.dll (folder/document icons). Closes via the "Close" button or the title-bar X.
- **Remove button beside Choose** — new `IDC_FOLDER_DLG_REMOVE_DEPS` button (shell32.dll icon index 131, `ButtonColor::Red`) removes all selected rows in the dep list; also cascade-removes covered file deps. Enabled only when one or more rows are selected (`LVN_ITEMCHANGED`).
- **Choose button now custom-styled** — `CreateCustomButtonWithIcon` with shell32.dll icon index 87 (`ButtonColor::Blue`), consistent with all other action buttons in the app.
- **Right-click context menu on dep list** — shows "Remove" (grayed if nothing selected) and, when a single folder row is selected, "Show files…". Commands delegate to the Remove handler and `ShowDepsFileListPopup` respectively.
- **Folder-coverage filter** — if a folder component is in the dep list, file deps whose `source_path` begins with the folder's `source_path` are omitted from the list (they remain stored internally). Prevents visual duplication.
- **`comp_deps_INTERNALS.txt`** — full internals reference: layout constants, control IDs, ListView columns, folder-coverage filter, `DepListSubclassProc` message handling, `ShowDepsFileListPopup` design, Remove handler cascade logic, and locale key table.
- **`API_list.txt`** updated with `comp_deps_INTERNALS.txt` entry.

### Fixed
- **Dep picker cascade-up: structural folders no longer skipped** — the WM_TIMER ancestor-check block used `if (tp.lParam > 0)` to decide whether to auto-tick a parent node; this excluded nodes with `lParam == 0` (structural folder nodes without their own `ComponentRow`). Changed to `if (tp.lParam != -1)` so all ancestor nodes up to the section header are ticked, regardless of whether they have a component row.
- **New locale keys added to `locale/en_GB.txt`**: `comp_deps_col_name`, `comp_deps_col_type`, `comp_type_folder`, `comp_type_file`, `comp_deps_none`, `comp_deps_remove`, `comp_deps_folder_dblclick`, `comp_deps_ctx_remove`, `comp_deps_ctx_showfiles`, `comp_deps_files_popup_title`, `comp_deps_files_popup_close`.

## [2026.03.16.08] - 2026-03-16

### Fixed
- **Components enable checkbox now uses the custom themed control** — the last native `BS_AUTOCHECKBOX` on the Components page replaced with `CreateCustomCheckbox`. Now theme-aware (Light/Dark/HC Black/HC White), owner-drawn with U+2714 tick and disabled visual via `ODS_DISABLED`, consistent with all other checkboxes in the app.
- **Enable-components toggle works on unsaved projects** — `IDC_COMP_ENABLE` handler had `if (s_currentProject.id <= 0) return 0` blocking the toggle before first save. Guard removed; component tree and list now un-grey immediately after ticking, even with `id == 0`.
- **"Save First" prompt replaced with custom i18n dialog** — `MessageBoxW` (native, no i18n buttons) replaced with `ShowConfirmDeleteDialog` using locale keys `comp_deps_unsaved_title` / `comp_deps_unsaved_msg`, proper themed Yes/No buttons, and `S()`-scaled layout.
- **Missing locale keys added to `en_GB.txt`** — `comp_info_tooltip` and `comp_info_icon_label` were present as C++ fallback strings only. Both are now defined in `locale/en_GB.txt`. The floppy-disk WM_PAINT label is now read from locale at paint time.

## [2026.03.15.11] - 2026-03-15

### Fixed
- **Dep picker: AskAtInstall subfolders and files now visible** — `EnsureTreeSnapshotsFromDb()` completely rewritten. Old code had `if (!snapVecs[si]->empty()) continue` inside the folder loop, bailing after the first folder per section. New version uses a stable `std::map<wstring, TreeNodeSnapshot>` for node storage (no iterator invalidation). Three passes: (1) folder rows → nodeMap entries, (2) file rows → `nodeMap[parentPath].virtualFiles.push_back(vf)`, (3) sort deepest-first, link children into parents bottom-up, move section roots into snapshot vectors.
- **Dep picker: real-path folder files visible (locale/, img/, WinUpdate etc.)** — new `PopulateSnapshotFilesFromDisk(std::vector<TreeNodeSnapshot>&)` walks the snapshot tree recursively; for any real-path node with empty `virtualFiles` it scans the disk directory with `FindFirstFileW` and populates `snap.virtualFiles`. Called once per dep-picker open for all four sections before `addVFS` traversal.
- **Components info icon hidden by overlapping controls** — replaced `SS_ICON` style (which calls `STM_SETICON` and auto-resizes the control to the icon's natural size, overlapping adjacent controls) with a plain `SS_NOTIFY` static. `HICON` stored in `GWLP_USERDATA`; subclass proc's `WM_PAINT` calls `DrawIconEx` at the exact control size. `WM_ERASEBKGND` clears with `COLOR_BTNFACE` to prevent bleed-through.

### Added
- **Dep picker: auto-file nodes for files without a ComponentRow** — `addVFS` now inserts a synthetic auto-file node for every `snap.virtualFiles` entry that has no matching `ComponentRow` (lParam ≥ `kAutoFileBase = 1,000,000`). On picker OK the handler finds the component by source path or auto-creates one via `DB::InsertComponent`. No files silently skipped.
- **Dep picker: DB-first unified `snap.virtualFiles` path in `addVFS`** — single code path iterating `snap.virtualFiles` only; no per-node disk I/O inside the render walk. DB rows take priority; `PopulateSnapshotFilesFromDisk` fills only nodes that remain empty after the DB pass.
- **Components page info icon (shell32.dll #258)** — floppy-disk icon to the right of the hint label with tooltip "Files and folders will not appear in the dependency picker until the project has been saved at least once." (`comp_info_tooltip` locale key). Subclassed with `CompInfoIcon_SubclassProc`.
- **"FYI!" text on diskette label** — `CompInfoIcon_SubclassProc` `WM_PAINT` overlays "FYI!" in dark navy bold Arial on the white label strip of the diskette graphic so the icon's purpose is readable at a glance.

## [2026.03.15.08] - 2026-03-15

### Fixed
- **Edit button now opens folder-edit dialog for tree-selected folders** — `IDC_COMP_EDIT` previously only inspected the ListView for a selected row and showed "Please select a component first" even when a folder was selected in the Components tree. The button now falls back to the tree selection and calls `IDM_COMP_TREE_CTX_EDIT`. The entire folder-edit flow is consolidated into a single `WM_COMMAND` case; the right-click context menu delegates to it via `SendMessageW` (no more duplicated inline logic).

### Added
- **Pre-selected checkbox in folder-edit dialog** — new `is_preselected` field on `ComponentRow` and `components` table (migration: `ALTER TABLE components ADD COLUMN is_preselected INTEGER DEFAULT 0`). The folder-edit dialog shows a "Pre-selected (ticked by default at install)" checkbox directly below Required. Checking Required force-ticks and disables Pre-selected; unchecking Required re-enables it. Cascades to all files in the folder (section-scoped) and persists to DB on Save.
- **Custom checkbox disabled visual** — `DrawCustomCheckbox` now checks `ODS_DISABLED` in `dis->itemState`; border, tick, and label all grey out when `EnableWindow(hCtrl, FALSE)` is called. Hover highlight is suppressed. Works across all four themes.

### Fixed
- **Dep picker: folders no longer auto-expand** — `TreeView_Expand` call removed from `addVFS`; sub-folders start collapsed so AskAtInstall is always visible without scrolling.
- **Dep picker: virtual folder files reliably placed** — replaced `handledFilePaths`/`virtualFiles`-in-`addVFS` with a `virtualFilePaths` map (`sourcePath → HTREEITEM`); second pass checks exact virtual match first, then deepest-real-path-prefix, then section header. Files placed correctly even when `snap.virtualFiles` is empty (DB-loaded projects).
- **Dep picker: auto-check ancestor folders** — ticking a file/folder fires a 1 ms timer that walks parents and auto-checks any ancestor with a `ComponentRow` (`lParam > 0`).
- **Dep picker: otherComponents rebuilt after save-first** — after save, `pData->otherComponents` is rebuilt from `s_components` immediately and the picker opens without the "press Choose again" message.
- **`CompFolderDlgData`: `sectionName` field added** — forwards VFS section name to save-first rebuild for correct section-aware exclusion.

## [2026.03.14.08] - 2026-03-14

### Fixed
- **Required state no longer lost after Files page deletion** — All four deletion paths (`IDC_FILES_REMOVE` ListView, multi-select tree, single-item tree, `IDM_TREEVIEW_REMOVE_FOLDER`) called `PurgeComponentRowsByPaths` then immediately reloaded `s_components` from DB via `DB::GetComponentsForProject`, wiping all unsaved `is_required=1` flags. `PurgeComponentRowsByPaths` is now memory-only (`std::remove_if` on `s_components`, no DB access, parameter `int projectId` removed). The four `s_components = DB::GetComponentsForProject(...)` reload lines are removed. Required state survives any number of Files page edits.
- **Any folder can be marked Required, not just the first** — Right-click "Edit Folder" previously did nothing for folders with no existing component rows. Two fixes: (1) Guard `if (paths.empty()) return 0` changed to `if (paths.empty() && snap->fullPath.empty()) return 0` so whole-folder (folder-type) components open the dialog. (2) A folder-type row (`source_type="folder"`, `source_path=snap->fullPath`) is now always upserted into `s_components` on OK — created if absent, `is_required` updated if present.
- **`UpdateCompTreeRequiredIcons`: two-phase matching, icon survives page switch** — Old logic scanned per-file paths and treated unregistered files as "skip", causing container folders with mixed registered+unregistered files to appear all-required and cascade incorrectly. New Phase 1: if a folder-type component row exists for `snap->fullPath`, use it as the authoritative answer (no file scan). Phase 2 (file-type only): an unregistered file now explicitly sets `allRequired=false`, preventing false positives on container folders while still allowing component-less subfolders to inherit parent state via `anyFound=false`.

## [2026.03.13.10] - 2026-03-13

### Changed
- **Components page: full in-memory architecture** — `s_components` is now loaded once in `MainWindow::Create` and never reloaded on a page switch. `SwitchPage` teardown no longer clears it; `SwitchPage(9)` only reads from DB when the vector is empty (first visit after project open). All four mutation handlers (`IDC_COMP_ENABLE`, `IDC_COMP_ADD`, `IDC_COMP_EDIT`, `IDC_COMP_REMOVE`) mutate `s_components` directly — no DB writes. Components are written to DB exclusively in `IDM_FILE_SAVE` (`DB::DeleteComponentsForProject` + full re-insert). Implements the project design rule "work in memory, save only on explicit Save" for the entire Components subsystem.
- **`DB::InsertComponent` return type changed from `bool` to `int`** — now returns the new row's DB id (via `sqlite3_last_insert_rowid`) or 0 on failure. The Save path updates the in-memory `id` field immediately after insert.

### Fixed
- **Required flag: folder-type components now matched** — `UpdateCompTreeRequiredIcons` and the Required cascade only matched `source_path` against per-file paths from `CollectSnapshotPaths`. Folder-type components (`source_type="folder"`) store the folder path in `source_path`, so they were never found and the shell32.dll #110 icon never appeared. Both code paths now additionally check `snap->fullPath` directly, fixing the match for folder nodes.
- **Required cascade: `DB::UpdateComponent` no longer called per row** — cascade changes stay in `s_components` until Save (consistent with in-memory model).
- **Legacy `dest_path` repair: no longer writes to DB** — the one-time repair block that infers sections for pre-tagging rows now updates `s_components` only; the fix is persisted on the next explicit Save.

## [2026.03.13.09] - 2026-03-13

### Changed
- **Files TreeView: ticking a parent auto-ticks all sub-folders** — `FilesTree_CtrlClickProc` now recursively applies the same check state to every descendant, both the native `TVIS_STATEIMAGEMASK` and `s_filesTreeMultiSel`. Unticking a parent unticks all children.
- **Remove confirm dialog is context-aware** — when the deduplicated delete list is exactly 1 item that has sub-folders, the message reads "Remove 1 folder and all its sub-folders?" (new locale key `confirm_remove_folder_subtree`). Single leaf-folder and multi-folder messages are unchanged.

## [2026.03.13.08] - 2026-03-13

### Added
- **Custom checkbox component (`checkbox.h/.cpp/_API.txt`)** — reusable owner-draw checkbox with U+2714 heavy tick glyph (Segoe UI Symbol, 1.4× box height, right stroke crosses the top border for a hand-written look). Colour palettes for Light, Dark, HC Black, and HC White themes are detected at paint time via `DetectCbTheme()`; live theme switches take effect without a restart. BM_GETCHECK/BM_SETCHECK subclass makes it a drop-in replacement for `BS_AUTOCHECKBOX`.
- **`CreateCheckboxStateImageList(int sizePx)`** — builds a 3-entry HIMAGELIST (blank / unchecked / checked) with GDI-drawn bitmaps matching the custom checkbox style. No external image files.
- **`UpdateTreeViewCheckboxImages(HWND, int sizePx)`** — replaces native `TVS_CHECKBOXES` bitmaps with the custom ones; call once at creation and again from `WM_SETTINGCHANGE`.
- **Files TreeView multi-select** — folders can be ticked for batch removal. Subclass proc syncs `s_filesTreeMultiSel` with the checkbox state; hint label explains the feature; right-click routes to multi-delete when the item is ticked.
- **4 px gap between TreeView checkbox and folder icon** — normal image list is 36 px wide (32 px icon + 4 px transparent left padding).

## [2026.03.13.07] - 2026-03-13

### Fixed
- **Registry Key Path dialog: gray label background eliminated** — the static label inside `RegKeyDialogProc` was painted with the default gray `COLOR_BTNFACE` brush returned by `DefWindowProc` for `WM_CTLCOLORSTATIC`, causing a visible gray band against the white dialog background. A `WM_CTLCOLORSTATIC` handler now returns `GetSysColorBrush(COLOR_WINDOW)` with `COLOR_WINDOW` / `COLOR_WINDOWTEXT` set on the DC so the label blends seamlessly with the dialog background.
- **Registry Key Path dialog: horizontal scrollbar replaces vertical** — the edit control was created with `WS_VSCROLL`, which showed a useless up/down scrollbar on the single-line registry path field. Replaced with `WS_HSCROLL`; `ES_AUTOHSCROLL` (already present) continues to drive the caret. Long paths can now be scrolled left/right.

## [2026.03.12.11] - 2026-03-12

### Changed
- **All custom dialogs: fully DPI-aware, no hardcoded pixel values** — every custom dialog in `ctrlw.cpp` (Quit, Duplicate Project, Rename Project, Unsaved Changes) and `mainwindow.cpp` (Registry Key, Add Key, Add Value, Edit Folder) now follows the measure-then-create pattern: named layout constants for every dimension, all wrapped in `S()`, outer window size computed via `AdjustWindowRectEx`. No hardcoded pixel sizes remain anywhere in the dialog system. Dialogs display correctly at all DPI settings (100%–200%+) without clipping or oversizing.
- **Button widths generous for i18n** — dialog buttons use layout constants of 150–200 design-px so translated labels are never clipped at any DPI.
- **`CompFolderEditDlgProc`: cascade hint measured at creation site** — hint text height is measured inline with `GetDC(NULL)` + `DrawTextW(DT_CALCRECT | DT_WORDBREAK)` and stored in `CompFolderDlgData::hintH`. The dialog is sized exactly to the measured text — hint never clips regardless of locale or DPI.

## [2026.03.12.08] - 2026-03-12

### Changed
- **Components page: Required-folder icon source switched to `shell32.dll` #110** — previously loaded from `imageres.dll` sequential index 110 (folder with blue circular badge). Changed to `shell32.dll` sequential index 110 (classic yellow folder with blue checkmark badge) — subtle, recognisable, and visually consistent with Windows Explorer. Path built via `GetSystemDirectoryW` + `\shell32.dll`, extracted with `ExtractIconExW`.

## [2026.03.11.10] - 2026-03-11

### Fixed
- **Components page: Required-folder icon (`ExtractIconExW`)** — `PrivateExtractIconsW` is undocumented and silently returned 0 on some Windows configurations, leaving image-list slot 3 empty. Switched to the documented `ExtractIconExW(imgresPath, 110, &hReq, NULL, 1)` which reliably loads `imageres.dll` sequential index 110 (folder with blue checkmark badge).
- **Components page: cascade scope — AskAtInstall no longer affected** — Legacy `ComponentRow` rows had `dest_path = ""` which made the section-filter a no-op. A repair block runs once on Components page load: it infers each legacy row's section by matching source paths against the four VFS snapshots (in original insertion order) and persists the fix to the DB. The filter `if (!cmp.dest_path.empty() && cmp.dest_path != section)` then correctly isolates each section.
- **Components page: `UpdateCompTreeRequiredIcons` inherits parent Required state** — Subfolders with no registered component files (e.g. `img/`, `locale/`) always showed the plain folder icon because `anyFound == false` unconditionally mapped to icon 0. A `parentIsRequired` parameter now propagates each node's resolved icon state into its children so component-less subfolders of a fully-required parent inherit the blue-checkmark badge and pass it further down the tree.

## [2026.03.11.08] - 2026-03-11

### Added
- **Components page: Required-folder icon** — folders where every file (recursively) is flagged Required are shown with `imageres.dll` icon #110 (folder with a blue checkmark badge) at image-list index 3. `UpdateCompTreeRequiredIcons(hTree, hItem)` walks the comp tree after every build and after each Edit Folder OK; reverts to the normal folder icon as soon as any file inside loses the Required flag.
- **Components page: "Required" hover tooltip** — `CompTree_TooltipSubclassProc` subclasses `s_hCompTreeView`. On `WM_MOUSEMOVE` it uses `TreeView_HitTest` to find the hovered item; shows `ShowMultilingualTooltip({L"", L"Required"})` when the item carries image index 3; hides on `WM_MOUSELEAVE` or when the cursor is over a non-required item.
- **Files page: native blue multi-select** — `TVS_CHECKBOXES` removed. Multi-selection tracked in `static std::set<HTREEITEM> s_filesTreeMultiSel`. `FilesTree_CtrlClickProc` handles Ctrl+Click (toggle), Shift+Click (range), and plain click (clear). `NM_CUSTOMDRAW / CDDS_ITEMPREPAINT` in `WM_NOTIFY` (idFrom 102) colours selected items with `COLOR_HIGHLIGHT`/`COLOR_HIGHLIGHTTEXT` — identical to Windows Explorer. Remove iterates the set; set is cleared on page teardown.

### Fixed
- **Components page: Edit Folder dialog clipping** — dialog height raised from 190 px to 240 px; cascade-hint label height raised from 22 px to 42 px so the two-line hint text is fully visible.
- **Components page: Required icon not loading** — `LoadLibraryExW(..., LOAD_LIBRARY_AS_DATAFILE)` prevents MUI/icon resolution on modern Windows and returned the wrong icon. Fixed to `LoadLibraryW`.

## [2026.03.10.13] - 2026-03-10

### Added
- **Folder expand/collapse memory — Files page**: `TreeNodeSnapshot` now stores `expanded` (Files page) and `compExpanded` (Components page) flags. `SaveTreeSnapshot` records each node's expanded state; `RestoreTreeSnapshot` restores it per node. On first visit all folders are fully expanded via `ExpandAllSubnodes`. Within a session, collapsing a folder on the Files page is remembered when revisiting.
- **Folder expand/collapse memory — Components page**: Independent of the Files page state. `SaveCompTreeExpansion` traverses the comp tree on leaving page 9 and writes each node's state back into its snapshot (`mutable compExpanded`). `VFSPicker_AddSubtree` expands per `compExpanded` when rebuilding the tree.

### Fixed
- **Components page title font**: The title static control was created with `NULL` as its menu-ID, so `WM_CTLCOLORSTATIC` never matched it and `s_hPageTitleFont` was not applied. Control now receives `(HMENU)5100` — consistent with every other page title.
- **Components page `AskAtInstall` root: wrong label and missing badge icon**: The root was inserted as `"Ask At Install"` (spaces, no badge). Label corrected to `"AskAtInstall"` to match the Files page. The `addRoot` helper now accepts an icon-index parameter; the badge icon (blue circle, index 2) is added to the comp-page image list exactly as on the Files page.
- **Context menu `Add Files` shown on root nodes**: Right-clicking Program Files, ProgramData, AppData (Roaming), or AskAtInstall showed an `Add Files` entry that would have been blocked anyway. The item is now omitted from the menu when `isSystemRoot` is true — menu only contains `Add Folder` and `Create Folder…` for these four roots.

## [2026.03.10.10] - 2026-03-10

### Added
- **Components page: folder TreeView** — split-pane layout mirroring the Files page. Left pane shows the virtual-folder tree built from VFS snapshots; selecting a folder populates the right pane with its files and their component metadata (name, description, required, type, source path).
- **Components enable auto-populate** — toggling the "Enable components" checkbox now automatically creates a `ComponentRow` for every file found in the current VFS snapshot; disabling clears all components from the DB.
- **VFS Picker dialog** — "Add Files / Folders" now opens a split-pane VFS browser instead of a native `IFileOpenDialog`. Users pick files from the right pane or a real-path folder from the left pane — no filesystem access required.
- **Component dependency selection** — the Component Edit dialog now shows a multi-select "Requires:" listbox so dependencies between components can be declared. Stored in the new `component_dependencies` DB table via `InsertComponentDependency` / `GetDependenciesForComponent` / `DeleteDependenciesForComponent`.
- **Folder-level Required flag (`CompFolderEditDlg`)** — right-click any folder in the Components tree to cascade the Required flag to all files in that folder and its subfolders in one step, with a descriptive cascade-hint label.
- **Context menus on Components panes** — right-click in the Components TreeView or ListView shows an Edit context menu item.
- `component_dependencies` DB table — schema + migration for existing DBs.

### Fixed
- **Drag-and-drop: toolbar button hover blocked** — `SetCapture` was called on every `WM_LBUTTONDOWN` in the TreeView subclass, preventing toolbar buttons from receiving `WM_MOUSEMOVE` (hover) until the mouse button was released. Capture is now deferred until the drag threshold is actually exceeded.
- **Files page `TVN_SELCHANGED` icon lookup** — `SHGetFileInfoW` now uses `SHGFI_USEFILEATTRIBUTES` so icons are resolved by extension only, with no filesystem access and no UI-thread blocking.
- **Tooltip window no longer steals keyboard focus** — `WS_EX_NOACTIVATE` added to the tooltip window extended styles.
- **Context menu alt-tab freeze** — `SetForegroundWindow` + `PostMessage(WM_NULL)` added around the Files-page TreeView `TrackPopupMenu` call; fixes the Windows issue where the popup blocks until the owning window is re-activated after an alt-tab.

### Changed
- **Removed tooltip debug log** — `tooltip_debug.log` written by `ShowMultilingualTooltip` / `HideTooltip` was hitting the disk on the UI thread. Logging removed; use `OutputDebugStringW` or a debugger instead.
- `tooltip_API.txt` §13 updated to document the removed log and recommend `OutputDebugStringW`.

## [2026.03.09.16] - 2026-03-09

### Fixed
- **Files page drag-and-drop: Merge brings all files across**: When merging a real-disk-path folder into an existing same-named folder, the files inside the dragged folder were silently dropped — they lived on disk, not in `s_virtualFolderFiles`, so the merge code never found them. `IngestRealPathFiles` now reads the folder's direct children from disk and appends them as `VirtualFolderFile` entries into the target before the merge proceeds.
- **Files page drag-and-drop: erratic UI freeze eliminated**: After a move or merge, clicking the resulting tree node triggered `TVN_SELCHANGED → PopulateListView`, which called `SHGetFileInfoW` per file on the UI thread and caused intermittent hangs. `CloneTreeSubtree` now always creates the clone with an empty lParam (virtual node) and calls `IngestRealPathFiles` to pre-populate `s_virtualFolderFiles`. `TVN_SELCHANGED` therefore always reads from the in-memory map and never blocks on a disk scan.

## [2026.03.09.12] - 2026-03-09

### Fixed
- **Tooltip Greek/Cyrillic/Ukrainian rendering**: Windows 11 returns `"Segoe UI Variable"` from `NONCLIENTMETRICS`, which is a GDI variable font that cannot render non-Latin scripts — they displayed as `|||||||`. `InitTooltipSystem` in `tooltip.cpp` now derives the font **height** from `NONCLIENTMETRICS` (keeps DPI-correctness) but overrides the face name to `"Segoe UI"` (the classic version with full Unicode coverage for Latin, Greek, Cyrillic, Arabic, Hebrew, CJK). A `DO NOT MODIFY` comment block documents why the face name must not be reverted to the system default.
- **Entry screen `g_guiFont` uses same font rule**: `CreateFontW` with hardcoded `"Segoe UI"` and a manually-scaled pixel size replaced by `NONCLIENTMETRICS` height + `"Segoe UI"` face name override — consistent with the tooltip font and DPI-correct.
- **Removed dead `g_tooltipFont` / `g_tooltipText` in `main.cpp`**: Two variables that were created but never read by the tooltip system have been removed. The tooltip system owns its font entirely inside `tooltip.cpp`.

### Added
- `entry_screen.h`: New header file declaring `EntryScreen_Run()` and `ENTRY_SCREEN_CLASS` — groundwork for moving the entry screen into its own translation unit to prevent future font/tooltip regressions.

## [2026.03.09.09] - 2026-03-09

### Added
- Entry page button tooltips: all four buttons (New Project, Open Project, Delete Project, Exit) now show hover tooltips via `SetButtonTooltip()`. Locale keys: `new_project_hint`, `open_project_hint`, `delete_project_hint`, `exit_hint` — added to `en_GB.txt`. Falls back to hardcoded English strings for locales missing the keys. Tooltips refresh on `CBN_SELCHANGE`.
- `tooltip_API.txt` §15 “Unicode and encoding”: documents UTF-8 BOM auto-stripping, Segoe UI Unicode coverage, ANSI-not-supported rule, and `Utf8ToW()` / `MultiByteToWideChar` usage

### Fixed
- **UTF-8 BOM in `LoadLocaleFile`**: all 20 locale files begin with `EF BB BF`. Any file whose first line is a key–value pair (no `#` comment) had its first key stored as `U+FEFF+keyname`, silently breaking lookups. `LoadLocaleFile` now strips the BOM from the first read line. Files without BOM are unaffected.

## [2026.03.08.10] - 2026-03-08

### Added
- Per-project **Add Folder** last-location memory: DB key `last_picker_folder_<projectId>` — `SHBrowseForFolderW` opens at last used parent directory via `PickerFolderCallback` / `BFFM_SETSELECTION`
- Per-project **Add Files** last-location memory: DB key `last_picker_files_<projectId>` — `GetOpenFileNameW` `lpstrInitialDir` set from DB; first-use fallback to `%USERPROFILE%` to prevent shell state bleed from the folder picker
- Drag-and-drop infrastructure: `DragSource` enum, drag state statics, `EnsureDragCursors()` (shell32 icon 109 = no-drop / icon 300 = can-drop), `IsDragDropValid()`, `CloneTreeSubtree()`, `CancelDrag()`, `HitTestTreeView()`, drop-target highlight via `TreeView_SelectDropTarget`, `WM_LBUTTONUP` drop handler, `WM_SETCURSOR` cursor override, `WM_CAPTURECHANGED` guard
- Public accessors on `MainWindow`: `GetFilesTreeView()`, `GetProgramFilesRoot()`, `GetProgramDataRoot()`, `GetAppDataRoot()`, `GetAskAtInstallRoot()`; `AddTreeNode()` moved to public

### Changed
- Removed `ImageList_DragEnter` / `ImageList_DragMove` ghost image — DWM compositing renders it behind all windows causing visual artifacts
- Drag activation refactored to `WM_PARENTNOTIFY` + threshold check (temporary; will be replaced by TreeView/ListView subclassing next session)

## [2026.03.08.09] - 2026-03-08

### Added
- `Close Project` toolbar button (`IDC_TB_CLOSE_PROJECT = 5083`) between Save and Exit — red, shell32.dll icon 131, forwards to `IDM_FILE_CLOSE`, i18n-ready via `close_project` locale key
- `SetButtonTooltip(HWND, const wchar_t*)` in `button.h`/`button.cpp` — stores tooltip text as `"TooltipText"` window property; `ButtonSubclassProc` shows on hover and hides on mouse-leave automatically
- `button_INTERNALS.txt` — full internal reference for button system, toolbar layout, icon indices, and step-by-step guide for new toolbar buttons

### Changed
- `ButtonSubclassProc` `WM_MOUSEMOVE` now shows tooltip if `"TooltipText"` property set; `WM_MOUSELEAVE` now calls `HideTooltip()` unconditionally
- `API_list.txt` updated with `button_INTERNALS.txt` entry

## [2026.03.08.08] - 2026-03-08

### Fixed
- Full tree (folders + files) now saved to DB on every Save via new `SaveTreeToDb` recursive helper — previously only virtual-node files were written, physical-path folder nodes produced zero DB rows
- Files-page tree now rebuilt from DB rows on project open (new Priority-2 DB-rebuild load path) — previously the `files` table was never queried on load, so projects with no session snapshot and no `directory` opened to a blank tree
- `s_currentProject.directory` now synced from `IDC_INSTALL_FOLDER` before `DB::UpdateProject` on every Save — was always empty for projects that never opened the Settings page

### Added
- `FileRow` struct in `db.h` mirroring the `files` table
- `DB::GetFilesForProject(int projectId)` in `db.cpp` — returns all file/folder rows for a project; `install_scope="__folder__"` marks folder nodes
- `SaveTreeToDb` static helper in `mainwindow.cpp` — recursively writes complete tree snapshot to DB
- `files_save_load_INTERNALS.txt` — internal architecture reference for the save/load subsystem
- `API_list.txt` — documentation index with `_API` vs `_INTERNALS` naming convention

## [Unreleased] - 2026-02-23

### Added
- TreeView indentation increased to 19 pixels for better visual hierarchy
- Project name field now separate from install folder path
- Automatic folder creation when adding files (uses filename without extension)
- Context menu "Create Folder" and "Remove Folder" options with confirmation dialogs
- Install path now displays as read-only dark blue text (RGB 0,51,153)

### Fixed
- Install path now properly updates when first folder under Program Files is renamed
- "Add Folder" button now correctly adds folders under Program Files hierarchy
- "Add Files" button creates folder structure automatically if none exists
- Install path updates correctly after folder deletion
- Project name protection - stops auto-updating once manually edited by user
- Folder hierarchy display with proper indentation under Program Files

### Changed
- All "directory" terminology changed to "folder" throughout UI
- Install folder field changed from editable to read-only display
- Quit dialog YES button now uses shell32.dll icon #112 with transparent background
- Button icon rendering uses PrivateExtractIconsW for proper transparency

### Technical
- Added UpdateInstallPathFromTree() helper function for centralized path updates
- Improved TreeView label edit handling to check first child position
- Enhanced folder add/remove operations to trigger install path updates
- Fixed EN_CHANGE event handling during programmatic control updates

## [Initial Release]

### Added
- Entry screen with language selection (20 languages supported)
- Project database management (SQLite)
- Create, open, and delete project functionality
- Main window with 8-button toolbar (Files, Registry, Shortcuts, Dependencies, Settings, Build, Test, Scripts)
- Files Management page with TreeView/ListView split-pane interface
- Multi-select support (Ctrl/Shift for files, checkboxes for folders)
- System file type icons in ListView
- Install directory configuration with folder picker
- Professional quit dialog with confirmation (Ctrl+W shortcut)
- Native Windows UI with system icons
- Full internationalization support
- Keyboard shortcuts (Ctrl+W for exit, F7 for Build, F5 for Test)
