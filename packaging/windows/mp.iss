#ifndef AppVersion
  #define AppVersion "0.0.0"
#endif

#ifndef SourceDir
  #error SourceDir is required
#endif

#ifndef OutputDir
  #define OutputDir "."
#endif

[Setup]
AppId={{E8A8F33D-820D-49D9-BD26-08F7831076E2}
AppName=mp
AppVersion={#AppVersion}
AppPublisher=kurumihere
AppPublisherURL=https://github.com/kurumihere/mp
AppSupportURL=https://github.com/kurumihere/mp/issues
AppUpdatesURL=https://github.com/kurumihere/mp/releases
DefaultDirName={localappdata}\Programs\mp
DefaultGroupName=mp
DisableProgramGroupPage=yes
PrivilegesRequired=lowest
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=10.0
OutputDir={#OutputDir}
OutputBaseFilename=mp-{#AppVersion}-windows-x86_64-setup
SetupIconFile={#SourceDir}\mp.ico
UninstallDisplayIcon={app}\mp.ico
Compression=lzma2
SolidCompression=yes
WizardStyle=modern

[Files]
Source: "{#SourceDir}\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
Name: "{autoprograms}\mp"; Filename: "{app}\mp.exe"; WorkingDir: "{app}"; IconFilename: "{app}\mp.ico"
