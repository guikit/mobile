#pragma once

#include <array>
#include <cstdint>
#include <SDL3/SDL.h>
#include "guikit/Context.hpp"
#include "guikit/mobile/Types.hpp"

namespace guikit::mobile {

/// Active touch point tracked by `TouchGestureRecognizer`.
struct TouchContact {
    SDL_FingerID fingerId{0};
    Vec2 posPx{0.0f, 0.0f};        ///< Current position in window logical coordinates
    Vec2 startPosPx{0.0f, 0.0f};   ///< Initial touch-down position
    Vec2 velocityPxPerSec{0.0f, 0.0f};
    double downTimeSec{0.0};
    bool active{false};
};

/// High-level mobile multi-touch gesture state for the current frame.
struct GestureState {
    uint32_t activeFingerCount{0};
    bool isPanning{false};
    bool isPinching{false};
    bool longPressTriggered{false};
    Vec2 panDeltaPx{0.0f, 0.0f};
    Vec2 inertialVelocityPx{0.0f, 0.0f};
    float pinchScaleDelta{1.0f};   ///< Relative scale factor (>1 = zoom in, <1 = zoom out)
    Vec2 pinchCenterPx{0.0f, 0.0f};
};

/// Translates SDL3 touch/finger events (`SDL_EVENT_FINGER_*`) and mouse-touch emulation into:
/// 1. Standard pointer interactions on `guikit::Context`
/// 2. Inertial / kinetic touch scrolling (momentum scroll for `ScrollArea`, `Table`, `TreeView`)
/// 3. Two-finger `Pinch-to-Zoom` (mapped to `OnMouseWheel` around pinch focal point for `MapView`, `Scene`, `ImageView`)
/// 4. Long-press detection (mapped to context menu / secondary action)
class TouchGestureRecognizer {
public:
    static constexpr size_t kMaxContacts = 10;

    TouchGestureRecognizer() = default;
    ~TouchGestureRecognizer() = default;

    /// Processes an SDL3 event and dispatches translated pointer/scroll/zoom events to `guikit::Context`.
    /// Returns true if the event was consumed or modified by the mobile gesture recognizer.
    bool ProcessSdlEvent(const SDL_Event& ev, Context& ui, Vec2 viewportSizePx);

    /// Steps inertial scroll momentum and long-press timers once per frame before `ui.BeginFrame(...)`.
    void UpdateFrame(double dtSeconds, Context& ui);

    [[nodiscard]] const GestureState& GetState() const noexcept { return m_state; }
    [[nodiscard]] std::span<const TouchContact> GetContacts() const noexcept { return m_contacts; }

    void SetInertialScrollEnabled(bool enabled) noexcept { m_inertialEnabled = enabled; }
    [[nodiscard]] bool IsInertialScrollEnabled() const noexcept { return m_inertialEnabled; }

private:
    TouchContact* FindOrAllocateContact(SDL_FingerID id);
    TouchContact* FindContact(SDL_FingerID id);
    uint32_t CountActiveContacts() const noexcept;

    std::array<TouchContact, kMaxContacts> m_contacts{};
    GestureState m_state{};
    double m_elapsedSec{0.0};
    float m_prevPinchDistancePx{0.0f};
    bool m_inertialEnabled{true};
    bool m_longPressFiredForCurrentTouch{false};
};

} // namespace guikit::mobile
