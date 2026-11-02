@echo off
setlocal EnableExtensions
rem Run ErMao. Default = release version (Release build, no console, no log file).
rem   run.bat                     release: no CMD window, no log
rem   run.bat dev                 development: Debug build + console log
rem   run.bat dev --level arena   development + extra game arguments
rem   run-dev.bat                 same as run.bat dev
rem The first run (or switching version) builds automatically.

set "ROOT=%~dp0"
set "CONFIG=Release"
set "DEVBUILD="
set "GAMEARGS="

:parse
if "%~1"=="" goto :ready
if /I "%~1"=="dev"     (set "CONFIG=Debug"   & set "DEVBUILD=1" & shift & goto :parse)
if /I "%~1"=="debug"   (set "CONFIG=Debug"   & set "DEVBUILD=1" & shift & goto :parse)
if /I "%~1"=="release" (set "CONFIG=Release" & set "DEVBUILD="  & shift & goto :parse)
set "GAMEARGS=%GAMEARGS% %1"
shift
goto :parse

:ready
set "EXE=%ROOT%build\bin\ErMao.exe"
set "MARKER=%ROOT%build\.ermao-config"
set "NEED_BUILD="
set "BUILT="
if exist "%MARKER%" set /p BUILT=<"%MARKER%"
if not exist "%EXE%" set "NEED_BUILD=1"
if not exist "%MARKER%" set "NEED_BUILD=1"
if exist "%MARKER%" if /I not "%BUILT%"=="%CONFIG%" set "NEED_BUILD=1"

if defined NEED_BUILD (
  echo [run] Building %CONFIG% - first run or version switch...
  echo [run] On a new machine, run setup.bat first.
  call "%ROOT%build.bat" %CONFIG%
  if errorlevel 1 (
    echo.
    echo [ERROR] Build failed. Please run setup.bat first.
    pause
    exit /b 1
  )
)

if not exist "%EXE%" (
  echo [ERROR] Cannot find %EXE%
  pause
  exit /b 1
)

if defined DEVBUILD (
  rem Development: keep this console open while the game runs.
  echo [run] Development build: Debug + console log. The window closes when the game exits.
  start "" /wait "%EXE%" --dev %GAMEARGS%
  set "RC=%ERRORLEVEL%"
  if not "%RC%"=="0" (
    echo.
    echo [ERROR] Game exited with code %RC%
    pause
  )
  exit /b %RC%
)

rem Release: detach and exit this batch file, so only the game window remains.
start "" "%EXE%" %GAMEARGS%
exit /b 0
