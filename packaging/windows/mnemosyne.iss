; Inno Setup script for Mnemosyne (mn).
;
; Build (from the repo root):
;   mingw32-make fetch-poppler
;   mingw32-make
;   iscc packaging\windows\mnemosyne.iss /DMyAppVersion=0.1.0
;
; Produces packaging\windows\Output\Mnemosyne-Setup-<version>.exe

#ifndef MyAppVersion
  #define MyAppVersion "0.0.0"
#endif

#define MyAppName "Mnemosyne"
#define MyAppExeName "mn.exe"
#define MyAppPublisher "Wayne Choy"
#define RepoRoot "..\.."

[Setup]
AppId={{6E4D6E56-4F4B-4A6C-9A0A-6D6E656D6F73}
AppName={#MyAppName}
AppVersion={#MyAppVersion}
AppPublisher={#MyAppPublisher}
DefaultDirName={localappdata}\Programs\{#MyAppName}
DefaultGroupName={#MyAppName}
DisableProgramGroupPage=yes
PrivilegesRequired=lowest
OutputDir=Output
OutputBaseFilename=Mnemosyne-Setup-{#MyAppVersion}
Compression=lzma2
SolidCompression=yes
ArchitecturesInstallIn64BitMode=x64compatible
UninstallDisplayIcon={app}\{#MyAppExeName}

[Files]
Source: "{#RepoRoot}\mn.exe"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#RepoRoot}\vendor\poppler-windows\bin\*"; DestDir: "{app}\poppler\bin"; Flags: ignoreversion recursesubdirs createallsubdirs skipifsourcedoesntexist

[Code]
const
  EnvKey = 'Environment';

function SendMessageTimeoutA(hWnd: LongInt; Msg: LongInt; wParam: LongInt;
  lParam: string; fuFlags: LongInt; uTimeout: LongInt; var lpdwResult: LongInt): LongInt;
  external 'SendMessageTimeoutA@user32.dll stdcall';

procedure BroadcastEnvironmentChange;
var
  ResultCode: LongInt;
begin
  { WM_SETTINGCHANGE = 0x001A, HWND_BROADCAST = 0xFFFF, SMTO_ABORTIFHUNG = 0x0002.
    Lets already-running processes (e.g. Explorer) pick up the PATH change without reboot. }
  SendMessageTimeoutA($FFFF, $001A, 0, 'Environment', $0002, 5000, ResultCode);
end;

function NeedsAddPath(Param: string): boolean;
var
  OrigPath: string;
begin
  if not RegQueryStringValue(HKEY_CURRENT_USER, EnvKey, 'Path', OrigPath) then
  begin
    Result := True;
    exit;
  end;
  Result := Pos(';' + Param + ';', ';' + OrigPath + ';') = 0;
end;

procedure AddToUserPath(Dir: string);
var
  OrigPath: string;
begin
  if not NeedsAddPath(Dir) then
    exit;
  if not RegQueryStringValue(HKEY_CURRENT_USER, EnvKey, 'Path', OrigPath) then
    OrigPath := '';
  if (OrigPath <> '') and (OrigPath[Length(OrigPath)] <> ';') then
    OrigPath := OrigPath + ';';
  RegWriteExpandStringValue(HKEY_CURRENT_USER, EnvKey, 'Path', OrigPath + Dir);
end;

procedure RemoveFromUserPath(Dir: string);
var
  OrigPath, NewPath: string;
begin
  if not RegQueryStringValue(HKEY_CURRENT_USER, EnvKey, 'Path', OrigPath) then
    exit;
  NewPath := ';' + OrigPath + ';';
  StringChangeEx(NewPath, ';' + Dir + ';', ';', True);
  Delete(NewPath, 1, 1);
  Delete(NewPath, Length(NewPath), 1);
  RegWriteExpandStringValue(HKEY_CURRENT_USER, EnvKey, 'Path', NewPath);
end;

procedure CurStepChanged(CurStep: TSetupStep);
begin
  if CurStep = ssPostInstall then
  begin
    AddToUserPath(ExpandConstant('{app}'));
    BroadcastEnvironmentChange;
  end;
end;

procedure CurUninstallStepChanged(CurUninstallStep: TUninstallStep);
begin
  if CurUninstallStep = usPostUninstall then
  begin
    RemoveFromUserPath(ExpandConstant('{app}'));
    BroadcastEnvironmentChange;
  end;
end;
