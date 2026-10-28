@echo off
setlocal EnableDelayedExpansion
cd /d "%~dp0"

echo.
echo  Building Dental Comms...
echo.

rem ---- locate the Microsoft C++ compiler -------------------------------
rem  If cl.exe is already on PATH we are inside a Developer Command Prompt
rem  and there is nothing to set up. Otherwise vswhere finds the newest
rem  Visual Studio (or Build Tools) install that actually carries the x64
rem  toolset, and its vcvars64.bat puts cl.exe, rc.exe and link.exe on PATH.
where cl.exe >nul 2>&1
if not errorlevel 1 goto :havecl

set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" set "VSWHERE=%ProgramFiles%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" goto :nocompiler

set "VCVARS="
for /f "usebackq tokens=*" %%I in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VCVARS=%%I\VC\Auxiliary\Build\vcvars64.bat"
if not exist "%VCVARS%" goto :nocompiler

rem  2>&1 as well as >nul: vcvars64.bat shells out to vswhere.exe itself and
rem  grumbles to stderr when it is not on PATH, which is noise we did not cause
rem  and which does not stop it working.
call "%VCVARS%" >nul 2>&1
where cl.exe >nul 2>&1
if errorlevel 1 goto :nocompiler

:havecl
for /f "delims=" %%V in ('cl 2^>^&1 ^| findstr /c:"Version"') do (
  echo  Compiler: %%V
  goto :gotver
)
:gotver

if not exist build mkdir build
if not exist build\obj mkdir build\obj

rem  /MD links the C++ runtime dynamically, so DentalComms.exe needs
rem  msvcp140.dll, vcruntime140.dll and vcruntime140_1.dll beside it.
rem  package.bat copies those three out of the Visual Studio redistributable
rem  into the zip that goes to the office.
rem
rem  NOMINMAX: windows.h defines min() and max() as macros, which breaks any
rem  std::min / std::max in the source. MinGW's headers were laxer about it.
rem  /D_WINSOCK_DEPRECATED_NO_WARNINGS: inet_ntoa and friends are flagged as
rem  deprecated by MSVC but are exactly what the Node original used, and are
rem  fine for an IPv4 office LAN.
set CXXFLAGS=/nologo /std:c++17 /O2 /EHsc /MD /W3 /GS /utf-8 /DUNICODE /D_UNICODE /DNOMINMAX /D_WIN32_WINNT=0x0A00 /D_CRT_SECURE_NO_WARNINGS /D_WINSOCK_DEPRECATED_NO_WARNINGS /Isrc /Fobuild\obj\ /Fdbuild\obj\

rem  A windowed program since 0.9: /SUBSYSTEM:WINDOWS and wWinMain, so the
rem  office gets an ordinary window and a notification-area icon rather than a
rem  console nobody dares close. No console is allocated at all.
rem  /MANIFEST:NO: the manifest is embedded by app.rc, and a second one from
rem  the linker would collide with it.
set LDFLAGS=/link /INCREMENTAL:NO /OPT:REF /OPT:ICF /SUBSYSTEM:WINDOWS /MANIFEST:NO
set LIBS=ws2_32.lib iphlpapi.lib shlwapi.lib ole32.lib oleaut32.lib uuid.lib advapi32.lib user32.lib gdi32.lib shell32.lib comctl32.lib

set SOURCES=src\main.cpp src\gui.cpp src\icons.cpp src\checks.cpp src\firewall.cpp src\shortcut.cpp src\routes.cpp src\state.cpp src\config.cpp src\network.cpp src\http.cpp src\json.cpp src\util.cpp

echo  Compiling resources...
rc /nologo /fo build\app.res src\app.rc
if errorlevel 1 goto :failed

echo  Compiling and linking...
cl %CXXFLAGS% %SOURCES% build\app.res /FeDentalComms.exe %LDFLAGS% %LIBS%
if errorlevel 1 goto :failed

call :sign "%CD%\DentalComms.exe"
if errorlevel 1 exit /b 1

echo.
echo  Built:  DentalComms.exe
echo.
echo  Start it by double-clicking DentalComms.exe, or run:
echo.
echo      DentalComms.exe
echo.
echo  Note: this is a /MD build. It starts here because Visual Studio is
echo  installed on this machine. The copy that goes to the office must be the
echo  one package.bat makes, which brings the three runtime DLLs with it.
echo.
exit /b 0

:nocompiler
echo  ERROR: no Microsoft C++ compiler found.
echo.
echo  Install the C++ build tools with this command, then run build.bat again:
echo.
echo      winget install Microsoft.VisualStudio.2022.BuildTools --override "--quiet --add Microsoft.VisualStudio.Workload.VCTools --includeRecommended"
echo.
echo  Or open a "x64 Native Tools Command Prompt" and run build.bat there.
echo.
exit /b 1

:failed
echo.
echo  BUILD FAILED - see the errors above.
exit /b 1

rem ---------------------------------------------------------------------
rem  :sign <file>   - signs in place, or does nothing if signing is not
rem  set up. The same subroutine, and the same variable, as package.bat.
rem
rem  Give it a command that takes the file to sign as its one argument:
rem
rem      set DENTALCOMMS_SIGN_CMD=powershell -NoProfile -ExecutionPolicy Bypass -File packaging\sign.ps1
rem
rem  This is worth doing on a development machine and not only for a
rem  release. Smart App Control refuses to start an unsigned exe at all,
rem  so where it is turned on a fresh build cannot be run even once until
rem  it carries a signature - and every rebuild drops the signature again,
rem  which is why this lives in the build rather than in package.bat only.
rem  package.bat also understands DENTALCOMMS_SIGN_PFX; one variable is
rem  enough here.
rem ---------------------------------------------------------------------
:sign
if not defined DENTALCOMMS_SIGN_CMD (
  echo  Not signed. Where Smart App Control is on this will not start -
  echo  set DENTALCOMMS_SIGN_CMD, see the note at the foot of this script.
  exit /b 0
)
echo  Signing %~nx1 ...
call %DENTALCOMMS_SIGN_CMD% "%~1"
if errorlevel 1 (
  echo  ERROR: signing %~nx1 failed.
  exit /b 1
)
exit /b 0
