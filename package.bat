@echo off
setlocal EnableDelayedExpansion
cd /d "%~dp0"

rem  Builds Dental Flow and produces the copy that goes to the office, in
rem  dist\:
rem
rem      DentalFlow-<version>-windows-x64.zip
rem
rem  Since the move to MSVC the build links the C++ runtime dynamically (/MD),
rem  so the zip carries msvcp140.dll, vcruntime140.dll and vcruntime140_1.dll
rem  beside DentalFlow.exe. Without them the server will not start on a front
rem  desk machine that has never had Visual Studio on it - it dies immediately
rem  with a dialog naming the missing DLL, before printing anything. Copying
rem  them next to the exe is Microsoft's supported "local deployment" and
rem  needs no installer and no admin rights, which is the whole point of this
rem  program: unzip it and double-click it.
rem
rem  data\ is deliberately NOT packaged. config.json holds one office's own
rem  station names and settings; Dental Flow writes a fresh one with the
rem  defaults on first start.

echo.
echo  Packaging Dental Flow...
echo.

rem ---- MSVC environment ------------------------------------------------
rem  Done here rather than leaning on build.bat because we need
rem  VCToolsRedistDir, and build.bat's setlocal throws its environment away
rem  when it returns. With cl.exe already on PATH, build.bat below skips
rem  straight past its own detection.
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

rem ---- the three runtime DLLs -----------------------------------------
rem  The folder is named for the toolset (Microsoft.VC145.CRT today), so match
rem  it with a wildcard instead of hard-coding a number that changes with
rem  every Visual Studio upgrade.
set "CRTDIR="
for /d %%D in ("%VCToolsRedistDir%x64\Microsoft.VC*.CRT") do set "CRTDIR=%%D"
if "%CRTDIR%"=="" (
  echo  ERROR: could not find the Visual C++ redistributable DLLs.
  echo         Looked under: %VCToolsRedistDir%x64\
  echo         Install the "C++ x64/x86 build tools" component in the Visual
  echo         Studio Installer, which is what brings that folder in.
  exit /b 1
)
echo  Runtime DLLs: %CRTDIR%

rem ---- version: src\app.rc is the one place it is declared -------------
set "VERSION="
for /f tokens^=4^ delims^=^" %%V in ('findstr /c:"VALUE \"FileVersion\"" src\app.rc') do set "VERSION=%%V"
if "%VERSION%"=="" (
  echo  ERROR: could not read FileVersion out of src\app.rc
  exit /b 1
)
rem 0.7.0.0 reads better as 0.7.0 in a file name
for /f "tokens=1,2,3 delims=." %%a in ("%VERSION%") do set "SHORTVER=%%a.%%b.%%c"

rem  Pre-release marker, carried in the file name so a beta is never mistaken
rem  for a final build sitting in the same folder. Clear it for a real release.
set "PRERELEASE=beta"
if defined PRERELEASE (set "FULLVER=%SHORTVER%-%PRERELEASE%") else (set "FULLVER=%SHORTVER%")
echo  Version:  %FULLVER%   (from src\app.rc)

rem ---- build ----------------------------------------------------------
call "%~dp0build.bat"
if errorlevel 1 (
  echo  Packaging stopped: the build failed.
  exit /b 1
)

if not exist dist mkdir dist

rem ---- portable zip ---------------------------------------------------
echo.
echo  Building the zip...
set "STAGE=dist\stage"
if exist "%STAGE%" rmdir /s /q "%STAGE%"
mkdir "%STAGE%\DentalFlow"
copy /y "DentalFlow.exe" "%STAGE%\DentalFlow\" >nul
copy /y "README.md"      "%STAGE%\DentalFlow\" >nul
copy /y "LICENSE"        "%STAGE%\DentalFlow\" >nul

rem  public\ is the browser client, served byte for byte as it is. It is not
rem  compiled and not affected by the compiler change, but the server is
rem  useless without it.
xcopy /e /i /q /y "public" "%STAGE%\DentalFlow\public" >nul
if errorlevel 1 (
  echo  ERROR: could not copy public\ into the package.
  exit /b 1
)

rem  The runtime. Copied one by one and checked, because a zip that is missing
rem  one of these looks perfectly fine here - this machine has them in
rem  System32 - and fails only on the machine you sent it to.
for %%L in (msvcp140.dll vcruntime140.dll vcruntime140_1.dll) do (
  if not exist "%CRTDIR%\%%L" (
    echo  ERROR: %%L is not in %CRTDIR%
    exit /b 1
  )
  copy /y "%CRTDIR%\%%L" "%STAGE%\DentalFlow\" >nul
  if errorlevel 1 (
    echo  ERROR: could not copy %%L into the package.
    exit /b 1
  )
  echo    + %%L
)

set "ZIP=dist\DentalFlow-%FULLVER%-windows-x64.zip"
if exist "%ZIP%" del /q "%ZIP%"
powershell -NoProfile -ExecutionPolicy Bypass -Command ^
  "Compress-Archive -Path '%STAGE%\DentalFlow' -DestinationPath '%ZIP%' -Force"
if errorlevel 1 (
  echo  ERROR: could not create the zip.
  exit /b 1
)
rmdir /s /q "%STAGE%"
echo  Built:  %ZIP%

rem ---- source snapshot ------------------------------------------------
echo.
echo  Building the source zip...
set "SRCSTAGE=dist\srcstage\DentalFlow-%FULLVER%"
if exist "dist\srcstage" rmdir /s /q "dist\srcstage"
mkdir "%SRCSTAGE%"
xcopy /e /i /q /y "src"     "%SRCSTAGE%\src"     >nul
xcopy /e /i /q /y "tests"   "%SRCSTAGE%\tests"   >nul
xcopy /e /i /q /y "public"  "%SRCSTAGE%\public"  >nul
xcopy /e /i /q /y ".vscode" "%SRCSTAGE%\.vscode" >nul
for %%F in (README.md LICENSE .gitignore build.bat run_tests.bat package.bat server.js.reference) do (
  if exist "%%F" copy /y "%%F" "%SRCSTAGE%\" >nul
)

set "SRCZIP=dist\DentalFlow-%FULLVER%-source.zip"
if exist "%SRCZIP%" del /q "%SRCZIP%"
powershell -NoProfile -ExecutionPolicy Bypass -Command ^
  "Compress-Archive -Path '%SRCSTAGE%' -DestinationPath '%SRCZIP%' -Force"
if errorlevel 1 (
  echo  ERROR: could not create the source zip.
  exit /b 1
)
rmdir /s /q "dist\srcstage"
echo  Built:  %SRCZIP%

echo.
echo  ---------------------------------------------------------------
dir /b dist\*.zip 2>nul
echo  ---------------------------------------------------------------
echo.
echo  NOT SIGNED. Windows SmartScreen will warn the first person who runs
echo  this. Right-click - Properties - Unblock, or click "More info" then
echo  "Run anyway", is the way through it.
echo.
exit /b 0

:nocompiler
echo  ERROR: no Microsoft C++ compiler found - see build.bat
exit /b 1
