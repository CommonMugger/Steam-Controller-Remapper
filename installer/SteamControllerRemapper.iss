#define MyAppName "Steam Controller Remapper"
#define MyAppVersion "1.7.2"
#define MyAppPublisher "CommonMugger"
#define MyAppExeName "Steam Controller Remapper.exe"
#define AppRegKey "SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\{8F3A1B2C-4D5E-6F7A-8B9C-0D1E2F3A4B5C}_is1"

[Setup]
AppId={{8F3A1B2C-4D5E-6F7A-8B9C-0D1E2F3A4B5C}
AppName={#MyAppName}
AppVersion={#MyAppVersion}
AppPublisher={#MyAppPublisher}
DefaultDirName={autopf}\{#MyAppName}
DefaultGroupName={#MyAppName}
OutputBaseFilename=SteamControllerRemapper-{#MyAppVersion}-Setup
OutputDir=output
SetupIconFile=..\resources\SteamControllerOFF.ico
Compression=lzma2
SolidCompression=yes
PrivilegesRequired=admin
UninstallDisplayName={#MyAppName}
UninstallDisplayIcon={app}\{#MyAppExeName}
WizardStyle=modern
DisableProgramGroupPage=yes
CloseApplications=yes
CloseApplicationsFilter=Steam Controller Remapper.exe
MinVersion=10.0

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Files]
; Desktop runtime — always updated
Source: "staging\Desktop\{#MyAppExeName}"; DestDir: "{app}"; Flags: ignoreversion
Source: "staging\Desktop\libVIIPER.dll"; DestDir: "{app}"; Flags: ignoreversion
; Widget helper and package files (temp — installed via PowerShell, then removed)
Source: "widget-install.ps1"; DestDir: "{tmp}"; Flags: deleteafterinstall
Source: "staging\Widget\SteamControllerRemapperWidget.msix"; DestDir: "{tmp}\Widget"; Flags: deleteafterinstall
Source: "staging\Widget\SteamControllerRemapperWidget.cer"; DestDir: "{tmp}\Widget"; Flags: deleteafterinstall
; USBIP driver installer (temp — only executed on fresh install)
Source: "staging\usbip\USBip-win2-x64.exe"; DestDir: "{tmp}\usbip"; Flags: deleteafterinstall

[Icons]
Name: "{autoprograms}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"

[Registry]
; Start with Windows (current user)
Root: HKCU; Subkey: "Software\Microsoft\Windows\CurrentVersion\Run"; \
  ValueType: string; ValueName: "{#MyAppName}"; \
  ValueData: """{app}\{#MyAppExeName}"""; Flags: uninsdeletevalue
; Sideloading — required for the Game Bar widget; set once, never overwritten on update
Root: HKLM; Subkey: "SOFTWARE\Microsoft\Windows\CurrentVersion\AppModelUnlock"; \
  ValueType: dword; ValueName: "AllowDevelopmentWithoutDevLicense"; ValueData: 1; \
  Flags: createvalueifdoesntexist
Root: HKLM; Subkey: "SOFTWARE\Microsoft\Windows\CurrentVersion\AppModelUnlock"; \
  ValueType: dword; ValueName: "AllowAllTrustedApps"; ValueData: 1; \
  Flags: createvalueifdoesntexist

[Run]
; USBIP driver — only on fresh install (service key absent) or if driver was removed
Filename: "{tmp}\usbip\USBip-win2-x64.exe"; \
  Parameters: "/VERYSILENT /SUPPRESSMSGBOXES /NORESTART /SP-"; \
  StatusMsg: "Installing USB/IP driver..."; \
  Check: NeedsUsbIp; \
  Flags: waituntilterminated

; Game Bar widget — only when version changes or on fresh install
Filename: "powershell.exe"; \
  Parameters: "-NoProfile -ExecutionPolicy Bypass -File ""{tmp}\widget-install.ps1"" -MsixPath ""{tmp}\Widget\SteamControllerRemapperWidget.msix"" -CertPath ""{tmp}\Widget\SteamControllerRemapperWidget.cer"""; \
  StatusMsg: "Installing Game Bar widget..."; \
  Check: ShouldInstallWidget; \
  Flags: runhidden waituntilterminated

; Offer to launch after setup
Filename: "{app}\{#MyAppExeName}"; \
  Description: "Launch {#MyAppName}"; \
  Flags: nowait postinstall skipifsilent

[UninstallRun]
Filename: "powershell.exe"; \
  Parameters: "-NoProfile -ExecutionPolicy Bypass -Command ""$p = Get-AppxPackage -Name SteamControllerRemapperWidget -ErrorAction SilentlyContinue; if ($p) {{ Remove-AppxPackage -Package $p.PackageFullName }}"""; \
  RunOnceId: "RemoveWidget"; \
  Flags: runhidden waituntilterminated

[Code]
// Returns the DisplayVersion string written by the previous Inno Setup install,
// or an empty string if the app has never been installed.
function GetInstalledVersion(): String;
begin
  if not RegQueryStringValue(HKLM, '{#AppRegKey}', 'DisplayVersion', Result) then
    Result := '';
end;

// True when no previous installation exists at all.
function IsFreshInstall(): Boolean;
begin
  Result := GetInstalledVersion() = '';
end;

// True when the installed version differs from the version being installed now
// (covers both fresh install and version upgrades/downgrades).
function IsVersionChange(): Boolean;
begin
  Result := GetInstalledVersion() <> '{#MyAppVersion}';
end;

// Skip USBIP if the driver service key already exists — the driver is already
// installed and the installer would otherwise prompt for a reboot every run.
function NeedsUsbIp(): Boolean;
begin
  // usbip-win2 0.9.x registers usbip2_ude (UDE bus driver) and usbip2_filter
  Result := not (
    RegKeyExists(HKEY_LOCAL_MACHINE, 'SYSTEM\CurrentControlSet\Services\usbip2_ude') or
    RegKeyExists(HKEY_LOCAL_MACHINE, 'SYSTEM\CurrentControlSet\Services\usbip2_filter') or
    RegKeyExists(HKEY_LOCAL_MACHINE, 'SYSTEM\CurrentControlSet\Services\mausbip')
  );
end;

// Reinstall the widget only when the app version has changed. On a same-version
// update run the widget is already correct, so we skip it to avoid killing Game Bar.
function ShouldInstallWidget(): Boolean;
begin
  Result := IsVersionChange();
end;
