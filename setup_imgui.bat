@REM Download ImGui v1.92.7 sources required to build this addon.
@REM Must match the ImGui version used by the installed arcdps version.
@echo off
setlocal enabledelayedexpansion

set IMGVER=v1.92.7
set BASE=https://raw.githubusercontent.com/ocornut/imgui/%IMGVER%

if not exist imgui mkdir imgui
if not exist imgui\backends mkdir imgui\backends
if errorlevel 1 (
    echo ERROR: Could not create imgui directories.
    exit /b 1
)

call :download imgui.h
call :download imgui.cpp
call :download imgui_demo.cpp
call :download imgui_draw.cpp
call :download imgui_internal.h
call :download imgui_tables.cpp
call :download imgui_widgets.cpp
call :download imconfig.h
call :download imstb_rectpack.h
call :download imstb_textedit.h
call :download imstb_truetype.h
call :download backends/imgui_impl_dx11.cpp
call :download backends/imgui_impl_dx11.h
call :download backends/imgui_impl_win32.cpp
call :download backends/imgui_impl_win32.h

echo ImGui %IMGVER% downloaded.
goto :eof

:download
echo Downloading %1 ...
powershell -Command "Invoke-WebRequest -Uri '%BASE%/%1' -OutFile 'imgui/%1'" >nul 2>&1
if errorlevel 1 (
    echo ERROR: Failed to download %1
    exit /b 1
)
if not exist "imgui/%1" (
    echo ERROR: Downloaded file imgui/%1 is missing.
    exit /b 1
)
goto :eof
