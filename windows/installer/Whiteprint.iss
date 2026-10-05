; Inno Setup script. Build with installer\package.ps1 (passes AppVersion and StageDir).
#ifndef AppVersion
  #define AppVersion "0.1.0"
#endif
#ifndef StageDir
  #define StageDir "..\..\.build\windows-stage"
#endif
#ifndef OutDir
  #define OutDir "..\..\.build\windows-release"
#endif

[Setup]
AppId={{6F0B7A52-3D3E-4C77-9B58-5E1A0F7C2A91}
AppName=Whiteprint
AppVersion={#AppVersion}
AppPublisher=Whiteprint
DefaultDirName={autopf}\Whiteprint
DefaultGroupName=Whiteprint
PrivilegesRequired=lowest
PrivilegesRequiredOverridesAllowed=dialog
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
UninstallDisplayIcon={app}\Whiteprint.exe
UninstallDisplayName=Whiteprint
OutputDir={#OutDir}
OutputBaseFilename=Whiteprint-{#AppVersion}-Setup
SetupIconFile=..\app\AppIcon.ico
WizardStyle=modern
WizardImageFile=wizard.bmp,wizard@2x.bmp
WizardSmallImageFile=wizard-small.bmp,wizard-small@2x.bmp
WizardImageBackColor=#1E4D8C
Compression=lzma2/max
SolidCompression=yes
ChangesAssociations=yes
DisableProgramGroupPage=yes
VersionInfoVersion={#AppVersion}

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"; Flags: unchecked

[Files]
Source: "{#StageDir}\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
Name: "{group}\Whiteprint"; Filename: "{app}\Whiteprint.exe"
Name: "{group}\Uninstall Whiteprint"; Filename: "{uninstallexe}"
Name: "{autodesktop}\Whiteprint"; Filename: "{app}\Whiteprint.exe"; Tasks: desktopicon

[Registry]
; HKA = HKCU for per-user installs, HKLM for admin installs.
Root: HKA; Subkey: "Software\Classes\.wprint"; ValueType: string; ValueName: ""; ValueData: "Whiteprint.Note"; Flags: uninsdeletevalue
Root: HKA; Subkey: "Software\Classes\Whiteprint.Note"; ValueType: string; ValueName: ""; ValueData: "Whiteprint note"; Flags: uninsdeletekey
Root: HKA; Subkey: "Software\Classes\Whiteprint.Note\DefaultIcon"; ValueType: string; ValueName: ""; ValueData: "{app}\Whiteprint.exe,0"
Root: HKA; Subkey: "Software\Classes\Whiteprint.Note\shell\open\command"; ValueType: string; ValueName: ""; ValueData: """{app}\Whiteprint.exe"" ""%1"""

[Run]
Filename: "{app}\Whiteprint.exe"; Description: "{cm:LaunchProgram,Whiteprint}"; Flags: nowait postinstall skipifsilent
