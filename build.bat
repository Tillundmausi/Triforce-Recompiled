@echo off
rem Builds ctrinspect + unit tests. Double-click friendly: finds CMake/Visual Studio itself.
rem Everything is also written to build.log.
setlocal EnableExtensions
cd /d "%~dp0"
echo Building Triforce Recompiled tools ... (this can take 1-2 minutes)
call :main > build.log 2>&1
set "RC=%errorlevel%"
type build.log
echo.
if "%RC%"=="0" (
  echo === BUILD OK ===  Next: inspect.bat
) else (
  echo === BUILD FAILED ===  See build.log for details
)
if not defined TFH_NOPAUSE pause
exit /b %RC%

:main
echo [build] Folder: %CD%
set "CMAKE="
for /f "delims=" %%i in ('where cmake 2^>nul') do if not defined CMAKE set "CMAKE=%%i"
if defined CMAKE goto have_cmake

set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" goto no_vs
set "VSPATH="
set "VSTMP=%TEMP%\tfh_vspath.txt"
"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath > "%VSTMP%"
set /p VSPATH=<"%VSTMP%"
del "%VSTMP%" >nul 2>nul
if not defined VSPATH goto no_cpp
echo [build] Visual Studio: %VSPATH%
set "CMAKE=%VSPATH%\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
if not exist "%CMAKE%" goto no_cmake

:have_cmake
echo [build] CMake: %CMAKE%
"%CMAKE%" -S . -B build -A x64
if errorlevel 1 exit /b 1
"%CMAKE%" --build build --config Release
if errorlevel 1 exit /b 1
echo.
echo [build] === Tests ===
build\Release\tfh_tests.exe
if errorlevel 1 exit /b 1
exit /b 0

:no_vs
echo [build] ERROR: Visual Studio Installer not found (vswhere.exe missing).
echo         Install Visual Studio 2022 Community with "Desktop development with C++".
exit /b 1
:no_cpp
echo [build] ERROR: Visual Studio found, but without the C++ compiler.
echo         Run install_cpp_tools.bat or add "Desktop development with C++" in the Visual Studio Installer.
exit /b 1
:no_cmake
echo [build] ERROR: Visual Studio is missing "C++ CMake tools for Windows".
echo         Visual Studio Installer - Modify - Individual components - "C++ CMake tools for Windows".
exit /b 1
