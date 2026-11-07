#include "guikit/mobile/SystemBridge.hpp"
#include <SDL3/SDL.h>
#include <algorithm>
#include <string>

namespace guikit::mobile {

namespace {

#ifdef __ANDROID__
struct PermissionRequestContext {
    SystemBridge* bridge{nullptr};
    Permission perm{Permission::Camera};
    SystemBridge::PermissionCallback callback{};
};
#endif

} // namespace

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

    if (SDL_InitSubSystem(SDL_INIT_HAPTIC)) {
        m_haptic = SDL_OpenHapticFromMouse();
        if (m_haptic) {
            SDL_InitHapticRumble(static_cast<SDL_Haptic*>(m_haptic));
        }
    }

    UpdateSafeArea();
    UpdateBatteryInfo();
    SyncFromHal();
    return ok;
}

void SystemBridge::Shutdown() {
    if (m_haptic) {
        SDL_CloseHaptic(static_cast<SDL_Haptic*>(m_haptic));
        m_haptic = nullptr;
    }
    m_hal.shutdown();
}

void SystemBridge::UpdateFrame(double dtSeconds) {
#ifdef __ANDROID__
    m_hal.set_android_context(SDL_GetAndroidJNIEnv(), SDL_GetAndroidActivity());
#endif
    m_hal.update_frame(dtSeconds);
    m_pollTimerSec += dtSeconds;
    UpdateSafeArea();
    if (m_pollTimerSec >= 2.0) {
        m_pollTimerSec = 0.0;
        UpdateBatteryInfo();
    }
    SyncFromHal();
}

void SystemBridge::RequestPermission(Permission perm, PermissionCallback callback) {
#ifdef __ANDROID__
    const char* androidPerm = GetAndroidPermissionString(perm);
    auto* reqCtx = new PermissionRequestContext{this, perm, std::move(callback)};

    const bool started = SDL_RequestAndroidPermission(
        androidPerm,
        [](void* userdata, const char* /*permission*/, bool granted) {
            auto* ctx = static_cast<PermissionRequestContext*>(userdata);
            const PermissionStatus status = granted ? PermissionStatus::Granted : PermissionStatus::Denied;
            if (ctx->bridge) {
                ctx->bridge->Hal().set_permission_status(
                    static_cast<keel::Permission>(ctx->perm),
                    static_cast<keel::PermissionStatus>(status)
                );
            }
            if (ctx->callback) {
                ctx->callback(ctx->perm, status);
            }
            delete ctx;
        },
        reqCtx
    );

    if (!started) {
        m_hal.set_permission_status(static_cast<keel::Permission>(perm), keel::PermissionStatus::Denied);
        if (reqCtx->callback) {
            reqCtx->callback(perm, PermissionStatus::Denied);
        }
        delete reqCtx;
    }
#else
    m_hal.request_permission(
        static_cast<keel::Permission>(perm),
        [&](keel::Permission p, keel::PermissionStatus st) {
            if (callback) {
                callback(static_cast<Permission>(p), static_cast<PermissionStatus>(st));
            }
        }
    );
#endif
}

void SystemBridge::TriggerHaptic(HapticStyle style) {
    if (!m_hal.is_haptics_enabled()) return;
    m_hal.trigger_haptic(static_cast<keel::HapticStyle>(style));
    if (m_haptic) {
        SDL_PlayHapticRumble(static_cast<SDL_Haptic*>(m_haptic), 0.65f, 20);
    }
}

void SystemBridge::VibrateMs(uint32_t durationMs) {
    if (!m_hal.is_haptics_enabled() || durationMs == 0) return;
    m_hal.vibrate_ms(durationMs);
    if (m_haptic) {
        SDL_PlayHapticRumble(static_cast<SDL_Haptic*>(m_haptic), 0.65f, durationMs);
    }
}

void SystemBridge::UpdateSafeArea() {
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

void SystemBridge::UpdateBatteryInfo() {
    int seconds = 0;
    int percent = -1;
    const SDL_PowerState state = SDL_GetPowerInfo(&seconds, &percent);

    keel::BatteryStatus bat = m_hal.battery_status();
    if (percent >= 0 && percent <= 100) {
        bat.level_pct = static_cast<float>(percent);
    } else if (m_hal.device_info().is_simulator) {
        bat.level_pct = 86.0f;
    }
    bat.is_charging = (state == SDL_POWERSTATE_CHARGING || state == SDL_POWERSTATE_CHARGED);
    m_hal.set_battery_status(bat);
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

void SystemBridge::ShowNativeToast(std::string_view message, bool longDuration) {
#ifdef __ANDROID__
    const std::string msgStr(message);
    SDL_ShowAndroidToast(msgStr.c_str(), longDuration ? 1 : 0, -1, 0, 0);
#else
    (void)message;
    (void)longDuration;
#endif
}

} // namespace guikit::mobile
