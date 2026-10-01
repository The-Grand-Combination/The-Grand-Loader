@echo off
setlocal
rem Builds the drop-in layout into build\ from any shell. Usage: build.cmd [x86-release|x86-debug]
set PRESET=%~1
if "%PRESET%"=="" set PRESET=x86-release

for /f "usebackq tokens=*" %%i in (`"%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" -latest -products * -property installationPath`) do set VSDIR=%%i
if not defined VSDIR (
    echo Visual Studio Build Tools not found.
    exit /b 1
)

call "%VSDIR%\VC\Auxiliary\Build\vcvarsall.bat" x86 >nul || exit /b 1
cd /d "%~dp0"
cmake --preset %PRESET% || exit /b 1
cmake --build --preset %PRESET%
