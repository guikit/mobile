@echo off
setlocal
cd /d "%~dp0..\.."

echo ========================================================
echo  GuiKit Mobile - Android SDK / NDK Setup
echo ========================================================

powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0install_android_sdk.ps1" %*
if errorlevel 1 (
    echo [ERROR] Android SDK installation failed!
    exit /b 1
)

endlocal
