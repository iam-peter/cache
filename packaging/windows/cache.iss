; Windows installer (Inno Setup 6 or 7), built by the nightly workflow:
;
;   iscc /O<output dir> /DAppVersion=0.2.0 /DAppFiles=<deployed app folder> cache.iss
;
; It installs into a fixed program folder, without administrator rights
; for the current user (%LOCALAPPDATA%\Programs\Cache) or, chosen in a
; dialog, for all users (Program Files). Two checkboxes (both ticked) add
; a Start menu entry and a desktop shortcut. It registers an uninstaller
; in Windows' list of apps.
;
; The notes live elsewhere, in the workspace folder (Documents\Cache
; unless chosen otherwise), and the settings in the registry
; (HKCU\Software\Cache). Neither updates nor the uninstaller touch them.
; /NOLAUNCH=1 keeps the program from starting after installing (for
; tests).

#ifndef AppVersion
  #define AppVersion "0.0.0"
#endif
#ifndef AppFiles
  #define AppFiles "..\..\Cache"
#endif

[Setup]
; Never change the AppId: it is how Windows and later versions recognise
; an existing installation
AppId={{D7F74E85-8D38-4E34-8525-2C29986F6DD2}
AppName=Cache
AppVersion={#AppVersion}
AppVerName=Cache {#AppVersion}
AppPublisher=Cache
AppPublisherURL=https://iam-peter.github.io/cache/
AppSupportURL=https://github.com/iam-peter/cache/issues
AppUpdatesURL=https://github.com/iam-peter/cache/releases
AppCopyright=MIT License
VersionInfoVersion={#AppVersion}
VersionInfoDescription=Cache Setup
; Always the same, known place: no folder to choose
DefaultDirName={autopf}\Cache
DisableDirPage=yes
DefaultGroupName=Cache
DisableProgramGroupPage=yes
PrivilegesRequired=lowest
PrivilegesRequiredOverridesAllowed=dialog
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=10.0
OutputBaseFilename=cache-windows-x64-setup
SetupIconFile=..\..\logo\cache.ico
UninstallDisplayIcon={app}\cache.exe
UninstallDisplayName=Cache
WizardStyle=modern
Compression=lzma2/max
SolidCompression=yes
; Close a running Cache before replacing its files
CloseApplications=yes
RestartApplications=no

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "startmenuicon"; Description: "Start menu entry"; GroupDescription: "{cm:AdditionalIcons}"
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"

[InstallDelete]
; Qt's folders from the previous version, so no outdated plugin or QML
; module stays behind. The DLLs next to the program are overwritten.
Type: filesandordirs; Name: "{app}\qml"
Type: filesandordirs; Name: "{app}\generic"
Type: filesandordirs; Name: "{app}\iconengines"
Type: filesandordirs; Name: "{app}\imageformats"
Type: filesandordirs; Name: "{app}\multimedia"
Type: filesandordirs; Name: "{app}\networkinformation"
Type: filesandordirs; Name: "{app}\platforms"
Type: filesandordirs; Name: "{app}\qmltooling"
Type: filesandordirs; Name: "{app}\styles"
Type: filesandordirs; Name: "{app}\tls"
Type: filesandordirs; Name: "{app}\translations"

[Files]
Source: "{#AppFiles}\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
Name: "{autoprograms}\Cache"; Filename: "{app}\cache.exe"; Tasks: startmenuicon
Name: "{autodesktop}\Cache"; Filename: "{app}\cache.exe"; Tasks: desktopicon

[Run]
Filename: "{app}\cache.exe"; Description: "{cm:LaunchProgram,Cache}"; Flags: nowait postinstall; Check: ShouldLaunch

[UninstallDelete]
; Leftovers that are not notes: the QML cache
Type: filesandordirs; Name: "{localappdata}\Cache\Cache\cache"
Type: dirifempty; Name: "{localappdata}\Cache\Cache"
Type: dirifempty; Name: "{localappdata}\Cache"

[Code]
function ShouldLaunch: Boolean;
begin
  Result := ExpandConstant('{param:NOLAUNCH|0}') = '0';
end;
