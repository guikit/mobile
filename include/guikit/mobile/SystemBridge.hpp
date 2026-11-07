#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include "guikit/mobile/Types.hpp"
#include "keel/mobile/system.h"

namespace guikit::mobile {

/// System-level bridge for Mobile OS features backed by `keel::SystemBridge` (`keelcpp/mobile`) + SDL3 window integration.
class SystemBridge {
public:
    using PermissionCallback = std::function<void(Permission, PermissionStatus)>;

    SystemBridge();
    ~SystemBridge();

    SystemBridge(const SystemBridge&) = delete;
    SystemBridge& operator=(const SystemBridge&) = delete;

    bool Initialize(void* nativeWindowHandle = nullptr);
    void Shutdown();

    void SetWindow(void* nativeWindowHandle) noexcept { m_window = nativeWindowHandle; }
    void UpdateFrame(double dtSeconds);

    // --- Device & OS Info ---
    [[nodiscard]] const DeviceInfo& GetDeviceInfo() const noexcept { return m_deviceInfo; }

    // --- Runtime Permissions ---
    [[nodiscard]] PermissionStatus GetPermissionStatus(Permission perm) const noexcept {
        return static_cast<PermissionStatus>(m_hal.permission_status(static_cast<keel::Permission>(perm)));
    }
    void RequestPermission(Permission perm, PermissionCallback callback = {});
    [[nodiscard]] static const char* GetAndroidPermissionString(Permission perm) noexcept {
        return keel::SystemBridge::android_permission_string(static_cast<keel::Permission>(perm));
    }
    [[nodiscard]] static std::string_view GetPermissionDisplayName(Permission perm) noexcept {
        const auto s = keel::SystemBridge::permission_display_name(static_cast<keel::Permission>(perm));
        return {s.data(), s.size()};
    }

    // --- Haptics & Tactile Feedback ---
    void TriggerHaptic(HapticStyle style = HapticStyle::Light);
    void VibrateMs(uint32_t durationMs);
    void SetHapticsEnabled(bool enabled) noexcept { m_hal.set_haptics_enabled(enabled); }
    [[nodiscard]] bool IsHapticsEnabled() const noexcept { return m_hal.is_haptics_enabled(); }
    [[nodiscard]] uint64_t GetHapticPulseCount() const noexcept { return m_hal.haptic_pulse_count(); }

    // --- Safe Area Insets & Screen Orientation ---
    [[nodiscard]] SafeAreaInsets GetSafeAreaInsets() const noexcept {
        const auto sa = m_hal.safe_area_insets();
        return {sa.top, sa.right, sa.bottom, sa.left};
    }
    [[nodiscard]] ScreenOrientation GetScreenOrientation() const noexcept {
        return static_cast<ScreenOrientation>(m_hal.screen_orientation());
    }
    void SetScreenOrientation(ScreenOrientation orientation) noexcept {
        m_hal.set_screen_orientation(static_cast<keel::ScreenOrientation>(orientation));
    }
    void SetSimulateMobileCutout(bool enabled) noexcept {
        m_hal.set_simulate_mobile_cutout(enabled);
    }
    [[nodiscard]] bool IsSimulatingMobileCutout() const noexcept {
        return m_hal.is_simulating_mobile_cutout();
    }

    // --- Battery & Thermal ---
    [[nodiscard]] const BatteryStatus& GetBatteryStatus() const noexcept { return m_battery; }

    // --- Soft Keyboard (IME) & Native Toast ---
    void SetSoftKeyboardVisible(bool visible);
    [[nodiscard]] bool IsSoftKeyboardVisible() const noexcept;
    void ShowNativeToast(std::string_view message, bool longDuration = false);

    [[nodiscard]] keel::SystemBridge& Hal() noexcept { return m_hal; }
    [[nodiscard]] const keel::SystemBridge& Hal() const noexcept { return m_hal; }

private:
    void SyncFromHal();
    void UpdateSafeArea();
    void UpdateBatteryInfo();

    keel::SystemBridge m_hal{};
    void* m_window{nullptr};
    void* m_haptic{nullptr};
    DeviceInfo m_deviceInfo{};
    BatteryStatus m_battery{};
    double m_pollTimerSec{0.0};
};

} // namespace guikit::mobile
