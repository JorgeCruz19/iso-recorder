; Instalator ISO Recorder (Inno Setup 6). Budowany przez scripts\installer.ps1 z gotowej paczki dist\IsoRecorder.
; Wersja jest podawana z zewnatrz: ISCC /DAppVersion=3.3 installer\IsoRecorder.iss
; Instalacja nie wymaga uprawnien administratora (domyslnie dla biezacego uzytkownika); mozna wybrac "dla wszystkich".

#ifndef AppVersion
  #define AppVersion "0.0"
#endif

[Setup]
AppId={{6E2B6C0F-5B0D-4E5C-9C0B-1F3E7A2D9B41}
AppName=ISO Recorder
AppVersion={#AppVersion}
AppVerName=ISO Recorder {#AppVersion}
AppPublisher=Studio Krause
AppPublisherURL=https://github.com/studiokrause/iso-recorder
AppSupportURL=https://github.com/studiokrause/iso-recorder
DefaultDirName={autopf}\ISO Recorder
DefaultGroupName=ISO Recorder
DisableProgramGroupPage=yes
PrivilegesRequired=lowest
PrivilegesRequiredOverridesAllowed=dialog
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=10.0.18362
OutputDir=..\dist
OutputBaseFilename=IsoRecorder-{#AppVersion}-setup
SetupIconFile=..\res\icon.ico
UninstallDisplayIcon={app}\IsoRecorder.exe
UninstallDisplayName=ISO Recorder {#AppVersion}
Compression=lzma2/max
SolidCompression=yes
LZMAUseSeparateProcess=yes
WizardStyle=modern
CloseApplications=yes
VersionInfoVersion={#AppVersion}.0.0
VersionInfoProductName=ISO Recorder
VersionInfoCompany=Studio Krause

[Languages]
Name: "en"; MessagesFile: "compiler:Default.isl"
Name: "pl"; MessagesFile: "compiler:Languages\Polish.isl"
Name: "de"; MessagesFile: "compiler:Languages\German.isl"
Name: "es"; MessagesFile: "compiler:Languages\Spanish.isl"
Name: "it"; MessagesFile: "compiler:Languages\Italian.isl"
Name: "fr"; MessagesFile: "compiler:Languages\French.isl"

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"; Flags: unchecked

[Files]
Source: "..\dist\IsoRecorder\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
Name: "{autoprograms}\ISO Recorder"; Filename: "{app}\IsoRecorder.exe"
Name: "{autodesktop}\ISO Recorder"; Filename: "{app}\IsoRecorder.exe"; Tasks: desktopicon

[Run]
Filename: "{app}\IsoRecorder.exe"; Description: "{cm:LaunchProgram,ISO Recorder}"; Flags: nowait postinstall skipifsilent
