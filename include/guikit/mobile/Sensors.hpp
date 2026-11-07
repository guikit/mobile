#pragma once

#include <array>
#include <cstdint>
#include <span>
#include <string>
#include "guikit/mobile/Types.hpp"
#include "keel/mobile/sensors.h"

namespace guikit::mobile {

/// Supported hardware & fused sensor modalities across Android NDK (ASensor) and iOS (CoreMotion).
enum class SensorType : uint8_t {
    Accelerometer = 0,  ///< 3-axis acceleration including gravity (m/s^2)
    LinearAcceleration, ///< 3-axis linear acceleration excluding gravity (m/s^2)
    Gravity,            ///< 3-axis gravity vector (m/s^2)
    Gyroscope,          ///< 3-axis angular velocity (rad/s)
    Magnetometer,       ///< 3-axis geomagnetic field (uT microtesla)
    RotationVector,     ///< Fused 9-DOF / 6-DOF attitude quaternion + Euler angles
    Barometer,          ///< Atmospheric pressure (hPa / mbar) & barometric altitude (m)
    AmbientLight,       ///< Ambient illuminance (lux)
    Proximity,          ///< Proximity distance (cm) & near/far flag
    StepCounter,        ///< Cumulative pedometer steps
    Count
};

/// Reference frame for computing Pitch and Roll from device attitude.
enum class AttitudeReference : uint8_t {
    Handheld = 0, ///< 0 deg Pitch/Roll when phone is held upright in hand (screen facing user, camera facing horizon)
    FlatTable     ///< 0 deg Pitch/Roll when phone lies flat on a horizontal table (screen facing ceiling)
};

/// Sampling rate presets matching Android NDK ASensorManager rates and iOS CMMotionManager intervals.
enum class SensorRate : uint8_t {
    Ui = 0,   ///< ~16.6 Hz (60,000 us) - Low battery impact for general UI orientation
    Normal,   ///< ~50 Hz   (20,000 us) - Standard telemetry & navigation
    Game,     ///< ~100 Hz  (10,000 us) - Responsive horizon, AR, and motion controls
    Fastest   ///< ~200 Hz+ ( 5,000 us) - High-rate DSP vibration & spectral analysis
};

/// Metadata describing an available physical or fused sensor on the device.
struct SensorInfo {
    SensorType type{SensorType::Accelerometer};
    std::string name{};
    std::string vendor{};
    float maxRange{0.0f};
    float resolution{0.0f};
    int32_t minDelayUs{0};
    bool available{false};
    bool enabled{false};
};

/// Coherent frame-locked snapshot of all active mobile sensors.
/// Read once per UI frame (`BeginFrame`) so all widgets can query telemetry with zero lock contention.
struct SensorSnapshot {
    uint64_t timestampNs{0};
    uint64_t sampleSequence{0};

    // --- 3-Axis IMU & Magnetic ---
    Vec3 accelerometer{0.0f, 0.0f, 9.80665f};      ///< m/s^2 (including gravity)
    Vec3 linearAcceleration{0.0f, 0.0f, 0.0f};     ///< m/s^2 (gravity removed)
    Vec3 gravity{0.0f, 0.0f, 9.80665f};            ///< m/s^2
    Vec3 gyroscope{0.0f, 0.0f, 0.0f};              ///< rad/s
    Vec3 magnetometer{21.5f, 4.2f, -43.8f};        ///< uT (microtesla)

    // --- Fused Attitude & Flight Dynamics ---
    Quaternion attitudeQuat{1.0f, 0.0f, 0.0f, 0.0f};
    EulerAngles attitude{};                        ///< Pitch, Roll, Yaw, Heading (deg)
    float gLoad{1.0f};                             ///< Total G-load factor (|accel| / 9.80665)
    float slipSkid{0.0f};                          ///< Lateral inclinometer ball offset [-1..+1]

