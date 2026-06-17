#define MyAppName "Steam Controller Remapper"
#define MyAppVersion "1.6.0"
#define MyAppPublisher "CommonMugger"
#define MyAppExeName "Steam Controller Remapper.exe"

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
; Desktop runtime
Source: "staging\Desktop\{#MyAppExeName}"; DestDir: "{app}"; Flags: ignoreversion
Source: "staging\Desktop\libVIIPER.dll"; DestDir: "{app}"; Flags: ignoreversion
; Widget installer helper script
Source: "widget-install.ps1"; DestDir: "{tmp}"; Flags: deleteafterinstall
; Widget package files (temp only — installed via PowerShell, then removed)
Source: "staging\Widget\SteamControllerRemapperWidget.msix"; DestDir: "{tmp}\Widget"; Flags: deleteafterinstall
Source: "staging\Widget\SteamControllerRemapperWidget.cer"; DestDir: "{tmp}\Widget"; Flags: deleteafterinstall
; USBIP driver installer (temp only — installed if not already present)
Source: "staging\usbip\USBip-win2-x64.exe"; DestDir: "{tmp}\usbip"; Flags: deleteafterinstall

[Icons]
Name: "{autoprograms}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"

[Registry]
; Start with Windows (current user)
Root: HKCU; Subkey: "Software\Microsoft\Windows\CurrentVersion\Run"; \
  ValueType: string; ValueName: "{#MyAppName}"; \
  ValueData: """{app}\{#MyAppExeName}"""; Flags: uninsdeletevalue
; Developer Mode — required to sideload the Game Bar widget without a Store license
Root: HKLM; Subkey: "SOFTWARE\Microsoft\Windows\CurrentVersion\AppModelUnlock"; \
  ValueType: dword; ValueName: "AllowDevelopmentWithoutDevLicense"; ValueData: 1; \
  Flags: createvalueifdoesntexist
Root: HKLM; Subkey: "SOFTWARE\Microsoft\Windows\CurrentVersion\AppModelUnlock"; \
  ValueType: dword; ValueName: "AllowAllTrustedApps"; ValueData: 1; \
  Flags: createvalueifdoesntexist

[Run]
; Install USBIP driver only when not already present (service key check in [Code])
Filename: "{tmp}\usbip\USBip-win2-x64.exe"; \
  Parameters: "/VERYSILENT /SUPPRESSMSGBOXES /NORESTART /SP-"; \
  StatusMsg: "Installing USB/IP driver..."; \
  Check: NeedsUsbIp; \
  Flags: waituntilterminated

; Import certificate, remove old widget, install new widget, restart Game Bar
Filename: "powershell.exe"; \
  Parameters: "-NoProfile -ExecutionPolicy Bypass -File ""{tmp}\widget-install.ps1"" -MsixPath ""{tmp}\Widget\SteamControllerRemapperWidget.msix"" -CertPath ""{tmp}\Widget\SteamControllerRemapperWidget.cer"""; \
  StatusMsg: "Installing Game Bar widget..."; \
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
function NeedsUsbIp(): Boolean;
begin
  Result := not (
    RegKeyExists(HKEY_LOCAL_MACHINE, 'SYSTEM\CurrentControlSet\Services\usbip_vhci') or
    RegKeyExists(HKEY_LOCAL_MACHINE, 'SYSTEM\CurrentControlSet\Services\usbip2_vhci')
  );
end;
