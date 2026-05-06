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
CloseApplications={#CloseApplications}
ChangesEnvironment={#ChangesEnvironment}
ChangesAssociations={#ChangesAssociations}
DisableDirPage={#DisableDirPage}
DisableProgramGroupPage={#DisableProgramGroupPage}
UsePreviousAppDir={#UsePreviousAppDir}
UsePreviousGroup={#UsePreviousGroup}
DirExistsWarning={#DirExistsWarning}
SetupLogging={#SetupLogging}
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

[CustomMessages]
; Shown when the named mutex is already held — installer is already running.
; Three choices: Yes = close the running one, No = close this one, Cancel = do nothing.
english.mutex_message=Another copy of this installer is already running.%n%nYou can close the running installer to continue, close this installer to let the other run, or cancel.
brazilianportuguese.mutex_message=Outra cópia deste instalador já está em execução.%n%nVocê pode fechar o instalador em execução para continuar, fechar este instalador para deixar o outro continuar, ou cancelar.
catalan.mutex_message=Una altra còpia d'aquest instal·lador ja s'està executant.%n%nPodeu tancar l'instal·lador en execució per continuar, tancar aquest instal·lador per deixar l'altre continuar, o cancel·lar.
czech.mutex_message=Jiná kopie tohoto instalačního programu již běží.%n%nMůžete zavřít spuštěný instalační program a pokračovat, zavřít tento instalační program a nechat ten druhý běžet, nebo zrušit.
danish.mutex_message=En anden kopi af dette installationsprogram kører allerede.%n%nDu kan lukke det kørende installationsprogram for at fortsætte, lukke dette installationsprogram og lade det andet køre, eller annullere.
dutch.mutex_message=Een andere kopie van dit installatieprogramma wordt al uitgevoerd.%n%nU kunt het actieve installatieprogramma sluiten om door te gaan, dit installatieprogramma sluiten om het andere te laten doorgaan, of annuleren.
finnish.mutex_message=Toinen kopio tästä asennusohjelmasta on jo käynnissä.%n%nVoit sulkea käynnissä olevan asennusohjelman jatkaaksesi, sulkea tämän asennusohjelman antaaksesi toisen jatkua, tai peruuttaa.
french.mutex_message=Une autre copie de ce programme d'installation est déjà en cours d'exécution.%n%nVous pouvez fermer le programme d'installation en cours pour continuer, fermer ce programme pour laisser l'autre continuer, ou annuler.
german.mutex_message=Eine andere Kopie dieses Installationsprogramms wird bereits ausgeführt.%n%nSie können das laufende Installationsprogramm schließen, um fortzufahren, dieses schließen, um das andere weiterlaufen zu lassen, oder abbrechen.
hebrew.mutex_message=עותק אחר של מתקין זה כבר פועל.%n%nתוכל לסגור את המתקין הפועל כדי להמשיך, לסגור את מתקין זה כדי לאפשר לאחר להמשיך, או לבטל.
hungarian.mutex_message=A telepítő egy másik példánya már fut.%n%nA futó telepítőt bezárhatja a folytatáshoz, bezárhatja ezt a telepítőt, hogy a másik futhasson, vagy megszakíthatja.
italian.mutex_message=È già in esecuzione un'altra copia di questo programma di installazione.%n%nÈ possibile chiudere il programma in esecuzione per continuare, chiudere questo per lasciare in esecuzione l'altro, oppure annullare.
japanese.mutex_message=このインストーラーの別のコピーが既に実行中です。%n%n実行中のインストーラーを閉じて続行するか、このインストーラーを閉じてもう一方を続行させるか、またはキャンセルすることができます。
norwegian.mutex_message=En annen kopi av dette installasjonsprogrammet kjører allerede.%n%nDu kan lukke det kjørende installasjonsprogrammet for å fortsette, lukke dette og la det andre kjøre, eller avbryte.
polish.mutex_message=Inna kopia tego instalatora jest już uruchomiona.%n%nMożesz zamknąć uruchomiony instalator, aby kontynuować, zamknąć ten instalator i pozwolić drugiemu działać, lub anulować.
portuguese.mutex_message=Outra cópia deste instalador já está em execução.%n%nPode fechar o instalador em execução para continuar, fechar este instalador para deixar o outro continuar, ou cancelar.
slovak.mutex_message=Iná kópia tohto inštalačného programu už beží.%n%nMôžete zatvoriť spustený inštalačný program a pokračovať, zatvoriť tento a nechať ten druhý bežať, alebo zrušiť.
slovenian.mutex_message=Druga kopija tega namestitvenega programa že deluje.%n%nLahko zaprete zagnani namestitveni program in nadaljujete, zaprete ta in pustite drugega delovati, ali pa prekličete.
spanish.mutex_message=Ya se está ejecutando otra copia de este instalador.%n%nPuede cerrar el instalador en ejecución para continuar, cerrar este instalador y dejar que el otro continúe, o cancelar.
swedish.mutex_message=En annan kopia av det här installationsprogrammet körs redan.%n%nDu kan stänga installationsprogrammet som körs för att fortsätta, stänga det här för att låta det andra fortsätta, eller avbryta.
turkish.mutex_message=Bu yükleyicinin başka bir kopyası zaten çalışıyor.%n%nDevam etmek için çalışan yükleyiciyi kapatabilir, diğerinin devam etmesi için bu yükleyiciyi kapatabilir veya iptal edebilirsiniz.
ukrainian.mutex_message=Інша копія цього інсталятора вже запущена.%n%nВи можете закрити запущений інсталятор, щоб продовжити, закрити цей інсталятор, щоб дозволити іншому продовжити, або скасувати.

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
; <<PATH_REGISTRY>>
; <<FILE_ASSOCIATIONS>>

[Run]
; Run app after install
Filename: "{app}\{#ExeName}"; Description: "Launch {#AppName}"; Flags: nowait postinstall skipifsilent

[UninstallDelete]
Type: filesandordirs; Name: "{app}"

[Code]
; Manually manage the setup mutex so we can show a custom localised message
; instead of Inno's built-in generic English dialog.
; If the user confirms, we try to find and close the running installer window.
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
  MB_YESNOCANCEL = 3;
  IDYES          = 6;
  IDNO           = 7;

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
