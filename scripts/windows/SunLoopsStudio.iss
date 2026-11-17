; The Windows installer of Sun Loops Studio (0.75.0), made by Inno Setup 6 from the folder scripts\build_windows.ps1 prepares:
;   iscc /DAppVersion=0.75.0 /DSourceDir=<that folder> /DOutputDir=dist [/DSetupIcon=icon.ico] scripts\windows\SunLoopsStudio.iss
; It installs for the user alone (no administrator needed: %LOCALAPPDATA%\Programs) or, if chosen, for everyone (Program Files);
; registers the .slp project files (their icon, open with a double-click); adds a Start menu entry and, if wanted, a desktop
; icon; optionally copies the four instruments into the VST3 folder for other music programs. Uninstalling removes all of it
; (the user's projects, sounds and settings stay).

#ifndef AppVersion
  #error AppVersion is needed (/DAppVersion=0.75.0)
#endif
#ifndef SourceDir
  #error SourceDir is needed (/DSourceDir=...)
#endif
#ifndef OutputDir
  #define OutputDir "."
#endif

[Setup]
AppId={{6F3C2B9A-5E1D-4C7B-9A3E-2D1F0C4B7A61}
AppName=Sun Loops Studio
AppVersion={#AppVersion}
AppVerName=Sun Loops Studio {#AppVersion}
AppPublisher=Sun Loops
AppPublisherURL=https://github.com/Hubert9204/Sun-Loops-Studio
AppSupportURL=https://github.com/Hubert9204/Sun-Loops-Studio/issues
AppUpdatesURL=https://github.com/Hubert9204/Sun-Loops-Studio/releases/latest
VersionInfoVersion={#AppVersion}
DefaultDirName={autopf}\Sun Loops Studio
DefaultGroupName=Sun Loops Studio
DisableProgramGroupPage=yes
UninstallDisplayIcon={app}\Sun Loops Studio.exe
UninstallDisplayName=Sun Loops Studio
OutputDir={#OutputDir}
OutputBaseFilename=Sun-Loops-Studio-{#AppVersion}-Windows-x64-Setup
Compression=lzma2/max
SolidCompression=yes
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=10.0
PrivilegesRequired=lowest
PrivilegesRequiredOverridesAllowed=dialog
ChangesAssociations=yes
CloseApplications=yes
WizardStyle=modern
LicenseFile={#SourceDir}\LICENSE.txt
#ifdef SetupIcon
SetupIconFile={#SetupIcon}
#endif

[Languages]
Name: "en"; MessagesFile: "compiler:Default.isl"
Name: "it"; MessagesFile: "compiler:Languages\Italian.isl"

[CustomMessages]
en.ProjectFile=Sun Loops Studio project
it.ProjectFile=Progetto di Sun Loops Studio
en.PluginsTask=Also install SunBurst, HexForge, SunSymphony and SunRiff for other music programs (VST3 folder)
it.PluginsTask=Installa SunBurst, HexForge, SunSymphony e SunRiff anche per altri programmi musicali (cartella VST3)
en.OtherTasks=Other tasks:
it.OtherTasks=Altre operazioni:

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"; Flags: unchecked
Name: "vst3"; Description: "{cm:PluginsTask}"; GroupDescription: "{cm:OtherTasks}"; Flags: unchecked

[Files]
; The program, its symbols (function names in crash reports), Helios's helper, the licences and the bundled plugins (VST3\),
; where the program looks for them.
Source: "{#SourceDir}\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs
; For other programs: the standard VST3 folder (Common Files\VST3, or the user's own when installed for the user alone).
Source: "{#SourceDir}\VST3\*"; DestDir: "{autocf}\VST3"; Tasks: vst3; Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
Name: "{autoprograms}\Sun Loops Studio"; Filename: "{app}\Sun Loops Studio.exe"
Name: "{autodesktop}\Sun Loops Studio"; Filename: "{app}\Sun Loops Studio.exe"; Tasks: desktopicon

[Registry]
; The .slp project files: their icon, and a double-click opens them (the program reads the file from its command line).
Root: HKA; Subkey: "Software\Classes\.slp"; ValueType: string; ValueName: ""; ValueData: "SunLoopsStudio.Project"; Flags: uninsdeletevalue
Root: HKA; Subkey: "Software\Classes\.slp\OpenWithProgids"; ValueType: string; ValueName: "SunLoopsStudio.Project"; ValueData: ""; Flags: uninsdeletevalue
Root: HKA; Subkey: "Software\Classes\SunLoopsStudio.Project"; ValueType: string; ValueName: ""; ValueData: "{cm:ProjectFile}"; Flags: uninsdeletekey
Root: HKA; Subkey: "Software\Classes\SunLoopsStudio.Project\DefaultIcon"; ValueType: string; ValueName: ""; ValueData: "{app}\Sun Loops Studio.exe,0"
Root: HKA; Subkey: "Software\Classes\SunLoopsStudio.Project\shell\open\command"; ValueType: string; ValueName: ""; ValueData: """{app}\Sun Loops Studio.exe"" ""%1"""
Root: HKA; Subkey: "Software\Classes\Applications\Sun Loops Studio.exe\SupportedTypes"; ValueType: string; ValueName: ".slp"; ValueData: ""; Flags: uninsdeletekey

[Run]
Filename: "{app}\Sun Loops Studio.exe"; Description: "{cm:LaunchProgram,Sun Loops Studio}"; Flags: nowait postinstall skipifsilent
