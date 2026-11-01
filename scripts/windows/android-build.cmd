@echo off
setlocal
cd /d "%~dp0..\.."

echo ========================================================
echo  GuiKit Mobile - Android NDK Build
echo ========================================================

powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0build_android.ps1" %*
if errorlevel 1 (
    echo [ERROR] Android build failed!
    echo [HINT] If Android SDK/NDK is not installed yet, run: scripts\windows\android-sdk-install.cmd
    exit /b 1
)

endlocal
