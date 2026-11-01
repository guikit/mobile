#include "guikit/mobile/SystemBridge.hpp"
#include <algorithm>
#include <cmath>
#include <string>

#ifdef __ANDROID__
#include <jni.h>
#include <sys/system_properties.h>
#endif

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

SystemBridge::SystemBridge() {
    m_permissions.fill(PermissionStatus::Granted);
}

SystemBridge::~SystemBridge() {
    Shutdown();
}

bool SystemBridge::Initialize(SDL_Window* window) {
    if (m_initialized) return true;
    m_window = window;

#ifdef __ANDROID__
    m_deviceInfo.platformName = "Android";
    m_deviceInfo.sdkInt = SDL_GetAndroidSDKVersion();
    m_deviceInfo.osVersion = std::to_string(m_deviceInfo.sdkInt);
    m_deviceInfo.isSimulator = false;
    m_simulateCutout = false;
    QueryAndroidDeviceInfo();

    // Default Android runtime permissions start in Unknown until requested/verified
    m_permissions.fill(PermissionStatus::Unknown);
#else
    m_deviceInfo.platformName = std::string(SDL_GetPlatform()) + " (Mobile Simulator)";
    m_deviceInfo.manufacturer = "GuiKit";
    m_deviceInfo.model = "Pixel 9 Pro Simulator";
    m_deviceInfo.osVersion = "Android 15 (API 35 Sim)";
    m_deviceInfo.sdkInt = 35;
    m_deviceInfo.isSimulator = true;
    m_permissions.fill(PermissionStatus::Granted);
#endif

    if (SDL_InitSubSystem(SDL_INIT_HAPTIC)) {
        m_haptic = SDL_OpenHapticFromMouse();
        if (m_haptic) {
            SDL_InitHapticRumble(m_haptic);
        }
    }

    UpdateSafeArea();
    UpdateBatteryInfo();
    m_initialized = true;
    return true;
}

void SystemBridge::Shutdown() {
    if (m_haptic) {
        SDL_CloseHaptic(m_haptic);
        m_haptic = nullptr;
    }
    m_initialized = false;
}

void SystemBridge::UpdateFrame(double dtSeconds) {
    m_pollTimerSec += dtSeconds;
    UpdateSafeArea();
    if (m_pollTimerSec >= 2.0) {
        m_pollTimerSec = 0.0;
        UpdateBatteryInfo();
    }
}

void SystemBridge::QueryAndroidDeviceInfo() {
#ifdef __ANDROID__
    char manufacturer[PROP_VALUE_MAX] = {0};
    char model[PROP_VALUE_MAX] = {0};
    char release[PROP_VALUE_MAX] = {0};

    if (__system_property_get("ro.product.manufacturer", manufacturer) > 0) {
        m_deviceInfo.manufacturer = manufacturer;
    }
    if (__system_property_get("ro.product.model", model) > 0) {
        m_deviceInfo.model = model;
    }
    if (__system_property_get("ro.build.version.release", release) > 0) {
        m_deviceInfo.osVersion = std::string("Android ") + release + " (API " + std::to_string(m_deviceInfo.sdkInt) + ")";
    }
#endif
}

PermissionStatus SystemBridge::GetPermissionStatus(Permission perm) const noexcept {
    const auto idx = static_cast<size_t>(perm);
    return idx < m_permissions.size() ? m_permissions[idx] : PermissionStatus::Unknown;
}

void SystemBridge::RequestPermission(Permission perm, PermissionCallback callback) {
    const auto idx = static_cast<size_t>(perm);
    if (idx >= m_permissions.size()) return;

#ifdef __ANDROID__
    const char* androidPerm = GetAndroidPermissionString(perm);
    auto* reqCtx = new PermissionRequestContext{this, perm, std::move(callback)};

    const bool started = SDL_RequestAndroidPermission(
        androidPerm,
        [](void* userdata, const char* /*permission*/, bool granted) {
            auto* ctx = static_cast<PermissionRequestContext*>(userdata);
            const PermissionStatus status = granted ? PermissionStatus::Granted : PermissionStatus::Denied;
            const auto pIdx = static_cast<size_t>(ctx->perm);
            if (ctx->bridge && pIdx < ctx->bridge->m_permissions.size()) {
                ctx->bridge->m_permissions[pIdx] = status;
            }
            if (ctx->callback) {
                ctx->callback(ctx->perm, status);
            }
            delete ctx;
        },
        reqCtx
    );

    if (!started) {
        m_permissions[idx] = PermissionStatus::Denied;
        if (reqCtx->callback) {
            reqCtx->callback(perm, PermissionStatus::Denied);
        }
        delete reqCtx;
    }
#else
    m_permissions[idx] = PermissionStatus::Granted;
    if (callback) {
        callback(perm, PermissionStatus::Granted);
    }
#endif
}

const char* SystemBridge::GetAndroidPermissionString(Permission perm) noexcept {
    switch (perm) {
        case Permission::Camera:         return "android.permission.CAMERA";
        case Permission::RecordAudio:    return "android.permission.RECORD_AUDIO";
        case Permission::FineLocation:   return "android.permission.ACCESS_FINE_LOCATION";
        case Permission::CoarseLocation: return "android.permission.ACCESS_COARSE_LOCATION";
        case Permission::BodySensors:    return "android.permission.BODY_SENSORS";
        case Permission::Bluetooth:      return "android.permission.BLUETOOTH_CONNECT";
        case Permission::Notifications:  return "android.permission.POST_NOTIFICATIONS";
    }
    return "android.permission.CAMERA";
}

