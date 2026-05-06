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
CloseApplications={#CloseApplications}
DisableDirPage={#DisableDirPage}
DisableProgramGroupPage={#DisableProgramGroupPage}
VersionInfoVersion={#VersionInfoVersion}
VersionInfoTextVersion={#VersionInfoTextVersion}
VersionInfoDescription={#VersionInfoDescription}
VersionInfoProductName={#VersionInfoProductName}
VersionInfoProductVersion={#VersionInfoProductVersion}
VersionInfoCompany={#VersionInfoCompany}
VersionInfoCopyright={#VersionInfoCopyright}
{#SignToolLine}

[Languages]
; <<LANGUAGES>>

[Tasks]
Name: desktopicon; Description: "Create a &desktop icon"; GroupDescription: "Additional icons:"; Flags: unchecked

[Files]
; Copy all files from SourceDir (set by generator)
Source: "{#SourceDir}\*"; DestDir: "{app}"; Flags: recursesubdirs createallsubdirs

[Icons]
Name: "{group}\{#AppName}"; Filename: "{app}\{#ExeName}"; WorkingDir: "{app}"
Name: "{userdesktop}\{#AppName}"; Filename: "{app}\{#ExeName}"; Tasks: desktopicon

[Registry]
; Example registry entries (installer modifies these)
Root: HKLM; Subkey: "Software\{#Publisher}\{#AppName}"; ValueType: string; ValueName: "InstallPath"; ValueData: "{app}"; Flags: preservestringtype

[Run]
; Run app after install
Filename: "{app}\{#ExeName}"; Description: "Launch {#AppName}"; Flags: nowait postinstall skipifsilent

[UninstallDelete]
Type: filesandordirs; Name: "{app}"
