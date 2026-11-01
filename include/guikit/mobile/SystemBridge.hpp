#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <SDL3/SDL.h>
#include "guikit/mobile/Types.hpp"

namespace guikit::mobile {

/// System-level bridge for Mobile OS features:
/// - Runtime Permissions (`SDL_RequestAndroidPermission` / iOS authorization)
/// - Haptic Feedback & Vibration (`VibrationEffect` / `SDL_Haptic` / `UIImpactFeedbackGenerator`)
/// - Display Safe Area Insets (`SDL_GetWindowSafeArea` + simulated device cutouts)
/// - Battery & Power telemetry (`SDL_GetPowerInfo` + Android BatteryManager)
/// - Native OS Toasts & Soft Keyboard (IME) control
class SystemBridge {
public:
    using PermissionCallback = std::function<void(Permission, PermissionStatus)>;

    SystemBridge();
    ~SystemBridge();

    SystemBridge(const SystemBridge&) = delete;
    SystemBridge& operator=(const SystemBridge&) = delete;

    bool Initialize(SDL_Window* window = nullptr);
    void Shutdown();

    void SetWindow(SDL_Window* window) noexcept { m_window = window; }
    void UpdateFrame(double dtSeconds);

    // --- Device & OS Info ---
    [[nodiscard]] const DeviceInfo& GetDeviceInfo() const noexcept { return m_deviceInfo; }

    // --- Runtime Permissions ---
    [[nodiscard]] PermissionStatus GetPermissionStatus(Permission perm) const noexcept;
    void RequestPermission(Permission perm, PermissionCallback callback = {});
    [[nodiscard]] static const char* GetAndroidPermissionString(Permission perm) noexcept;
    [[nodiscard]] static std::string_view GetPermissionDisplayName(Permission perm) noexcept;

    // --- Haptics & Tactile Feedback ---
    void TriggerHaptic(HapticStyle style = HapticStyle::Light);
    void VibrateMs(uint32_t durationMs);
    void SetHapticsEnabled(bool enabled) noexcept { m_hapticsEnabled = enabled; }
    [[nodiscard]] bool IsHapticsEnabled() const noexcept { return m_hapticsEnabled; }
    [[nodiscard]] uint64_t GetHapticPulseCount() const noexcept { return m_hapticCount; }

    // --- Safe Area Insets & Screen Orientation ---
    [[nodiscard]] SafeAreaInsets GetSafeAreaInsets() const noexcept { return m_safeArea; }
    void SetSimulateMobileCutout(bool enabled) noexcept { m_simulateCutout = enabled; }
    [[nodiscard]] bool IsSimulatingMobileCutout() const noexcept { return m_simulateCutout; }

    // --- Battery & Thermal ---
    [[nodiscard]] const BatteryStatus& GetBatteryStatus() const noexcept { return m_battery; }

    // --- Soft Keyboard (IME) & Native Toast ---
    void SetSoftKeyboardVisible(bool visible);
    [[nodiscard]] bool IsSoftKeyboardVisible() const noexcept;
    void ShowNativeToast(std::string_view message, bool longDuration = false);

private:
    void QueryAndroidDeviceInfo();
    void UpdateSafeArea();
    void UpdateBatteryInfo();

    SDL_Window* m_window{nullptr};
    SDL_Haptic* m_haptic{nullptr};
    DeviceInfo m_deviceInfo{};
    SafeAreaInsets m_safeArea{};
    BatteryStatus m_battery{};
    std::array<PermissionStatus, 7> m_permissions{};

    bool m_initialized{false};
    bool m_hapticsEnabled{true};
    bool m_simulateCutout{true};
    uint64_t m_hapticCount{0};
    double m_pollTimerSec{0.0};
};

} // namespace guikit::mobile
