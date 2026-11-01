<#
.SYNOPSIS
    Installs and launches guikit_mobile_demo.apk on a connected Android phone or emulator via ADB.
    Automatically grants runtime permissions (-g) and starts GuiKitMobileActivity.

.PARAMETER Abi
    Target Android ABI (arm64-v8a, x86_64). Default: arm64-v8a.

.PARAMETER Serial
    Optional ADB device serial if multiple devices are connected.

.PARAMETER NoLaunch
    Install the APK without automatically launching the application on the device.
#>
param(
    [ValidateSet("arm64-v8a", "x86_64")]
    [string]$Abi = "arm64-v8a",
    [string]$Serial = "",
    [switch]$NoLaunch
)

$ErrorActionPreference = "Stop"
$RepoRoot = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$BuildDir = Join-Path $RepoRoot "build_android_$Abi"
$ApkPath  = Join-Path $BuildDir "guikit_mobile_demo.apk"

function Find-Adb {
    if (Get-Command "adb" -ErrorAction SilentlyContinue) {
        return (Get-Command "adb").Source
    }
    $sdkCandidates = @(
        $env:ANDROID_HOME,
        $env:ANDROID_SDK_ROOT,
        (Join-Path $env:LOCALAPPDATA "Android\Sdk")
    ) | Where-Object { $_ -and (Test-Path $_) }

    foreach ($sdk in $sdkCandidates) {
        $adbExe = Join-Path $sdk "platform-tools\adb.exe"
        if (Test-Path $adbExe) {
            return $adbExe
        }
    }
    throw "adb.exe not found. Run scripts\windows\android-sdk-install.cmd first."
}

$adbExe = Find-Adb
Write-Host "[ADB] Using ADB: $adbExe" -ForegroundColor Cyan

# Build APK if missing
if (-not (Test-Path $ApkPath)) {
    Write-Host "[ADB] APK not found at $ApkPath. Building now..." -ForegroundColor Yellow
    & (Join-Path $PSScriptRoot "build_android.ps1") -Abi $Abi
}

# Check connected devices
$devicesOutput = & $adbExe devices
$deviceLines = $devicesOutput | Select-Object -Skip 1 | Where-Object { $_.Trim() -ne "" }

if (-not $deviceLines) {
    Write-Host ""
    Write-Host "[ADB] No Android devices detected!" -ForegroundColor Red
    Write-Host "Please check on your phone:" -ForegroundColor Yellow
    Write-Host "  1. Settings -> About Phone -> Tap 'Build Number' 7 times to unlock Developer Options."
    Write-Host "  2. Settings -> System -> Developer Options -> Enable 'USB Debugging'."
    Write-Host "  3. Connect phone via USB cable and accept the 'Allow USB debugging' prompt on screen."
    exit 1
}

$unauthorized = $deviceLines | Where-Object { $_ -match "\sunauthorized" }
if ($unauthorized) {
    Write-Host ""
    Write-Host "[ADB] Phone is connected but UNAUTHORIZED:" -ForegroundColor Yellow
    $unauthorized | ForEach-Object { Write-Host "  $_" }
    Write-Host "Please unlock your phone screen and tap 'Allow' on the 'Allow USB debugging?' dialog." -ForegroundColor Cyan
    exit 1
}

$onlineDevices = $deviceLines | Where-Object { $_ -match "\sdevice$" }
if (-not $onlineDevices) {
    throw "No online ADB device in 'device' state found. Raw output:`n$($devicesOutput -join "`n")"
}

$adbArgs = @()
if ($Serial) {
    $adbArgs += @("-s", $Serial)
}

Write-Host "[ADB] Installing $ApkPath (with runtime permissions granted)..." -ForegroundColor Cyan
& $adbExe @adbArgs install -r -g $ApkPath
if ($LASTEXITCODE -ne 0) {
    throw "ADB install failed."
}

if (-not $NoLaunch) {
    Write-Host "[ADB] Launching GuiKit Mobile Demo on device..." -ForegroundColor Cyan
    & $adbExe @adbArgs shell am start -n "io.guikit.mobile.demo/io.guikit.mobile.demo.GuiKitMobileActivity"
    if ($LASTEXITCODE -ne 0) {
        throw "Failed to launch GuiKitMobileActivity."
    }
}

Write-Host ""
Write-Host "[ADB] Successfully installed and launched GuiKit Mobile on device!" -ForegroundColor Green
