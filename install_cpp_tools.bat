@echo off
rem Adds the C++ parts to your existing Visual Studio:
rem   workload "Desktop development with C++" + "C++ CMake tools for Windows"
rem Windows asks for admin rights once (click Yes). Download about 2-6 GB.
setlocal EnableExtensions
cd /d "%~dp0"

set "VSI=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer"
if not exist "%VSI%\vswhere.exe" goto no_vs

set "VSTMP=%TEMP%\tfh_vspath.txt"
"%VSI%\vswhere.exe" -latest -products * -property installationPath > "%VSTMP%"
set "VSPATH="
set /p VSPATH=<"%VSTMP%"
del "%VSTMP%" >nul 2>nul
if not defined VSPATH goto no_vs

echo Visual Studio found: %VSPATH%
echo.
echo Installing "Desktop development with C++" + CMake tools.
echo Windows will ask for admin rights - please click "Yes".
echo In the installer window click "Modify" (bottom right). This can take a while.
echo.
"%VSI%\setup.exe" modify --installPath "%VSPATH%" --add Microsoft.VisualStudio.Workload.NativeDesktop --add Microsoft.VisualStudio.Component.VC.CMake.Project --includeRecommended --norestart
set "RC=%errorlevel%"
echo.
if "%RC%"=="0" goto ok
if "%RC%"=="3010" goto ok
echo The installer returned code %RC%. If a window is still open, check it.
pause
exit /b %RC%

:ok
echo === Done. Next: double-click setup_game.bat ===
pause
exit /b 0

:no_vs
echo Visual Studio Installer not found.
echo Download "Visual Studio 2022 Community" from visualstudio.microsoft.com and select
echo "Desktop development with C++" during installation.
pause
exit /b 1
