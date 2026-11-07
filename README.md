# GuiKit Mobile (`guikit::mobile`)

**GuiKit Mobile** is a C++20 Mobile UI & Application Framework built on top of [`guikit`](third_party/guikit) (`main`) and [`keel::mobile`](third_party/mobile) (`keelcpp/mobile`), targeting **Android** (via Android NDK `ASensorManager`, Camera2 NDK, AAudio NDK, and JNI) and **iOS**, alongside a full **Desktop Mobile Workstation Simulator**.

---

## Architecture & Modules (`include/guikit/mobile/`)

| Header / Class | Responsibility | Hardware Backend (`keel::mobile`) | Desktop Simulator Backend |
| :--- | :--- | :--- | :--- |
| [`SensorManager`](include/guikit/mobile/sensors.hpp) | 9-DOF IMU (`Accelerometer`, `LinearAcceleration`, `Gravity`, `Gyroscope`, `Magnetometer`, `RotationVector` attitude quaternion/Euler), `Barometer` (hPa + ISA altitude), `AmbientLight` (lux), `Proximity`, `StepCounter` + lock-free frame snapshots & time-series ring buffers | `keel::SensorManager` (Android NDK `<android/sensor.h>` `ASensorManager` / `ALooper`) | Real-time 6-DOF flight/motion & multi-harmonic vibration synthesizer |
| [`CameraService`](include/guikit/mobile/camera.hpp) | Multi-camera enumeration (`Back`/`Front`), live RGB888 (`guikit::ImageViewData`) & GPU texture streaming, 256-bin R/G/B/Luma histograms, Torch/Flash control, Exposure EV, Digital Zoom, Snapshot capture | `keel::CameraService` (Android Camera2 NDK `ACameraManager` / `AImageReader`) | Physical Webcam (`SDL_Camera`) + fallback SMPTE/Siemens optical test camera |
| [`LocationService`](include/guikit/mobile/location.hpp) | WGS-84 GNSS telemetry (`Lat`, `Lon`, `Alt`, accuracy, speed in m/s & knots, true bearing), Haversine distance, breadcrumb track recording for `guikit::MapView` | `keel::LocationService` (Android `LocationManager` JNI `gps` / `network`) | Figure-8 patrol route generator over embedded `demo_bay.geojson` S-57 chart |
| [`AudioCapture`](include/guikit/mobile/audio_capture.hpp) | Low-latency PCM microphone capture, RMS/Peak dBFS metering for `guikit::LevelMeter`, and 1024-point Hann FFT spectrum slices for `guikit::WaterfallView` & `guikit::SignalView` | `keel::AudioCapture` (Android NDK AAudio `<aaudio/AAudio.h>`) | Windows WinMM `waveIn` hardware microphone or calibrated multi-harmonic + noise DSP generator |
| [`SystemBridge`](include/guikit/mobile/system_bridge.hpp) | Runtime permissions (`Camera`, `RecordAudio`, `FineLocation`, `BodySensors`), tactile Haptics (`Vibrator`), `SafeAreaInsets`, Battery telemetry, Native Toast & Soft Keyboard (IME) | `keel::SystemBridge` (Android JNI `BatteryManager`, `Vibrator`, `Toast`, `checkSelfPermission`) + `SDL_GetWindowSafeArea` | Windows `GetSystemPowerStatus`, simulated cutout insets, and permission callbacks |
| [`TouchGestureRecognizer`](include/guikit/mobile/touch_gesture.hpp) | Multi-touch finger tracking, kinetic/inertial scroll momentum, two-finger `Pinch-to-Zoom`, and `Long-Press` recognition | `SDL_EVENT_FINGER_*` | Touch & mouse gesture translation |
| [`MobileRuntime`](include/guikit/mobile/mobile_runtime.hpp) | Unified coordinator managing all subsystems and mobile app lifecycle (`OnEnterBackground` / `OnEnterForeground` / Android Back button) | Full Android lifecycle | Desktop workstation mode |
| [`MobileWindow`](include/guikit/mobile/mobile_window.hpp) | Mobile & tablet application host (`IsTablet()`, `IsPortrait()`, `GetSafeAreaPadding()`, `RenderStatusBar()`, `RenderBottomNavigationBar()`, `RenderSimulatorDrawer()`) | `SdlMobileWindowBackend` | Resizable desktop simulator window |

---

## Quick Start

### 1. Build & Run Desktop Mobile Simulator (Windows)
```cmd
:: Configure, build, and run unit tests
scripts\windows\desktop-build.cmd

:: Build and launch the interactive Mobile Sensor & Camera Lab
scripts\windows\desktop-run.cmd
```

### 2. Install Android SDK & NDK + Build & Deploy Android APK (`arm64-v8a`)
No manual Android Studio installation is required. Run `android-sdk-install.cmd` once to automatically download Portable OpenJDK 17, Google Android Command-Line Tools, Platform 34, and Android NDK r27 LTS into the standard Windows SDK path `%LOCALAPPDATA%\Android\Sdk` (and configure `ANDROID_HOME` / `ANDROID_NDK_HOME`), then build and deploy:
```cmd
:: Step 1: Install/verify Android SDK & NDK in %LOCALAPPDATA%\Android\Sdk
scripts\windows\android-sdk-install.cmd

:: Step 2: Cross-compile libmain.so and package signed guikit_mobile_demo.apk
scripts\windows\android-build.cmd

:: Step 3: Install APK via ADB onto a connected Android phone, grant permissions, and launch
scripts\windows\android-install.cmd
```
