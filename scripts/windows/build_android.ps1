<#
.SYNOPSIS
    Cross-compiles guikit::mobile (libmain.so) for Android using CMake + Ninja + Android NDK,
    and packages a signed, zipaligned Android APK (guikit_mobile_demo.apk) using Android SDK Build-Tools.

.PARAMETER Abi
    Target Android ABI (arm64-v8a, x86_64). Default: arm64-v8a.

.PARAMETER ApiLevel
    Target Android API Level. Default: 26 (Android 8.0+).

.PARAMETER BuildType
    CMake build configuration (Release or Debug). Default: Release.

.PARAMETER AutoInstallSdk
    Automatically run install_android_sdk.ps1 if NDK is not found.
#>
param(
    [ValidateSet("arm64-v8a", "x86_64")]
    [string]$Abi = "arm64-v8a",
    [int]$ApiLevel = 26,
    [ValidateSet("Release", "Debug")]
    [string]$BuildType = "Release",
    [switch]$AutoInstallSdk,
    [switch]$Clean
)

$ErrorActionPreference = "Stop"
$RepoRoot = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$BuildDir = Join-Path $RepoRoot "build_android_$Abi"

function Ensure-Ninja {
    if (Get-Command "ninja" -ErrorAction SilentlyContinue) {
        return
    }
    $vsNinja = Get-ChildItem -Path "C:\Program Files\Microsoft Visual Studio", "C:\Program Files (x86)\Microsoft Visual Studio" `
        -Filter "ninja.exe" -Recurse -ErrorAction SilentlyContinue | Select-Object -First 1
    if ($vsNinja) {
        $ninjaDir = Split-Path -Parent $vsNinja.FullName
        Write-Host "[Android] Using Visual Studio Ninja: $ninjaDir" -ForegroundColor Cyan
        $env:PATH = "$ninjaDir;$env:PATH"
    }
}

function Find-AndroidSdkAndNdk {
    $sdkCandidates = @(
        $env:ANDROID_HOME,
        $env:ANDROID_SDK_ROOT,
        (Join-Path $env:LOCALAPPDATA "Android\Sdk")
    ) | Where-Object { $_ -and (Test-Path $_) }

    foreach ($sdk in $sdkCandidates) {
        $ndkRoot = Join-Path $sdk "ndk"
        if (Test-Path $ndkRoot) {
            $ndkDir = Get-ChildItem -Path $ndkRoot -Directory | Sort-Object Name -Descending | Select-Object -First 1
            if ($ndkDir -and (Test-Path (Join-Path $ndkDir.FullName "build\cmake\android.toolchain.cmake"))) {
                $env:ANDROID_HOME = $sdk
                $env:ANDROID_SDK_ROOT = $sdk
                $env:ANDROID_NDK_HOME = $ndkDir.FullName
                return @{
                    Sdk = $sdk
                    Ndk = $ndkDir.FullName
                }
            }
        }
    }
    return $null
}

function Find-Jdk17($SdkRoot) {
    if ($env:JAVA_HOME -and (Test-Path (Join-Path $env:JAVA_HOME "bin\javac.exe"))) {
        return $env:JAVA_HOME
    }
    $localJdkRoot = Join-Path $SdkRoot "jdk-17"
    if (Test-Path $localJdkRoot) {
        $jdkDir = Get-ChildItem -Path $localJdkRoot -Directory -Filter "jdk*" -ErrorAction SilentlyContinue | Select-Object -First 1
        if ($jdkDir -and (Test-Path (Join-Path $jdkDir.FullName "bin\javac.exe"))) {
            $env:JAVA_HOME = $jdkDir.FullName
            $env:PATH = "$($jdkDir.FullName)\bin;$env:PATH"
            return $jdkDir.FullName
        }
    }
    throw "JDK 17 (javac.exe) not found. Run scripts\windows\android-sdk-install.cmd first."
}

if ($Clean -and (Test-Path $BuildDir)) {
    Remove-Item -Recurse -Force $BuildDir
}

Ensure-Ninja
$paths = Find-AndroidSdkAndNdk
if (-not $paths) {
    if ($AutoInstallSdk) {
        & (Join-Path $PSScriptRoot "install_android_sdk.ps1")
        $paths = Find-AndroidSdkAndNdk
    }
    if (-not $paths) {
        throw "Android SDK/NDK not found. Run scripts\windows\android-sdk-install.cmd first."
    }
}

$sdkPath = $paths.Sdk
$ndkPath = $paths.Ndk
$jdkPath = Find-Jdk17 $sdkPath

$toolchainFile = Join-Path $ndkPath "build\cmake\android.toolchain.cmake"
Write-Host "[Android] Using NDK Toolchain: $toolchainFile" -ForegroundColor Green
Write-Host "[Android] Configuring CMake for Android ($Abi, API $ApiLevel, $BuildType) ..." -ForegroundColor Cyan

& cmake -G Ninja -S $RepoRoot -B $BuildDir `
    "-DCMAKE_TOOLCHAIN_FILE=$toolchainFile" `
    "-DANDROID_ABI=$Abi" `
    "-DANDROID_PLATFORM=android-$ApiLevel" `
    "-DANDROID_STL=c++_shared" `
    "-DCMAKE_BUILD_TYPE=$BuildType" `
    "-DGUIKIT_MOBILE_BUILD_DEMO=ON" `
    "-DGUIKIT_MOBILE_BUILD_TESTS=OFF"

if ($LASTEXITCODE -ne 0) {
    throw "Android CMake configuration failed."
}

Write-Host "[Android] Building libmain.so ($BuildType) ..." -ForegroundColor Cyan
& cmake --build $BuildDir --target main --parallel
if ($LASTEXITCODE -ne 0) {
    throw "Android native build failed."
}

$soFile = Join-Path $BuildDir "libmain.so"
if ($BuildType -eq "Release") {
    $stripExe = Join-Path $ndkPath "toolchains\llvm\prebuilt\windows-x86_64\bin\llvm-strip.exe"
    if ((Test-Path $stripExe) -and (Test-Path $soFile)) {
        & $stripExe --strip-unneeded $soFile
    }
}

# ==============================================================================
# Package Android APK (aapt2 + javac + d8 + zipalign + apksigner)
# ==============================================================================
Write-Host "[Android] Packaging guikit_mobile_demo.apk ..." -ForegroundColor Cyan

$buildToolsDir = Get-ChildItem -Path (Join-Path $sdkPath "build-tools") -Directory | Sort-Object Name -Descending | Select-Object -First 1
$platformDir   = Get-ChildItem -Path (Join-Path $sdkPath "platforms") -Directory | Sort-Object Name -Descending | Select-Object -First 1
if (-not $buildToolsDir -or -not $platformDir) {
    throw "Android build-tools or platforms not found in $sdkPath"
}

$aapt2Exe     = Join-Path $buildToolsDir.FullName "aapt2.exe"
$d8Bat        = Join-Path $buildToolsDir.FullName "d8.bat"
$zipalignExe  = Join-Path $buildToolsDir.FullName "zipalign.exe"
$apksignerBat = Join-Path $buildToolsDir.FullName "apksigner.bat"
$androidJar   = Join-Path $platformDir.FullName "android.jar"
$javacExe     = Join-Path $jdkPath "bin\javac.exe"
$jarExe       = Join-Path $jdkPath "bin\jar.exe"
$keytoolExe   = Join-Path $jdkPath "bin\keytool.exe"

$apkWorkDir = Join-Path $BuildDir "apk_work"
if (Test-Path $apkWorkDir) {
    Remove-Item -Recurse -Force $apkWorkDir
}
$genJavaDir = Join-Path $apkWorkDir "gen_java"
$classesDir = Join-Path $apkWorkDir "classes"
$dexStage   = Join-Path $apkWorkDir "stage"
New-Item -ItemType Directory -Force -Path $genJavaDir, $classesDir, $dexStage | Out-Null

$manifestFile = Join-Path $RepoRoot "android\app\src\main\AndroidManifest.xml"
$resDir       = Join-Path $RepoRoot "android\app\src\main\res"
$resZip       = Join-Path $apkWorkDir "resources.zip"
$unalignedApk = Join-Path $apkWorkDir "unaligned.apk"
$alignedApk   = Join-Path $apkWorkDir "aligned.apk"
$finalApk     = Join-Path $BuildDir "guikit_mobile_demo.apk"

# 1. Compile & link Android resources + Manifest with aapt2
& $aapt2Exe compile --dir $resDir -o $resZip
if ($LASTEXITCODE -ne 0) { throw "aapt2 compile failed." }

& $aapt2Exe link -o $unalignedApk -I $androidJar --manifest $manifestFile --java $genJavaDir $resZip
if ($LASTEXITCODE -ne 0) { throw "aapt2 link failed." }

# 2. Compile SDL3 Java glue + GuiKitMobileActivity + R.java
$sdlJavaDir = Join-Path $RepoRoot "third_party\guikit\third_party\SDL\android-project\app\src\main\java"
$appJavaDir = Join-Path $RepoRoot "android\app\src\main\java"
$javaFiles = @(
    Get-ChildItem -Path $sdlJavaDir -Filter "*.java" -Recurse | ForEach-Object { $_.FullName }
    Get-ChildItem -Path $appJavaDir -Filter "*.java" -Recurse | ForEach-Object { $_.FullName }
    Get-ChildItem -Path $genJavaDir -Filter "*.java" -Recurse | ForEach-Object { $_.FullName }
)
$javaListFile = Join-Path $apkWorkDir "java_sources.txt"
$javaFiles | Set-Content -Path $javaListFile -Encoding ascii

& $javacExe --release 11 -encoding UTF-8 -cp $androidJar -d $classesDir "@$javaListFile"
if ($LASTEXITCODE -ne 0) { throw "javac compilation failed." }

# 3. Convert .class files to classes.dex using d8
$classFiles = Get-ChildItem -Path $classesDir -Filter "*.class" -Recurse | ForEach-Object { $_.FullName }
$classListFile = Join-Path $apkWorkDir "class_files.txt"
$classFiles | Set-Content -Path $classListFile -Encoding ascii

& $d8Bat --lib $androidJar --min-api $ApiLevel --output $dexStage "@$classListFile"
if ($LASTEXITCODE -ne 0) { throw "d8 DEX compilation failed." }

# 4. Stage native libraries (libmain.so + libc++_shared.so)
$libAbiDir = Join-Path $dexStage "lib\$Abi"
New-Item -ItemType Directory -Force -Path $libAbiDir | Out-Null
Copy-Item -Path $soFile -Destination (Join-Path $libAbiDir "libmain.so") -Force

$tripleMap = @{
    "arm64-v8a" = "aarch64-linux-android"
    "x86_64"    = "x86_64-linux-android"
}
$triple = $tripleMap[$Abi]
$libcppShared = Join-Path $ndkPath "toolchains\llvm\prebuilt\windows-x86_64\sysroot\usr\lib\$triple\libc++_shared.so"
if (Test-Path $libcppShared) {
    Copy-Item -Path $libcppShared -Destination (Join-Path $libAbiDir "libc++_shared.so") -Force
}

# Add classes.dex and uncompressed native libraries into unaligned.apk
Push-Location $dexStage
try {
    & $jarExe uf $unalignedApk "classes.dex"
    if ($LASTEXITCODE -ne 0) { throw "Failed to add classes.dex to APK." }
    & $jarExe u0f $unalignedApk "lib"
    if ($LASTEXITCODE -ne 0) { throw "Failed to add native libs to APK." }
} finally {
    Pop-Location
}

# 5. Align APK on 4-byte / page boundaries with zipalign
& $zipalignExe -f -p 4 $unalignedApk $alignedApk
if ($LASTEXITCODE -ne 0) { throw "zipalign failed." }

# 6. Sign APK with debug keystore using apksigner
$keystoreFile = Join-Path $BuildDir "debug.keystore"
if (-not (Test-Path $keystoreFile)) {
    & $keytoolExe -genkeypair -v -keystore $keystoreFile -storepass android -alias androiddebugkey `
        -keypass android -keyalg RSA -keysize 2048 -validity 10000 `
        -dname "CN=Android Debug,O=GuiKit,C=US" | Out-Null
}

& $apksignerBat sign --ks $keystoreFile --ks-key-alias androiddebugkey --ks-pass pass:android --key-pass pass:android --out $finalApk $alignedApk
if ($LASTEXITCODE -ne 0) { throw "apksigner sign failed." }

& $apksignerBat verify $finalApk
if ($LASTEXITCODE -ne 0) { throw "apksigner verify failed." }

Write-Host ""
Write-Host "[Android] Build & APK packaging succeeded!" -ForegroundColor Green
Get-ChildItem -Path $BuildDir -Include "libmain.so", "guikit_mobile_demo.apk" -Recurse |
    Where-Object { $_.DirectoryName -eq $BuildDir } |
    Select-Object Name, FullName, @{Name="SizeKB";Expression={[math]::Round($_.Length / 1KB, 1)}} |
    Format-Table -AutoSize
