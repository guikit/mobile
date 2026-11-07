#include "guikit/mobile/Sensors.hpp"

namespace guikit::mobile {

SensorManager::SensorManager() = default;

SensorManager::~SensorManager() {
    Shutdown();
}

bool SensorManager::Initialize() {
    SyncSimConfigToHal();
    const bool ok = m_hal.initialize();
    SyncFromHal();
    return ok;
}

void SensorManager::Shutdown() {
    m_hal.shutdown();
}

void SensorManager::SetPaused(bool paused) {
    m_hal.set_paused(paused);
}

bool SensorManager::SetSensorEnabled(SensorType type, bool enabled) {
    const bool ok = m_hal.set_sensor_enabled(static_cast<keel::SensorType>(type), enabled);
    SyncFromHal();
    return ok;
}

bool SensorManager::IsSensorEnabled(SensorType type) const noexcept {
    return m_hal.is_sensor_enabled(static_cast<keel::SensorType>(type));
}

bool SensorManager::IsSensorAvailable(SensorType type) const noexcept {
    return m_hal.is_sensor_available(static_cast<keel::SensorType>(type));
}

void SensorManager::SetSamplingRate(SensorRate rate) {
    m_hal.set_sampling_rate(static_cast<keel::SensorRate>(rate));
}

void SensorManager::SetAttitudeReference(AttitudeReference ref) noexcept {
    m_hal.set_attitude_reference(static_cast<keel::AttitudeReference>(ref));
    SyncFromHal();
}

void SensorManager::TareAttitude() noexcept {
    m_hal.tare_attitude();
    SyncFromHal();
}

void SensorManager::ResetAttitudeTare() noexcept {
    m_hal.reset_attitude_tare();
    SyncFromHal();
}

void SensorManager::UpdateFrame(double dtSeconds) {
    SyncSimConfigToHal();
    m_hal.update_frame(dtSeconds);
    SyncFromHal();
}

void SensorManager::SyncSimConfigToHal() noexcept {
    auto& dst = m_hal.sim_config();
    dst.animate_motion = m_simConfig.animateMotion;
    dst.manual_pitch_deg = m_simConfig.manualPitchDeg;
    dst.manual_roll_deg = m_simConfig.manualRollDeg;
    dst.manual_heading_deg = m_simConfig.manualHeadingDeg;
    dst.vibration_freq_hz = m_simConfig.vibrationFreqHz;
    dst.vibration_amp_g = m_simConfig.vibrationAmpG;
    dst.sim_pressure_hpa = m_simConfig.simPressureHpa;
    dst.sim_light_lux = m_simConfig.simLightLux;
    dst.sim_proximity_near = m_simConfig.simProximityNear;
}

void SensorManager::SyncFromHal() {
    const auto& hs = m_hal.snapshot();
    m_snapshot.timestampNs = hs.timestamp_ns;
    m_snapshot.sampleSequence = hs.sample_sequence;
    m_snapshot.accelerometer = {hs.accelerometer.x, hs.accelerometer.y, hs.accelerometer.z};
    m_snapshot.linearAcceleration = {hs.linear_acceleration.x, hs.linear_acceleration.y, hs.linear_acceleration.z};
    m_snapshot.gravity = {hs.gravity.x, hs.gravity.y, hs.gravity.z};
    m_snapshot.gyroscope = {hs.gyroscope.x, hs.gyroscope.y, hs.gyroscope.z};
    m_snapshot.magnetometer = {hs.magnetometer.x, hs.magnetometer.y, hs.magnetometer.z};
    m_snapshot.attitudeQuat = {hs.attitude_quat.w, hs.attitude_quat.x, hs.attitude_quat.y, hs.attitude_quat.z};
    m_snapshot.attitude = {
        hs.attitude.pitch_deg,
        hs.attitude.roll_deg,
        hs.attitude.yaw_deg,
        hs.attitude.heading_deg
    };
    m_snapshot.gLoad = hs.g_load;
    m_snapshot.slipSkid = hs.slip_skid;
    m_snapshot.pressureHpa = hs.pressure_hpa;
    m_snapshot.seaLevelPressureHpa = hs.sea_level_pressure_hpa;
    m_snapshot.barometricAltitudeMeters = hs.barometric_altitude_meters;
    m_snapshot.verticalSpeedMps = hs.vertical_speed_mps;
    m_snapshot.ambientLightLux = hs.ambient_light_lux;
    m_snapshot.proximityCm = hs.proximity_cm;
    m_snapshot.proximityNear = hs.proximity_near;
    m_snapshot.stepCount = hs.step_count;

    const auto infos = m_hal.sensor_infos();
    for (size_t i = 0; i < m_infos.size() && i < infos.size(); ++i) {
        m_infos[i].type = static_cast<SensorType>(infos[i].type);
        m_infos[i].name.assign(infos[i].name.data(), infos[i].name.size());
        m_infos[i].vendor.assign(infos[i].vendor.data(), infos[i].vendor.size());
        m_infos[i].maxRange = infos[i].max_range;
        m_infos[i].resolution = infos[i].resolution;
        m_infos[i].minDelayUs = infos[i].min_delay_us;
        m_infos[i].available = infos[i].available;
        m_infos[i].enabled = infos[i].enabled;
    }
}

float SensorManager::PressureToAltitudeMeters(float pressureHpa, float seaLevelHpa) noexcept {
    return keel::SensorManager::pressure_to_altitude_meters(pressureHpa, seaLevelHpa);
}

EulerAngles SensorManager::QuaternionToEuler(Quaternion q, AttitudeReference ref) noexcept {
    const auto e = keel::SensorManager::quaternion_to_euler(
        keel::Quaternion{q.w, q.x, q.y, q.z},
        static_cast<keel::AttitudeReference>(ref)
    );
    return EulerAngles{e.pitch_deg, e.roll_deg, e.yaw_deg, e.heading_deg};
}

} // namespace guikit::mobile
