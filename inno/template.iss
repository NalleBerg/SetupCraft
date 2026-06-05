[Setup]
AppName={#AppName}
AppVersion={#AppVersion}
AppPublisher={#Publisher}
AppPublisherURL={#PublisherURL}
AppSupportURL={#SupportURL}
AppCopyright={#AppCopyright}
AppId={#AppId}
DefaultDirName={#DefaultDirBase}\{#AppName}
DefaultGroupName={#AppName}
OutputDir={#OutputDir}
OutputBaseFilename={#OutputBase}
Compression={#Compression}
SolidCompression={#SolidCompression}
PrivilegesRequired={#PrivilegesRequired}
PrivilegesRequiredOverridesAllowed={#PrivilegesRequiredOverridesAllowed}
WizardStyle={#WizardStyle}
Uninstallable={#Uninstallable}
UninstallDisplayName={#UninstallDisplayName}
UninstallFilesDir={#UninstallFilesDir}
CloseApplications={#CloseApplications}
ChangesEnvironment={#ChangesEnvironment}
ChangesAssociations={#ChangesAssociations}
DisableDirPage={#DisableDirPage}
DisableProgramGroupPage={#DisableProgramGroupPage}
UsePreviousAppDir={#UsePreviousAppDir}
UsePreviousGroup={#UsePreviousGroup}
DirExistsWarning={#DirExistsWarning}
SetupLogging={#SetupLogging}
LanguageDetectionMethod={#LanguageDetectionMethod}
ShowLanguageDialog={#ShowLanguageDialog}
VersionInfoVersion={#VersionInfoVersion}
VersionInfoTextVersion={#VersionInfoTextVersion}
VersionInfoDescription={#VersionInfoDescription}
VersionInfoProductName={#VersionInfoProductName}
VersionInfoProductVersion={#VersionInfoProductVersion}
VersionInfoCompany={#VersionInfoCompany}
VersionInfoCopyright={#VersionInfoCopyright}
DisableWelcomePage={#DisableWelcomePage}
LicenseFile={#LicenseFile}
DisableReadyPage={#DisableReadyPage}
AlwaysShowDirOnReadyPage={#AlwaysShowDirOnReadyPage}
AlwaysShowGroupOnReadyPage={#AlwaysShowGroupOnReadyPage}
DisableFinishedPage={#DisableFinishedPage}
{#SetupIconFileLine}
{#UninstallDisplayIconLine}
{#WizardImageFileLine}
{#WizardSmallImageFileLine}
{#SignToolLine}

[Languages]
; <<LANGUAGES>>

[CustomMessages]
; <<CUSTOM_MESSAGES>>
; <<TYPES>>
; <<COMPONENTS>>
[Tasks]
Name: desktopicon; Description: "Create a &desktop icon"; GroupDescription: "Additional icons:"; Flags: unchecked

[Files]
; <<FILES>>

; <<TASKS>>

; <<ICONS>>

[Registry]
; Auto-generated registry entries (installer base path, PATH additions, file associations, custom entries)
Root: HKLM; Subkey: "Software\{#Publisher}\{#AppName}"; ValueType: string; ValueName: "InstallPath"; ValueData: "{app}"; Flags: preservestringtype
; <<PATH_REGISTRY>>
; <<FILE_ASSOCIATIONS>>
; <<CUSTOM_REGISTRY>>

; <<RUN>>

; <<UNINSTALL_RUN>>

[UninstallDelete]
Type: filesandordirs; Name: "{app}"

[Code]
// Manually manage the setup mutex so we can show a custom localised message
// instead of Inno's built-in generic English dialog.
// If the user confirms, we try to find and close the running installer window.
function WinCreateMutex(lpAttr: Cardinal; bOwner: Boolean; lpName: String): Cardinal;
  external 'CreateMutexW@kernel32.dll stdcall';
function WinCloseHandle(h: Cardinal): Boolean;
  external 'CloseHandle@kernel32.dll stdcall';
function WinGetLastError(): Cardinal;
  external 'GetLastError@kernel32.dll stdcall';
function FindSetupWindow(lpClassName: Cardinal; lpWindowName: String): Cardinal;
  external 'FindWindowW@user32.dll stdcall';
function PostCloseMessage(hWnd: Cardinal; Msg: Cardinal; wParam: Cardinal; lParam: Cardinal): Boolean;
  external 'PostMessageW@user32.dll stdcall';

const
  ERROR_ALREADY_EXISTS = 183;
  WM_CLOSE       = 16;
  GWL_STYLE      = -16;
  PBS_SMOOTH     = 1;

var
  g_hSetupMutex: Cardinal;

function InitializeSetup(): Boolean;
var
  err: Cardinal;
  res: Integer;
  hWnd: Cardinal;
begin
  g_hSetupMutex := WinCreateMutex(0, True, '{#SetupMutex}');
  err := WinGetLastError();
  if (g_hSetupMutex <> 0) and (err = ERROR_ALREADY_EXISTS) then
  begin
    WinCloseHandle(g_hSetupMutex);
    g_hSetupMutex := 0;
    res := MsgBox(CustomMessage('mutex_message'), mbError, MB_YESNOCANCEL);
    if res = IDYES then
    begin
      hWnd := FindSetupWindow(0, '{#AppName} Setup');
      if hWnd <> 0 then
        PostCloseMessage(hWnd, WM_CLOSE, 0, 0);
      Sleep(1500);
      g_hSetupMutex := WinCreateMutex(0, True, '{#SetupMutex}');
      err := WinGetLastError();
      if (g_hSetupMutex <> 0) and (err = ERROR_ALREADY_EXISTS) then
      begin
        WinCloseHandle(g_hSetupMutex);
        g_hSetupMutex := 0;
        Result := False;
      end else
        Result := True;
    end else
      Result := False;
  end else
    Result := True;
end;

; <<INSTALL_PROGRESS_CODE>>
; <<DISK_SPACE_CODE>>
; <<DEP_ENFORCE_CODE>>
; <<SETUP_LOG_PROC>>
procedure DeinitializeSetup();
begin
  if g_hSetupMutex <> 0 then
  begin
    WinCloseHandle(g_hSetupMutex);
    g_hSetupMutex := 0;
  end;
  ; <<SETUP_LOG_CALL>>
end;
