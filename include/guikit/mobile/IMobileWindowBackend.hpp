#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include "guikit/backend/ICanvas.hpp"
#include "guikit/backend/VectorBackend.hpp"
#include "guikit/core/FrameMetrics.hpp"
#include "guikit/core/Theme.hpp"

namespace guikit {
class Context;
} // namespace guikit

namespace guikit::mobile {

class MobileRuntime;

/// Configuration options for initializing a `guikit::mobile::MobileWindow` application host.
struct MobileWindowConfig {
    std::string title = "GuiKit Mobile Application";
    int width = 1180;                             ///< Initial window width on desktop (ignored on fullscreen mobile OS)
    int height = 820;                             ///< Initial window height on desktop (ignored on fullscreen mobile OS)
    int minWidth = 320;
    int minHeight = 240;
    bool resizable = true;
    bool highDpi = true;
    bool vsync = true;
    bool reactiveMode = true;
    bool applySafeAreaPadding = true;             ///< Automatically apply OS SafeAreaInsets padding to root container
    int idleHeartbeatMs = 250;                    ///< Periodic wakeup interval in reactive mode (-1 to disable)
    ThemeMode initialThemeMode = ThemeMode::Dark;
    VectorCanvasFactory canvasFactory = nullptr;  ///< Optional custom `ICanvas` factory override (defaults to ThorCanvas)
};

/// Callbacks invoked by an `IMobileWindowBackend` into its hosting `MobileWindow`.
struct MobileWindowHostCallbacks {
    std::function<bool()> onCloseRequested;
    std::function<void(int width, int height)> onResize;
};

/// Abstract platform windowing, touch input, and swapchain presentation backend for `guikit::mobile::MobileWindow`.
/// Decouples `MobileWindow` from concrete OS/windowing libraries (SDL3, Android ANativeActivity, iOS UIKit/Metal, GLFW, etc.).
class IMobileWindowBackend {
public:
    virtual ~IMobileWindowBackend() = default;

    /// Creates the platform window/surface, graphics context, and initializes `MobileRuntime`.
    virtual bool Initialize(
        const MobileWindowConfig& config,
        Context& ui,
        MobileRuntime& runtime,
        const MobileWindowHostCallbacks& callbacks
    ) = 0;

    /// Releases all platform window, texture, and GPU resources.
    virtual void Shutdown() noexcept = 0;

    /// Polls or waits for platform events, routes mobile lifecycle/touch/back events to `runtime`,
    /// and dispatches pointer/keyboard events to `ui`. Returns `false` if the application loop should exit.
    virtual bool PumpEvents(
        Context& ui,
        MobileRuntime& runtime,
        bool reactiveMode,
        int sleepTimeoutMs
    ) = 0;

    /// Returns the active 2D vector rendering canvas owned by this backend.
    [[nodiscard]] virtual ICanvas& GetCanvas() noexcept = 0;
    [[nodiscard]] virtual const ICanvas& GetCanvas() const noexcept = 0;

    /// Ensures the offscreen/streaming frame target matches current display dimensions before `ui.BeginFrame()`.
    virtual void PrepareFrameTarget(Context& ui) = 0;

    /// Uploads dirty regions (or full frame) and presents the swapchain.
    virtual void PresentFrame(const Context& ui, FrameMetrics& metrics, bool forcePresent) = 0;

    /// Sleeps briefly when frame pacing is needed.
    virtual void PaceFrame(double elapsedMs, bool presented) noexcept {
        (void)elapsedMs;
        (void)presented;
    }

    [[nodiscard]] virtual int GetWidth() const noexcept = 0;
    [[nodiscard]] virtual int GetHeight() const noexcept = 0;
    [[nodiscard]] virtual float GetDisplayScale() const noexcept = 0;
    [[nodiscard]] virtual float GetDisplayPixelsPerMm() const noexcept = 0;
    [[nodiscard]] virtual bool IsMinimized() const noexcept = 0;

    [[nodiscard]] virtual bool IsVsyncEnabled() const noexcept = 0;
    virtual void SetVsyncEnabled(bool enabled) noexcept = 0;

    [[nodiscard]] virtual void* GetNativeWindowHandle() const noexcept { return nullptr; }
    [[nodiscard]] virtual void* GetNativeRendererHandle() const noexcept { return nullptr; }

    virtual void OpenUrl(std::string_view url) const noexcept = 0;
};

using MobileWindowBackendFactory = std::unique_ptr<IMobileWindowBackend>(*)();

/// Registers the default platform `IMobileWindowBackend` factory used when `MobileWindow` is constructed without an explicit backend.
void RegisterDefaultMobileWindowBackendFactory(MobileWindowBackendFactory factory) noexcept;

/// Instantiates the registered default `IMobileWindowBackend` (or returns `nullptr` if none is registered).
[[nodiscard]] std::unique_ptr<IMobileWindowBackend> CreateDefaultMobileWindowBackend();

} // namespace guikit::mobile
