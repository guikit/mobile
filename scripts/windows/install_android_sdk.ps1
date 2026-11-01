<#
.SYNOPSIS
    Installs or verifies Android SDK, Android NDK r27 LTS, and Portable OpenJDK 17
    in the standard Windows directory (%LOCALAPPDATA%\Android\Sdk) and configures
    user environment variables (ANDROID_HOME, ANDROID_SDK_ROOT, ANDROID_NDK_HOME).

.PARAMETER SdkRoot
    Target directory for Android SDK installation. Defaults to %ANDROID_HOME% or %LOCALAPPDATA%\Android\Sdk.

.PARAMETER NdkVersion
    Android NDK version to install. Default: 27.2.12479018 (r27c LTS).

.PARAMETER Force
    Re-run sdkmanager package verification even if NDK directory already exists.
#>
param(
    [string]$SdkRoot = "",
    [string]$NdkVersion = "27.2.12479018",
    [switch]$Force
)

$ErrorActionPreference = "Stop"
$ProgressPreference = "SilentlyContinue"

if (-not $SdkRoot) {
    $SdkRoot = if ($env:ANDROID_HOME) {
        $env:ANDROID_HOME
    } elseif ($env:ANDROID_SDK_ROOT) {
        $env:ANDROID_SDK_ROOT
    } else {
        Join-Path $env:LOCALAPPDATA "Android\Sdk"
    }
}

Write-Host "========================================================" -ForegroundColor Cyan
Write-Host " GuiKit Mobile - Android SDK & NDK Installer" -ForegroundColor Cyan
Write-Host " Target SDK Root: $SdkRoot" -ForegroundColor Cyan
Write-Host "========================================================" -ForegroundColor Cyan

New-Item -ItemType Directory -Force -Path $SdkRoot | Out-Null

# 1. Ensure Java (JDK 17+) is available for sdkmanager / Gradle
$javaCmd = Get-Command "java" -ErrorAction SilentlyContinue
if (-not $javaCmd) {
    $jdkRoot = Join-Path $SdkRoot "jdk-17"
    $javaExe = Get-ChildItem -Path $jdkRoot -Filter "java.exe" -Recurse -ErrorAction SilentlyContinue | Select-Object -First 1
    if (-not $javaExe) {
        New-Item -ItemType Directory -Force -Path $jdkRoot | Out-Null
        $jdkZip = Join-Path $SdkRoot "openjdk17.zip"
        Write-Host "[SDK] Downloading Portable Microsoft OpenJDK 17 into $jdkRoot ..." -ForegroundColor Cyan
        & curl.exe -fL --retry 3 -o $jdkZip "https://aka.ms/download-jdk/microsoft-jdk-17.0.13-windows-x64.zip"
        if ($LASTEXITCODE -ne 0) {
            throw "Failed to download OpenJDK 17."
        }
        Expand-Archive -Path $jdkZip -DestinationPath $jdkRoot -Force
        Remove-Item $jdkZip -Force
        $javaExe = Get-ChildItem -Path $jdkRoot -Filter "java.exe" -Recurse | Select-Object -First 1
    }
    $javaBin = Split-Path -Parent $javaExe.FullName
    $env:JAVA_HOME = Split-Path -Parent $javaBin
    $env:PATH = "$javaBin;$env:PATH"
    [Environment]::SetEnvironmentVariable("JAVA_HOME", $env:JAVA_HOME, "User")
    Write-Host "[SDK] JAVA_HOME configured: $env:JAVA_HOME" -ForegroundColor Green
} else {
    Write-Host "[SDK] Found system Java: $($javaCmd.Source)" -ForegroundColor Green
}

# 2. Ensure Android Command-Line Tools (sdkmanager)
$cmdlineLatest = Join-Path $SdkRoot "cmdline-tools\latest"
$sdkManager = Join-Path $cmdlineLatest "bin\sdkmanager.bat"

if (-not (Test-Path $sdkManager)) {
    $cmdZip = Join-Path $SdkRoot "commandlinetools.zip"
    Write-Host "[SDK] Downloading Google Android Command-Line Tools ..." -ForegroundColor Cyan
    & curl.exe -fL --retry 3 -o $cmdZip "https://dl.google.com/android/repository/commandlinetools-win-11076708_latest.zip"
    if ($LASTEXITCODE -ne 0) {
        throw "Failed to download Android Command-Line Tools."
    }
    $tempExtract = Join-Path $SdkRoot "cmdline-tools-tmp"
    if (Test-Path $tempExtract) { Remove-Item -Recurse -Force $tempExtract }
    Expand-Archive -Path $cmdZip -DestinationPath $tempExtract -Force
    Remove-Item $cmdZip -Force
    New-Item -ItemType Directory -Force -Path (Join-Path $SdkRoot "cmdline-tools") | Out-Null
    if (Test-Path $cmdlineLatest) { Remove-Item -Recurse -Force $cmdlineLatest }
    Move-Item -Path (Join-Path $tempExtract "cmdline-tools") -Destination $cmdlineLatest -Force
    Remove-Item -Recurse -Force $tempExtract
}

# 3. Install Platform-Tools, Android 34 Platform, Build-Tools & NDK r27
$installedNdk = Join-Path $SdkRoot "ndk\$NdkVersion"
$toolchainFile = Join-Path $installedNdk "build\cmake\android.toolchain.cmake"

if ($Force -or (-not (Test-Path $toolchainFile))) {
    Write-Host "[SDK] Accepting Android SDK licenses..." -ForegroundColor Cyan
    $yesInput = ("y`n" * 30)
    $yesInput | & $sdkManager --sdk_root="$SdkRoot" --licenses | Out-Null

    Write-Host "[SDK] Installing platform-tools, android-34, build-tools;34.0.0, and ndk;$NdkVersion ..." -ForegroundColor Cyan
    & $sdkManager --sdk_root="$SdkRoot" `
        "platform-tools" `
        "platforms;android-34" `
        "build-tools;34.0.0" `
        "ndk;$NdkVersion"
    if ($LASTEXITCODE -ne 0) {
        throw "sdkmanager package installation failed."
    }
} else {
    Write-Host "[SDK] Android NDK $NdkVersion is already installed at: $installedNdk" -ForegroundColor Green
}

# 4. Set & Persist Environment Variables
$env:ANDROID_HOME = $SdkRoot
$env:ANDROID_SDK_ROOT = $SdkRoot
$env:ANDROID_NDK_HOME = $installedNdk

[Environment]::SetEnvironmentVariable("ANDROID_HOME", $SdkRoot, "User")
[Environment]::SetEnvironmentVariable("ANDROID_SDK_ROOT", $SdkRoot, "User")
[Environment]::SetEnvironmentVariable("ANDROID_NDK_HOME", $installedNdk, "User")

Write-Host ""
Write-Host "========================================================" -ForegroundColor Green
Write-Host " Android SDK & NDK Ready!" -ForegroundColor Green
Write-Host " ANDROID_HOME     = $env:ANDROID_HOME" -ForegroundColor Green
Write-Host " ANDROID_NDK_HOME = $env:ANDROID_NDK_HOME" -ForegroundColor Green
Write-Host "========================================================" -ForegroundColor Green
