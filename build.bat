@echo off
setlocal
rem ErMao Engine build script. Usage: build.bat [Release|Debug]
rem If dependencies are missing, run setup.bat first.
set CONFIG=%1
if "%CONFIG%"=="" set CONFIG=Release
set "ROOT=%~dp0"

set "VSDIR="
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if exist "%VSWHERE%" (
  for /f "usebackq tokens=*" %%i in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSDIR=%%i"
)
if not defined VSDIR for %%e in (Enterprise Professional Community BuildTools) do (
  if not defined VSDIR if exist "%ProgramFiles%\Microsoft Visual Studio\2022\%%e\VC\Auxiliary\Build\vcvars64.bat" set "VSDIR=%ProgramFiles%\Microsoft Visual Studio\2022\%%e"
  if not defined VSDIR if exist "%ProgramFiles(x86)%\Microsoft Visual Studio\2022\%%e\VC\Auxiliary\Build\vcvars64.bat" set "VSDIR=%ProgramFiles(x86)%\Microsoft Visual Studio\2022\%%e"
)
if not defined VSDIR (
  echo [ERROR] Visual Studio 2022 with C++ workload not found.
  echo         Run setup.bat first to check/install dependencies.
  exit /b 1
)
echo [1/3] VS: %VSDIR%
call "%VSDIR%\VC\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 exit /b 1

set "CMAKE="
for /f "delims=" %%i in ('where cmake.exe 2^>nul') do if not defined CMAKE set "CMAKE=%%i"
if not defined CMAKE if exist "%VSDIR%\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe" set "CMAKE=%VSDIR%\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
if not defined CMAKE if exist "%ROOT%.tools\cmake\bin\cmake.exe" set "CMAKE=%ROOT%.tools\cmake\bin\cmake.exe"
if not defined CMAKE (
  echo [ERROR] CMake not found.
  echo         Run setup.bat first to install CMake.
  exit /b 1
)

echo [2/3] CMake configure
"%CMAKE%" -S "%ROOT%." -B "%ROOT%build" -G "Visual Studio 17 2022" -A x64
if errorlevel 1 (
  echo [ERROR] CMake configure failed. Run setup.bat to check dependencies.
  exit /b 1
)

echo [3/3] Build %CONFIG%
"%CMAKE%" --build "%ROOT%build" --config %CONFIG% -- /m /nologo /v:m
if errorlevel 1 (
  echo [ERROR] Build failed. Run setup.bat to check dependencies.
  exit /b 1
)
echo.
echo Build OK: %ROOT%build\bin\ErMao.exe
endlocal
