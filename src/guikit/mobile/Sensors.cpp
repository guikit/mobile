#include "guikit/mobile/Sensors.hpp"
#include <SDL3/SDL.h>
#include <algorithm>
#include <cmath>
#include <numbers>

#ifdef __ANDROID__
#include <android/looper.h>
#include <android/sensor.h>
#endif

namespace guikit::mobile {

namespace {

constexpr float kGravityStandard = 9.80665f;
constexpr float kRadToDeg = 180.0f / std::numbers::pi_v<float>;
constexpr float kDegToRad = std::numbers::pi_v<float> / 180.0f;

[[nodiscard]] constexpr int32_t RateToDelayUs(SensorRate rate) noexcept {
    switch (rate) {
        case SensorRate::Ui:      return 60000; // ~16.6 Hz
        case SensorRate::Normal:  return 20000; // 50 Hz
        case SensorRate::Game:    return 10000; // 100 Hz
        case SensorRate::Fastest: return 5000;  // 200 Hz
    }
    return 10000;
}

[[nodiscard]] Quaternion EulerToQuaternion(float pitchDeg, float rollDeg, float yawDeg) noexcept {
    // Smartphone coordinate system (X = Right, Y = Forward/Up, Z = Out of screen):
    // - Pitch (Дифферент): rotation around X-axis
    // - Roll  (Крен):      rotation around Y-axis
    // - Yaw   (Курс):      rotation around Z-axis
    const float halfPitch = (pitchDeg * kDegToRad) * 0.5f; // X-axis
    const float halfRoll  = (rollDeg  * kDegToRad) * 0.5f; // Y-axis
    const float halfYaw   = (yawDeg   * kDegToRad) * 0.5f; // Z-axis

    const float cx = std::cos(halfPitch);
    const float sx = std::sin(halfPitch);
    const float cy = std::cos(halfRoll);
    const float sy = std::sin(halfRoll);
    const float cz = std::cos(halfYaw);
    const float sz = std::sin(halfYaw);

    return Quaternion{
        cx * cy * cz + sx * sy * sz, // w
        sx * cy * cz - cx * sy * sz, // x (Pitch)
        cx * sy * cz + sx * cy * sz, // y (Roll)
        cx * cy * sz - sx * sy * cz  // z (Yaw)
    };
}

void AppendRingSample(std::vector<float>& buf, float value, size_t maxSamples) {
    if (buf.size() < maxSamples) {
        buf.push_back(value);
    } else if (!buf.empty()) {
        std::move(buf.begin() + 1, buf.end(), buf.begin());
        buf.back() = value;
    }
}

#ifdef __ANDROID__
// Android sensor type constants (defined in <android/sensor.h> and Android Sensor API)
constexpr int kAndroidTypeAccelerometer      = ASENSOR_TYPE_ACCELEROMETER;       // 1
constexpr int kAndroidTypeMagneticField      = ASENSOR_TYPE_MAGNETIC_FIELD;      // 2
constexpr int kAndroidTypeGyroscope          = ASENSOR_TYPE_GYROSCOPE;           // 4
constexpr int kAndroidTypeLight              = ASENSOR_TYPE_LIGHT;               // 5
constexpr int kAndroidTypePressure           = 6;  // TYPE_PRESSURE
constexpr int kAndroidTypeProximity          = ASENSOR_TYPE_PROXIMITY;           // 8
constexpr int kAndroidTypeGravity            = 9;  // TYPE_GRAVITY
constexpr int kAndroidTypeLinearAcceleration = 10; // TYPE_LINEAR_ACCELERATION
constexpr int kAndroidTypeRotationVector     = 11; // TYPE_ROTATION_VECTOR
constexpr int kAndroidTypeStepCounter        = 19; // TYPE_STEP_COUNTER

[[nodiscard]] int ToAndroidSensorType(SensorType type) noexcept {
    switch (type) {
        case SensorType::Accelerometer:      return kAndroidTypeAccelerometer;
        case SensorType::LinearAcceleration: return kAndroidTypeLinearAcceleration;
        case SensorType::Gravity:            return kAndroidTypeGravity;
        case SensorType::Gyroscope:          return kAndroidTypeGyroscope;
        case SensorType::Magnetometer:       return kAndroidTypeMagneticField;
        case SensorType::RotationVector:     return kAndroidTypeRotationVector;
        case SensorType::Barometer:          return kAndroidTypePressure;
        case SensorType::AmbientLight:       return kAndroidTypeLight;
        case SensorType::Proximity:          return kAndroidTypeProximity;
        case SensorType::StepCounter:        return kAndroidTypeStepCounter;
        case SensorType::Count:              break;
    }
    return -1;
}
#endif

} // namespace

struct SensorManager::AndroidImpl {
#ifdef __ANDROID__
    ASensorManager* manager{nullptr};
    ALooper* looper{nullptr};
    ASensorEventQueue* queue{nullptr};
    std::array<const ASensor*, static_cast<size_t>(SensorType::Count)> sensors{};
    Vec3 filteredAccel{0.0f, kGravityStandard, 0.0f};
    bool hasFilteredAccel{false};
    uint64_t initialStepOffset{0};
    bool hasInitialSteps{false};
#endif
};

SensorManager::SensorManager()
    : m_android(new AndroidImpl{})
{
    m_accelHistX.reserve(kDefaultHistorySamples);
    m_accelHistY.reserve(kDefaultHistorySamples);
    m_accelHistZ.reserve(kDefaultHistorySamples);
    m_accelHistMag.reserve(kDefaultHistorySamples);
    m_gyroHistMag.reserve(kDefaultHistorySamples);
    m_magHistMag.reserve(kDefaultHistorySamples);
    m_baroAltHist.reserve(kDefaultHistorySamples);
}

SensorManager::~SensorManager() {
    Shutdown();
    delete m_android;
    m_android = nullptr;
}

bool SensorManager::Initialize() {
    if (m_initialized) return true;

    const auto initDefaultInfo = [&](SensorType t, const char* name, float maxRange, float res) {
        auto& info = m_infos[static_cast<size_t>(t)];
        info.type = t;
        info.name = name;
        info.vendor = "GuiKit Simulated IMU";
        info.maxRange = maxRange;
        info.resolution = res;
        info.minDelayUs = 5000;
        info.available = true;
        info.enabled = true;
    };

    initDefaultInfo(SensorType::Accelerometer,      "3-Axis Accelerometer",       156.9f, 0.0024f);
    initDefaultInfo(SensorType::LinearAcceleration, "Linear Acceleration",        156.9f, 0.0024f);
    initDefaultInfo(SensorType::Gravity,            "Gravity Vector",               9.81f, 0.001f);
    initDefaultInfo(SensorType::Gyroscope,          "3-Axis MEMS Gyroscope",       34.9f, 0.001f);
    initDefaultInfo(SensorType::Magnetometer,       "3-Axis Geomagnetic Sensor", 4900.0f, 0.15f);
    initDefaultInfo(SensorType::RotationVector,     "9-DOF Attitude Fusion",        1.0f, 0.0001f);
    initDefaultInfo(SensorType::Barometer,          "MEMS Barometric Pressure",  1260.0f, 0.01f);
    initDefaultInfo(SensorType::AmbientLight,       "Ambient Light Photodiode", 65535.0f, 1.0f);
    initDefaultInfo(SensorType::Proximity,          "IR Proximity Sensor",          5.0f, 1.0f);
    initDefaultInfo(SensorType::StepCounter,        "Hardware Pedometer",       100000.0f, 1.0f);

#ifdef __ANDROID__
    m_android->manager = ASensorManager_getInstance();
    if (m_android->manager) {
        m_android->looper = ALooper_forThread();
        if (!m_android->looper) {
            m_android->looper = ALooper_prepare(ALOOPER_PREPARE_ALLOW_NON_CALLBACKS);
        }
        m_android->queue = ASensorManager_createEventQueue(
            m_android->manager,
            m_android->looper,
            ALOOPER_POLL_CALLBACK,
            nullptr,
            nullptr
        );

        if (m_android->queue) {
            m_hasHardwareBackend = true;
            const int32_t delayUs = RateToDelayUs(m_rate);

            for (size_t i = 0; i < static_cast<size_t>(SensorType::Count); ++i) {
                const auto st = static_cast<SensorType>(i);
                const int androidType = ToAndroidSensorType(st);
                const ASensor* s = ASensorManager_getDefaultSensor(m_android->manager, androidType);
                m_android->sensors[i] = s;

                auto& info = m_infos[i];
                if (s) {
                    info.name = ASensor_getName(s) ? ASensor_getName(s) : info.name;
                    info.vendor = ASensor_getVendor(s) ? ASensor_getVendor(s) : "Android NDK";
                    info.resolution = ASensor_getResolution(s);
                    info.minDelayUs = ASensor_getMinDelay(s);
                    info.available = true;
                    if (ASensorEventQueue_enableSensor(m_android->queue, s) >= 0) {
                        const int32_t targetUs = std::max(delayUs, info.minDelayUs);
                        ASensorEventQueue_setEventRate(m_android->queue, s, targetUs);
                        info.enabled = true;
                    }
                } else {
                    info.available = false;
                    info.enabled = false;
                }
            }
        }
    }
#endif

    // Pre-fill history buffers so SignalView immediately displays a full window on startup
    for (size_t i = 0; i < kDefaultHistorySamples / 2; ++i) {
        StepSimulator(0.01);
        PushHistorySample(m_snapshot);
    }

    m_initialized = true;
    return true;
}

void SensorManager::Shutdown() {
    if (!m_initialized) return;

#ifdef __ANDROID__
    if (m_android && m_android->manager && m_android->queue) {
        for (size_t i = 0; i < static_cast<size_t>(SensorType::Count); ++i) {
            if (m_android->sensors[i] && m_infos[i].enabled) {
                ASensorEventQueue_disableSensor(m_android->queue, m_android->sensors[i]);
            }
            m_android->sensors[i] = nullptr;
        }
        ASensorManager_destroyEventQueue(m_android->manager, m_android->queue);
        m_android->queue = nullptr;
    }
#endif

    m_initialized = false;
}

void SensorManager::SetPaused(bool paused) {
    if (m_paused == paused) return;
    m_paused = paused;

#ifdef __ANDROID__
    if (m_android && m_android->queue) {
        const int32_t delayUs = RateToDelayUs(m_rate);
        for (size_t i = 0; i < static_cast<size_t>(SensorType::Count); ++i) {
            const ASensor* s = m_android->sensors[i];
            if (!s || !m_infos[i].enabled) continue;
            if (paused) {
                ASensorEventQueue_disableSensor(m_android->queue, s);
            } else {
                ASensorEventQueue_enableSensor(m_android->queue, s);
                ASensorEventQueue_setEventRate(m_android->queue, s, std::max(delayUs, m_infos[i].minDelayUs));
            }
        }
    }
#endif
}

bool SensorManager::SetSensorEnabled(SensorType type, bool enabled) {
    const auto idx = static_cast<size_t>(type);
    if (idx >= m_infos.size()) return false;

    auto& info = m_infos[idx];
    if (!info.available && !IsUsingSimulator()) return false;
    info.enabled = enabled;

#ifdef __ANDROID__
    if (m_android && m_android->queue && m_android->sensors[idx] && !m_paused) {
        if (enabled) {
            ASensorEventQueue_enableSensor(m_android->queue, m_android->sensors[idx]);
            const int32_t delayUs = std::max(RateToDelayUs(m_rate), info.minDelayUs);
            ASensorEventQueue_setEventRate(m_android->queue, m_android->sensors[idx], delayUs);
        } else {
            ASensorEventQueue_disableSensor(m_android->queue, m_android->sensors[idx]);
        }
    }
#endif
    return true;
}

bool SensorManager::IsSensorEnabled(SensorType type) const noexcept {
    const auto idx = static_cast<size_t>(type);
    return idx < m_infos.size() ? m_infos[idx].enabled : false;
}

bool SensorManager::IsSensorAvailable(SensorType type) const noexcept {
    const auto idx = static_cast<size_t>(type);
    return idx < m_infos.size() ? (m_infos[idx].available || IsUsingSimulator()) : false;
}

void SensorManager::SetSamplingRate(SensorRate rate) {
    m_rate = rate;
#ifdef __ANDROID__
    if (m_android && m_android->queue && !m_paused) {
        const int32_t delayUs = RateToDelayUs(m_rate);
        for (size_t i = 0; i < static_cast<size_t>(SensorType::Count); ++i) {
            if (m_android->sensors[i] && m_infos[i].enabled) {
                ASensorEventQueue_setEventRate(
                    m_android->queue,
                    m_android->sensors[i],
                    std::max(delayUs, m_infos[i].minDelayUs)
                );
            }
        }
    }
#endif
}

float SensorManager::GetEstimatedSampleRateHz() const noexcept {
    switch (m_rate) {
        case SensorRate::Ui:      return 16.6f;
        case SensorRate::Normal:  return 50.0f;
        case SensorRate::Game:    return 100.0f;
        case SensorRate::Fastest: return 200.0f;
    }
    return 100.0f;
}

bool SensorManager::IsUsingSimulator() const noexcept {
    return m_forceSimulator || !m_hasHardwareBackend;
}

void SensorManager::UpdateFrame(double dtSeconds) {
    if (m_paused) return;

    const double clampedDt = std::clamp(dtSeconds, 0.001, 0.1);
    if (IsUsingSimulator()) {
        StepSimulator(clampedDt);
    } else {
        PollAndroidSensors();
    }
    PushHistorySample(m_snapshot);
}

void SensorManager::PollAndroidSensors() {
#ifdef __ANDROID__
    if (!m_android || !m_android->queue) return;

    ASensorEvent ev{};
    bool gotEvent = false;
    const float prevAlt = m_snapshot.barometricAltitudeMeters;

    // Remap physical sensor axes to current screen orientation (Portrait vs Landscape)
    const SDL_DisplayID displayId = SDL_GetPrimaryDisplay();
    const SDL_DisplayOrientation dispOrient = displayId ? SDL_GetCurrentDisplayOrientation(displayId) : SDL_ORIENTATION_PORTRAIT;
    const auto remapToScreen = [dispOrient](float x, float y, float z) noexcept -> Vec3 {
        switch (dispOrient) {
            case SDL_ORIENTATION_LANDSCAPE:         return {-y,  x, z}; // 90 deg CCW (ROTATION_90)
            case SDL_ORIENTATION_LANDSCAPE_FLIPPED: return { y, -x, z}; // 90 deg CW  (ROTATION_270)
            case SDL_ORIENTATION_PORTRAIT_FLIPPED:  return {-x, -y, z}; // 180 deg    (ROTATION_180)
            default:                                return { x,  y, z}; // Portrait   (ROTATION_0)
        }
    };

    const bool hasAccelerometer = m_infos[static_cast<size_t>(SensorType::Accelerometer)].available;

    while (ASensorEventQueue_getEvents(m_android->queue, &ev, 1) > 0) {
        gotEvent = true;
        m_snapshot.timestampNs = static_cast<uint64_t>(ev.timestamp);

        switch (ev.type) {
            case kAndroidTypeAccelerometer: {
                m_snapshot.accelerometer = remapToScreen(ev.acceleration.x, ev.acceleration.y, ev.acceleration.z);
                m_snapshot.gLoad = m_snapshot.accelerometer.Magnitude() / kGravityStandard;
                m_snapshot.slipSkid = std::clamp(-m_snapshot.accelerometer.x / kGravityStandard, -1.0f, 1.0f);

                // Smooth low-pass filter on hardware MEMS accelerometer to eliminate hand tremor
                // while remaining 100% immune to vendor pseudo-gyro / RotationVector 45-deg flips
                constexpr float kAccelSmoothing = 0.22f;
                if (!m_android->hasFilteredAccel) {
                    m_android->filteredAccel = m_snapshot.accelerometer;
                    m_android->hasFilteredAccel = true;
                } else {
                    m_android->filteredAccel.x += kAccelSmoothing * (m_snapshot.accelerometer.x - m_android->filteredAccel.x);
                    m_android->filteredAccel.y += kAccelSmoothing * (m_snapshot.accelerometer.y - m_android->filteredAccel.y);
                    m_android->filteredAccel.z += kAccelSmoothing * (m_snapshot.accelerometer.z - m_android->filteredAccel.z);
                }

                const Vec3& g = m_android->filteredAccel;
                const float xyMag = std::sqrt(g.x * g.x + g.y * g.y);
                const float yzMag = std::sqrt(g.y * g.y + g.z * g.z);
                if (m_attitudeRef == AttitudeReference::Handheld) {
                    // Real Artificial Horizon (Vertical in hand = 0 deg Horizon, Flat on table = -90 deg, Sky = +90 deg)
                    // Non-negative xyMag denominator ensures Pitch stays strictly in [0, +90] whenever g.z < 0 (Blue Sky)
                    // and strictly in [-90, 0] whenever g.z > 0 (Brown Ground), never jumping at 45 deg!
                    m_rawUntaredPitchDeg = std::clamp(std::atan2(-g.z, xyMag) * kRadToDeg, -90.0f, 90.0f);
                } else {
                    // Flat on table = 0 deg
                    m_rawUntaredPitchDeg = std::clamp(std::atan2(g.y, g.z) * kRadToDeg, -90.0f, 90.0f);
                }
                m_rawUntaredRollDeg = std::atan2(-g.x, yzMag) * kRadToDeg;
                m_snapshot.attitude.pitchDeg = std::clamp(
                    m_rawUntaredPitchDeg - (m_hasAttitudeTare ? m_pitchTareDeg : 0.0f),
                    -90.0f,
                    90.0f
                );
                m_snapshot.attitude.rollDeg = m_rawUntaredRollDeg - (m_hasAttitudeTare ? m_rollTareDeg : 0.0f);
                m_snapshot.attitudeQuat = EulerToQuaternion(
                    m_snapshot.attitude.pitchDeg,
                    m_snapshot.attitude.rollDeg,
                    m_snapshot.attitude.yawDeg
                );
                break;
            }

            case kAndroidTypeLinearAcceleration:
                m_snapshot.linearAcceleration = remapToScreen(ev.acceleration.x, ev.acceleration.y, ev.acceleration.z);
                break;

            case kAndroidTypeGravity:
                m_snapshot.gravity = remapToScreen(ev.vector.x, ev.vector.y, ev.vector.z);
                break;

            case kAndroidTypeGyroscope:
                m_snapshot.gyroscope = remapToScreen(ev.vector.x, ev.vector.y, ev.vector.z);
                break;

            case kAndroidTypeMagneticField:
                m_snapshot.magnetometer = remapToScreen(ev.magnetic.x, ev.magnetic.y, ev.magnetic.z);
                break;

            case kAndroidTypeRotationVector: {
                const Vec3 qv = remapToScreen(ev.data[0], ev.data[1], ev.data[2]);
                const float qw = (ev.data[3] != 0.0f)
                    ? ev.data[3]
                    : std::sqrt(std::max(0.0f, 1.0f - (qv.x * qv.x + qv.y * qv.y + qv.z * qv.z)));
                EulerAngles rawEuler = QuaternionToEuler({qw, qv.x, qv.y, qv.z}, m_attitudeRef);
                m_snapshot.attitude.yawDeg = rawEuler.yawDeg;
                m_snapshot.attitude.headingDeg = rawEuler.headingDeg;
                if (!hasAccelerometer) {
                    m_snapshot.attitudeQuat = {qw, qv.x, qv.y, qv.z};
                    m_rawUntaredPitchDeg = rawEuler.pitchDeg;
                    m_rawUntaredRollDeg  = rawEuler.rollDeg;
                    m_snapshot.attitude.pitchDeg = std::clamp(
                        rawEuler.pitchDeg - (m_hasAttitudeTare ? m_pitchTareDeg : 0.0f),
                        -90.0f,
                        90.0f
                    );
                    m_snapshot.attitude.rollDeg = rawEuler.rollDeg - (m_hasAttitudeTare ? m_rollTareDeg : 0.0f);
                }
                break;
            }

            case kAndroidTypePressure:
                m_snapshot.pressureHpa = ev.pressure;
                m_snapshot.seaLevelPressureHpa = m_seaLevelPressureHpa;
                m_snapshot.barometricAltitudeMeters = PressureToAltitudeMeters(ev.pressure, m_seaLevelPressureHpa);
                break;

            case kAndroidTypeLight:
                m_snapshot.ambientLightLux = ev.light;
                break;

            case kAndroidTypeProximity:
                m_snapshot.proximityCm = ev.distance;
                m_snapshot.proximityNear = (ev.distance < m_infos[static_cast<size_t>(SensorType::Proximity)].maxRange);
                break;

            case kAndroidTypeStepCounter: {
                const auto rawSteps = static_cast<uint64_t>(ev.u64.step_counter);
                if (!m_android->hasInitialSteps) {
                    m_android->initialStepOffset = rawSteps;
                    m_android->hasInitialSteps = true;
                }
                m_snapshot.stepCount = (rawSteps >= m_android->initialStepOffset)
                    ? (rawSteps - m_android->initialStepOffset)
                    : rawSteps;
                break;
            }
            default:
                break;
        }
    }

    if (gotEvent) {
        ++m_snapshot.sampleSequence;
        m_snapshot.verticalSpeedMps = (m_snapshot.barometricAltitudeMeters - prevAlt) * 30.0f;
    }
#endif
}

void SensorManager::StepSimulator(double dtSeconds) {
    m_simTimeSec += dtSeconds;
    const float t = static_cast<float>(m_simTimeSec);

    float pitchDeg = m_simConfig.manualPitchDeg;
    float rollDeg  = m_simConfig.manualRollDeg;
    float yawDeg   = m_simConfig.manualHeadingDeg;

    if (m_simConfig.animateMotion) {
        pitchDeg += 8.5f * std::sin(t * 0.85f) + 2.0f * std::cos(t * 2.1f);
        rollDeg  += 18.0f * std::sin(t * 0.55f) + 3.5f * std::sin(t * 1.4f);
        yawDeg    = std::fmod(m_simConfig.manualHeadingDeg + t * 6.0f + 360.0f, 360.0f);
    }

    m_snapshot.timestampNs = SDL_GetTicksNS();
    ++m_snapshot.sampleSequence;

    m_snapshot.attitudeQuat = EulerToQuaternion(pitchDeg, rollDeg, yawDeg);
    m_snapshot.attitude.pitchDeg   = pitchDeg;
    m_snapshot.attitude.rollDeg    = rollDeg;
    m_snapshot.attitude.yawDeg     = yawDeg;
    m_snapshot.attitude.headingDeg = std::fmod(yawDeg + 360.0f, 360.0f);

    // Gravity projection in device body frame
    const float pitchRad = pitchDeg * kDegToRad;
    const float rollRad  = rollDeg  * kDegToRad;
    const Vec3 grav{
        kGravityStandard * std::sin(rollRad) * std::cos(pitchRad),
        -kGravityStandard * std::sin(pitchRad),
        kGravityStandard * std::cos(rollRad) * std::cos(pitchRad)
    };
    m_snapshot.gravity = grav;

    // Multi-harmonic vibration + motion linear acceleration
    const float vibOmega = 2.0f * std::numbers::pi_v<float> * m_simConfig.vibrationFreqHz;
    const float vibAmpMs2 = m_simConfig.vibrationAmpG * kGravityStandard;
    const Vec3 linAcc{
        vibAmpMs2 * std::sin(vibOmega * t) + 0.35f * vibAmpMs2 * std::sin(2.4f * vibOmega * t),
        vibAmpMs2 * std::cos(vibOmega * t * 0.9f),
        vibAmpMs2 * 0.7f * std::sin(vibOmega * t * 1.3f)
    };
    m_snapshot.linearAcceleration = linAcc;
    m_snapshot.accelerometer = {
        grav.x + linAcc.x,
        grav.y + linAcc.y,
        grav.z + linAcc.z
    };
    m_snapshot.gLoad = m_snapshot.accelerometer.Magnitude() / kGravityStandard;
    m_snapshot.slipSkid = std::clamp(std::sin(rollRad) * 0.35f, -1.0f, 1.0f);

    // Angular velocity (rad/s)
    if (m_simConfig.animateMotion) {
        m_snapshot.gyroscope = {
            (8.5f * 0.85f * kDegToRad) * std::cos(t * 0.85f),
            (18.0f * 0.55f * kDegToRad) * std::cos(t * 0.55f),
            6.0f * kDegToRad
        };
    } else {
        m_snapshot.gyroscope = {0.0f, 0.0f, 0.0f};
    }

    // Geomagnetic field vector (~48 uT total field rotated by heading & inclination)
    const float headRad = m_snapshot.attitude.headingDeg * kDegToRad;
    m_snapshot.magnetometer = {
        22.0f * std::cos(headRad) + 0.6f * std::sin(vibOmega * t),
        22.0f * std::sin(headRad) + 0.6f * std::cos(vibOmega * t),
        -42.5f + 1.2f * std::sin(pitchRad)
    };

    // Barometer & altitude
    const float prevAlt = m_snapshot.barometricAltitudeMeters;
    const float pressureWave = m_simConfig.animateMotion ? (0.45f * std::sin(t * 0.25f)) : 0.0f;
    m_snapshot.pressureHpa = m_simConfig.simPressureHpa + pressureWave;
    m_snapshot.seaLevelPressureHpa = m_seaLevelPressureHpa;
    m_snapshot.barometricAltitudeMeters = PressureToAltitudeMeters(m_snapshot.pressureHpa, m_seaLevelPressureHpa);
    m_snapshot.verticalSpeedMps = (dtSeconds > 0.0001)
        ? static_cast<float>((m_snapshot.barometricAltitudeMeters - prevAlt) / dtSeconds)
        : 0.0f;

    // Ambient light & proximity & pedometer
    m_snapshot.ambientLightLux = std::max(1.0f, m_simConfig.simLightLux + 25.0f * std::sin(t * 0.4f));
    m_snapshot.proximityNear = m_simConfig.simProximityNear;
    m_snapshot.proximityCm = m_simConfig.simProximityNear ? 0.0f : 5.0f;
    if (m_simConfig.animateMotion) {
        m_snapshot.stepCount = static_cast<uint64_t>(t * 1.6f);
    }
}

void SensorManager::PushHistorySample(const SensorSnapshot& snap) {
    AppendRingSample(m_accelHistX, snap.accelerometer.x, kDefaultHistorySamples);
    AppendRingSample(m_accelHistY, snap.accelerometer.y, kDefaultHistorySamples);
    AppendRingSample(m_accelHistZ, snap.accelerometer.z, kDefaultHistorySamples);
    // Normalize magnitude around 0 (-1..+1 G dynamic range for SignalView)
    const float normG = std::clamp((snap.accelerometer.Magnitude() - kGravityStandard) / (2.0f * kGravityStandard), -1.0f, 1.0f);
    AppendRingSample(m_accelHistMag, normG, kDefaultHistorySamples);
    AppendRingSample(m_gyroHistMag, std::clamp(snap.gyroscope.Magnitude() / 4.0f, -1.0f, 1.0f), kDefaultHistorySamples);
    AppendRingSample(m_magHistMag, snap.magnetometer.Magnitude(), kDefaultHistorySamples);
    AppendRingSample(m_baroAltHist, snap.barometricAltitudeMeters, kDefaultHistorySamples);
}

void SensorManager::SetAttitudeReference(AttitudeReference ref) noexcept {
    if (m_attitudeRef != ref) {
        m_attitudeRef = ref;
        ResetAttitudeTare();
    }
}

void SensorManager::TareAttitude() noexcept {
    m_pitchTareDeg = m_rawUntaredPitchDeg;
    m_rollTareDeg  = m_rawUntaredRollDeg;
    m_hasAttitudeTare = true;
    m_snapshot.attitude.pitchDeg = 0.0f;
    m_snapshot.attitude.rollDeg  = 0.0f;
}

void SensorManager::ResetAttitudeTare() noexcept {
    m_hasAttitudeTare = false;
    m_pitchTareDeg = 0.0f;
    m_rollTareDeg  = 0.0f;
}

float SensorManager::PressureToAltitudeMeters(float pressureHpa, float seaLevelHpa) noexcept {
    if (pressureHpa <= 0.0f || seaLevelHpa <= 0.0f) return 0.0f;
    // International Barometric Formula: h = 44330 * (1 - (P / P0)^(1/5.255))
    return 44330.0f * (1.0f - std::pow(pressureHpa / seaLevelHpa, 0.190294957f));
}

EulerAngles SensorManager::QuaternionToEuler(Quaternion q, AttitudeReference ref) noexcept {
    EulerAngles out{};

    // World Up (Zenith) unit vector expressed in smartphone screen frame (X=Right, Y=Up, Z=Out):
    //   - Phone flat on table (screen up):        u = (0, 0, +1)
    //   - Phone held vertically upright (in hand): u = (0, +1, 0)
    //   - Phone tilted back (camera pointing up):  u = (0, cos, -sin)
    const float ux = 2.0f * (q.x * q.z - q.w * q.y);
    const float uy = 2.0f * (q.y * q.z + q.w * q.x);
    const float uz = 1.0f - 2.0f * (q.x * q.x + q.y * q.y);

    const float xyMag = std::sqrt(ux * ux + uy * uy);
    const float yzMag = std::sqrt(uy * uy + uz * uz);

    if (ref == AttitudeReference::Handheld) {
        // Real Artificial Horizon (Vertical panel in front of pilot = 0 deg Horizon):
        //   - Vertical in hand (xyMag=1, uz=0)  ->   0 deg (Horizon)
        //   - Flat on table    (xyMag=0, uz=+1) -> -90 deg (Ground / Nadir)
        //   - Camera to zenith (xyMag=0, uz=-1) -> +90 deg (Sky / Zenith)
        out.pitchDeg = std::clamp(std::atan2(-uz, xyMag) * kRadToDeg, -90.0f, 90.0f);
    } else {
        // FlatTable reference (0 deg when phone lies flat on a horizontal table):
        out.pitchDeg = std::clamp(std::atan2(uy, uz) * kRadToDeg, -90.0f, 90.0f);
    }

    // Roll (Крен): lateral bank angle around viewing axis using non-negative yzMag denominator
    // so pitching through 45 deg or 90 deg NEVER flips Roll by 180 deg!
    out.rollDeg = std::atan2(-ux, yzMag) * kRadToDeg;

    // Yaw / Heading (Курс): device +X (right edge) projected onto World East-North plane (pitch-invariant)
    const float xEast  = 1.0f - 2.0f * (q.y * q.y + q.z * q.z);
    const float xNorth = 2.0f * (q.x * q.y + q.w * q.z);
    out.yawDeg = std::atan2(-xNorth, xEast) * kRadToDeg;
    out.headingDeg = std::fmod(out.yawDeg + 360.0f, 360.0f);

    return out;
}

} // namespace guikit::mobile
