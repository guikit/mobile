@echo off
setlocal
cd /d "%~dp0..\.."

set CONFIG=Release
if /I "%~1"=="Debug" set CONFIG=Debug

call "%~dp0desktop-build.cmd" %CONFIG%
if errorlevel 1 exit /b 1

echo [INFO] Launching GuiKit Mobile Workstation Simulator...
start "" "build\%CONFIG%\guikit_mobile_demo.exe"
endlocal
