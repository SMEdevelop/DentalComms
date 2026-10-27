; Inno Setup script for Dental Comms.
;
; Compile it through package.bat, not directly: that script reads the version
; out of src\app.rc and passes it in, so the version is declared in exactly
; one place.
;
; Per-user install by design, into %LOCALAPPDATA%\Programs. Dental Comms keeps
; the office's rooms and buttons in data\config.json beside its own exe (and
; reads port.txt from there), so it has to land somewhere the signed-in user
; can write. That is the same layout as the portable zip, which is still
; offered: the installed copy and an unzipped folder behave identically. It
; also means no UAC prompt to install, and "Start with Windows" is the same
; per-user Run entry the app's own tray menu writes.
;
; One page and one click: everything this used to ask (which folder, start
; with Windows, a desktop icon, the licence) has a right answer for a server
; computer, so it is no longer a question. The licence is installed beside
; the program, and the "not for emergencies" note is on the app's own
; Welcome screen, where it is read.

#ifndef AppVersion
  #define AppVersion "0.0.0"
#endif
; AppVersionFull carries the pre-release marker ("0.9.3-beta") where
; AppVersion is only ever digits, because VersionInfoVersion below refuses
; anything else. package.bat passes both in.
#ifndef AppVersionFull
  #define AppVersionFull AppVersion
#endif
; Where the three Visual C++ runtime DLLs are, passed in by package.bat.
#ifndef CrtDir
  #define CrtDir ""
#endif

#define AppName      "Dental Comms"
#define AppExeName   "DentalComms.exe"
#define AppPublisher "SMEdevelop"

