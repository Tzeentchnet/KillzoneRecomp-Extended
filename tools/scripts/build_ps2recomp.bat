@echo off
setlocal
set "ROOT=%KZ_ROOT%"
if not defined ROOT for %%I in ("%~dp0..\..") do set "ROOT=%%~fI"
set "BUILD_ROOT=%KZ_BUILD_ROOT%"
if not defined BUILD_ROOT set "BUILD_ROOT=%ROOT%\build"
if not defined KZ_JOBS set "KZ_JOBS=8"
set "CMAKE_EXE=%KZ_CMAKE%"
if not defined CMAKE_EXE if exist "%ProgramFiles%\CMake\bin\cmake.exe" set "CMAKE_EXE=%ProgramFiles%\CMake\bin\cmake.exe"
if not defined CMAKE_EXE set "CMAKE_EXE=cmake"
if not defined KZ_NINJA if exist "%ROOT%\tools\dl\ninja\ninja.exe" set "KZ_NINJA=%ROOT%\tools\dl\ninja\ninja.exe"
if defined KZ_NINJA for %%I in ("%KZ_NINJA%") do set "PATH=%%~dpI;%PATH%"
call "%~dp0vsenv.bat" "%CMAKE_EXE%" -S "%ROOT%\ext\PS2Recomp" -B "%BUILD_ROOT%\ps2recomp" -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo -DPS2X_BUILD_STUDIO=OFF -DPS2X_BUILD_TEST=OFF || exit /b 1
call "%~dp0vsenv.bat" "%CMAKE_EXE%" --build "%BUILD_ROOT%\ps2recomp" -j %KZ_JOBS%
