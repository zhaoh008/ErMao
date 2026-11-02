@echo off
setlocal
rem Double-click to run. Builds first if the exe is missing.
set "ROOT=%~dp0"
set "EXE=%ROOT%build\bin\ErMao.exe"

if not exist "%EXE%" (
  echo [run] Binary not found, building...
  echo [run] On a new machine, run setup.bat first to install dependencies.
  call "%ROOT%build.bat"
  if errorlevel 1 (
    echo.
    echo [ERROR] Build failed. Please run setup.bat first.
    pause
    exit /b 1
  )
)

if not exist "%EXE%" (
  echo [ERROR] Still cannot find %EXE%
  pause
  exit /b 1
)

start "" "%EXE%" %*
endlocal
