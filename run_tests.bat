@echo off
setlocal EnableDelayedExpansion
cd /d "%~dp0"

rem ---- locate the Microsoft C++ compiler (same dance as build.bat) ------
where cl.exe >nul 2>&1
if not errorlevel 1 goto :havecl

set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" set "VSWHERE=%ProgramFiles%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" goto :nocompiler

set "VCVARS="
for /f "usebackq tokens=*" %%I in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VCVARS=%%I\VC\Auxiliary\Build\vcvars64.bat"
if not exist "%VCVARS%" goto :nocompiler
call "%VCVARS%" >nul 2>&1
where cl.exe >nul 2>&1
if errorlevel 1 goto :nocompiler
:havecl

if not exist build mkdir build
if not exist build\obj-test mkdir build\obj-test

echo  Building the self-test...
cl /nologo /std:c++17 /O1 /EHsc /MD /W3 /utf-8 /DUNICODE /D_UNICODE /DNOMINMAX ^
  /D_WIN32_WINNT=0x0A00 /D_CRT_SECURE_NO_WARNINGS /D_WINSOCK_DEPRECATED_NO_WARNINGS ^
  /Isrc /Fobuild\obj-test\ /Fdbuild\obj-test\ ^
  tests\selftest.cpp src\routes.cpp src\state.cpp src\config.cpp ^
  src\network.cpp src\http.cpp src\json.cpp src\util.cpp src\shortcut.cpp ^
  src\checks.cpp src\firewall.cpp ^
  /Febuild\selftest.exe ^
  /link /INCREMENTAL:NO ws2_32.lib iphlpapi.lib shlwapi.lib ole32.lib oleaut32.lib uuid.lib advapi32.lib
if errorlevel 1 ( echo  BUILD FAILED & exit /b 1 )

rem  Smart App Control will not start an unsigned exe, and a test binary
rem  is rebuilt constantly, so without this the suite cannot be run at all
rem  on a machine that has it turned on. See the note beside :sign below.
call :sign "%CD%\build\selftest.exe"
if errorlevel 1 exit /b 1

echo.
build\selftest.exe
exit /b %errorlevel%

:nocompiler
echo  ERROR: no C++ compiler found - see build.bat
exit /b 1

rem ---------------------------------------------------------------------
rem  :sign <file>   - signs in place, or does nothing if signing is not
rem  set up. A copy of the subroutine in build.bat and package.bat, which
rem  share the variable; nothing links them, so a fix here needs applying
rem  there too.
rem
rem      set DENTALCOMMS_SIGN_CMD=powershell -NoProfile -ExecutionPolicy Bypass -File packaging\sign.ps1
rem
rem  Signing every test build is a round trip to the signing service, so
rem  it happens only when that variable is set. Leave it unset on a
rem  machine without Smart App Control and the suite runs as it always did.
rem ---------------------------------------------------------------------
:sign
if not defined DENTALCOMMS_SIGN_CMD (
  echo  Not signed. Where Smart App Control is on the self-test will not
  echo  start - set DENTALCOMMS_SIGN_CMD, see the note at the foot of this
  echo  script.
  exit /b 0
)
echo  Signing %~nx1 ...
call %DENTALCOMMS_SIGN_CMD% "%~1"
if errorlevel 1 (
  echo  ERROR: signing %~nx1 failed.
  exit /b 1
)
exit /b 0
