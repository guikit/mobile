#include "guikit/mobile/touch_gesture.hpp"
#include <SDL3/SDL.h>
#include <algorithm>
#include <cmath>

namespace guikit::mobile {

void TouchGestureRecognizer::OnTouchDown(uint64_t fingerId, Vec2 posPx, Context& ui) {
    if (TouchContact* c = FindOrAllocateContact(fingerId)) {
        c->posPx = posPx;
        c->startPosPx = posPx;
        c->velocityPxPerSec = {0.0f, 0.0f};
        c->downTimeSec = m_elapsedSec;
        c->active = true;
    }

    m_state.activeFingerCount = CountActiveContacts();
    m_state.inertialVelocityPx = {0.0f, 0.0f};
    m_longPressFiredForCurrentTouch = false;

    if (m_state.activeFingerCount == 1) {
        ui.OnMouseMove(posPx);
        ui.OnMouseDown(1);
        m_state.isPinching = false;
        m_prevPinchDistancePx = 0.0f;
    } else if (m_state.activeFingerCount == 2) {
        // Transition from single-finger tap to two-finger pinch gesture
        ui.OnMouseUp(1);
        m_state.isPinching = true;
        m_state.isPanning = false;

        const TouchContact* f0 = nullptr;
        const TouchContact* f1 = nullptr;
        for (const auto& contact : m_contacts) {
            if (!contact.active) continue;
            if (!f0) f0 = &contact;
            else { f1 = &contact; break; }
        }
        if (f0 && f1) {
            m_prevPinchDistancePx = std::hypot(f1->posPx.x - f0->posPx.x, f1->posPx.y - f0->posPx.y);
            m_state.pinchCenterPx = {
                (f0->posPx.x + f1->posPx.x) * 0.5f,
                (f0->posPx.y + f1->posPx.y) * 0.5f
            };
        }
    }
}

void TouchGestureRecognizer::OnTouchMove(uint64_t fingerId, Vec2 posPx, Vec2 deltaPx, Context& ui) {
    if (TouchContact* c = FindContact(fingerId)) {
        c->posPx = posPx;
        c->velocityPxPerSec = {deltaPx.x * 60.0f, deltaPx.y * 60.0f};
    }

    m_state.activeFingerCount = CountActiveContacts();

    if (m_state.activeFingerCount == 1) {
        ui.OnMouseMove(posPx);
        m_state.panDeltaPx = deltaPx;

        if (const TouchContact* c = FindContact(fingerId)) {
            const float totalMove = std::hypot(c->posPx.x - c->startPosPx.x, c->posPx.y - c->startPosPx.y);
            if (totalMove > 8.0f) {
                m_state.isPanning = true;
                // Feed touch drag into smooth scroll wheel so ScrollArea & Table scroll naturally by finger
                if (std::abs(deltaPx.y) > 0.5f || std::abs(deltaPx.x) > 0.5f) {
                    ui.OnMouseWheel(deltaPx.x * 0.04f, deltaPx.y * 0.04f);
                }
            }
        }
    } else if (m_state.activeFingerCount >= 2) {
        const TouchContact* f0 = nullptr;
        const TouchContact* f1 = nullptr;
        for (const auto& contact : m_contacts) {
            if (!contact.active) continue;
            if (!f0) f0 = &contact;
            else { f1 = &contact; break; }
        }
        if (f0 && f1) {
            const float dist = std::hypot(f1->posPx.x - f0->posPx.x, f1->posPx.y - f0->posPx.y);
            m_state.pinchCenterPx = {
                (f0->posPx.x + f1->posPx.x) * 0.5f,
                (f0->posPx.y + f1->posPx.y) * 0.5f
            };
            if (m_prevPinchDistancePx > 8.0f && dist > 8.0f) {
                const float ratio = dist / m_prevPinchDistancePx;
                m_state.pinchScaleDelta = ratio;
                const float wheelSteps = (dist - m_prevPinchDistancePx) * 0.035f;
                if (std::abs(wheelSteps) > 0.02f) {
                    ui.OnMouseMove(m_state.pinchCenterPx);
                    ui.OnMouseWheel(0.0f, wheelSteps);
                }
            }
            m_prevPinchDistancePx = dist;
        }
    }
}

void TouchGestureRecognizer::OnTouchUp(uint64_t fingerId, Vec2 posPx, Context& ui) {
    if (TouchContact* c = FindContact(fingerId)) {
        if (m_state.isPanning && m_inertialEnabled) {
            m_state.inertialVelocityPx = c->velocityPxPerSec;
        }
        c->active = false;
    }

    m_state.activeFingerCount = CountActiveContacts();
    if (m_state.activeFingerCount == 0) {
        ui.OnMouseMove(posPx);
        ui.OnMouseUp(1);
        m_state.isPanning = false;
        m_state.isPinching = false;
        m_state.pinchScaleDelta = 1.0f;
        m_prevPinchDistancePx = 0.0f;
    }
}

bool TouchGestureRecognizer::ProcessSdlEvent(
    const void* sdlEvent,
    Context& ui,
    Vec2 viewportSizePx
) {
    if (!sdlEvent) return false;
    const auto& ev = *static_cast<const SDL_Event*>(sdlEvent);

    const float vw = std::max(1.0f, viewportSizePx.x);
    const float vh = std::max(1.0f, viewportSizePx.y);

    if (ev.type == SDL_EVENT_FINGER_DOWN) {
        OnTouchDown(static_cast<uint64_t>(ev.tfinger.fingerID), {ev.tfinger.x * vw, ev.tfinger.y * vh}, ui);
        return true;
    }

    if (ev.type == SDL_EVENT_FINGER_MOTION) {
        OnTouchMove(
            static_cast<uint64_t>(ev.tfinger.fingerID),
            {ev.tfinger.x * vw, ev.tfinger.y * vh},
            {ev.tfinger.dx * vw, ev.tfinger.dy * vh},
            ui
        );
        return true;
    }

    if (ev.type == SDL_EVENT_FINGER_UP || ev.type == SDL_EVENT_FINGER_CANCELED) {
        OnTouchUp(static_cast<uint64_t>(ev.tfinger.fingerID), {ev.tfinger.x * vw, ev.tfinger.y * vh}, ui);
        return true;
    }

    return false;
}

void TouchGestureRecognizer::UpdateFrame(double dtSeconds, Context& ui) {
    m_elapsedSec += dtSeconds;
    m_state.longPressTriggered = false;
    m_state.panDeltaPx = {0.0f, 0.0f};
    m_state.pinchScaleDelta = 1.0f;

    // 1. Check single-finger stationary Long-Press (~500 ms)
    if (m_state.activeFingerCount == 1 && !m_state.isPanning && !m_longPressFiredForCurrentTouch) {
        for (const auto& c : m_contacts) {
            if (!c.active) continue;
            const float moveDist = std::hypot(c.posPx.x - c.startPosPx.x, c.posPx.y - c.startPosPx.y);
            if (moveDist < 10.0f && (m_elapsedSec - c.downTimeSec) >= 0.52) {
                m_longPressFiredForCurrentTouch = true;
                m_state.longPressTriggered = true;
                ui.OnMouseMove(c.posPx);
                ui.OnMouseDown(3);
                ui.OnMouseUp(3);
            }
            break;
        }
    }

    // 2. Apply exponential decay kinetic/inertial scroll after finger release
    if (m_inertialEnabled && m_state.activeFingerCount == 0) {
        const float speed = std::hypot(m_state.inertialVelocityPx.x, m_state.inertialVelocityPx.y);
        if (speed > 18.0f) {
            const float decay = std::exp(-6.5f * static_cast<float>(dtSeconds));
            m_state.inertialVelocityPx.x *= decay;
            m_state.inertialVelocityPx.y *= decay;

            const float wheelX = static_cast<float>(m_state.inertialVelocityPx.x * dtSeconds * 0.04);
            const float wheelY = static_cast<float>(m_state.inertialVelocityPx.y * dtSeconds * 0.04);
            if (std::abs(wheelX) > 0.005f || std::abs(wheelY) > 0.005f) {
                ui.OnMouseWheel(wheelX, wheelY);
                ui.RequestRepaint(2);
            }
        } else {
            m_state.inertialVelocityPx = {0.0f, 0.0f};
        }
    }
}

TouchContact* TouchGestureRecognizer::FindOrAllocateContact(uint64_t id) {
    if (TouchContact* existing = FindContact(id)) {
        return existing;
    }
    for (auto& c : m_contacts) {
        if (!c.active) {
            c.fingerId = id;
            return &c;
        }
    }
    return &m_contacts[0];
}

TouchContact* TouchGestureRecognizer::FindContact(uint64_t id) {
    for (auto& c : m_contacts) {
        if (c.active && c.fingerId == id) {
            return &c;
        }
    }
    return nullptr;
}

uint32_t TouchGestureRecognizer::CountActiveContacts() const noexcept {
    uint32_t count = 0;
    for (const auto& c : m_contacts) {
        if (c.active) ++count;
    }
    return count;
}

} // namespace guikit::mobile
