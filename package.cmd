@echo off
setlocal
rem Builds the release configuration and writes one zip per release unit into out\release\:
rem   the-grand-loader-<version>.zip, performance-patch-<version>.zip, the-grand-loader-sdk-<version>.zip
call "%~dp0build.cmd" x86-release || exit /b 1
cd /d "%~dp0"
for /f "usebackq tokens=*" %%i in (`"%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" -latest -products * -property installationPath`) do set VSDIR=%%i
call "%VSDIR%\VC\Auxiliary\Build\vcvarsall.bat" x86 >nul || exit /b 1
if exist out\release rmdir /s /q out\release
cpack --preset x86-release || exit /b 1
dir /b out\release\*.zip
