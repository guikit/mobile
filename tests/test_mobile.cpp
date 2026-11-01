#include <cassert>
#include <cmath>
#include <iostream>
#include "guikit/Mobile.hpp"

using namespace guikit;
using namespace guikit::mobile;

namespace {

void TestSensors() {
    SensorManager sensors;
    assert(sensors.Initialize());
    assert(!sensors.GetSensorInfos().empty());

    // ISA Barometric formula at standard sea level pressure (1013.25 hPa -> 0 m)
    const float alt0 = SensorManager::PressureToAltitudeMeters(1013.25f, 1013.25f);
    assert(std::abs(alt0) < 0.05f);

    // Lower pressure (~900 hPa) -> positive altitude (~988 m)
    const float alt900 = SensorManager::PressureToAltitudeMeters(900.0f, 1013.25f);
    assert(alt900 > 900.0f && alt900 < 1100.0f);

    // Identity quaternion -> 0 pitch, 0 roll, 0 yaw
    const EulerAngles euler0 = SensorManager::QuaternionToEuler({1.0f, 0.0f, 0.0f, 0.0f});
    assert(std::abs(euler0.pitchDeg) < 1e-3f);
    assert(std::abs(euler0.rollDeg) < 1e-3f);

    // Pure X-axis rotation (+30 deg forward/backward tilt) -> Pitch = +30 deg, Roll = 0 deg
    const float rad15 = 15.0f * (3.14159265f / 180.0f);
    const EulerAngles eulerPitch = SensorManager::QuaternionToEuler({std::cos(rad15), std::sin(rad15), 0.0f, 0.0f});
    assert(std::abs(eulerPitch.pitchDeg - 30.0f) < 0.05f);
    assert(std::abs(eulerPitch.rollDeg) < 0.05f);

    // Pure Y-axis rotation (+30 deg left/right bank) -> Roll = +30 deg, Pitch = 0 deg
    const EulerAngles eulerRoll = SensorManager::QuaternionToEuler({std::cos(rad15), 0.0f, std::sin(rad15), 0.0f});
    assert(std::abs(eulerRoll.rollDeg - 30.0f) < 0.05f);
    assert(std::abs(eulerRoll.pitchDeg) < 0.05f);

    // Handheld reference (Real Artificial Horizon):
    // 1) Phone held vertically upright (+90 deg from table) -> 0 deg Pitch (Horizon) & 0 deg Roll
    const float rad45 = 45.0f * (3.14159265f / 180.0f);
    const EulerAngles eulerHand0 = SensorManager::QuaternionToEuler(
        {std::cos(rad45), std::sin(rad45), 0.0f, 0.0f},
        AttitudeReference::Handheld
    );
    assert(std::abs(eulerHand0.pitchDeg) < 0.05f);
    assert(std::abs(eulerHand0.rollDeg) < 0.05f);

    // 2) Phone tilted +45 deg back toward sky (+135 deg from table) -> +45 deg Pitch (Blue Sky) & 0 deg Roll
    const float rad67_5 = 67.5f * (3.14159265f / 180.0f);
    const EulerAngles eulerHandPlus45 = SensorManager::QuaternionToEuler(
        {std::cos(rad67_5), std::sin(rad67_5), 0.0f, 0.0f},
        AttitudeReference::Handheld
    );
    assert(std::abs(eulerHandPlus45.pitchDeg - 45.0f) < 0.05f);
    assert(std::abs(eulerHandPlus45.rollDeg) < 0.05f);

    // 3) Phone tilted +60 deg past vertical (+150 deg from table) -> +60 deg Pitch (Blue Sky) & 0 deg Roll (never jumps to -90 deg!)
    const float rad75 = 75.0f * (3.14159265f / 180.0f);
    const EulerAngles eulerHandPlus60 = SensorManager::QuaternionToEuler(
        {std::cos(rad75), std::sin(rad75), 0.0f, 0.0f},
        AttitudeReference::Handheld
    );
    assert(std::abs(eulerHandPlus60.pitchDeg - 60.0f) < 0.05f);
    assert(std::abs(eulerHandPlus60.rollDeg) < 0.05f);

    // 4) Phone tilted -45 deg forward toward table (+45 deg from table) -> -45 deg Pitch (Brown Ground) & 0 deg Roll
    const float rad22_5 = 22.5f * (3.14159265f / 180.0f);
    const EulerAngles eulerHandMinus45 = SensorManager::QuaternionToEuler(
        {std::cos(rad22_5), std::sin(rad22_5), 0.0f, 0.0f},
        AttitudeReference::Handheld
    );
    assert(std::abs(eulerHandMinus45.pitchDeg - (-45.0f)) < 0.05f);
    assert(std::abs(eulerHandMinus45.rollDeg) < 0.05f);

    sensors.UpdateFrame(0.016);
    const auto& snap = sensors.GetSnapshot();
    assert(snap.sampleSequence > 0);
    assert(snap.gLoad > 0.2f && snap.gLoad < 3.0f);
    assert(!sensors.GetAccelMagnitudeHistory().empty());

    sensors.Shutdown();
    std::cout << "[PASS] TestSensors\n";
}

void TestCamera() {
    CameraService cam;
    assert(cam.Initialize());
    assert(!cam.GetDevices().empty());
    assert(cam.IsStreaming());

    cam.UpdateFrame(0.016, nullptr);
    const auto img = cam.GetImageViewData();
    assert(!img.Empty());
    assert(img.channels == 3);

    const auto& stats = cam.GetFrameStats();
    assert(stats.width == img.width && stats.height == img.height);
    assert(stats.meanLuminance > 1.0f);

    cam.SetTorchEnabled(true);
    assert(cam.IsTorchEnabled());
    assert(cam.CaptureSnapshot());
    assert(cam.GetCapturedPhotoCount() == 1);
    assert(!cam.GetLastCapturedRgb().empty());

    if (cam.GetDevices().size() > 1) {
        const size_t prevIdx = cam.GetActiveDeviceIndex();
        assert(cam.SwitchCameraFacing());
        assert(cam.GetActiveDeviceIndex() != prevIdx);
    }

    cam.Shutdown();
    std::cout << "[PASS] TestCamera\n";
}

void TestLocation() {
    LocationService loc;
    assert(loc.Initialize());

    const GeoCoord a{44.6000, 33.5000};
    const GeoCoord b{44.6100, 33.5000};
    const double dist = LocationService::DistanceMeters(a, b);
    assert(dist > 1050.0 && dist < 1160.0);

    const float brgNorth = LocationService::BearingDegrees(a, b);
    assert(brgNorth < 1.0f || brgNorth > 359.0f);

    loc.UpdateFrame(0.25);
    assert(loc.GetLatestFix().hasFix);
    assert(!loc.GetBreadcrumbTrack().empty());

    loc.Shutdown();
    std::cout << "[PASS] TestLocation\n";
}

void TestAudioCapture() {
    AudioCapture audio;
    audio.SetForceSyntheticSignal(true);
    audio.SetSynthFundamentalHz(1000.0f);
    assert(audio.Initialize(48000));

    for (int i = 0; i < 8; ++i) {
        audio.UpdateFrame(0.02);
    }

    assert(audio.GetWaveformSamples().size() == AudioCapture::kWaveformWindowSamples);
    assert(audio.GetSpectrumSliceDb().size() == AudioCapture::kSpectrumBins);
    assert(audio.GetSpectrumSliceSequence() > 0);
    assert(std::abs(audio.GetDominantFreqHz() - 1000.0f) < 120.0f);

    audio.Shutdown();
    std::cout << "[PASS] TestAudioCapture\n";
}

void TestTouchAndSystem() {
    Context ui;
    TouchGestureRecognizer touch;
    SystemBridge sys;
    assert(sys.Initialize(nullptr));

    sys.TriggerHaptic(HapticStyle::Light);
    assert(sys.GetHapticPulseCount() == 1);

    SDL_Event downEv{};
    downEv.type = SDL_EVENT_FINGER_DOWN;
    downEv.tfinger.fingerID = 1;
    downEv.tfinger.x = 0.5f;
    downEv.tfinger.y = 0.5f;
    assert(touch.ProcessSdlEvent(downEv, ui, {800.0f, 600.0f}));
    assert(touch.GetState().activeFingerCount == 1);

    SDL_Event upEv{};
    upEv.type = SDL_EVENT_FINGER_UP;
    upEv.tfinger.fingerID = 1;
    upEv.tfinger.x = 0.5f;
    upEv.tfinger.y = 0.4f;
    assert(touch.ProcessSdlEvent(upEv, ui, {800.0f, 600.0f}));
    assert(touch.GetState().activeFingerCount == 0);

    sys.Shutdown();
    std::cout << "[PASS] TestTouchAndSystem\n";
}

} // namespace

int main() {
    TestSensors();
    TestCamera();
    TestLocation();
    TestAudioCapture();
    TestTouchAndSystem();
    std::cout << "All guikit::mobile tests passed!\n";
    return 0;
}
