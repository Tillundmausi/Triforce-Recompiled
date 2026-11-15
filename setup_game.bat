@echo off
rem ============================================================================
rem  Triforce Recompiled - fetches Azahar (pinned version), hooks in our frontend
rem  and builds dist\TriforceRecompiled.exe. Double-click friendly.
rem  First run: ~500 MB download + 15-40 minutes build. Log: setup_game.log
rem ============================================================================
setlocal EnableExtensions
cd /d "%~dp0"
echo Setting up Triforce Recompiled ... (log: setup_game.log)
echo The first run takes 15-40 minutes. Please keep this window open.
call :main > setup_game.log 2>&1
set "RC=%errorlevel%"
type setup_game.log
echo.
if "%RC%"=="0" (
  echo === DONE ===  Now start dist\TriforceRecompiled.exe
) else (
  echo === FAILED ===  See setup_game.log for details
)
pause
exit /b %RC%

:main
set "AZAHAR_COMMIT=11eceda5e0e682a1f44e396bf954a1b9137097ca"
set "IMGUI_TAG=v1.92.9"
set "AZ=external\azahar"
echo [setup] Folder: %CD%

rem ---------- git ----------
set "GIT="
for /f "delims=" %%i in ('where git 2^>nul') do if not defined GIT set "GIT=%%i"
if defined GIT goto have_git
if exist "%ProgramFiles%\Git\cmd\git.exe" set "GIT=%ProgramFiles%\Git\cmd\git.exe"
if defined GIT goto have_git
echo [setup] Git is missing - installing Git via winget ...
winget install --id Git.Git -e --source winget --accept-package-agreements --accept-source-agreements --silent
if exist "%ProgramFiles%\Git\cmd\git.exe" set "GIT=%ProgramFiles%\Git\cmd\git.exe"
if not defined GIT goto no_git
:have_git
echo [setup] Git: %GIT%

rem ---------- Python (needed by Azahar/SPIRV-Tools at configure time) ----------
call :find_python
if defined PY goto have_py
echo [setup] Python is missing - installing Python 3.12 via winget ...
winget install --id Python.Python.3.12 -e --source winget --scope user --accept-package-agreements --accept-source-agreements --silent
call :find_python
if not defined PY goto no_python
:have_py
echo [setup] Python: %PY%

rem ---------- Fetch Azahar ----------
if exist "%AZ%\.git" goto have_repo
echo [setup] Downloading Azahar ...
"%GIT%" clone --filter=blob:none https://github.com/azahar-emu/azahar "%AZ%"
if errorlevel 1 exit /b 1
:have_repo
rem Only check out if not already on the pinned commit (otherwise everything is rebuilt).
"%GIT%" -C "%AZ%" rev-parse HEAD > "%TEMP%\tfh_head.txt"
set "HEADREV="
set /p HEADREV=<"%TEMP%\tfh_head.txt"
del "%TEMP%\tfh_head.txt" >nul 2>nul
if "%HEADREV%"=="%AZAHAR_COMMIT%" goto have_commit
"%GIT%" -C "%AZ%" checkout --force %AZAHAR_COMMIT%
if errorlevel 1 exit /b 1
:have_commit
echo [setup] Downloading Azahar dependencies (submodules) ...
"%GIT%" -C "%AZ%" submodule update --init --recursive --depth 1 --jobs 8
if errorlevel 1 exit /b 1

rem ---------- Hook our frontend into Azahar ----------
findstr /C:"TFH_GAME_DIR" "%AZ%\src\CMakeLists.txt" >nul
if not errorlevel 1 goto hooked
echo [setup] Hooking the Triforce Recompiled frontend into Azahar ...
>> "%AZ%\src\CMakeLists.txt" echo.
>> "%AZ%\src\CMakeLists.txt" echo # --- Triforce Recompiled (added by setup_game.bat) ---
>> "%AZ%\src\CMakeLists.txt" echo if (TFH_GAME_DIR)
>> "%AZ%\src\CMakeLists.txt" echo     add_subdirectory(${TFH_GAME_DIR} tfh_game)
>> "%AZ%\src\CMakeLists.txt" echo endif()
:hooked

rem ---------- Azahar patches (Steam transport for local wireless play) ----------
for %%P in ("%CD%\patches\*.patch") do call :apply_patch "%%~fP"
if errorlevel 1 exit /b 1

rem ---------- Dear ImGui (launcher UI) ----------
if exist "external\imgui\imgui.cpp" goto have_imgui
echo [setup] Downloading Dear ImGui %IMGUI_TAG% ...
"%GIT%" clone --depth 1 --branch %IMGUI_TAG% https://github.com/ocornut/imgui "external\imgui"
if errorlevel 1 exit /b 1
:have_imgui

