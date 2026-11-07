#pragma once

#include <array>
#include <cstdint>
#include <span>
#include <string>
#include <vector>
#include "guikit/mobile/Types.hpp"
#include "guikit/widgets/ImageView.hpp"
#include "keel/mobile/camera.h"

namespace guikit::mobile {

/// Physical facing direction of a mobile camera sensor.
enum class CameraFacing : uint8_t {
    Back = 0,     ///< Rear-facing primary/ultra-wide/telephoto camera
    Front,        ///< Front-facing selfie / FaceID camera
    External      ///< USB UVC or desktop webcam
};

/// Flash & Torch illumination mode.
enum class FlashMode : uint8_t {
    Off = 0,
    Auto,
    On,
    Torch         ///< Continuous LED flashlight illumination
};

/// Descriptor for an available camera device.
struct CameraDeviceInfo {
    uint32_t deviceIndex{0};
    uint32_t sdlCameraId{0};
    std::string androidCameraId{};
    std::string name{};
    CameraFacing facing{CameraFacing::Back};
    int sensorOrientationDeg{90};
    bool hasFlashUnit{false};
    int preferredWidth{640};
    int preferredHeight{480};
    float preferredFps{30.0f};
    bool isSynthetic{false};
};

/// Statistics and histogram computed in real time from the active camera frame.
struct CameraFrameStats {
    uint32_t width{0};
    uint32_t height{0};
    uint64_t frameSequence{0};
    float fps{0.0f};
    float meanLuminance{0.0f};             ///< Average luminance [0..255]
    float contrastRms{0.0f};               ///< RMS contrast
    std::array<uint32_t, 256> histLuma{};  ///< 256-bin luminance histogram
    std::array<uint32_t, 256> histR{};     ///< 256-bin Red histogram
    std::array<uint32_t, 256> histG{};     ///< 256-bin Green histogram
    std::array<uint32_t, 256> histB{};     ///< 256-bin Blue histogram
};

/// Mobile Camera Service backed by `keel::CameraService` (`keelcpp/mobile`) + SDL3 texture bridge.
class CameraService {
public:
    CameraService();
    ~CameraService();

    CameraService(const CameraService&) = delete;
    CameraService& operator=(const CameraService&) = delete;

    /// Enumerates physical cameras (and registers the fallback procedural test camera).
    bool Initialize();
    void Shutdown();

    /// Starts or stops the camera stream.
    bool OpenCamera(size_t deviceIndex = 0, int width = 640, int height = 480);
    void CloseCamera();
    [[nodiscard]] bool IsStreaming() const noexcept { return m_hal.is_streaming(); }

    /// Pauses/resumes hardware camera stream across mobile app background/foreground transitions.
    void SetPaused(bool paused) { m_hal.set_paused(paused); }
    [[nodiscard]] bool IsPaused() const noexcept { return m_hal.is_paused(); }

    /// Switches between Front and Back camera (or cycles available cameras).
    bool SwitchCameraFacing();

    /// Acquires the latest video frame, updates RGB888 buffer for `guikit::ImageView` and GPU texture.
    void UpdateFrame(double dtSeconds, void* nativeRenderer = nullptr);

    /// Returns list of available camera devices.
    [[nodiscard]] std::span<const CameraDeviceInfo> GetDevices() const noexcept { return m_devices; }
    [[nodiscard]] size_t GetActiveDeviceIndex() const noexcept { return m_hal.active_device_index(); }
    [[nodiscard]] const CameraDeviceInfo* GetActiveDevice() const noexcept;

    /// Returns non-owning `ImageViewData` descriptor (3-channel RGB888) ready to pass to `ui.ImageView(...)`.
    [[nodiscard]] ImageViewData GetImageViewData() const noexcept;

    /// Returns `SDL_Texture*` cast to `uint64_t` for direct drawing via `ICanvas::DrawTexture`.
    [[nodiscard]] uint64_t GetPreviewTextureId() const noexcept {
        return reinterpret_cast<uint64_t>(m_previewTexture);
    }

    /// Returns real-time frame statistics and 256-bin RGB/Luma histograms.
    [[nodiscard]] const CameraFrameStats& GetFrameStats() const noexcept { return m_stats; }

    // --- Mobile Camera Hardware Controls ---
    void SetFlashMode(FlashMode mode) { m_hal.set_flash_mode(static_cast<keel::FlashMode>(mode)); }
    [[nodiscard]] FlashMode GetFlashMode() const noexcept { return static_cast<FlashMode>(m_hal.flash_mode()); }

    void SetTorchEnabled(bool enabled) { m_hal.set_torch_enabled(enabled); }
    [[nodiscard]] bool IsTorchEnabled() const noexcept { return m_hal.is_torch_enabled(); }

    /// Normalized tap-to-focus target in viewport coordinates [0..1, 0..1].
    void TriggerTapToFocus(Vec2 normalizedPoint) {
        m_hal.trigger_tap_to_focus(keel::Vec2f{normalizedPoint.x, normalizedPoint.y});
    }
    [[nodiscard]] Vec2 GetFocusPoint() const noexcept {
        const auto fp = m_hal.focus_point();
        return {fp.x, fp.y};
    }

    /// Exposure compensation in EV stops (-3.0 .. +3.0).
    void SetExposureCompensationEv(float ev) noexcept { m_hal.set_exposure_compensation_ev(ev); }
    [[nodiscard]] float GetExposureCompensationEv() const noexcept { return m_hal.exposure_compensation_ev(); }

    /// Digital / Optical zoom factor (1.0x .. 8.0x).
    void SetZoomFactor(float zoom) noexcept { m_hal.set_zoom_factor(zoom); }
    [[nodiscard]] float GetZoomFactor() const noexcept { return m_hal.zoom_factor(); }

    /// Captures still snapshot from the current video frame and increments capture counter.
    bool CaptureSnapshot() { return m_hal.capture_snapshot(); }
    [[nodiscard]] uint32_t GetCapturedPhotoCount() const noexcept { return m_hal.captured_photo_count(); }
    [[nodiscard]] std::span<const uint8_t> GetLastCapturedRgb() const noexcept {
        const auto s = m_hal.last_captured_rgb();
        return {s.data(), s.size()};
    }

    [[nodiscard]] keel::CameraService& Hal() noexcept { return m_hal; }
    [[nodiscard]] const keel::CameraService& Hal() const noexcept { return m_hal; }

private:
    void SyncDevicesFromHal();
    void SyncStatsFromHal();

    keel::CameraService m_hal{};
    std::vector<CameraDeviceInfo> m_devices{};
    CameraFrameStats m_stats{};
    void* m_sdlCamera{nullptr};
    void* m_previewTexture{nullptr};
    uint32_t m_texWidth{0};
    uint32_t m_texHeight{0};
};

} // namespace guikit::mobile
