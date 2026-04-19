@echo off
REM Build the sweep test on Windows. Self-contained: sets up vcvars for the
REM host architecture, clones hidapi, builds it, fetches the sweep test source
REM from GitHub, compiles, and copies hidapi.dll next to the resulting .exe.
REM
REM Usage: build_windows.bat
REM   Run from any empty working directory. Requires:
REM     - Visual Studio 2022 Build Tools (C++ workload)
REM     - Git for Windows

setlocal enableextensions

echo [1/6] setting up vcvars for %PROCESSOR_ARCHITECTURE%
set VS_BUILD=C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build
if /i "%PROCESSOR_ARCHITECTURE%"=="ARM64" (
    call "%VS_BUILD%\vcvarsarm64.bat" >nul
) else (
    call "%VS_BUILD%\vcvars64.bat" >nul
)
if errorlevel 1 (echo vcvars failed & exit /b 1)

echo [2/6] cloning hidapi (skip if already present)
if not exist hidapi-upstream (
    git clone -b feature/input-report-buffer-size https://github.com/auxcorelabs/hidapi.git hidapi-upstream
    if errorlevel 1 (echo git clone failed & exit /b 1)
) else (
    echo     hidapi-upstream already exists, skipping clone
)

echo [3/6] configuring hidapi via cmake
pushd hidapi-upstream
if not exist build mkdir build
cd build
cmake .. -DCMAKE_BUILD_TYPE=RelWithDebInfo
if errorlevel 1 (echo cmake configure failed & popd & exit /b 1)

echo [4/6] building hidapi
cmake --build . --config RelWithDebInfo
if errorlevel 1 (echo cmake build failed & popd & exit /b 1)
popd

echo [5/6] fetching sweep source + resolving hidapi lib/dll
curl -sSL -o hidapi_sweep_test.c https://raw.githubusercontent.com/auxcorelabs/hidapi-inputbuffer-sweep-test/main/hidapi_sweep_test.c
if not exist hidapi_sweep_test.c (echo curl failed & exit /b 1)

set HIDLIB=hidapi-upstream\build\src\windows\RelWithDebInfo\hidapi.lib
set HIDDLL=hidapi-upstream\build\src\windows\RelWithDebInfo\hidapi.dll
if not exist "%HIDLIB%" set HIDLIB=hidapi-upstream\build\src\windows\hidapi.lib
if not exist "%HIDDLL%" set HIDDLL=hidapi-upstream\build\src\windows\hidapi.dll
echo     HIDLIB=%HIDLIB%
echo     HIDDLL=%HIDDLL%

echo [6/6] compiling sweep_input_buffers.exe
cl.exe /nologo /I hidapi-upstream\hidapi /Fe:sweep_input_buffers.exe hidapi_sweep_test.c "%HIDLIB%"
if errorlevel 1 (echo cl failed & exit /b 1)
copy /Y "%HIDDLL%" . >nul

echo.
echo === DONE ===
echo Built: sweep_input_buffers.exe (+ hidapi.dll)
echo Run:   sweep_input_buffers.exe 0 1025