rem ---------- Steamworks SDK (optional, for online play) ----------
set "STEAMDIR="
if exist "external\steamworks\sdk\public\steam\steam_api.h" goto have_steam
set "STEAMZIP="
for %%Z in (steamworks_sdk*.zip) do set "STEAMZIP=%%Z"
if not defined STEAMZIP goto no_steam
echo [setup] Extracting Steamworks SDK (%STEAMZIP%) ...
powershell -NoProfile -Command "Expand-Archive -Force -LiteralPath '%STEAMZIP%' -DestinationPath 'external\steamworks'"
if not exist "external\steamworks\sdk\public\steam\steam_api.h" goto no_steam
:have_steam
set "STEAMDIR=%CD:\=/%/external/steamworks/sdk"
echo [setup] Steamworks SDK: %STEAMDIR%
goto steam_done
:no_steam
echo [setup] NOTE: No Steamworks SDK found - building without online play.
:steam_done

rem ---------- Find CMake ----------
set "CMAKE="
for /f "delims=" %%i in ('where cmake 2^>nul') do if not defined CMAKE set "CMAKE=%%i"
if defined CMAKE goto have_cmake
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" goto no_vs
set "VSTMP=%TEMP%\tfh_vspath.txt"
"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath > "%VSTMP%"
set "VSPATH="
set /p VSPATH=<"%VSTMP%"
del "%VSTMP%" >nul 2>nul
if not defined VSPATH goto no_cpp
set "CMAKE=%VSPATH%\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
if not exist "%CMAKE%" goto no_cpp
:have_cmake
echo [setup] CMake: %CMAKE%

rem ---------- Configure + build ----------
set "GAMEDIR=%CD:\=/%/game"
set "IMGUIDIR=%CD:\=/%/external/imgui"
"%CMAKE%" -S "%AZ%" -B build-game -A x64 ^
  -DTFH_GAME_DIR="%GAMEDIR%" -DTFH_IMGUI_DIR="%IMGUIDIR%" -DTFH_STEAMWORKS_DIR="%STEAMDIR%" ^
  -DPython3_EXECUTABLE="%PY%" -DSDL_RENDER=ON -DSDL_FILESYSTEM=ON ^
  -DENABLE_QT=OFF -DENABLE_TESTS=OFF -DENABLE_ROOM=OFF -DENABLE_WEB_SERVICE=OFF ^
  -DENABLE_SCRIPTING=OFF -DENABLE_GDBSTUB=OFF -DENABLE_LIBUSB=OFF -DENABLE_DISCORD_RPC=OFF ^
  -DENABLE_BUILTIN_KEYBLOB=OFF -DENABLE_SOFTWARE_RENDERER=OFF -DCITRA_WARNINGS_AS_ERRORS=OFF ^
  -DCMAKE_C_FLAGS_RELEASE="/O2 /Ob2 /DNDEBUG /Z7" -DCMAKE_CXX_FLAGS_RELEASE="/O2 /Ob2 /DNDEBUG /Z7" ^
  -DCMAKE_EXE_LINKER_FLAGS_RELEASE="/DEBUG /OPT:REF /OPT:ICF"
if errorlevel 1 exit /b 1
"%CMAKE%" --build build-game --config Release --target tfh_game -- /m
if errorlevel 1 exit /b 1

if not exist dist mkdir dist
copy /y "build-game\bin\Release\TriforceRecompiled.exe" "dist\TriforceRecompiled.exe"
if errorlevel 1 exit /b 1
copy /y "build-game\bin\Release\*.dll" "dist\" >nul 2>nul
copy /y "build-game\bin\Release\TriforceRecompiled.pdb" "dist\TriforceRecompiled.pdb" >nul 2>nul
if exist "build-game\bin\Release\steam_api64.dll" copy /y "build-game\bin\Release\steam_api64.dll" "dist\" >nul
echo [setup] Done: %CD%\dist\TriforceRecompiled.exe
exit /b 0

:apply_patch
rem Applies a patch unless it is already applied.
"%GIT%" -C "%AZ%" apply --ignore-whitespace --reverse --check "%~1" >nul 2>nul
if not errorlevel 1 exit /b 0
echo [setup] Applying patch: %~nx1
"%GIT%" -C "%AZ%" apply --ignore-whitespace "%~1"
if errorlevel 1 echo [setup] ERROR: patch %~nx1 does not apply to Azahar.
exit /b %errorlevel%

:find_python
rem Deliberately not "where python": that is often just the Microsoft Store placeholder.
set "PY="
for /d %%d in ("%LOCALAPPDATA%\Programs\Python\Python3*") do if exist "%%d\python.exe" set "PY=%%d\python.exe"
if defined PY exit /b 0
for /d %%d in ("%ProgramFiles%\Python3*") do if exist "%%d\python.exe" set "PY=%%d\python.exe"
if defined PY exit /b 0
exit /b 0

:no_python
echo [setup] ERROR: Python could not be installed. Please install Python 3 from python.org.
exit /b 1
:no_git
echo [setup] ERROR: Git could not be installed. Please install it from git-scm.com.
exit /b 1
:no_vs
echo [setup] ERROR: Visual Studio not found. Run install_cpp_tools.bat first.
exit /b 1
:no_cpp
echo [setup] ERROR: Visual Studio has no C++ compiler. Run install_cpp_tools.bat first.
exit /b 1
