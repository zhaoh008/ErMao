@echo off
setlocal
if not exist "%~dp0build\bin\ErMao.exe" (
  echo Binary not found, building first...
  call "%~dp0build.bat"
  if errorlevel 1 exit /b 1
)
start "" "%~dp0build\bin\ErMao.exe" %*
endlocal