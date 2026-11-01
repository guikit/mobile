# GuiKit Mobile (`guikit::mobile`)

**GuiKit Mobile** is a C++20 Mobile Hardware Abstraction Layer (HAL) and Application Framework built on top of [`guikit`](third_party/guikit) (submodule tracking branch `mobile`), targeting **Android** (via Android NDK `ASensorManager`, Camera2 NDK, Audio, and JNI) and **iOS** (planned via `CoreMotion`, `AVFoundation`, and `CoreLocation`), alongside a full **Desktop Mobile Workstation Simulator**.

---

## Architecture & Modules (`include/guikit/mobile/`)

| Header / Class | Responsibility | Android Backend | Desktop Simulator Backend |
| :--- | :--- | :--- | :--- |
| [`SensorManager`](include/guikit/mobile/Sensors.hpp) | 9-DOF IMU (`Accelerometer`, `LinearAcceleration`, `Gravity`, `Gyroscope`, `Magnetometer`, `RotationVector` attitude quaternion/Euler), `Barometer` (hPa + ISA altitude), `AmbientLight` (lux), `Proximity`, `StepCounter` + lock-free frame snapshots & time-series ring buffers | Android NDK `<android/sensor.h>` (`ASensorManager` / `ALooper`) | Real-time 6-DOF flight/motion & multi-harmonic vibration synthesizer |
| [`CameraService`](include/guikit/mobile/Camera.hpp) | Multi-camera enumeration (`Back`/`Front`), live RGB888 & GPU texture streaming, 256-bin R/G/B/Luma histograms, Torch/Flash control, Exposure EV, Digital Zoom, Snapshot capture | `SDL_Camera` (Android Camera2 NDK) | Physical Webcam (`SDL_Camera`) + fallback SMPTE/Siemens optical test camera |
| [`LocationService`](include/guikit/mobile/Location.hpp) | WGS-84 GNSS telemetry (`Lat`, `Lon`, `Alt`, accuracy, speed in m/s & knots, true bearing), Haversine distance, breadcrumb track recording for `guikit::MapView` | Android `LocationManager` JNI (`gps` / `network`) | Figure-8 patrol route generator over embedded `demo_bay.geojson` S-57 chart |
| [`AudioCapture`](include/guikit/mobile/AudioCapture.hpp) | Low-latency PCM microphone capture, RMS/Peak dBFS metering for `guikit::LevelMeter`, and 1024-point Hann FFT spectrum slices for `guikit::WaterfallView` & `guikit::SignalView` | `SDL_AudioStream` (AAudio / OpenSLES) | Hardware microphone or calibrated multi-harmonic + noise DSP generator |
| [`SystemBridge`](include/guikit/mobile/SystemBridge.hpp) | Runtime permissions (`Camera`, `RecordAudio`, `FineLocation`, `BodySensors`), tactile Haptics (`VibrationEffect`), `SafeAreaInsets`, Battery telemetry, Soft Keyboard (IME) | `SDL_RequestAndroidPermission`, JNI `Vibrator`, `SDL_GetWindowSafeArea` | Simulated Pixel 9 Pro cutout insets, rumble haptics, and permission callbacks |
| [`TouchGestureRecognizer`](include/guikit/mobile/TouchGesture.hpp) | Multi-touch finger tracking, kinetic/inertial scroll momentum, two-finger `Pinch-to-Zoom`, and `Long-Press` recognition | `SDL_EVENT_FINGER_*` | Touch & mouse gesture translation |
| [`MobileRuntime`](include/guikit/mobile/MobileRuntime.hpp) | Unified coordinator managing all subsystems and mobile app lifecycle (`OnPause` / `OnResume` / Android Back button) | Full Android lifecycle | Desktop workstation mode |

---

## Quick Start

### 1. Build & Run Desktop Mobile Simulator (Windows)
```cmd
:: Configure, build, and run unit tests
scripts\windows\desktop-build.cmd

:: Build and launch the interactive Mobile Sensor & Camera Lab
scripts\windows\desktop-run.cmd
```

### 2. Install Android SDK & NDK + Build Android Native Binary (`arm64-v8a`)
No manual Android Studio installation is required. Run `android-sdk-install.cmd` once to automatically download Portable OpenJDK 17, Google Android Command-Line Tools, Platform 34, and Android NDK r27 LTS into the standard Windows SDK path `%LOCALAPPDATA%\Android\Sdk` (and configure `ANDROID_HOME` / `ANDROID_NDK_HOME`), then run `android-build.cmd`:
```cmd
:: Step 1: Install/verify Android SDK & NDK in %LOCALAPPDATA%\Android\Sdk
scripts\windows\android-sdk-install.cmd

:: Step 2: Cross-compile libmain.so for Android (arm64-v8a)
scripts\windows\android-build.cmd
```
