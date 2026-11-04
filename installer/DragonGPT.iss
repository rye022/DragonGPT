#ifndef SourceRoot
  #define SourceRoot ".."
#endif
#ifndef RuntimeRoot
  #define RuntimeRoot SourceRoot + "\dist\DragonGPT"
#endif
#ifndef OutputRoot
  #define OutputRoot "..\.."
#endif
[Setup]
AppId={{D8B412CF-D26D-488F-91D3-D775C3537D30}
AppName=DragonGPT
AppVersion=5.0.0
AppVerName=DragonGPT V5.0.0
AppPublisher=DragonGPT
DefaultDirName={localappdata}\Programs\DragonGPT
DefaultGroupName=DragonGPT
PrivilegesRequired=lowest
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=10.0.17763
OutputDir={#OutputRoot}
OutputBaseFilename=DragonGPT-Setup-v5.0.0-win-x64
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
UninstallDisplayIcon={app}\DragonGPT.exe
CloseApplications=yes
RestartApplications=no
SetupLogging=yes
VersionInfoVersion=5.0.0.0
DisableProgramGroupPage=yes

[Tasks]
Name: "desktopicon"; Description: "Create a desktop shortcut"; GroupDescription: "Additional shortcuts:"; Flags: unchecked
Name: "autostart"; Description: "Start DragonGPT when I sign in"; GroupDescription: "Startup:"; Flags: unchecked

[Files]
Source: "{#RuntimeRoot}\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs; Excludes: "data\*,preview-data\*,runtime\*,offline-runtime\*,voice-cache\*,*\runtime\*,*\model\*,*\models\*,*\__pycache__\*,*ready.json,*.dpapi,*.sqlite*,*.db,*.ini,*.log,.installation.json"

[Icons]
Name: "{group}\DragonGPT"; Filename: "{app}\DragonGPT.exe"; Parameters: "--desktop"; WorkingDir: "{app}"
Name: "{group}\DragonGPT User Guide"; Filename: "{app}\docs\DRAGONGPT_5_MANUAL.md"
Name: "{group}\Uninstall DragonGPT"; Filename: "{uninstallexe}"
Name: "{autodesktop}\DragonGPT 5.0"; Filename: "{app}\DragonGPT.exe"; Parameters: "--desktop"; WorkingDir: "{app}"; Tasks: desktopicon
Name: "{userstartup}\DragonGPT 5.0"; Filename: "{app}\DragonGPT.exe"; Parameters: "--desktop"; WorkingDir: "{app}"; Tasks: autostart

[Run]
Filename: "{app}\DragonGPT.exe"; Parameters: "--desktop"; Description: "Launch DragonGPT"; Flags: nowait postinstall skipifsilent

; Deliberately no [UninstallDelete] directive: local memories and configuration
; in %LOCALAPPDATA%\DragonGPT\UserData, and legacy portable data, are retained.
