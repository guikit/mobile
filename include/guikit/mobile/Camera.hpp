#pragma once

#include <array>
#include <cstdint>
#include <span>
#include <string>
#include <vector>
#include <SDL3/SDL.h>
#include "guikit/mobile/Types.hpp"
#include "guikit/widgets/ImageView.hpp"

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
    SDL_CameraID sdlCameraId{0};
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

/// Mobile Camera Service managing video preview, RGB/Luminance analysis, Torch, Focus, and Photo Capture.
/// Uses SDL3 Camera backend (Android Camera2 NDK / Windows MediaFoundation / V4L2 / CoreMedia)
/// with an automatic procedural test-chart camera fallback when no physical camera is available.
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
    [[nodiscard]] bool IsStreaming() const noexcept { return m_streaming; }

    /// Pauses/resumes hardware camera stream across mobile app background/foreground transitions.
    void SetPaused(bool paused);
    [[nodiscard]] bool IsPaused() const noexcept { return m_paused; }

    /// Switches between Front and Back camera (or cycles available cameras).
    bool SwitchCameraFacing();

    /// Acquires the latest video frame, updates RGB888 buffer for `guikit::ImageView` and GPU texture.
    void UpdateFrame(double dtSeconds, SDL_Renderer* renderer = nullptr);

    /// Returns list of available camera devices.
    [[nodiscard]] std::span<const CameraDeviceInfo> GetDevices() const noexcept { return m_devices; }
    [[nodiscard]] size_t GetActiveDeviceIndex() const noexcept { return m_activeDeviceIndex; }
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
    void SetFlashMode(FlashMode mode);
    [[nodiscard]] FlashMode GetFlashMode() const noexcept { return m_flashMode; }

    void SetTorchEnabled(bool enabled);
    [[nodiscard]] bool IsTorchEnabled() const noexcept { return m_torchEnabled; }

    /// Normalized tap-to-focus target in viewport coordinates [0..1, 0..1].
    void TriggerTapToFocus(Vec2 normalizedPoint);
    [[nodiscard]] Vec2 GetFocusPoint() const noexcept { return m_focusPoint; }

    /// Exposure compensation in EV stops (-2.0 .. +2.0).
    void SetExposureCompensationEv(float ev) noexcept;
    [[nodiscard]] float GetExposureCompensationEv() const noexcept { return m_exposureEv; }

    /// Digital / Optical zoom factor (1.0x .. 8.0x).
    void SetZoomFactor(float zoom) noexcept;
    [[nodiscard]] float GetZoomFactor() const noexcept { return m_zoomFactor; }

    /// Captures still snapshot from the current video frame and increments capture counter.
    bool CaptureSnapshot();
    [[nodiscard]] uint32_t GetCapturedPhotoCount() const noexcept { return m_capturedPhotos; }
    [[nodiscard]] std::span<const uint8_t> GetLastCapturedRgb() const noexcept { return m_lastCapturedRgb; }

private:
    void GenerateProceduralFrame(double dtSeconds, float pitchDeg = 0.0f, float rollDeg = 0.0f);
    void ComputeFrameHistograms();
    bool OpenAndroidCamera(const CameraDeviceInfo& dev, int width, int height);
    void CloseAndroidCamera();

    struct AndroidCameraState;
    AndroidCameraState* m_androidCam{nullptr};

    std::vector<CameraDeviceInfo> m_devices;
    size_t m_activeDeviceIndex{0};
    SDL_Camera* m_sdlCamera{nullptr};
    SDL_Texture* m_previewTexture{nullptr};

    uint32_t m_frameWidth{480};
    uint32_t m_frameHeight{640};
    std::vector<uint8_t> m_rgbPixels;         ///< 3-channel RGB888 for `guikit::ImageView`
    std::vector<uint32_t> m_rgbaPixels;       ///< 4-channel RGBA8888 for `SDL_Texture`
    std::vector<uint8_t> m_lastCapturedRgb;
    CameraFrameStats m_stats{};

    bool m_initialized{false};
    bool m_streaming{false};
    bool m_paused{false};
    bool m_usingSynthetic{false};
    bool m_torchEnabled{false};
    FlashMode m_flashMode{FlashMode::Off};
    Vec2 m_focusPoint{0.5f, 0.5f};
    float m_exposureEv{0.0f};
    float m_zoomFactor{1.0f};
    uint32_t m_capturedPhotos{0};
    double m_simTimeSec{0.0};
    double m_retryTimerSec{0.0};
    double m_fpsAccumSec{0.0};
    uint32_t m_fpsFrameCount{0};
};

} // namespace guikit::mobile
