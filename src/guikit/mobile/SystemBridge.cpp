#include "guikit/mobile/SystemBridge.hpp"
#include <SDL3/SDL.h>
#include <algorithm>

namespace guikit::mobile {

SystemBridge::SystemBridge() = default;

SystemBridge::~SystemBridge() {
    Shutdown();
}

bool SystemBridge::Initialize(void* nativeWindowHandle) {
    m_window = nativeWindowHandle;

    void* jniEnv = nullptr;
    void* activity = nullptr;
#ifdef __ANDROID__
    jniEnv = SDL_GetAndroidJNIEnv();
    activity = SDL_GetAndroidActivity();
#endif
    const bool ok = m_hal.initialize(jniEnv, activity);

#ifdef __ANDROID__
    m_hal.set_simulate_mobile_cutout(false);
#endif

    UpdateWindowSafeArea();
    SyncFromHal();
    return ok;
}

void SystemBridge::Shutdown() {
    m_hal.shutdown();
}

void SystemBridge::UpdateFrame(double dtSeconds) {
#ifdef __ANDROID__
    m_hal.set_android_context(SDL_GetAndroidJNIEnv(), SDL_GetAndroidActivity());
#endif
    m_hal.update_frame(dtSeconds);
    UpdateWindowSafeArea();
    SyncFromHal();
}

void SystemBridge::RequestPermission(Permission perm, PermissionCallback callback) {
    m_hal.request_permission(
        static_cast<keel::Permission>(perm),
        [&](keel::Permission p, keel::PermissionStatus st) {
            if (callback) {
                callback(static_cast<Permission>(p), static_cast<PermissionStatus>(st));
            }
        }
    );
}

void SystemBridge::UpdateWindowSafeArea() {
    if (!m_window) return;

    auto* sdlWin = static_cast<SDL_Window*>(m_window);
    const SDL_DisplayID displayId = SDL_GetDisplayForWindow(sdlWin);
    const SDL_DisplayOrientation dispOrient = displayId
        ? SDL_GetCurrentDisplayOrientation(displayId)
        : SDL_GetCurrentDisplayOrientation(SDL_GetPrimaryDisplay());
    switch (dispOrient) {
        case SDL_ORIENTATION_LANDSCAPE:
            m_hal.set_screen_orientation(keel::ScreenOrientation::LandscapeLeft);
            break;
        case SDL_ORIENTATION_LANDSCAPE_FLIPPED:
            m_hal.set_screen_orientation(keel::ScreenOrientation::LandscapeRight);
            break;
        case SDL_ORIENTATION_PORTRAIT_FLIPPED:
            m_hal.set_screen_orientation(keel::ScreenOrientation::PortraitUpsideDown);
            break;
        default:
            m_hal.set_screen_orientation(keel::ScreenOrientation::Portrait);
            break;
    }

    SDL_Rect safeRect{};
    int winW = 0;
    int winH = 0;
    SDL_GetWindowSizeInPixels(sdlWin, &winW, &winH);
    keel::SafeAreaInsets sa{};
    if (SDL_GetWindowSafeArea(sdlWin, &safeRect) && winW > 0 && winH > 0) {
        sa.left   = static_cast<float>(std::max(0, safeRect.x));
        sa.top    = static_cast<float>(std::max(0, safeRect.y));
        sa.right  = static_cast<float>(std::max(0, winW - (safeRect.x + safeRect.w)));
        sa.bottom = static_cast<float>(std::max(0, winH - (safeRect.y + safeRect.h)));
    }
#ifdef __ANDROID__
    const float dpi = std::max(1.0f, SDL_GetWindowDisplayScale(sdlWin));
    sa.top = std::max(sa.top, 44.0f * dpi);
    if (winH >= winW) {
        sa.bottom = std::max(sa.bottom, 50.0f * dpi);
    } else if (sa.left < 1.0f && sa.right < 1.0f) {
        sa.right = std::max(sa.right, 50.0f * dpi);
    }
    m_hal.set_safe_area_insets(sa);
#else
    if (!m_hal.is_simulating_mobile_cutout()) {
        m_hal.set_safe_area_insets(sa);
    }
#endif
}

void SystemBridge::SyncFromHal() {
    const auto& di = m_hal.device_info();
    m_deviceInfo.platformName.assign(di.platform_name.data(), di.platform_name.size());
    m_deviceInfo.manufacturer.assign(di.manufacturer.data(), di.manufacturer.size());
    m_deviceInfo.model.assign(di.model.data(), di.model.size());
    m_deviceInfo.osVersion.assign(di.os_version.data(), di.os_version.size());
    m_deviceInfo.sdkInt = di.sdk_int;
    m_deviceInfo.isSimulator = di.is_simulator;

    const auto& bat = m_hal.battery_status();
    m_battery.levelPct = bat.level_pct;
    m_battery.isCharging = bat.is_charging;
    m_battery.temperatureC = bat.temperature_c;
    m_battery.voltageMv = bat.voltage_mv;
}

void SystemBridge::SetSoftKeyboardVisible(bool visible) {
    if (!m_window) return;
    auto* sdlWin = static_cast<SDL_Window*>(m_window);
    if (visible) {
        SDL_StartTextInput(sdlWin);
    } else {
        SDL_StopTextInput(sdlWin);
    }
}

bool SystemBridge::IsSoftKeyboardVisible() const noexcept {
    return m_window ? SDL_TextInputActive(static_cast<SDL_Window*>(m_window)) : false;
}

} // namespace guikit::mobile
