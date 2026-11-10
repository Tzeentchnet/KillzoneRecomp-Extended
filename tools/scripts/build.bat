@echo off
setlocal
rem Build the Killzone runner. Usage: build.bat [Debug|RelWithDebInfo]
set "CFG=%~1"
if not defined KZ_JOBS set "KZ_JOBS=8"
if not defined CFG set "CFG=RelWithDebInfo"
set "ROOT=%KZ_ROOT%"
if not defined ROOT for %%I in ("%~dp0..\..") do set "ROOT=%%~fI"
set "BUILD_ROOT=%KZ_BUILD_ROOT%"
if not defined BUILD_ROOT set "BUILD_ROOT=%ROOT%\build"
call "%~dp0vsenv.bat" cmake -S "%ROOT%" -B "%BUILD_ROOT%\%CFG%" -G Ninja -DCMAKE_BUILD_TYPE=%CFG% || exit /b 1
call "%~dp0vsenv.bat" cmake --build "%BUILD_ROOT%\%CFG%" --target killzone -j %KZ_JOBS%
