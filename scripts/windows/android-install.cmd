@echo off
setlocal
echo ========================================================
echo  GuiKit Mobile - Install and Run Demo APK via ADB
echo ========================================================
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0deploy_android.ps1" %*
exit /b %ERRORLEVEL%
