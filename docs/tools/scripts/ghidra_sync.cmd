@echo off
setlocal
rem Keeps the shared Ghidra server program in step with docs/re/symbols (docs/re/ghidra.md, "Ghidra Server").
rem Usage: ghidra_sync.cmd check|export|import [commit comment]
rem   check    names in the CSVs vs the server (read-only)
rem   export   names added on the server that the CSVs lack, as CSV rows (read-only)
rem   import   applies the CSVs to the server program and commits a new version
rem Settings come from the environment, or from ghidra_sync.local.cmd next to this file (git-ignored):
rem   GHIDRA_HOME    Ghidra install (default C:\Program Files (x86)\ghidra_12.1.4_PUBLIC)
rem   JAVA_HOME      JDK 21
rem   GHIDRA_URL     ghidra://host:port/Repository (no spaces in the repository name)
rem   GHIDRA_USER    server user
rem   GHIDRA_PW      password; when unset, analyzeHeadless asks for it
rem Output: %TEMP%\ghidra_sync\V2Symbols_<cmd>.txt

if exist "%~dp0ghidra_sync.local.cmd" call "%~dp0ghidra_sync.local.cmd"
if not defined GHIDRA_HOME set "GHIDRA_HOME=C:\Program Files (x86)\ghidra_12.1.4_PUBLIC"
if not defined GHIDRA_URL (
    echo GHIDRA_URL is not set. Create ghidra_sync.local.cmd next to this script or set the variable.
    exit /b 1
)
if not defined GHIDRA_USER (
    echo GHIDRA_USER is not set.
    exit /b 1
)
if defined JAVA_HOME set "PATH=%JAVA_HOME%\bin;%PATH%"

set "CMD=%~1"
set "REPO=%~dp0..\..\.."
set "OUT=%TEMP%\ghidra_sync"
if not exist "%OUT%" mkdir "%OUT%"

if /i "%CMD%"=="import" (
    for /f %%s in ('git -C "%REPO%" rev-parse --short HEAD') do set "SHA=%%s"
) else if /i not "%CMD%"=="check" if /i not "%CMD%"=="export" (
    echo Usage: ghidra_sync.cmd check^|export^|import [commit comment]
    exit /b 1
)
set "COMMENT=%~2"
if /i "%CMD%"=="import" if not defined COMMENT set "COMMENT=docs/re/symbols @ %SHA%"

set HEADLESS="%GHIDRA_HOME%\support\analyzeHeadless.bat" "%GHIDRA_URL%" -connect %GHIDRA_USER% -p ^
    -process v2game.exe -noanalysis -scriptPath "%REPO%\docs\tools\ghidra" ^
    -postScript V2Symbols.java "%REPO%\docs\re\symbols" "%OUT%" %CMD%

if /i "%CMD%"=="import" (
    if defined GHIDRA_PW (echo %GHIDRA_PW%| %HEADLESS% -commit "%COMMENT%") else (%HEADLESS% -commit "%COMMENT%")
) else (
    if defined GHIDRA_PW (echo %GHIDRA_PW%| %HEADLESS% -readOnly) else (%HEADLESS% -readOnly)
)
echo Result: %OUT%\V2Symbols_%CMD%.txt
