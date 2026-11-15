@echo off
rem ============================================================================
rem  Publishes Triforce Recompiled on GitHub in one go:
rem    1. makes sure dist\TriforceRecompiled.exe is up to date (runs setup_game.bat)
rem    2. packs the release zip (packaging\make_release.ps1)
rem    3. uploads the source code to github.com/Tillundmausi/Triforce-Recompiled
rem    4. creates the GitHub release with the zip
rem  Uses your own GitHub login (GitHub CLI opens your browser the first time).
rem  Game files, the Steamworks SDK, builds and logs are never uploaded.
rem ============================================================================
setlocal EnableExtensions
cd /d "%~dp0"
set "VERSION=0.1.0"
set "REPO=Tillundmausi/Triforce-Recompiled"
echo === Triforce Recompiled %VERSION% - GitHub release ===
echo.

rem ---------- 1. build up to date? ----------
powershell -NoProfile -Command "$e='dist\TriforceRecompiled.exe'; if (-not (Test-Path $e)) { exit 1 }; $t=(Get-Item $e).LastWriteTime; $n=(Get-ChildItem game,src,patches -Recurse -File | Measure-Object -Property LastWriteTime -Maximum).Maximum; if ($n -gt $t) { exit 1 }; exit 0"
if not errorlevel 1 goto build_ok
echo [1/4] The game must be rebuilt first - starting setup_game.bat ...
call setup_game.bat
if errorlevel 1 goto fail
:build_ok
echo [1/4] Build is up to date.

rem ---------- 2. package ----------
echo [2/4] Packing the release ...
powershell -NoProfile -ExecutionPolicy Bypass -File "packaging\make_release.ps1" -Version %VERSION%
if errorlevel 1 goto fail

rem ---------- tools: git + GitHub CLI ----------
set "GIT="
for /f "delims=" %%i in ('where git 2^>nul') do if not defined GIT set "GIT=%%i"
if not defined GIT if exist "%ProgramFiles%\Git\cmd\git.exe" set "GIT=%ProgramFiles%\Git\cmd\git.exe"
if not defined GIT goto no_git

set "GH="
for /f "delims=" %%i in ('where gh 2^>nul') do if not defined GH set "GH=%%i"
if not defined GH if exist "%ProgramFiles%\GitHub CLI\gh.exe" set "GH=%ProgramFiles%\GitHub CLI\gh.exe"
if defined GH goto have_gh
echo Installing GitHub CLI via winget ...
winget install --id GitHub.cli -e --source winget --accept-package-agreements --accept-source-agreements --silent
if exist "%ProgramFiles%\GitHub CLI\gh.exe" set "GH=%ProgramFiles%\GitHub CLI\gh.exe"
if not defined GH goto no_gh
:have_gh

"%GH%" auth status >nul 2>nul
if not errorlevel 1 goto have_auth
echo.
echo GitHub login: a code is shown below and your browser opens - paste the code there.
"%GH%" auth login --hostname github.com --git-protocol https --web
"%GH%" auth status >nul 2>nul
if errorlevel 1 goto no_auth
:have_auth
"%GH%" auth setup-git >nul 2>nul

rem Create the repository if it does not exist yet.
"%GH%" repo view %REPO% >nul 2>nul
if not errorlevel 1 goto repo_ok
echo Creating the repository github.com/%REPO% ...
"%GH%" repo create %REPO% --public --description "The Legend of Zelda: Tri Force Heroes on PC - online co-op over Steam"
if errorlevel 1 goto fail
:repo_ok

rem ---------- 3. source code ----------
echo [3/4] Preparing the source upload ...
if exist "docs\FEASIBILITY.md" del "docs\FEASIBILITY.md"
if exist "docs\PROGRESS.md" del "docs\PROGRESS.md"
if not exist ".git" "%GIT%" init -b main
"%GIT%" remote remove origin >nul 2>nul
"%GIT%" remote add origin "https://github.com/%REPO%.git"
"%GIT%" config user.name >nul 2>nul || "%GIT%" config user.name "Tillundmausi"
"%GIT%" config user.email >nul 2>nul || "%GIT%" config user.email "Tillundmausi@users.noreply.github.com"
"%GIT%" add -A
if errorlevel 1 goto fail