    // --- Environmental & Biometric / Activity ---
    float pressureHpa{1013.25f};                   ///< Atmospheric pressure in hPa
    float seaLevelPressureHpa{1013.25f};           ///< Reference QNH sea-level pressure in hPa
    float barometricAltitudeMeters{0.0f};          ///< Computed ISA barometric altitude in meters
    float verticalSpeedMps{0.0f};                  ///< Vertical climb/descent rate in m/s
    float ambientLightLux{320.0f};                 ///< Ambient light in lux
    float proximityCm{5.0f};                       ///< Proximity sensor distance in cm
    bool proximityNear{false};                     ///< True when object/ear is close to screen
    uint64_t stepCount{0};                         ///< Cumulative pedometer steps in current session
};

/// Simulator synthesis parameters for testing IMU, Barometer, and Environmental sensors on Desktop.
struct SensorSimConfig {
    bool animateMotion{true};        ///< Automatically oscillate pitch/roll/heading and vibration
    float manualPitchDeg{6.5f};      ///< Manual pitch bias in degrees
    float manualRollDeg{-12.0f};     ///< Manual roll bias in degrees
    float manualHeadingDeg{42.0f};   ///< Manual compass heading in degrees
    float vibrationFreqHz{18.0f};    ///< Synthetic vibration tone frequency in Hz
    float vibrationAmpG{0.15f};      ///< Synthetic vibration amplitude in G
    float simPressureHpa{1008.4f};   ///< Simulated barometer pressure in hPa
    float simLightLux{450.0f};       ///< Simulated ambient light in lux
    bool simProximityNear{false};    ///< Simulated proximity trigger
};

/// Cross-platform Sensor Manager backed by `keel::SensorManager` (`keelcpp/mobile`).
class SensorManager {
public:
    static constexpr size_t kDefaultHistorySamples = keel::SensorManager::kDefaultHistorySamples;

    SensorManager();
    ~SensorManager();

    SensorManager(const SensorManager&) = delete;
    SensorManager& operator=(const SensorManager&) = delete;

    /// Initializes hardware sensor manager (`keel::SensorManager`).
    bool Initialize();
    void Shutdown();

    /// Pauses/resumes hardware sensor polling during mobile lifecycle transitions (`OnPause`/`OnResume`).
    void SetPaused(bool paused);
    [[nodiscard]] bool IsPaused() const noexcept { return m_hal.is_paused(); }

    /// Enables or disables a specific sensor modality.
    bool SetSensorEnabled(SensorType type, bool enabled);
    [[nodiscard]] bool IsSensorEnabled(SensorType type) const noexcept;
    [[nodiscard]] bool IsSensorAvailable(SensorType type) const noexcept;

    /// Configures sampling rate across active sensors.
    void SetSamplingRate(SensorRate rate);
    [[nodiscard]] SensorRate GetSamplingRate() const noexcept { return static_cast<SensorRate>(m_hal.sampling_rate()); }
    [[nodiscard]] float GetEstimatedSampleRateHz() const noexcept { return m_hal.estimated_sample_rate_hz(); }

    /// Latches latest hardware/simulator readings into the frame snapshot and updates time-series buffers.
    void UpdateFrame(double dtSeconds);

    /// Returns the lock-free snapshot latched for the current UI frame.
    [[nodiscard]] const SensorSnapshot& GetSnapshot() const noexcept { return m_snapshot; }

    /// Returns metadata for all supported sensor types.
    [[nodiscard]] std::span<const SensorInfo> GetSensorInfos() const noexcept { return m_infos; }

    /// Reference QNH pressure in hPa used for barometric altitude calculation (default 1013.25 hPa).
    void SetSeaLevelPressureHpa(float qnhHpa) noexcept { m_hal.set_sea_level_pressure_hpa(qnhHpa); }
    [[nodiscard]] float GetSeaLevelPressureHpa() const noexcept { return m_hal.sea_level_pressure_hpa(); }

