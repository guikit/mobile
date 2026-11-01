#include "guikit/mobile/MobileRuntime.hpp"

namespace guikit::mobile {

MobileRuntime::~MobileRuntime() {
    Shutdown();
}

bool MobileRuntime::Initialize(SDL_Window* window, SDL_Renderer* renderer) {
    if (m_initialized) return true;

    m_window = window;
    m_renderer = renderer;

    m_system.Initialize(window);
    m_sensors.Initialize();
    m_camera.Initialize();
    m_location.Initialize();
    m_audio.Initialize();

    m_initialized = true;
    return true;
}

void MobileRuntime::Shutdown() {
    if (!m_initialized) return;

    m_audio.Shutdown();
    m_location.Shutdown();
    m_camera.Shutdown();
    m_sensors.Shutdown();
    m_system.Shutdown();

    m_initialized = false;
}

bool MobileRuntime::ProcessEvent(const SDL_Event& ev, Context& ui, Vec2 viewportSizePx) {
    // 1. Mobile application background/foreground lifecycle
    if (ev.type == SDL_EVENT_WILL_ENTER_BACKGROUND || ev.type == SDL_EVENT_DID_ENTER_BACKGROUND) {
        m_inBackground = true;
        m_sensors.SetPaused(true);
        m_audio.SetCapturing(false);
        return true;
    }

    if (ev.type == SDL_EVENT_WILL_ENTER_FOREGROUND || ev.type == SDL_EVENT_DID_ENTER_FOREGROUND) {
        m_inBackground = false;
        m_sensors.SetPaused(false);
        m_audio.SetCapturing(true);
        ui.InvalidateAll();
        ui.RequestRepaint(3);
        return true;
    }

    // 2. Android hardware/gesture Back button -> Escape key in GuiKit (closes modals/popovers)
    if (ev.type == SDL_EVENT_KEY_DOWN && ev.key.scancode == SDL_SCANCODE_AC_BACK) {
        ui.OnKeyDown(Key::Escape);
        m_system.TriggerHaptic(HapticStyle::Selection);
        return true;
    }

    // 3. Multi-touch finger gestures (kinetic scroll, pinch-to-zoom, long-press)
    if (m_touch.ProcessSdlEvent(ev, ui, viewportSizePx)) {
        return true;
    }

    return false;
}

void MobileRuntime::UpdateFrame(double dtSeconds, Context& ui) {
    if (!m_initialized || m_inBackground) return;

    m_system.UpdateFrame(dtSeconds);
    m_touch.UpdateFrame(dtSeconds, ui);
    m_sensors.UpdateFrame(dtSeconds);
    m_camera.UpdateFrame(dtSeconds, m_renderer);
    m_location.UpdateFrame(dtSeconds);
    m_audio.UpdateFrame(dtSeconds);

    // Keep UI smoothly refreshing while active telemetry streams are running
    if (!m_sensors.IsPaused() || m_camera.IsStreaming() || m_audio.IsCapturing()) {
        ui.RequestRepaint(1);
    }
}

} // namespace guikit::mobile
