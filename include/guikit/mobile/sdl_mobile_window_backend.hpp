#pragma once

#include <cstdint>
#include <memory>
#include <vector>
#include "guikit/mobile/i_mobile_window_backend.hpp"

struct SDL_Window;
struct SDL_Renderer;
struct SDL_Texture;
union SDL_Event;

namespace guikit::mobile {

/// SDL3 + Vector Canvas (`ThorCanvas` / `ICanvas`) implementation of `IMobileWindowBackend`.
/// Encapsulates SDL3 windowing, streaming texture presentation, DPI scaling, and event routing
/// so `MobileWindow` and application code remain 100% decoupled from SDL3.
class SdlMobileWindowBackend final : public IMobileWindowBackend {
public:
    explicit SdlMobileWindowBackend(std::unique_ptr<ICanvas> canvas = nullptr);
    ~SdlMobileWindowBackend() override;

    SdlMobileWindowBackend(const SdlMobileWindowBackend&) = delete;
    SdlMobileWindowBackend& operator=(const SdlMobileWindowBackend&) = delete;

    bool Initialize(
        const MobileWindowConfig& config,
        Context& ui,
        MobileRuntime& runtime,
        const MobileWindowHostCallbacks& callbacks
    ) override;

    void Shutdown() noexcept override;

    bool PumpEvents(
        Context& ui,
        MobileRuntime& runtime,
        bool reactiveMode,
        int sleepTimeoutMs
    ) override;

    [[nodiscard]] ICanvas& GetCanvas() noexcept override { return *m_canvas; }
    [[nodiscard]] const ICanvas& GetCanvas() const noexcept override { return *m_canvas; }

    void PrepareFrameTarget(Context& ui) override;
    void PresentFrame(const Context& ui, FrameMetrics& metrics, bool forcePresent) override;
    void PaceFrame(double elapsedMs, bool presented) noexcept override;

    [[nodiscard]] int GetWidth() const noexcept override { return m_width; }
    [[nodiscard]] int GetHeight() const noexcept override { return m_height; }
    [[nodiscard]] float GetDisplayScale() const noexcept override { return m_displayScale; }
    [[nodiscard]] float GetDisplayPixelsPerMm() const noexcept override { return m_displayPxPerMm; }
    [[nodiscard]] bool IsMinimized() const noexcept override { return m_minimized; }

    [[nodiscard]] bool IsVsyncEnabled() const noexcept override { return m_vsyncEnabled; }
    void SetVsyncEnabled(bool enabled) noexcept override;

    [[nodiscard]] void* GetNativeWindowHandle() const noexcept override { return m_window; }
    [[nodiscard]] void* GetNativeRendererHandle() const noexcept override { return m_renderer; }

    void OpenUrl(std::string_view url) const noexcept override;

private:
    void ProcessEvent(const SDL_Event& ev, Context& ui, MobileRuntime& runtime);
    void UpdateDisplayMetrics() noexcept;

    MobileWindowConfig m_config{};
    MobileWindowHostCallbacks m_callbacks{};

    bool m_sdlVideoInitialized{false};
    SDL_Window* m_window{nullptr};
    SDL_Renderer* m_renderer{nullptr};
    SDL_Texture* m_screenTexture{nullptr};

    int m_width{1180};
    int m_height{820};
    int m_bufWidth{0};
    int m_bufHeight{0};
    float m_displayScale{1.0f};
    float m_displayPxPerMm{96.0f / 25.4f};
    std::vector<uint32_t> m_pixelBuffer;

    std::unique_ptr<ICanvas> m_canvas;
    bool m_hasExplicitCanvas{false};

    bool m_shouldExit{false};
    bool m_minimized{false};
    bool m_vsyncEnabled{true};
};

/// Creates a new `SdlMobileWindowBackend` instance using the default registered vector canvas (`ThorCanvas`).
[[nodiscard]] std::unique_ptr<IMobileWindowBackend> CreateSdlMobileWindowBackend();

/// Creates a new `SdlMobileWindowBackend` instance with an explicitly injected `ICanvas` backend.
[[nodiscard]] std::unique_ptr<IMobileWindowBackend> CreateSdlMobileWindowBackend(std::unique_ptr<ICanvas> canvas);

/// Registers `CreateSdlMobileWindowBackend` as the default `IMobileWindowBackend` factory for `guikit::mobile::MobileWindow`.
void RegisterSdlMobileWindowBackend() noexcept;

namespace detail {
struct SdlMobileBackendAutoRegistrar {
    SdlMobileBackendAutoRegistrar() noexcept { RegisterSdlMobileWindowBackend(); }
};
inline const SdlMobileBackendAutoRegistrar g_sdlMobileBackendAutoRegistrar{};
} // namespace detail

} // namespace guikit::mobile
