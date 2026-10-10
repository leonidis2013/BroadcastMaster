; BroadcastMaster Windows installer (Inno Setup 6). Built by GitHub Actions:
;   ISCC.exe /DAppVersion=0.2.1 /DSourceVst3=<path to BroadcastMaster.vst3 folder> /DOutputDir=<dir> installer\windows\BroadcastMaster.iss
#ifndef AppVersion
  #define AppVersion "0.0.0"
#endif

[Setup]
AppId={{B7C1D2E3-4F58-4A69-9B7C-3E2D1A0F8C55}
AppName=BroadcastMaster
AppVersion={#AppVersion}
AppVerName=BroadcastMaster {#AppVersion}
AppPublisher=BroadcastMaster Audio
DefaultDirName={commoncf64}\VST3
DisableDirPage=yes
DisableProgramGroupPage=yes
DisableReadyPage=no
UsePreviousAppDir=no
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
PrivilegesRequired=admin
OutputDir={#OutputDir}
OutputBaseFilename=BroadcastMaster-Windows-Installer
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
UninstallDisplayName=BroadcastMaster {#AppVersion} (VST3)
CloseApplications=no

[Messages]
WelcomeLabel2=This installs the BroadcastMaster {#AppVersion} VST3 plug-in into C:\Program Files\Common Files\VST3.%n%nOlder BroadcastMaster copies there are removed first, so your host always loads this version.%n%nClose LiveProfessor, OBS or any other host before continuing, then rescan plug-ins afterwards.

[InstallDelete]
; old copies, including Explorer duplicates such as "BroadcastMaster.vst3 - Copy" or "BroadcastMaster 2.vst3"
Type: filesandordirs; Name: "{commoncf64}\VST3\BroadcastMaster.vst3"
Type: filesandordirs; Name: "{commoncf64}\VST3\BroadcastMaster*.vst3"

[Files]
Source: "{#SourceVst3}\*"; DestDir: "{commoncf64}\VST3\BroadcastMaster.vst3"; Flags: ignoreversion recursesubdirs createallsubdirs

[UninstallDelete]
Type: filesandordirs; Name: "{commoncf64}\VST3\BroadcastMaster.vst3"