    /// Sets the reference frame for Pitch & Roll (default: `AttitudeReference::Handheld`).
    void SetAttitudeReference(AttitudeReference ref) noexcept;
    [[nodiscard]] AttitudeReference GetAttitudeReference() const noexcept {
        return static_cast<AttitudeReference>(m_hal.attitude_reference());
    }

    /// Sets the active display orientation so physical MEMS sensor axes are remapped to screen space.
    void SetScreenOrientation(ScreenOrientation orientation) noexcept {
        m_hal.set_screen_orientation(static_cast<keel::ScreenOrientation>(orientation));
    }
    [[nodiscard]] ScreenOrientation GetScreenOrientation() const noexcept {
        return static_cast<ScreenOrientation>(m_hal.screen_orientation());
    }

    /// Zeros (tares) Pitch and Roll to the current device posture, or clears the tare offset.
    void TareAttitude() noexcept;
    void ResetAttitudeTare() noexcept;
    [[nodiscard]] bool HasAttitudeTare() const noexcept { return m_hal.has_attitude_tare(); }

    /// Contiguous time-series history buffers (oldest -> newest) ready for `guikit::SignalView`.
    [[nodiscard]] std::span<const float> GetAccelHistoryX() const noexcept {
        const auto s = m_hal.accel_history_x();
        return {s.data(), s.size()};
    }
    [[nodiscard]] std::span<const float> GetAccelHistoryY() const noexcept {
        const auto s = m_hal.accel_history_y();
        return {s.data(), s.size()};
    }
    [[nodiscard]] std::span<const float> GetAccelHistoryZ() const noexcept {
        const auto s = m_hal.accel_history_z();
        return {s.data(), s.size()};
    }
    [[nodiscard]] std::span<const float> GetAccelMagnitudeHistory() const noexcept {
        const auto s = m_hal.accel_magnitude_history();
        return {s.data(), s.size()};
    }
    [[nodiscard]] std::span<const float> GetGyroMagnitudeHistory() const noexcept {
        const auto s = m_hal.gyro_magnitude_history();
        return {s.data(), s.size()};
    }
    [[nodiscard]] std::span<const float> GetMagMagnitudeHistory() const noexcept {
        const auto s = m_hal.mag_magnitude_history();
        return {s.data(), s.size()};
    }
    [[nodiscard]] std::span<const float> GetBaroAltitudeHistory() const noexcept {
        const auto s = m_hal.baro_altitude_history();
        return {s.data(), s.size()};
    }

    /// Access to desktop simulator controls (also usable on Android in "Demo Override" mode).
    [[nodiscard]] SensorSimConfig& GetSimConfig() noexcept { return m_simConfig; }
    [[nodiscard]] const SensorSimConfig& GetSimConfig() const noexcept { return m_simConfig; }
    void SetUseSimulatorOverride(bool forceSim) noexcept { m_hal.set_use_simulator_override(forceSim); }
    [[nodiscard]] bool IsUsingSimulator() const noexcept { return m_hal.is_using_simulator(); }

    /// Underlying `keel::SensorManager` HAL instance.
    [[nodiscard]] keel::SensorManager& Hal() noexcept { return m_hal; }
    [[nodiscard]] const keel::SensorManager& Hal() const noexcept { return m_hal; }

    /// Helper: converts atmospheric pressure (hPa) to ISA altitude in meters.
    [[nodiscard]] static float PressureToAltitudeMeters(float pressureHpa, float seaLevelHpa = 1013.25f) noexcept;

    /// Helper: converts unit quaternion to Euler angles (pitch, roll, yaw, heading in degrees).
    [[nodiscard]] static EulerAngles QuaternionToEuler(
        Quaternion q,
        AttitudeReference ref = AttitudeReference::FlatTable
    ) noexcept;

private:
    void SyncFromHal();
    void SyncSimConfigToHal() noexcept;

    keel::SensorManager m_hal{};
    std::array<SensorInfo, static_cast<size_t>(SensorType::Count)> m_infos{};
    SensorSnapshot m_snapshot{};
    SensorSimConfig m_simConfig{};
};

} // namespace guikit::mobile
