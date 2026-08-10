@echo off
setlocal EnableDelayedExpansion
cd /d "%~dp0"

set "GPP="
where g++ >nul 2>&1 && set "GPP=g++"
if "%GPP%"=="" (
  for %%D in (
    "%LOCALAPPDATA%\Microsoft\WinGet\Packages\BrechtSanders.WinLibs.POSIX.UCRT_Microsoft.Winget.Source_8wekyb3d8bbwe\mingw64\bin"
    "C:\msys64\ucrt64\bin"
    "C:\mingw64\bin"
  ) do (
    if exist "%%~D\g++.exe" ( set "PATH=%%~D;!PATH!" & set "GPP=g++" & goto :found )
  )
)
:found
if "%GPP%"=="" ( echo  ERROR: no C++ compiler found - see build.bat & exit /b 1 )

if not exist build mkdir build

echo  Building the self-test...
g++ -std=c++17 -O1 -DUNICODE -D_UNICODE -D_WIN32_WINNT=0x0A00 -Isrc ^
  tests\selftest.cpp src\routes.cpp src\state.cpp src\config.cpp ^
  src\network.cpp src\http.cpp src\json.cpp src\util.cpp ^
  -o build\selftest.exe ^
  -lws2_32 -liphlpapi -lshlwapi -lole32 -loleaut32 -luuid
if errorlevel 1 ( echo  BUILD FAILED & exit /b 1 )

echo.
build\selftest.exe
exit /b %errorlevel%