std::string_view SystemBridge::GetPermissionDisplayName(Permission perm) noexcept {
    switch (perm) {
        case Permission::Camera:         return "Camera (android.permission.CAMERA)";
        case Permission::RecordAudio:    return "Microphone (android.permission.RECORD_AUDIO)";
        case Permission::FineLocation:   return "Fine GNSS Location (ACCESS_FINE_LOCATION)";
        case Permission::CoarseLocation: return "Coarse Location (ACCESS_COARSE_LOCATION)";
        case Permission::BodySensors:    return "Body & Activity Sensors (BODY_SENSORS)";
        case Permission::Bluetooth:      return "Bluetooth BLE (BLUETOOTH_CONNECT)";
        case Permission::Notifications:  return "Notifications (POST_NOTIFICATIONS)";
    }
    return "Unknown Permission";
}

void SystemBridge::TriggerHaptic(HapticStyle style) {
    if (!m_hapticsEnabled) return;

    uint32_t durationMs = 12;
    switch (style) {
        case HapticStyle::Selection: durationMs = 8;  break;
        case HapticStyle::Light:     durationMs = 14; break;
        case HapticStyle::Medium:    durationMs = 24; break;
        case HapticStyle::Heavy:     durationMs = 42; break;
        case HapticStyle::Success:   durationMs = 30; break;
        case HapticStyle::Warning:   durationMs = 45; break;
        case HapticStyle::Error:     durationMs = 65; break;
    }
    VibrateMs(durationMs);
}

void SystemBridge::VibrateMs(uint32_t durationMs) {
    if (!m_hapticsEnabled || durationMs == 0) return;
    ++m_hapticCount;

    if (m_haptic) {
        SDL_PlayHapticRumble(m_haptic, 0.65f, durationMs);
    }

#ifdef __ANDROID__
    auto* env = static_cast<JNIEnv*>(SDL_GetAndroidJNIEnv());
    jobject activity = static_cast<jobject>(SDL_GetAndroidActivity());
    if (env && activity) {
        jclass activityClass = env->GetObjectClass(activity);
        jmethodID getSystemService = env->GetMethodID(
            activityClass,
            "getSystemService",
            "(Ljava/lang/String;)Ljava/lang/Object;"
        );
        if (getSystemService) {
            jstring vibStr = env->NewStringUTF("vibrator");
            jobject vibrator = env->CallObjectMethod(activity, getSystemService, vibStr);
            env->DeleteLocalRef(vibStr);

            if (!env->ExceptionCheck() && vibrator) {
                jclass vibClass = env->GetObjectClass(vibrator);
                jmethodID vibrateMethod = env->GetMethodID(vibClass, "vibrate", "(J)V");
                if (vibrateMethod) {
                    env->CallVoidMethod(vibrator, vibrateMethod, static_cast<jlong>(durationMs));
                    if (env->ExceptionCheck()) {
                        env->ExceptionClear();
                    }
                }
                env->DeleteLocalRef(vibClass);
                env->DeleteLocalRef(vibrator);
            } else if (env->ExceptionCheck()) {
                env->ExceptionClear();
            }
        }
        env->DeleteLocalRef(activityClass);
        env->DeleteLocalRef(activity);
    }
#endif
}

void SystemBridge::UpdateSafeArea() {
    m_safeArea = {};

    if (m_window) {
        SDL_Rect safeRect{};
        int winW = 0;
        int winH = 0;
        SDL_GetWindowSize(m_window, &winW, &winH);
        if (SDL_GetWindowSafeArea(m_window, &safeRect) && winW > 0 && winH > 0) {
            m_safeArea.left   = static_cast<float>(std::max(0, safeRect.x));
            m_safeArea.top    = static_cast<float>(std::max(0, safeRect.y));
            m_safeArea.right  = static_cast<float>(std::max(0, winW - (safeRect.x + safeRect.w)));
            m_safeArea.bottom = static_cast<float>(std::max(0, winH - (safeRect.y + safeRect.h)));
        }
    }

    if (m_deviceInfo.isSimulator && m_simulateCutout &&
        m_safeArea.top == 0.0f && m_safeArea.bottom == 0.0f) {
        m_safeArea.top = 28.0f;    // Status bar + punch-hole camera cutout
        m_safeArea.bottom = 16.0f; // Gesture navigation indicator bar
    }
}

void SystemBridge::UpdateBatteryInfo() {
    int seconds = 0;
    int percent = -1;
    const SDL_PowerState state = SDL_GetPowerInfo(&seconds, &percent);

    if (percent >= 0 && percent <= 100) {
        m_battery.levelPct = static_cast<float>(percent);
    } else if (m_deviceInfo.isSimulator) {
        m_battery.levelPct = 86.0f;
    }

    m_battery.isCharging = (state == SDL_POWERSTATE_CHARGING || state == SDL_POWERSTATE_CHARGED);
}

void SystemBridge::SetSoftKeyboardVisible(bool visible) {
    if (!m_window) return;
    if (visible) {
        SDL_StartTextInput(m_window);
    } else {
        SDL_StopTextInput(m_window);
    }
}

bool SystemBridge::IsSoftKeyboardVisible() const noexcept {
    return m_window ? SDL_TextInputActive(m_window) : false;
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
