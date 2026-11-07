#include "guikit/mobile/mobile_window.hpp"
#include <algorithm>
#include <chrono>
#include <iostream>
#include "guikit/guikit.hpp"

namespace guikit::mobile {

namespace {

MobileWindowBackendFactory g_defaultMobileBackendFactory = nullptr;

} // namespace

void RegisterDefaultMobileWindowBackendFactory(MobileWindowBackendFactory factory) noexcept {
    g_defaultMobileBackendFactory = factory;
}

std::unique_ptr<IMobileWindowBackend> CreateDefaultMobileWindowBackend() {
    if (g_defaultMobileBackendFactory != nullptr) {
        return g_defaultMobileBackendFactory();
    }
    return nullptr;
}

MobileWindow::MobileWindow(
    MobileWindowConfig config,
    std::unique_ptr<IMobileWindowBackend> backend
)
    : m_config(std::move(config))
    , m_backend(backend ? std::move(backend) : CreateDefaultMobileWindowBackend())
    , m_theme(m_config.initialThemeMode == ThemeMode::Light
              ? UiTheme::Light(Density::Standard)
              : UiTheme::Dark(Density::Standard))
    , m_lightMode(m_config.initialThemeMode == ThemeMode::Light)
    , m_reactiveMode(m_config.reactiveMode) {
#ifdef __ANDROID__
    m_showSimulatorDrawer = false;
#endif
}

MobileWindow::~MobileWindow() {
    m_mobile.Shutdown();
    if (m_backend) {
        m_backend->Shutdown();
    }
}

int MobileWindow::Run(RenderCallback renderCallback) {
    m_renderCallback = std::move(renderCallback);
    return Run();
}

int MobileWindow::Run() {
    if (!m_backend) {
        m_backend = CreateDefaultMobileWindowBackend();
        if (!m_backend) {
            std::cerr << "guikit::mobile::MobileWindow::Run() failed: no IMobileWindowBackend registered.\n";
            return 1;
        }
    }

    m_ui.SetTheme(m_theme);
    m_ui.SetClearColor(m_theme.palette.background);
    m_ui.SetIdleHeartbeatMs(m_config.idleHeartbeatMs);

    const MobileWindowHostCallbacks hostCallbacks{
        .onCloseRequested = [this]() -> bool {
            if (OnCloseRequested()) {
                m_running = false;
                return true;
            }
            return false;
        },
        .onResize = [this](int w, int h) {
            OnResize(w, h);
        }
    };

    if (!m_backend->Initialize(m_config, m_ui, m_mobile, hostCallbacks)) {
        return 1;
    }

    if (!m_mobile.System().GetDeviceInfo().isSimulator) {
        m_showSimulatorDrawer = false;
    }

    m_running = true;
    OnInit();

    auto prevTime = std::chrono::steady_clock::now();

    while (m_running) {
        const auto now = std::chrono::steady_clock::now();
        const double dtSeconds = std::clamp(
            std::chrono::duration<double>(now - prevTime).count(),
            0.001,
            0.1
        );
        prevTime = now;

        const int timeoutMs = m_ui.GetRecommendedSleepTimeoutMs();
        if (!m_backend->PumpEvents(m_ui, m_mobile, m_reactiveMode, timeoutMs)) {
            m_running = false;
            break;
        }
        if (!m_running) {
            break;
        }
        if (!m_backend->IsMinimized() && !m_mobile.IsInBackground()) {
            RenderFrame(dtSeconds);
        } else {
            m_backend->PaceFrame(0.0, false);
        }
    }

    OnShutdown();
    m_mobile.Shutdown();
    m_backend->Shutdown();
    return 0;
}

void MobileWindow::OnRender(Context& ui, MobileRuntime& mobile) {
    if (m_renderCallback) {
        m_renderCallback(ui, mobile);
    }
}

void MobileWindow::RenderFrame(double dtSeconds) {
    if (!m_backend) {
        return;
    }
    const int winW = m_backend->GetWidth();
    const int winH = m_backend->GetHeight();
    if (winW <= 0 || winH <= 0) {
        return;
    }

    m_backend->PrepareFrameTarget(m_ui);
    m_mobile.UpdateFrame(dtSeconds, m_ui);

    const auto frameWorkStart = std::chrono::steady_clock::now();
    m_metrics.BeginFrame();

    const float effDpi = std::max(0.1f, GetDpiScale());
    m_ui.BeginFrame({static_cast<float>(winW), static_cast<float>(winH)}, effDpi);
    {
        if (m_config.applySafeAreaPadding) {
            if (auto root = m_ui.Column({
                .padding = GetSafeAreaPadding(),
                .widthMode = SizeMode::Stretch,
                .heightMode = SizeMode::Stretch
            })) {
                OnRender(m_ui, m_mobile);
            }
        } else {
            OnRender(m_ui, m_mobile);
        }
    }
    m_ui.EndFrame(m_backend->GetCanvas());
    m_metrics.MarkCpuDone();

    const bool shouldPresent = m_ui.HasDirtyDamage() || !m_reactiveMode;
    if (shouldPresent) {
        m_backend->PresentFrame(m_ui, m_metrics, !m_reactiveMode);
    }

    const auto frameEnd = std::chrono::steady_clock::now();
    const double elapsedMs = std::chrono::duration<double, std::milli>(frameEnd - frameWorkStart).count();
    m_backend->PaceFrame(elapsedMs, shouldPresent);

    m_metrics.EndFrame(60.0f);
}

Padding MobileWindow::GetSafeAreaPadding() const noexcept {
    if (m_mobile.System().GetDeviceInfo().isSimulator) {
        return Padding{};
    }
    const float effDpi = std::max(0.1f, GetDpiScale());
    const SafeAreaInsets safe = m_mobile.System().GetSafeAreaInsets();
    return Padding(
        safe.left / effDpi,
        safe.top / effDpi,
        safe.right / effDpi,
        safe.bottom / effDpi
    );
}

void MobileWindow::OpenUrl(std::string_view url) const noexcept {
    if (m_backend) {
        m_backend->OpenUrl(url);
    }
}

int MobileWindow::GetWidth() const noexcept {
    return m_backend ? m_backend->GetWidth() : m_config.width;
}

int MobileWindow::GetHeight() const noexcept {
    return m_backend ? m_backend->GetHeight() : m_config.height;
}

float MobileWindow::GetDpiScale() const noexcept {
    return m_backend ? m_backend->GetDisplayScale() : 1.0f;
}

float MobileWindow::GetDisplayPixelsPerMm() const noexcept {
    return m_backend ? m_backend->GetDisplayPixelsPerMm() : (96.0f / 25.4f);
}

bool MobileWindow::IsMinimized() const noexcept {
    return m_backend && m_backend->IsMinimized();
}

bool MobileWindow::IsVsyncEnabled() const noexcept {
    return m_backend ? m_backend->IsVsyncEnabled() : m_config.vsync;
}

void MobileWindow::SetVsyncEnabled(bool enabled) noexcept {
    m_config.vsync = enabled;
    if (m_backend) {
        m_backend->SetVsyncEnabled(enabled);
    }
}

void MobileWindow::SetReactiveMode(bool enabled) noexcept {
    m_reactiveMode = enabled;
    if (!m_reactiveMode) {
        m_ui.InvalidateAll();
    }
}

void MobileWindow::SetLightMode(bool lightMode) noexcept {
    m_lightMode = lightMode;
    m_theme.SetMode(m_lightMode ? ThemeMode::Light : ThemeMode::Dark);
    m_ui.SetTheme(m_theme);
    m_ui.SetClearColor(m_theme.palette.background);
    m_ui.InvalidateAll();
}

// =========================================================================
// Composable Mobile Shell UI Helpers
// =========================================================================

void MobileWindow::RenderStatusBar(Context& ui) {
    const auto& dev = m_mobile.System().GetDeviceInfo();
    const auto& bat = m_mobile.System().GetBatteryStatus();
    const auto& snap = m_mobile.Sensors().GetSnapshot();
    const auto& fix = m_mobile.Location().GetLatestFix();

    if (auto bar = ui.Box({
        .widthMode = SizeMode::Stretch,
        .heightMode = SizeMode::Fit,
        .padding = Padding::Symmetric(10.0f, 5.0f),
        .background = {.normal = Color(13, 18, 28, 255)},
        .border = {.width = 1.0f, .radius = 0.0f}
    })) {
        if (auto row = ui.Row({.gap = 8.0f, .align = Alignment::Center, .widthMode = SizeMode::Stretch})) {
            ui.Text(dev.model, {.role = TextRole::LabelSmall, .variant = UiVariant::Info});
            ui.Spacer();

            ui.Text(ui.Format("{} sat | {:.0f} lx", fix.satellitesUsed, snap.ambientLightLux), {
                .role = TextRole::CodeSmall,
                .variant = UiVariant::Muted
            });

            ui.Text(ui.Format("{}{:.0f}%", bat.isCharging ? "CHG " : "BAT ", bat.levelPct), {
                .role = TextRole::CodeSmall,
                .variant = bat.levelPct > 25.0f ? UiVariant::Success : UiVariant::Warning
            });

            if (dev.isSimulator) {
                if (ui.Size(UiSize::Xs).Button({
                    .text = m_showSimulatorDrawer ? "Hide Sim" : "Sim",
                    .icon = Icon::Settings(),
                    .color = m_showSimulatorDrawer ? StateColor::Primary() : StateColor{}
                }).clicked) {
                    m_showSimulatorDrawer = !m_showSimulatorDrawer;
                    m_mobile.System().TriggerHaptic(HapticStyle::Selection);
                }
            }
        }
    }
}

bool MobileWindow::RenderBottomNavigationBar(
    Context& ui,
    size_t& ioActiveTab,
    std::span<const std::string_view> tabLabels
) {
    bool changed = false;
    if (auto bar = ui.Box({
        .widthMode = SizeMode::Stretch,
        .heightMode = SizeMode::Fit,
        .padding = Padding::Symmetric(8.0f, 6.0f),
        .background = {.normal = Color(15, 23, 42, 255)},
        .border = {.width = 1.0f, .radius = 0.0f}
    })) {
        if (ui.Size(UiSize::Sm).Segmented(ioActiveTab, tabLabels, {
            .stretch = StretchMode::Fill
        }).changed) {
            m_mobile.System().TriggerHaptic(HapticStyle::Selection);
            changed = true;
        }
    }
    return changed;
}

void MobileWindow::RenderSimulatorDrawer(Context& ui) {
    if (!m_showSimulatorDrawer) {
        return;
    }

    auto& sim = m_mobile.Sensors().GetSimConfig();

    if (auto panel = ui.Card({
        .minWidth = 280.0f,
        .gap = 10.0f,
        .padding = Padding::All(14.0f),
        .widthMode = SizeMode::Fit,
        .heightMode = SizeMode::Stretch
    })) {
        ui.Size(UiSize::Sm).Heading({
            .label = "Mobile Hardware Sim",
            .description = "Real-time IMU, Baro & Light injection",
            .icon = Icon::Settings(),
            .divider = true
        });

        bool forceSim = m_mobile.Sensors().IsUsingSimulator();
        if (auto el = ui.Size(UiSize::Sm).Element({.label = "Force Simulator"})) {
            if (ui.Size(UiSize::Sm).Switch(forceSim).changed) {
                m_mobile.Sensors().SetUseSimulatorOverride(forceSim);
            }
        }

        if (auto el = ui.Size(UiSize::Sm).Element({.label = "Auto IMU Motion"})) {
            ui.Size(UiSize::Sm).Switch(sim.animateMotion);
        }

        if (auto el = ui.Size(UiSize::Sm).Element({.label = "Pitch Bias (deg)"})) {
            ui.Size(UiSize::Sm).Slider(sim.manualPitchDeg, -45.0f, 45.0f, {.valueFormat = "%+.1f deg"});
        }

        if (auto el = ui.Size(UiSize::Sm).Element({.label = "Roll Bias (deg)"})) {
            ui.Size(UiSize::Sm).Slider(sim.manualRollDeg, -75.0f, 75.0f, {.valueFormat = "%+.1f deg"});
        }

        if (auto el = ui.Size(UiSize::Sm).Element({.label = "Compass Heading"})) {
            ui.Size(UiSize::Sm).Slider(sim.manualHeadingDeg, 0.0f, 359.0f, {.valueFormat = "%03.0f deg"});
        }

        if (auto el = ui.Size(UiSize::Sm).Element({.label = "Vibration Freq"})) {
            ui.Size(UiSize::Sm).Slider(sim.vibrationFreqHz, 2.0f, 45.0f, {.valueFormat = "%.1f Hz"});
        }

        if (auto el = ui.Size(UiSize::Sm).Element({.label = "Vibration Amp"})) {
            ui.Size(UiSize::Sm).Slider(sim.vibrationAmpG, 0.0f, 1.0f, {.valueFormat = "%.2f G"});
        }

        if (auto el = ui.Size(UiSize::Sm).Element({.label = "Barometer (hPa)"})) {
            ui.Size(UiSize::Sm).Slider(sim.simPressureHpa, 950.0f, 1045.0f, {.valueFormat = "%.1f hPa"});
        }

        if (auto el = ui.Size(UiSize::Sm).Element({.label = "Ambient Light"})) {
            ui.Size(UiSize::Sm).Slider(sim.simLightLux, 5.0f, 2000.0f, {.valueFormat = "%.0f lx"});
        }

        if (auto el = ui.Size(UiSize::Sm).Element({.label = "Proximity Near"})) {
            ui.Size(UiSize::Sm).Switch(sim.simProximityNear);
        }
    }
}

} // namespace guikit::mobile
