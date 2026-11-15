@echo off
rem Checks the game dump in this folder. Builds ctrinspect first if needed.
rem Output also goes to inspect.log, the result to game_data\report.json.
setlocal EnableExtensions
cd /d "%~dp0"

if not exist build\Release\ctrinspect.exe (
  echo ctrinspect.exe is missing - building it first ...
  set "TFH_NOPAUSE=1"
  call build.bat
  set "TFH_NOPAUSE="
)
if not exist build\Release\ctrinspect.exe (
  echo Build failed - see build.log
  pause
  exit /b 1
)

set "DUMP=%~1"
if "%DUMP%"=="" for %%F in (*.cia *.3ds *.cci *.cxi) do if not defined DUMP set "DUMP=%%F"
if "%DUMP%"=="" (
  echo No dump found. Put the .cia into this folder or drag it onto inspect.bat.
  pause
  exit /b 1
)

echo Checking: %DUMP%
echo.
build\Release\ctrinspect.exe "%DUMP%" --out game_data > inspect.log 2>&1
set "RC=%errorlevel%"
type inspect.log
echo.
if "%RC%"=="0" echo RESULT: complete - executable extracted.
if "%RC%"=="1" echo RESULT: error - see inspect.log
if "%RC%"=="2" echo RESULT: encrypted - decrypt it on your own console with GodMode9.
if "%RC%"=="3" echo RESULT: hash mismatch - dump damaged or decrypted incorrectly.
pause
exit /b %RC%