rem Safety net: refuse to upload game files, SDKs or binaries.
"%GIT%" ls-files > "%TEMP%\tfh_files.txt"
findstr /i /r "\.cia \.3ds \.cci \.cxi \.exe \.dll \.pdb \.zip steamworks_sdk" "%TEMP%\tfh_files.txt" > "%TEMP%\tfh_bad.txt"
if not errorlevel 1 goto unsafe
del "%TEMP%\tfh_files.txt" "%TEMP%\tfh_bad.txt" >nul 2>nul

echo.
echo === Changes that will be uploaded ===
"%GIT%" status --short
echo.
echo Release files:
dir /b release
echo.
choice /m "Upload the source code and publish release v%VERSION% on GitHub"
if errorlevel 2 goto cancelled

"%GIT%" diff --cached --quiet
if not errorlevel 1 goto committed
"%GIT%" commit -q -m "Triforce Recompiled %VERSION%" -m "Launcher with setup assistant, Steam online co-op, Discord Rich Presence." -m "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>" -m "Claude-Session: https://claude.ai/code/session_015m14Mwjn2YjpRbcygvMXMg"
if errorlevel 1 goto fail
:committed
rem Keep commits that already exist on GitHub (e.g. a README created on the website).
"%GIT%" fetch origin >nul 2>nul
"%GIT%" rev-parse --verify --quiet origin/main >nul 2>nul
if errorlevel 1 goto push
"%GIT%" merge --allow-unrelated-histories -X ours -m "Merge existing GitHub history" origin/main
if errorlevel 1 goto fail
:push
"%GIT%" push -u origin main
if errorlevel 1 goto fail

rem ---------- 4. release ----------
echo [4/4] Publishing release v%VERSION% ...
set "ASSETS=release\TriforceRecompiled-v%VERSION%-windows-x64.zip release\SHA256SUMS.txt"
if exist "release\TriforceRecompiled-v%VERSION%-symbols.zip" set "ASSETS=%ASSETS% release\TriforceRecompiled-v%VERSION%-symbols.zip"
"%GH%" release view v%VERSION% --repo %REPO% >nul 2>nul
if not errorlevel 1 goto update_release
"%GH%" release create v%VERSION% %ASSETS% --repo %REPO% --target main --title "Triforce Recompiled v%VERSION%" --notes-file "docs\release-notes-v%VERSION%.md" --latest
if errorlevel 1 goto fail
goto done
:update_release
echo Release v%VERSION% already exists - replacing its files.
"%GH%" release upload v%VERSION% %ASSETS% --repo %REPO% --clobber
if errorlevel 1 goto fail

:done
echo.
echo === DONE ===  https://github.com/%REPO%/releases/tag/v%VERSION%
start "" "https://github.com/%REPO%/releases/tag/v%VERSION%"
pause
exit /b 0

:unsafe
echo.
echo === STOPPED ===  These files must not be uploaded:
type "%TEMP%\tfh_bad.txt"
"%GIT%" reset -q
pause
exit /b 1
:no_git
echo Git was not found. Run setup_game.bat once - it installs Git.
pause
exit /b 1
:no_gh
echo GitHub CLI could not be installed. Get it from https://cli.github.com and run this again.
pause
exit /b 1
:no_auth
echo GitHub login did not complete. Run this file again.
pause
exit /b 1
:cancelled
"%GIT%" reset -q
echo Cancelled - nothing was uploaded.
pause
exit /b 0
:fail
echo.
echo === FAILED ===  See the messages above.
pause
exit /b 1
