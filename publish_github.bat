@echo off
rem ============================================================================
rem  Uploads this project to https://github.com/Tillundmausi/Triforce-Recompiled
rem  Uses your own GitHub login (Git Credential Manager opens a browser window
rem  the first time). Game files, the Steamworks SDK, builds and logs are never
rem  uploaded - see .gitignore.
rem ============================================================================
setlocal EnableExtensions
cd /d "%~dp0"
set "REPO=https://github.com/Tillundmausi/Triforce-Recompiled.git"

set "GIT="
for /f "delims=" %%i in ('where git 2^>nul') do if not defined GIT set "GIT=%%i"
if not defined GIT if exist "%ProgramFiles%\Git\cmd\git.exe" set "GIT=%ProgramFiles%\Git\cmd\git.exe"
if not defined GIT (
  echo Git was not found. Run setup_game.bat once ^(it installs Git^) or install it from git-scm.com.
  pause
  exit /b 1
)

rem Old German docs that were replaced by English ones.
if exist "docs\FEASIBILITY.md" del "docs\FEASIBILITY.md"
if exist "docs\PROGRESS.md" del "docs\PROGRESS.md"

if not exist ".git" (
  "%GIT%" init -b main
  "%GIT%" remote add origin "%REPO%"
)
"%GIT%" remote set-url origin "%REPO%"

rem Commit identity for this repository only (GitHub no-reply address, keeps your e-mail private).
"%GIT%" config user.name >nul 2>nul || "%GIT%" config user.name "Tillundmausi"
"%GIT%" config user.email >nul 2>nul || "%GIT%" config user.email "Tillundmausi@users.noreply.github.com"

echo.
echo === Files that will be uploaded ===
"%GIT%" add -A
"%GIT%" status --short
echo.
echo Check the list above: there must be NO .cia/.3ds files, no steamworks zip, no dist/ folder.
choice /m "Upload now"
if errorlevel 2 exit /b 0

"%GIT%" commit -m "Triforce Recompiled 0.1.0" -m "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>" -m "Claude-Session: https://claude.ai/code/session_015m14Mwjn2YjpRbcygvMXMg"

rem If the GitHub repository already has commits (e.g. a README created on the website),
rem merge them first and keep our version of conflicting files.
"%GIT%" fetch origin >nul 2>nul
"%GIT%" rev-parse --verify --quiet origin/main >nul 2>nul && "%GIT%" merge --allow-unrelated-histories -X ours -m "Merge existing GitHub history" origin/main

"%GIT%" push -u origin main
if errorlevel 1 (
  echo.
  echo === Upload failed ===  Check that you are logged in to GitHub and own the repository.
  pause
  exit /b 1
)
echo.
echo === Done ===  https://github.com/Tillundmausi/Triforce-Recompiled
pause
