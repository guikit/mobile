#include "guikit/mobile/Location.hpp"
#include <SDL3/SDL.h>
#include <algorithm>
#include <cmath>
#include <numbers>

#ifdef __ANDROID__
#include <jni.h>
#endif

namespace guikit::mobile {

namespace {

constexpr double kEarthRadiusMeters = 6371008.8;
constexpr double kDegToRad = std::numbers::pi / 180.0;
constexpr double kRadToDeg = 180.0 / std::numbers::pi;

} // namespace

LocationService::LocationService() {
    m_trackHistory.reserve(kMaxTrackHistory);
}

LocationService::~LocationService() {
    Shutdown();
}

bool LocationService::Initialize() {
    if (m_initialized) return true;

    m_latestFix.coord = m_simCenter;
    m_latestFix.altitudeMeters = 18.5f;
    m_latestFix.horizontalAccuracyM = 3.2f;
    m_latestFix.verticalAccuracyM = 4.8f;
    m_latestFix.speedMetersPerSec = 6.4f;
    m_latestFix.courseDeg = 45.0f;
    m_latestFix.satellitesInView = 22;
    m_latestFix.satellitesUsed = 14;
    m_latestFix.hasFix = true;
    m_latestFix.isMock = true;

    m_trackHistory.clear();
    m_totalDistanceMeters = 0.0;

    // Seed initial trajectory points so MapView immediately renders a live track
    for (int i = 0; i < 24; ++i) {
        StepSimulatedRoute(0.25);
    }

    m_initialized = true;
    return true;
}

void LocationService::Shutdown() {
    m_initialized = false;
}

void LocationService::SetEnabled(bool enabled) {
    m_enabled = enabled;
}

void LocationService::SetSimulatedRouteCenter(GeoCoord center) noexcept {
    m_simCenter = center;
    m_trackHistory.clear();
    m_totalDistanceMeters = 0.0;
    m_latestFix.coord = center;
}

void LocationService::UpdateFrame(double dtSeconds) {
    if (!m_enabled) return;

    const double clampedDt = std::clamp(dtSeconds, 0.001, 0.25);
    if (!PollAndroidLocation()) {
        StepSimulatedRoute(clampedDt);
    }
}

bool LocationService::PollAndroidLocation() {
#ifdef __ANDROID__
    auto* env = static_cast<JNIEnv*>(SDL_GetAndroidJNIEnv());
    jobject activity = static_cast<jobject>(SDL_GetAndroidActivity());
    if (!env || !activity) return false;

    bool gotFix = false;
    jclass activityClass = env->GetObjectClass(activity);
    jmethodID getSystemService = env->GetMethodID(
        activityClass,
        "getSystemService",
        "(Ljava/lang/String;)Ljava/lang/Object;"
    );

    if (getSystemService) {
        jstring locStr = env->NewStringUTF("location");
        jobject locManager = env->CallObjectMethod(activity, getSystemService, locStr);
        env->DeleteLocalRef(locStr);

        if (!env->ExceptionCheck() && locManager) {
            jclass lmClass = env->GetObjectClass(locManager);
            jmethodID getLastKnown = env->GetMethodID(
                lmClass,
                "getLastKnownLocation",
                "(Ljava/lang/String;)Landroid/location/Location;"
            );

            if (getLastKnown) {
                jstring gpsProvider = env->NewStringUTF("gps");
                jobject locationObj = env->CallObjectMethod(locManager, getLastKnown, gpsProvider);
                env->DeleteLocalRef(gpsProvider);

                if (env->ExceptionCheck()) {
                    env->ExceptionClear();
                    locationObj = nullptr;
                }

                if (!locationObj) {
                    jstring netProvider = env->NewStringUTF("network");
                    locationObj = env->CallObjectMethod(locManager, getLastKnown, netProvider);
                    env->DeleteLocalRef(netProvider);
                    if (env->ExceptionCheck()) {
                        env->ExceptionClear();
                        locationObj = nullptr;
                    }
                }

                if (locationObj) {
                    jclass locClass = env->GetObjectClass(locationObj);
                    jmethodID getLat = env->GetMethodID(locClass, "getLatitude", "()D");
                    jmethodID getLon = env->GetMethodID(locClass, "getLongitude", "()D");
                    jmethodID getAlt = env->GetMethodID(locClass, "getAltitude", "()D");
                    jmethodID getAcc = env->GetMethodID(locClass, "getAccuracy", "()F");
                    jmethodID getSpd = env->GetMethodID(locClass, "getSpeed", "()F");
                    jmethodID getBrg = env->GetMethodID(locClass, "getBearing", "()F");

                    const double lat = env->CallDoubleMethod(locationObj, getLat);
                    const double lon = env->CallDoubleMethod(locationObj, getLon);
                    const double alt = env->CallDoubleMethod(locationObj, getAlt);
                    const float acc  = env->CallFloatMethod(locationObj, getAcc);
                    const float spd  = env->CallFloatMethod(locationObj, getSpd);
                    const float brg  = env->CallFloatMethod(locationObj, getBrg);

                    m_latestFix.coord = {lat, lon};
                    m_latestFix.altitudeMeters = static_cast<float>(alt);
                    m_latestFix.horizontalAccuracyM = std::max(1.0f, acc);
                    m_latestFix.speedMetersPerSec = spd;
                    m_latestFix.courseDeg = brg;
                    m_latestFix.timestampMs = SDL_GetTicks();
                    m_latestFix.hasFix = true;
                    m_latestFix.isMock = false;
                    m_hasAndroidLocation = true;
                    AppendTrackPoint(m_latestFix.coord);
                    gotFix = true;

                    env->DeleteLocalRef(locClass);
                    env->DeleteLocalRef(locationObj);
                }
            }
            env->DeleteLocalRef(lmClass);
            env->DeleteLocalRef(locManager);
        } else if (env->ExceptionCheck()) {
            env->ExceptionClear();
        }
    }

    env->DeleteLocalRef(activityClass);
    env->DeleteLocalRef(activity);
    return gotFix;
#else
    return false;
#endif
}

void LocationService::StepSimulatedRoute(double dtSeconds) {
    m_simTimeSec += dtSeconds;
    m_trackSampleTimerSec += dtSeconds;

    const double omega = 0.085;
    const double phase = m_simTimeSec * omega;

    // Figure-8 / patrol loop around m_simCenter (~1.2 km radius)
    const double dLat = 0.011 * std::sin(phase);
    const double dLon = 0.018 * std::sin(phase * 0.5) * std::cos(phase * 0.5);

    const GeoCoord prevCoord = m_latestFix.coord;
    const GeoCoord newCoord{
        m_simCenter.lat + dLat,
        m_simCenter.lon + dLon
    };

    const double stepDist = DistanceMeters(prevCoord, newCoord);
    if (dtSeconds > 0.001 && stepDist > 0.01) {
        m_latestFix.speedMetersPerSec = static_cast<float>(std::clamp(stepDist / dtSeconds, 0.0, 45.0));
        m_latestFix.courseDeg = BearingDegrees(prevCoord, newCoord);
    }

    m_latestFix.coord = newCoord;
    m_latestFix.altitudeMeters = 16.0f + 4.5f * static_cast<float>(std::sin(m_simTimeSec * 0.2));
    m_latestFix.horizontalAccuracyM = 2.8f + 0.6f * static_cast<float>(std::cos(m_simTimeSec * 0.35));
    m_latestFix.timestampMs = SDL_GetTicks();
    m_latestFix.hasFix = true;
    m_latestFix.isMock = true;

    if (m_trackHistory.empty() || m_trackSampleTimerSec >= 0.20) {
        m_trackSampleTimerSec = 0.0;
        AppendTrackPoint(newCoord);
    }
}

void LocationService::AppendTrackPoint(GeoCoord pt) {
    if (!m_trackHistory.empty()) {
        const double d = DistanceMeters(m_trackHistory.back(), pt);
        if (d < 1.5) return;
        m_totalDistanceMeters += d;
    }

    if (m_trackHistory.size() < kMaxTrackHistory) {
        m_trackHistory.push_back(pt);
    } else {
        std::move(m_trackHistory.begin() + 1, m_trackHistory.end(), m_trackHistory.begin());
        m_trackHistory.back() = pt;
    }
}

double LocationService::DistanceMeters(GeoCoord a, GeoCoord b) noexcept {
    const double lat1 = a.lat * kDegToRad;
    const double lat2 = b.lat * kDegToRad;
    const double dLat = (b.lat - a.lat) * kDegToRad;
    const double dLon = (b.lon - a.lon) * kDegToRad;

    const double sinLat = std::sin(dLat * 0.5);
    const double sinLon = std::sin(dLon * 0.5);
    const double h = sinLat * sinLat + std::cos(lat1) * std::cos(lat2) * sinLon * sinLon;
    return 2.0 * kEarthRadiusMeters * std::asin(std::min(1.0, std::sqrt(h)));
}

float LocationService::BearingDegrees(GeoCoord a, GeoCoord b) noexcept {
    const double lat1 = a.lat * kDegToRad;
    const double lat2 = b.lat * kDegToRad;
    const double dLon = (b.lon - a.lon) * kDegToRad;

    const double y = std::sin(dLon) * std::cos(lat2);
    const double x = std::cos(lat1) * std::sin(lat2) -
                     std::sin(lat1) * std::cos(lat2) * std::cos(dLon);
    const double brg = std::fmod(std::atan2(y, x) * kRadToDeg + 360.0, 360.0);
    return static_cast<float>(brg);
}

} // namespace guikit::mobile
