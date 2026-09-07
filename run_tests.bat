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
  /Febuild\selftest.exe ^
  /link /INCREMENTAL:NO ws2_32.lib iphlpapi.lib shlwapi.lib ole32.lib oleaut32.lib uuid.lib advapi32.lib
if errorlevel 1 ( echo  BUILD FAILED & exit /b 1 )

echo.
build\selftest.exe
exit /b %errorlevel%

:nocompiler
echo  ERROR: no C++ compiler found - see build.bat
exit /b 1
