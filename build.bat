@echo off
setlocal EnableDelayedExpansion
cd /d "%~dp0"

echo.
echo  Building Dental Flow...
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

rem  /MD links the C++ runtime dynamically, so DentalFlow.exe needs
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

rem  A console program: the default /SUBSYSTEM:CONSOLE and main() entry point
rem  are what we want, so the office sees the "Dental Flow is running" window.
set LDFLAGS=/link /INCREMENTAL:NO /OPT:REF /OPT:ICF
set LIBS=ws2_32.lib iphlpapi.lib shlwapi.lib ole32.lib oleaut32.lib uuid.lib advapi32.lib

set SOURCES=src\main.cpp src\routes.cpp src\state.cpp src\config.cpp src\network.cpp src\http.cpp src\json.cpp src\util.cpp

echo  Compiling resources...
rc /nologo /fo build\app.res src\app.rc
if errorlevel 1 goto :failed

echo  Compiling and linking...
cl %CXXFLAGS% %SOURCES% build\app.res /FeDentalFlow.exe %LDFLAGS% %LIBS%
if errorlevel 1 goto :failed

echo.
echo  Built:  DentalFlow.exe
echo.
echo  Start it by double-clicking DentalFlow.exe, or run:
echo.
echo      DentalFlow.exe
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
