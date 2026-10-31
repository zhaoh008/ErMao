@echo off
setlocal
rem One-click environment setup: check/install deps -> build -> smoke test
rem Usage: setup.bat [-WithVulkan] [-NoBuild]
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0setup.ps1" %*
set "RC=%ERRORLEVEL%"
if not "%RC%"=="0" (
  echo.
  echo [ERROR] Setup did not finish. See setup.log for details.
  pause
)
exit /b %RC%
