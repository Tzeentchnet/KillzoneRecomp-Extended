@echo off
setlocal
rem Usage: vsenv.bat <command...>  -- runs command inside the latest VS x64 dev environment
set "VCVARS=%KZ_VCVARS64%"
if defined VCVARS goto validate

set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if exist "%VSWHERE%" (
    for /f "usebackq tokens=*" %%I in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VCVARS=%%I\VC\Auxiliary\Build\vcvars64.bat"
)
if defined VCVARS goto validate

for %%R in ("%ProgramFiles%\Microsoft Visual Studio" "%ProgramFiles(x86)%\Microsoft Visual Studio") do (
    if exist "%%~R" for /d %%V in ("%%~R\*") do (
        for /d %%E in ("%%~fV\*") do (
            if exist "%%~fE\VC\Auxiliary\Build\vcvars64.bat" set "VCVARS=%%~fE\VC\Auxiliary\Build\vcvars64.bat"
        )
    )
)

:validate
if not exist "%VCVARS%" (
    echo MSVC x64 Build Tools environment not found. Install the C++ workload or set KZ_VCVARS64 to vcvars64.bat. 1>&2
    exit /b 1
)
call "%VCVARS%" >nul || exit /b 1
%*
set "RESULT=%ERRORLEVEL%"
endlocal & exit /b %RESULT%
