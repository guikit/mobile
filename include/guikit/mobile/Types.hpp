#pragma once

#include <cmath>
#include <cstdint>
#include <string>
#include <string_view>
#include "guikit/core/Types.hpp"

namespace guikit::mobile {

/// 3D vector used for 3-axis physical sensors (Accelerometer, Gyroscope, Magnetometer).
struct Vec3 {
    float x{0.0f};
    float y{0.0f};
    float z{0.0f};

    constexpr Vec3() noexcept = default;
    constexpr Vec3(float x_, float y_, float z_) noexcept : x(x_), y(y_), z(z_) {}

    [[nodiscard]] float Magnitude() const noexcept {
        return std::sqrt(x * x + y * y + z * z);
    }
};

/// Unit quaternion representing 3D device orientation (Rotation Vector / Attitude).
struct Quaternion {
    float w{1.0f};
    float x{0.0f};
    float y{0.0f};
    float z{0.0f};

    constexpr Quaternion() noexcept = default;
    constexpr Quaternion(float w_, float x_, float y_, float z_) noexcept
        : w(w_), x(x_), y(y_), z(z_) {}
};

/// Euler orientation angles in degrees derived from fused IMU sensors.
struct EulerAngles {
    float pitchDeg{0.0f};   ///< Nose up/down [-90..+90]
    float rollDeg{0.0f};    ///< Bank left/right [-180..+180]
    float yawDeg{0.0f};     ///< Azimuth / Yaw [-180..+180]
    float headingDeg{0.0f}; ///< Magnetic / True compass heading [0..360)
};

/// Display safe-area insets in logical points (status bar, camera cutout/notch, gesture bar).
struct SafeAreaInsets {
    float top{0.0f};
    float right{0.0f};
    float bottom{0.0f};
    float left{0.0f};
};

/// Device screen orientation modes.
enum class ScreenOrientation : uint8_t {
    Portrait,
    LandscapeLeft,
    PortraitUpsideDown,
    LandscapeRight,
    Auto
};

/// Mobile runtime permissions (mapped to Android Manifest permissions & iOS Info.plist keys).
enum class Permission : uint8_t {
    Camera,
    RecordAudio,
    FineLocation,
    CoarseLocation,
    BodySensors,
    Bluetooth,
    Notifications
};

/// Authorization status of a runtime permission.
enum class PermissionStatus : uint8_t {
    Unknown,
    Granted,
    Denied,
    Restricted
};

/// Standard mobile tactile haptic feedback patterns.
enum class HapticStyle : uint8_t {
    Light,
    Medium,
    Heavy,
    Selection,
    Success,
    Warning,
    Error
};

/// Real-time device battery and thermal telemetry.
struct BatteryStatus {
    float levelPct{100.0f};    ///< Battery charge [0..100]%
    bool isCharging{false};    ///< True when plugged into AC/USB/Wireless charger
    float temperatureC{28.5f}; ///< Battery temperature in Celsius
    float voltageMv{4150.0f};  ///< Battery voltage in millivolts
};

/// Summary information about the host mobile device or simulator.
struct DeviceInfo {
    std::string platformName{"Android"};
    std::string manufacturer{"Generic"};
    std::string model{"Mobile Device"};
    std::string osVersion{"14"};
    int sdkInt{34};
    bool isSimulator{false};
};

} // namespace guikit::mobile