[Setup]
; Never change AppId - it is how Windows recognises an existing install and
; offers an upgrade instead of a second copy.
AppId={{35A25D57-AF51-4494-BF96-1CF1A2B1985C}
AppName={#AppName}
AppVersion={#AppVersionFull}
AppVerName={#AppName} {#AppVersionFull}
AppPublisher={#AppPublisher}
AppPublisherURL=https://github.com/SMEdevelop/DentalComms
AppSupportURL=https://github.com/SMEdevelop/DentalComms/issues
AppUpdatesURL=https://github.com/SMEdevelop/DentalComms/releases
; Digits only - Inno rejects "0.9.3-beta" here, so this one stays numeric.
VersionInfoVersion={#AppVersion}

DefaultDirName={localappdata}\Programs\{#AppName}
DefaultGroupName={#AppName}
DisableProgramGroupPage=yes
DisableDirPage=yes
DisableReadyMemo=yes

; "lowest" keeps the whole install inside the user's own profile: no admin
; prompt, and nothing lands anywhere the app cannot later write to.
PrivilegesRequired=lowest

; Paths below are relative to the repository root, not to this file.
SourceDir=..
OutputDir=dist
OutputBaseFilename=DentalComms-Setup-{#AppVersionFull}

Compression=lzma2/max
SolidCompression=yes
WizardStyle=modern
UninstallDisplayName={#AppName}
UninstallDisplayIcon={app}\{#AppExeName}
SetupIconFile=src\app.ico

; Signs the setup engine, the second program Setup unpacks to %TEMP% and
; runs - see "the setup engine" in package.bat for why it matters. Only
; signed builds pass SignEngine: in this mode the first compile for each new
; version stops until the engine file it writes out has been signed, which
; an unsigned build would never get past.
#ifdef SignEngine
SignedUninstaller=yes
SignedUninstallerDir=build\signed-engine
#endif

; A running Dental Comms is found by RunningCopies() below rather than by
; Restart Manager. That also catches a portable copy or an old Dental Flow
; running from another folder, which would hold the port the new copy needs
; - and Restart Manager would close the server without saying so, which on a
; busy morning means a front desk that has quietly stopped hearing alerts.
CloseApplications=no

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Messages]
ReadyLabel1=Dental Comms is ready to install on this computer.
ReadyLabel2b=Click Install. It takes a few seconds and needs no administrator password.

; public\ is replaced as a whole, so a file a newer version no longer ships
; cannot linger and be served. data\ and port.txt are the office's own and
; are never touched here.
[InstallDelete]
Type: filesandordirs; Name: "{app}\public"

[Files]
Source: "{#AppExeName}"; DestDir: "{app}"; Flags: ignoreversion
Source: "README.md";     DestDir: "{app}"; Flags: ignoreversion
Source: "LICENSE";       DestDir: "{app}"; Flags: ignoreversion
; The browser client, served as it is. The server is useless without it.
Source: "public\*";      DestDir: "{app}\public"; Flags: ignoreversion recursesubdirs createallsubdirs
; The Visual C++ runtime, installed beside the exe rather than into the
; system: this is a per-user install and cannot write to System32, and this
; way uninstalling takes its own copy with it and cannot break another
; program's. Without these three the server fails to start on any machine
; that has never had Visual Studio or the VC++ Redistributable on it.
Source: "{#CrtDir}\msvcp140.dll";       DestDir: "{app}"; Flags: ignoreversion
Source: "{#CrtDir}\vcruntime140.dll";   DestDir: "{app}"; Flags: ignoreversion
Source: "{#CrtDir}\vcruntime140_1.dll"; DestDir: "{app}"; Flags: ignoreversion

[Icons]
Name: "{autoprograms}\{#AppName}"; Filename: "{app}\{#AppExeName}"

; "Start with Windows" is written by CurStepChanged below rather than by a
; [Registry] entry, so that an update leaves alone an office that switched it
; off in the tray menu.

[Run]
; A fresh install opens the window rather than going to the tray, so whoever
; installed it is walked through the Welcome and sees the address to give
; the other computers straight away.
Filename: "{app}\{#AppExeName}"; Description: "Start {#AppName} now"; \
    Flags: nowait postinstall skipifsilent; Check: not WasRunning
; An update to a server that was running puts it straight back, quietly, in
; silent installs too - winget upgrades are silent, and an office's messaging
; must not stay off just because nobody was there to tick a box.
Filename: "{app}\{#AppExeName}"; Parameters: "--tray"; Flags: nowait; \
    Check: WasRunning

[Code]
const
  RunKey = 'Software\Microsoft\Windows\CurrentVersion\Run';

var
  // Folders of Dental Comms (or Dental Flow) copies seen outside this
  // install - running, or set to start at sign-in - one per line. They are
  // where an office's existing settings are, if it has any.
  OldDirs: String;
  // Where settings were copied from, for the last wizard page.
  CopiedFrom: String;
  // An old copy was set to start at sign-in, by a Run entry or a shortcut
  // in the Startup folder.
  HadStartup: Boolean;
  // Dental Comms was already installed in {app} before this ran.
  WasInstalled: Boolean;
  // Setup turned a running server off, and so starts it again at the end.
  StoppedServer: Boolean;

function WasRunning(): Boolean;
begin
  Result := StoppedServer;
end;

function CRLF(): String;
begin
  Result := Chr(13) + Chr(10);
end;

procedure NoteOldDir(const Dir: String);
begin
  if (Dir <> '') and
     (Pos(CRLF() + Lowercase(Dir) + CRLF(), CRLF() + Lowercase(OldDirs)) = 0) then
    OldDirs := OldDirs + Dir + CRLF();
end;

// The exe a Run entry or shortcut command starts: the quoted part, or
// everything up to the first space when it is not quoted.
function ExeFromCommand(const Cmd: String): String;
var
  S: String;
  P: Integer;
begin
  S := Trim(Cmd);
  if Copy(S, 1, 1) = '"' then
  begin
    Delete(S, 1, 1);
    P := Pos('"', S);
  end
  else
    P := Pos(' ', S);
  if P > 0 then
    S := Copy(S, 1, P - 1);
  Result := S;
end;

function IsOurExeName(const Path: String): Boolean;
var
  Name: String;
begin
  Name := Lowercase(ExtractFileName(Path));
  Result := (Name = 'dentalcomms.exe') or (Name = 'dentalflow.exe');
end;

// Every Dental Comms or Dental Flow server running in this Windows session,
// one exe path per line, noting the folder of each as a source of settings.
// Asked of WMI, because the copy to find may be a portable one or an old
// Dental Flow in any folder - nothing a mutex or a fixed path would catch.
// A process whose path WMI will not reveal belongs to another user; it is
// not ours to stop and it does not share our files, so it is left out.
// If WMI itself is unavailable this finds nothing and Setup carries on,
// which is no worse than having no check at all.
function RunningCopies(): String;
var
  Locator, Service, Procs, Proc: Variant;
  I: Integer;
  Path: String;
begin
  Result := '';
  try
    Locator := CreateOleObject('WbemScripting.SWbemLocator');
    Service := Locator.ConnectServer('.', 'root\CIMV2');
    Procs := Service.ExecQuery('SELECT ExecutablePath FROM Win32_Process ' +
      'WHERE Name = ''DentalComms.exe'' OR Name = ''DentalFlow.exe''');
    for I := 0 to Procs.Count - 1 do
    begin
      Proc := Procs.ItemIndex(I);
      if not VarIsNull(Proc.ExecutablePath) then
      begin
        Path := Proc.ExecutablePath;
        if Path <> '' then
        begin
          Result := Result + '    ' + Path + CRLF();
          NoteOldDir(ExtractFileDir(Path));
        end;
      end;
    end;
  except
    Result := '';
  end;
end;

const
  WM_COMMAND = $0111;
  // The tray menu's "Turn Dental Comms off". Every 0.9 version, Dental Flow
  // included, understands it; gui.cpp promises never to renumber it.
  IDM_TRAY_EXIT = 204;

// Asks every Dental Comms (or Dental Flow) server window to turn off, the way
// its own tray menu would, so it closes cleanly and says goodbye to the
// screens connected to it. Console-era copies have no window; for those the
// person is asked to close them, as before.
procedure AskServersToStop();
var
  Wnd: HWND;
  I: Integer;
begin
  for I := 1 to 10 do
  begin
    Wnd := FindWindowByClassName('DentalCommsServer');
    if Wnd = 0 then
      Wnd := FindWindowByClassName('DentalFlowServer');
    if Wnd = 0 then
      Exit;
    PostMessage(Wnd, WM_COMMAND, IDM_TRAY_EXIT, 0);
    Sleep(400);
  end;
end;

// Updating or removing the server while it runs would fail on its locked
// files, or - for a copy in another folder - leave the new one unable to get
// the port. Setup turns it off itself after saying so, and an update starts
// it again at the end (see [Run]). Only a copy that will not go - one from
// the console days - still needs a person, and a silent run cannot ask, so
// that case alone stops with exit code 1.
function StoppedFirst(const Doing: String; Restarts: Boolean): Boolean;
var
  Running, Msg: String;
  I: Integer;
begin
  Result := True;
  Running := RunningCopies();
  if Running = '' then
    Exit;

  Msg := 'Dental Comms is running on this computer.' + CRLF() + CRLF();
  if Restarts then
    Msg := Msg + 'Setup will turn it off for about a minute while it ' + Doing +
           ', then start it again. Messages pause until then.'
  else
    Msg := Msg + 'Setup will turn it off before ' + Doing +
           '. Messages stop on every computer in the office.';
  if SuppressibleMsgBox(Msg, mbConfirmation, MB_OKCANCEL, IDOK) <> IDOK then
  begin
    Result := False;
    Exit;
  end;

  AskServersToStop();
  for I := 1 to 40 do
  begin
    Running := RunningCopies();
    if Running = '' then
      Break;
    Sleep(250);
  end;
  StoppedServer := Restarts;

  while Running <> '' do
  begin
    if SuppressibleMsgBox('Dental Comms is still running on this computer:' + CRLF() + CRLF() +
         Running + CRLF() +
         'Turn it off: right-click its icon by the clock (under the ^ arrow) and ' +
         'choose "Turn Dental Comms off" (or "Turn Dental Flow off"). An older ' +
         'copy may be a black window instead; close that.' + CRLF() + CRLF() +
         'Click Retry once it is off.',
         mbError, MB_RETRYCANCEL, IDCANCEL) <> IDRETRY then
    begin
      Result := False;
      Exit;
    end;
    Running := RunningCopies();
  end;
end;

function InitializeSetup(): Boolean;
begin
  Result := StoppedFirst('it updates', True);
end;

function InitializeUninstall(): Boolean;
begin
  Result := StoppedFirst('removing it', False);
end;

// The folder of the exe a Run entry starts, when that exe is ours and is
// not this install's. Empty otherwise.
function OldDirFromRunValue(const Name: String): String;
var
  Cmd, Exe: String;
begin
  Result := '';
  if RegQueryStringValue(HKCU, RunKey, Name, Cmd) then
  begin
    Exe := ExeFromCommand(Cmd);
    if IsOurExeName(Exe) and
       (CompareText(ExtractFileDir(Exe), ExpandConstant('{app}')) <> 0) then
      Result := ExtractFileDir(Exe);
  end;
end;

// Shortcuts in the Startup folder that start a Dental Comms or Dental Flow
// server - the README used to suggest exactly that. Each one found is noted
// as a source of settings and, when Remove is set, deleted: from now on the
// Run entry starts the installed copy, and an old copy starting as well
// would take the port first. Shortcuts to anything else (such as the
// browser shortcut "Set up this computer" makes) are left alone.
procedure StartupShortcuts(Remove: Boolean);
var
  Shell, Link: Variant;
  Dir, Target: String;
  Rec: TFindRec;
begin
  Dir := ExpandConstant('{userstartup}');
  if not FindFirst(Dir + '\*.lnk', Rec) then
    Exit;
  try
    Shell := CreateOleObject('WScript.Shell');
    repeat
      Link := Shell.CreateShortcut(Dir + '\' + Rec.Name);
      Target := Link.TargetPath;
      if IsOurExeName(Target) and
         (CompareText(ExtractFileDir(Target), ExpandConstant('{app}')) <> 0) then
      begin
        NoteOldDir(ExtractFileDir(Target));
        HadStartup := True;
        if Remove then
          DeleteFile(Dir + '\' + Rec.Name);
      end;
    until not FindNext(Rec);
  except
    // An unreadable shortcut is not worth failing an install over.
  end;
  FindClose(Rec);
end;

// Copies one settings file from an old folder, unless this install already
// has its own. Returns True when it copied.
function TakeFile(const OldDir, Rel: String): Boolean;
var
  Src, Dst: String;
begin
  Result := False;
  Src := OldDir + '\' + Rel;
  Dst := ExpandConstant('{app}\') + Rel;
  if FileExists(Src) and not FileExists(Dst) then
  begin
    ForceDirectories(ExtractFileDir(Dst));
    Result := CopyFile(Src, Dst, True);
  end;
end;

// Moving from the portable zip (or from Dental Flow) to this install. The
// office's rooms, buttons and port come along, and "start at sign-in" now
// starts the installed copy instead of the old one.
procedure CurStepChanged(CurStep: TSetupStep);
var
  Rest, Dir, StartCmd: String;
  P: Integer;
begin
  // Read before [Registry] runs: the "startup" task overwrites the
  // DentalComms Run value, and with it the only record of where an old
  // copy that started at sign-in lives.
  if CurStep = ssInstall then
  begin
    NoteOldDir(OldDirFromRunValue('DentalComms'));
    NoteOldDir(OldDirFromRunValue('DentalFlow'));
    HadStartup := (OldDirFromRunValue('DentalComms') <> '') or
                  (OldDirFromRunValue('DentalFlow') <> '');
    WasInstalled := FileExists(ExpandConstant('{app}\{#AppExeName}'));
    StartupShortcuts(False);
    Exit;
  end;
  if CurStep <> ssPostInstall then
    Exit;

  // The first old folder that has settings wins; config.json and port.txt
  // are taken as a pair so a port never arrives without its rooms.
  Rest := OldDirs;
  while (Rest <> '') and (CopiedFrom = '') do
  begin
    P := Pos(CRLF(), Rest);
    Dir := Copy(Rest, 1, P - 1);
    Delete(Rest, 1, P + 1);
    if FileExists(Dir + '\data\config.json') and
       not FileExists(ExpandConstant('{app}\data\config.json')) then
    begin
      TakeFile(Dir, 'data\config.json');
      TakeFile(Dir, 'port.txt');
      CopiedFrom := Dir;
    end;
  end;

  // A new install starts with Windows - a server that does not come back
  // after a restart takes the office's messaging with it. An old copy set to
  // start at sign-in hands that over to this one. An update of this install
  // leaves the setting as the office left it.
  StartCmd := '"' + ExpandConstant('{app}\{#AppExeName}') + '" --tray';
  if HadStartup or not WasInstalled then
  begin
    RegWriteStringValue(HKCU, RunKey, 'DentalComms', StartCmd);
    StartupShortcuts(True);
  end;
  if OldDirFromRunValue('DentalFlow') <> '' then
    RegDeleteValue(HKCU, RunKey, 'DentalFlow');
end;

procedure CurPageChanged(CurPageID: Integer);
begin
  if (CurPageID = wpFinished) and StoppedServer then
    WizardForm.FinishedLabel.Caption := WizardForm.FinishedLabel.Caption +
      CRLF() + CRLF() + 'Dental Comms has been started again. Its icon is by the clock.';
  if (CurPageID = wpFinished) and (CopiedFrom <> '') then
    WizardForm.FinishedLabel.Caption := WizardForm.FinishedLabel.Caption +
      CRLF() + CRLF() + 'Your rooms and buttons were copied from:' + CRLF() +
      CopiedFrom + CRLF() + CRLF() +
      'Once Dental Comms is working, you can delete that old folder.';
end;

// Uninstalling removes the program. Whether it also removes the office's
// rooms and buttons is the user's call - so ask, and default to keeping them.
procedure CurUninstallStepChanged(CurStep: TUninstallStep);
var
  AppDir, Cmd, Gap, Msg: String;
begin
  AppDir := ExpandConstant('{app}');

  // The [Registry] entry only covers a startup entry the installer itself
  // wrote. One written from the tray menu would otherwise outlive the
  // program it starts.
  if (CurStep = usUninstall) and
     RegQueryStringValue(HKCU, RunKey, 'DentalComms', Cmd) and
     (CompareText(ExtractFileDir(ExeFromCommand(Cmd)), AppDir) = 0) then
    RegDeleteValue(HKCU, RunKey, 'DentalComms');

  if CurStep <> usPostUninstall then
    Exit;

  if not DirExists(AppDir + '\data') and
     not FileExists(AppDir + '\port.txt') then
  begin
    RemoveDir(AppDir);
    Exit;
  end;

  Gap := CRLF() + CRLF();
  Msg := 'Also delete this office''s rooms, buttons and settings?' + Gap +
         'This removes the data folder (and port.txt, if there is one) from:' +
         Gap + AppDir + Gap +
         'Choose No to keep them - reinstalling Dental Comms will pick them ' +
         'up again.';

  // SuppressibleMsgBox, not MsgBox: a silent uninstall (/VERYSILENT
  // /SUPPRESSMSGBOXES, which is what winget and deployment tools use) never
  // shows this, and the explicit IDNO default means that quiet path keeps
  // the settings rather than deleting them unasked.
  if SuppressibleMsgBox(Msg, mbConfirmation, MB_YESNO or MB_DEFBUTTON2,
                        IDNO) = IDYES then
  begin
    DelTree(AppDir + '\data', True, True, True);
    DeleteFile(AppDir + '\port.txt');
    RemoveDir(AppDir);
  end;
end;
