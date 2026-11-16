#ifndef Version
  #define Version "0.0.0"
#endif
#ifndef Source
  #define Source "..\..\..\release\RelWithDebInfo\obs-goome"
#endif
#ifndef OutDir
  #define OutDir "..\..\..\release"
#endif

[Setup]
AppId={{1DF07C3B-2398-4542-9AE8-B79B19E7C33C}
AppName=goome for OBS
AppVersion={#Version}
AppVerName=goome for OBS {#Version}
AppPublisher=goome-ui
AppPublisherURL=https://github.com/goome-ui/obs-goome
AppSupportURL=https://github.com/goome-ui/obs-goome/issues
DefaultDirName={commonappdata}\obs-studio\plugins\obs-goome
DisableDirPage=no
UsePreviousAppDir=yes
DisableProgramGroupPage=yes
DirExistsWarning=no
PrivilegesRequired=admin
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
OutputDir={#OutDir}
OutputBaseFilename=obs-goome-{#Version}-windows-x64-installer
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
UninstallDisplayName=goome for OBS

[Messages]
WelcomeLabel2=This puts the goome overlay source into OBS Studio 31 or newer.%n%nClose OBS before you go on.

[Files]
Source: "{#Source}\*"; DestDir: "{app}"; Excludes: "*.pdb"; Flags: recursesubdirs ignoreversion

[UninstallDelete]
Type: dirifempty; Name: "{app}"
