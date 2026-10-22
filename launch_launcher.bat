@echo off
setlocal
cd /d "%~dp0"

for /f "tokens=1 delims==" %%V in ('set KZ_ 2^>nul') do set "%%V="
for /f "tokens=1 delims==" %%V in ('set PS2X_ 2^>nul') do set "%%V="

set "KZ_EXE=%~dp0build\win-msvc-rel\killzone.exe"
if not exist "%KZ_EXE%" (
    echo Killzone launcher not found:
    echo   %KZ_EXE%
    echo.
    echo Build it first with:
    echo   tools\scripts\cmake_preset.ps1 -Preset win-msvc-rel -Action build -Target killzone
    pause
    exit /b 1
)

start "" "%KZ_EXE%" --launcher
exit /b 0
