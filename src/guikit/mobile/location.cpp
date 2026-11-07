#include "guikit/mobile/Location.hpp"
#include <SDL3/SDL.h>

namespace guikit::mobile {

LocationService::LocationService() {
    m_trackHistory.reserve(kMaxTrackHistory);
}

LocationService::~LocationService() {
    Shutdown();
}

bool LocationService::Initialize() {
    void* jniEnv = nullptr;
    void* activity = nullptr;
#ifdef __ANDROID__
    jniEnv = SDL_GetAndroidJNIEnv();
    activity = SDL_GetAndroidActivity();
#endif
    const bool ok = m_hal.initialize(jniEnv, activity);
    SyncFromHal();
    return ok;
}

void LocationService::Shutdown() {
    m_hal.shutdown();
}

void LocationService::SetSimulatedRouteCenter(GeoCoord center) noexcept {
    m_hal.set_simulated_route_center(keel::GeoCoord{center.lat, center.lon});
    SyncFromHal();
}

void LocationService::UpdateFrame(double dtSeconds) {
#ifdef __ANDROID__
    m_hal.set_android_context(SDL_GetAndroidJNIEnv(), SDL_GetAndroidActivity());
#endif
    m_hal.update_frame(dtSeconds);
    SyncFromHal();
}

void LocationService::SyncFromHal() {
    const auto& hf = m_hal.latest_fix();
    m_latestFix.coord = {hf.coord.lat, hf.coord.lon};
    m_latestFix.altitudeMeters = hf.altitude_meters;
    m_latestFix.horizontalAccuracyM = hf.horizontal_accuracy_m;
    m_latestFix.verticalAccuracyM = hf.vertical_accuracy_m;
    m_latestFix.speedMetersPerSec = hf.speed_mps;
    m_latestFix.courseDeg = hf.course_deg;
    m_latestFix.satellitesInView = hf.satellites_in_view;
    m_latestFix.satellitesUsed = hf.satellites_used;
    m_latestFix.timestampMs = hf.timestamp_ms != 0 ? hf.timestamp_ms : SDL_GetTicks();
    m_latestFix.hasFix = hf.has_fix;
    m_latestFix.isMock = hf.is_mock;

    const auto track = m_hal.breadcrumb_track();
    m_trackHistory.resize(track.size());
    for (size_t i = 0; i < track.size(); ++i) {
        m_trackHistory[i] = GeoCoord{track[i].lat, track[i].lon};
    }
}

} // namespace guikit::mobile
