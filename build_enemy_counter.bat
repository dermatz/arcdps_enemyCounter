@echo off
@REM Build Enemy Counter for Visual Studio compiler (x64).
@REM Tries common vcvars64.bat locations (local dev, VS Build Tools, GitHub Actions).
setlocal enabledelayedexpansion

if exist "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" (
    call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" >nul 2>&1
) else if exist "C:\Program Files\Microsoft Visual Studio\2022\Enterprise\VC\Auxiliary\Build\vcvars64.bat" (
    call "C:\Program Files\Microsoft Visual Studio\2022\Enterprise\VC\Auxiliary\Build\vcvars64.bat" >nul 2>&1
) else if exist "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" (
    call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul 2>&1
) else if exist "C:\Program Files\Microsoft Visual Studio\2022\Professional\VC\Auxiliary\Build\vcvars64.bat" (
    call "C:\Program Files\Microsoft Visual Studio\2022\Professional\VC\Auxiliary\Build\vcvars64.bat" >nul 2>&1
) else if exist "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" (
    call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul 2>&1
) else (
    echo ERROR: Could not find vcvars64.bat. Please run this script from a Visual Studio x64 developer prompt.
    exit /b 1
)

if "%WindowsSdkDir%"=="" (
    echo ERROR: WindowsSdkDir is not set. SDK environment was not loaded correctly.
    exit /b 1
)

set OUT_DIR=out
set OUT_EXE=arcdps_enemy_counter.dll
set INCLUDES=/I.\imgui /I.\imgui\backends /I "%WindowsSdkDir%Include\um" /I "%WindowsSdkDir%Include\shared"
set SOURCES=enemy_counter.cpp .\imgui\backends\imgui_impl_dx11.cpp .\imgui\backends\imgui_impl_win32.cpp .\imgui\imgui*.cpp
set LIBS=/LIBPATH:"%WindowsSdkDir%Lib\um\x64" d3d11.lib d3dcompiler.lib
if not exist %OUT_DIR% mkdir %OUT_DIR%
cl /LD /EHsc /O2 %INCLUDES% /D UNICODE /D _UNICODE %SOURCES% /Fe%OUT_DIR%/%OUT_EXE% /Fo%OUT_DIR%/ /link %LIBS%
if errorlevel 1 (
    echo ERROR: Build failed.
    exit /b 1
)
