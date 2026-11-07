#pragma once

#include "guikit/context.hpp"
#include "guikit/mobile/audio_capture.hpp"
#include "guikit/mobile/camera.hpp"
#include "guikit/mobile/location.hpp"
#include "guikit/mobile/sensors.hpp"
#include "guikit/mobile/system_bridge.hpp"
#include "guikit/mobile/touch_gesture.hpp"
#include "guikit/mobile/types.hpp"

namespace guikit::mobile {

/// Unified Mobile Hardware & Lifecycle Runtime for GuiKit applications.
/// Aggregates Sensors, Camera, GNSS Location, Microphone Audio Capture, System Bridge (Permissions/Haptics/SafeArea),
/// and Multi-Touch Gesture recognition into a single backend-agnostic lifecycle coordinator.
class MobileRuntime {
public:
    MobileRuntime() = default;
    ~MobileRuntime();

    MobileRuntime(const MobileRuntime&) = delete;
    MobileRuntime& operator=(const MobileRuntime&) = delete;

    /// Initializes all mobile subsystems.
    bool Initialize(void* nativeWindowHandle = nullptr, void* nativeRendererHandle = nullptr);
    void Shutdown();

    /// Backend-agnostic mobile lifecycle and system navigation hooks.
    void OnEnterBackground();
    void OnEnterForeground(Context& ui);
    void OnBackNavigation(Context& ui);

    /// Handles a native platform event (e.g. `const SDL_Event*`) for background/foreground transitions,
    /// Android hardware/gesture Back button, and multi-touch gesture translation.
    bool ProcessEvent(const void* nativeEvent, Context& ui, Vec2 viewportSizePx);

    /// Updates all active hardware/simulator subsystems for the current frame.
    /// Automatically requests a UI repaint (`ui.RequestRepaint(1)`) when live sensor/camera/audio streaming is active.
    void UpdateFrame(double dtSeconds, Context& ui);

    [[nodiscard]] bool IsInBackground() const noexcept { return m_inBackground; }

    // --- Subsystem Accessors ---
    [[nodiscard]] SensorManager& Sensors() noexcept { return m_sensors; }
    [[nodiscard]] const SensorManager& Sensors() const noexcept { return m_sensors; }

    [[nodiscard]] CameraService& Camera() noexcept { return m_camera; }
    [[nodiscard]] const CameraService& Camera() const noexcept { return m_camera; }

    [[nodiscard]] LocationService& Location() noexcept { return m_location; }
    [[nodiscard]] const LocationService& Location() const noexcept { return m_location; }

    [[nodiscard]] AudioCapture& Audio() noexcept { return m_audio; }
    [[nodiscard]] const AudioCapture& Audio() const noexcept { return m_audio; }

    [[nodiscard]] SystemBridge& System() noexcept { return m_system; }
    [[nodiscard]] const SystemBridge& System() const noexcept { return m_system; }

    [[nodiscard]] TouchGestureRecognizer& Touch() noexcept { return m_touch; }
    [[nodiscard]] const TouchGestureRecognizer& Touch() const noexcept { return m_touch; }

private:
    void* m_window{nullptr};
    void* m_renderer{nullptr};
    SensorManager m_sensors{};
    CameraService m_camera{};
    LocationService m_location{};
    AudioCapture m_audio{};
    SystemBridge m_system{};
    TouchGestureRecognizer m_touch{};
    bool m_initialized{false};
    bool m_inBackground{false};
};

} // namespace guikit::mobile
