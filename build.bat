@echo off
setlocal EnableDelayedExpansion
cd /d "%~dp0"

echo.
echo  Building Dental Flow...
echo.

rem ---- locate a C++ compiler -------------------------------------------
set "GPP="
where g++ >nul 2>&1 && set "GPP=g++"

if "%GPP%"=="" (
  for %%D in (
    "%LOCALAPPDATA%\Microsoft\WinGet\Packages\BrechtSanders.WinLibs.POSIX.UCRT_Microsoft.Winget.Source_8wekyb3d8bbwe\mingw64\bin"
    "%LOCALAPPDATA%\Microsoft\WinGet\Packages\BrechtSanders.WinLibs.POSIX.MSVCRT_Microsoft.Winget.Source_8wekyb3d8bbwe\mingw64\bin"
    "C:\msys64\ucrt64\bin"
    "C:\msys64\mingw64\bin"
    "C:\mingw64\bin"
  ) do (
    if exist "%%~D\g++.exe" (
      set "PATH=%%~D;!PATH!"
      set "GPP=g++"
      goto :found
    )
  )
)
:found

if "%GPP%"=="" (
  echo  ERROR: no C++ compiler found.
  echo.
  echo  Install one with this command, then run build.bat again:
  echo.
  echo      winget install BrechtSanders.WinLibs.POSIX.UCRT
  echo.
  exit /b 1
)

for /f "delims=" %%V in ('g++ --version') do (
  echo  Compiler: %%V
  goto :gotver
)
:gotver

if not exist build mkdir build

set CXXFLAGS=-std=c++17 -O2 -DUNICODE -D_UNICODE -D_WIN32_WINNT=0x0A00 -Wall -Isrc
set LIBS=-lws2_32 -liphlpapi -lshlwapi -lole32 -loleaut32 -luuid

set SOURCES=src\main.cpp src\routes.cpp src\state.cpp src\config.cpp src\network.cpp src\http.cpp src\json.cpp src\util.cpp

echo  Compiling and linking...
g++ %CXXFLAGS% %SOURCES% -o DentalFlow.exe %LIBS%
if errorlevel 1 goto :failed

echo.
echo  Built:  DentalFlow.exe
echo.
echo  Start it by double-clicking DentalFlow.exe, or run:
echo.
echo      DentalFlow.exe
echo.
exit /b 0

:failed
echo.
echo  BUILD FAILED - see the errors above.
exit /b 1
