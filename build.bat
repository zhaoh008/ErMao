@echo off
setlocal
rem ErMao Engine build script. Usage: build.bat [Release|Debug]
set CONFIG=%1
if "%CONFIG%"=="" set CONFIG=Release

set "VSDIR="
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if exist "%VSWHERE%" (
  for /f "usebackq tokens=*" %%i in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSDIR=%%i"
)
if not defined VSDIR if exist "d:\Program Files\Microsoft Visual Studio\2022\Enterprise\VC\Auxiliary\Build\vcvars64.bat" set "VSDIR=d:\Program Files\Microsoft Visual Studio\2022\Enterprise"
if not defined VSDIR (
  echo [ERROR] Visual Studio 2022 with C++ workload not found.
  exit /b 1
)
echo [1/3] VS: %VSDIR%
call "%VSDIR%\VC\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 exit /b 1

echo [2/3] CMake configure...
cmake -S "%~dp0." -B "%~dp0build" -G "Visual Studio 17 2022" -A x64
if errorlevel 1 exit /b 1

echo [3/3] Build %CONFIG%...
cmake --build "%~dp0build" --config %CONFIG% -- /m /nologo /v:m
if errorlevel 1 exit /b 1
echo.
echo Build OK: %~dp0build\bin\ErMao.exe
endlocal