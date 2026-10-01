@echo off
setlocal
rem Builds and copies the drop-in layout into the game folder.
rem Usage: deploy.cmd [x86-release|x86-debug]
rem The game folder comes from V2_GAME_DIR, which deploy.local.cmd (git-ignored) may set, e.g.:
rem     set "V2_GAME_DIR=C:\Program Files (x86)\Steam\steamapps\common\Victoria 2"

cd /d "%~dp0"
if exist "%~dp0deploy.local.cmd" call "%~dp0deploy.local.cmd"
if not defined V2_GAME_DIR (
    echo V2_GAME_DIR is not set. Create deploy.local.cmd next to this script or set the variable.
    exit /b 1
)
if not exist "%V2_GAME_DIR%\v2game.exe" (
    echo v2game.exe not found in "%V2_GAME_DIR%".
    exit /b 1
)

call "%~dp0build.cmd" %1 || exit /b 1

echo.
echo Deploying to "%V2_GAME_DIR%"
xcopy /E /Y /I "build\*" "%V2_GAME_DIR%\" >nul || exit /b 1
echo Done.
