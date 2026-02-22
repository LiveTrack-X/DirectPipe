; DirectPipe Installer Script
; Inno Setup 6.x

#define AppName "DirectPipe"
#define AppVersion "1.0.0"
#define AppPublisher "DirectPipe"
#define AppURL "https://github.com/LiveTrack-X/DirectLoopMic"
#define AppExeName "DirectPipe.exe"

[Setup]
AppId={{D1R3CTP1P3-B5A7-4C12-9E8F-A1B2C3D4E5F6}
AppName={#AppName}
AppVersion={#AppVersion}
AppPublisher={#AppPublisher}
AppPublisherURL={#AppURL}
AppSupportURL={#AppURL}
AppUpdatesURL={#AppURL}
DefaultDirName={autopf}\{#AppName}
DefaultGroupName={#AppName}
LicenseFile=assets\license.txt
OutputDir=..\build\installer
OutputBaseFilename=DirectPipe-Setup-{#AppVersion}
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
PrivilegesRequired=admin

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"; Flags: unchecked
Name: "startmenu"; Description: "Create Start Menu shortcut"; GroupDescription: "{cm:AdditionalIcons}"
Name: "autostart"; Description: "Start DirectPipe on Windows startup"; GroupDescription: "Startup Options"; Flags: unchecked
Name: "obsplugin"; Description: "Install OBS Studio Plugin"; GroupDescription: "Components"

[Files]
; Main application
Source: "..\build\bin\Release\DirectPipe.exe"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\build\bin\Release\*.dll"; DestDir: "{app}"; Flags: ignoreversion skipifsourcedoesntexist

; OBS Plugin
Source: "..\build\lib\Release\obs-directpipe-source.dll"; DestDir: "{code:GetOBSPluginDir}"; Tasks: obsplugin; Flags: ignoreversion skipifsourcedoesntexist
Source: "..\obs-plugin\data\locale\*"; DestDir: "{code:GetOBSDataDir}\locale"; Tasks: obsplugin; Flags: ignoreversion recursesubdirs

[Icons]
Name: "{group}\{#AppName}"; Filename: "{app}\{#AppExeName}"
Name: "{group}\Uninstall {#AppName}"; Filename: "{uninstallexe}"
Name: "{autodesktop}\{#AppName}"; Filename: "{app}\{#AppExeName}"; Tasks: desktopicon

[Registry]
; Auto-start on Windows startup
Root: HKCU; Subkey: "Software\Microsoft\Windows\CurrentVersion\Run"; ValueType: string; ValueName: "DirectPipe"; ValueData: """{app}\{#AppExeName}"" --minimized"; Tasks: autostart; Flags: uninsdeletevalue

[Run]
Filename: "{app}\{#AppExeName}"; Description: "{cm:LaunchProgram,{#StringChange(AppName, '&', '&&')}}"; Flags: nowait postinstall skipifsilent

[Code]
function GetOBSPluginDir(Param: String): String;
var
  OBSPath: String;
begin
  // Try to find OBS installation
  if RegQueryStringValue(HKLM, 'SOFTWARE\OBS Studio', '', OBSPath) then
    Result := OBSPath + '\obs-plugins\64bit'
  else
    Result := ExpandConstant('{userappdata}') + '\obs-studio\plugins\obs-directpipe-source\bin\64bit';
end;

function GetOBSDataDir(Param: String): String;
var
  OBSPath: String;
begin
  if RegQueryStringValue(HKLM, 'SOFTWARE\OBS Studio', '', OBSPath) then
    Result := OBSPath + '\data\obs-plugins\obs-directpipe-source'
  else
    Result := ExpandConstant('{userappdata}') + '\obs-studio\plugins\obs-directpipe-source\data';
end;
