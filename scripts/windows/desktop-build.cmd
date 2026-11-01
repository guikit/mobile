@echo off
setlocal
cd /d "%~dp0..\.."

set CONFIG=Release
if /I "%~1"=="Debug" set CONFIG=Debug

echo ========================================================
echo  GuiKit Mobile Simulator Desktop Build (%CONFIG%)
echo ========================================================

taskkill /F /IM guikit_mobile_demo.exe >nul 2>&1

if not exist "build\CMakeCache.txt" (
    echo [INFO] Configuring CMake build directory...
    cmake -B build -S .
    if errorlevel 1 (
        echo [ERROR] CMake configuration failed!
        exit /b 1
    )
)

echo [INFO] Building guikit_mobile_demo and test_mobile (%CONFIG%)...
cmake --build build --config %CONFIG%
if errorlevel 1 (
    echo [ERROR] Build failed!
    exit /b 1
)

echo [INFO] Running unit tests...
ctest --test-dir build -C %CONFIG% --output-on-failure
if errorlevel 1 (
    echo [ERROR] Unit tests failed!
    exit /b 1
)

echo.
echo ========================================================
echo  Build and tests succeeded!
echo  Binary: build\%CONFIG%\guikit_mobile_demo.exe
echo ========================================================
endlocal
