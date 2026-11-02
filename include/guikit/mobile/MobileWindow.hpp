#pragma once

#include <algorithm>
#include <cstdint>
#include <functional>
#include <memory>
#include <span>
#include <string_view>
#include "guikit/Context.hpp"
#include "guikit/core/FrameMetrics.hpp"
#include "guikit/mobile/IMobileWindowBackend.hpp"
#include "guikit/mobile/MobileRuntime.hpp"

namespace guikit::mobile {

/// Platform-agnostic mobile & tablet application host for `guikit::mobile`.
/// Mirrors `guikit::MainWindow` while integrating:
/// - `MobileRuntime` hardware & simulator lifecycle (Sensors, Camera, GNSS, Audio, Haptics, Multi-Touch)
/// - Native High-DPI scaling (`GetDpiScale()`) and OS `SafeAreaInsets` root padding
/// - Smartphone vs. Tablet (`IsTablet()`) and Portrait vs. Landscape (`IsPortrait()`) form-factor detection
/// - Composable Mobile Shell helpers (`RenderStatusBar`, `RenderBottomNavigationBar`, `RenderSimulatorDrawer`)
/// Zero dependency on SDL3 or OS-specific headers; delegates windowing to an interchangeable `IMobileWindowBackend`.
class MobileWindow {
public:
    using RenderCallback = std::function<void(Context& ui, MobileRuntime& mobile)>;

    explicit MobileWindow(
        MobileWindowConfig config = {},
        std::unique_ptr<IMobileWindowBackend> backend = nullptr
    );
    virtual ~MobileWindow();

    MobileWindow(const MobileWindow&) = delete;
    MobileWindow& operator=(const MobileWindow&) = delete;

    /// Initializes the window backend + `MobileRuntime` and enters the main application loop.
    /// Invokes `OnInit()`, then `OnRender(ui, mobile)` each frame, and `OnShutdown()` before exit.
    int Run();

    /// Convenience overload that runs the main loop using the provided per-frame `renderCallback`.
    int Run(RenderCallback renderCallback);

    /// Requests the main loop to terminate cleanly at the end of the current step.
    void Close() noexcept { m_running = false; }

    /// Opens an external URL in the device's system browser via the active `IMobileWindowBackend`.
    void OpenUrl(std::string_view url) const noexcept;

    // =========================================================================
    // Subsystems & State Accessors
    // =========================================================================

    [[nodiscard]] Context& GetUi() noexcept { return m_ui; }
    [[nodiscard]] const Context& GetUi() const noexcept { return m_ui; }

    [[nodiscard]] MobileRuntime& GetMobile() noexcept { return m_mobile; }
    [[nodiscard]] const MobileRuntime& GetMobile() const noexcept { return m_mobile; }

    [[nodiscard]] IMobileWindowBackend* GetBackend() noexcept { return m_backend.get(); }
    [[nodiscard]] const IMobileWindowBackend* GetBackend() const noexcept { return m_backend.get(); }

    [[nodiscard]] ICanvas& GetCanvas() noexcept { return m_backend->GetCanvas(); }
    [[nodiscard]] const ICanvas& GetCanvas() const noexcept { return m_backend->GetCanvas(); }

    [[nodiscard]] const FrameMetrics& GetMetrics() const noexcept { return m_metrics; }

    // =========================================================================
    // Form-Factor, Orientation, High-DPI & Safe Area Queries
    // =========================================================================

    [[nodiscard]] int GetWidth() const noexcept;
    [[nodiscard]] int GetHeight() const noexcept;
    [[nodiscard]] float GetDpiScale() const noexcept;
    [[nodiscard]] float GetDisplayPixelsPerMm() const noexcept;

    /// Viewport dimensions in density-independent logical points (`dp = px / GetDpiScale()`).
    [[nodiscard]] float GetLogicalWidth() const noexcept {
        return static_cast<float>(GetWidth()) / std::max(0.1f, GetDpiScale());
    }
    [[nodiscard]] float GetLogicalHeight() const noexcept {
        return static_cast<float>(GetHeight()) / std::max(0.1f, GetDpiScale());
    }

    [[nodiscard]] bool IsPortrait() const noexcept { return GetHeight() >= GetWidth(); }
    [[nodiscard]] bool IsLandscape() const noexcept { return GetWidth() > GetHeight(); }

    /// Returns `true` when the shortest screen dimension is at least `600 dp` (standard Android/iOS tablet breakpoint).
    [[nodiscard]] bool IsTablet() const noexcept {
        return std::min(GetLogicalWidth(), GetLogicalHeight()) >= 600.0f;
    }

    /// Computes logical `Padding` (`dp`) from the current OS `SafeAreaInsets` (camera cutout, status bar, gesture bar).
    [[nodiscard]] Padding GetSafeAreaPadding() const noexcept;

    [[nodiscard]] bool IsMinimized() const noexcept;
    [[nodiscard]] bool IsVsyncEnabled() const noexcept;
    void SetVsyncEnabled(bool enabled) noexcept;

    [[nodiscard]] bool IsReactiveMode() const noexcept { return m_reactiveMode; }
    void SetReactiveMode(bool enabled) noexcept;

    [[nodiscard]] UiTheme& GetTheme() noexcept { return m_theme; }
    [[nodiscard]] const UiTheme& GetTheme() const noexcept { return m_theme; }
    void SetLightMode(bool lightMode) noexcept;
    [[nodiscard]] bool IsLightMode() const noexcept { return m_lightMode; }

    [[nodiscard]] bool IsSimulatorDrawerOpen() const noexcept { return m_showSimulatorDrawer; }
    void SetSimulatorDrawerOpen(bool open) noexcept { m_showSimulatorDrawer = open; }

    // =========================================================================
    // Composable Mobile Shell UI Helpers
    // =========================================================================

    /// Renders a compact mobile top status bar showing device model, GNSS satellites, ambient light, battery,
    /// and (when running in desktop simulator mode) the `Sim` drawer toggle button.
    void RenderStatusBar(Context& ui);

    /// Renders a bottom segmented tab bar with automatic selection haptic feedback.
    /// Returns `true` if `ioActiveTab` changed this frame.
    bool RenderBottomNavigationBar(
        Context& ui,
        size_t& ioActiveTab,
        std::span<const std::string_view> tabLabels
    );

    /// Renders the collapsible desktop Simulator Controls drawer when `IsSimulatorDrawerOpen()` is true.
    void RenderSimulatorDrawer(Context& ui);

protected:
    /// Called once after the `IMobileWindowBackend`, `Context`, and `MobileRuntime` have been initialized.
    virtual void OnInit() {}

    /// Called every frame to construct the immediate-mode mobile UI tree.
    virtual void OnRender(Context& ui, MobileRuntime& mobile);

    /// Called when the window or screen orientation pixel dimensions change.
    virtual void OnResize(int width, int height) {
        (void)width;
        (void)height;
    }

    /// Called when the user or OS requests the application to close.
    virtual bool OnCloseRequested() { return true; }

    /// Called once before `MobileRuntime` and the window backend are shut down.
    virtual void OnShutdown() {}

private:
    void RenderFrame(double dtSeconds);

    MobileWindowConfig m_config{};
    std::unique_ptr<IMobileWindowBackend> m_backend;
    RenderCallback m_renderCallback;

    Context m_ui{};
    MobileRuntime m_mobile{};
    FrameMetrics m_metrics{};

    UiTheme m_theme = UiTheme::Dark(Density::Standard);
    bool m_lightMode{false};
    bool m_running{false};
    bool m_reactiveMode{true};
    bool m_showSimulatorDrawer{true};
};

} // namespace guikit::mobile
