#pragma once

#include <cstdint>
#include <span>
#include <vector>
#include "guikit/geo/MapGrid.hpp"
#include "guikit/mobile/Types.hpp"

namespace guikit::mobile {

/// Single GNSS / Fused Location fix (GPS / GLONASS / Galileo / Network).
struct LocationFix {
    GeoCoord coord{59.9343, 30.3351};   ///< Latitude & Longitude in WGS-84 degrees
    float altitudeMeters{14.0f};        ///< Altitude above WGS-84 ellipsoid / MSL in meters
    float horizontalAccuracyM{3.8f};    ///< 1-sigma horizontal accuracy radius in meters
    float verticalAccuracyM{5.2f};      ///< Vertical accuracy in meters
    float speedMetersPerSec{0.0f};      ///< Ground speed in m/s
    float courseDeg{0.0f};              ///< Ground track / bearing in degrees [0..360)
    int satellitesInView{18};
    int satellitesUsed{12};
    uint64_t timestampMs{0};
    bool hasFix{true};
    bool isMock{false};

    [[nodiscard]] float SpeedKnots() const noexcept { return speedMetersPerSec * 1.943844f; }
    [[nodiscard]] float SpeedKmh() const noexcept { return speedMetersPerSec * 3.6f; }
};

/// Mobile GNSS / Location service (Android LocationManager JNI + Desktop Route Simulator).
/// Directly produces `guikit::GeoCoord` trajectories for rendering on `guikit::MapView`.
class LocationService {
public:
    static constexpr size_t kMaxTrackHistory = 512;

    LocationService();
    ~LocationService();

    LocationService(const LocationService&) = delete;
    LocationService& operator=(const LocationService&) = delete;

    bool Initialize();
    void Shutdown();

    void SetEnabled(bool enabled);
    [[nodiscard]] bool IsEnabled() const noexcept { return m_enabled; }

    void UpdateFrame(double dtSeconds);

    [[nodiscard]] const LocationFix& GetLatestFix() const noexcept { return m_latestFix; }
    [[nodiscard]] std::span<const GeoCoord> GetBreadcrumbTrack() const noexcept { return m_trackHistory; }

    void ClearBreadcrumbTrack() noexcept { m_trackHistory.clear(); }
    void SetSimulatedRouteCenter(GeoCoord center) noexcept;

    /// Computes great-circle Haversine distance in meters between two WGS-84 coordinates.
    [[nodiscard]] static double DistanceMeters(GeoCoord a, GeoCoord b) noexcept;

    /// Computes initial forward azimuth (bearing) in degrees [0..360) from coordinate `a` to `b`.
    [[nodiscard]] static float BearingDegrees(GeoCoord a, GeoCoord b) noexcept;

    /// Total distance traveled along the recorded breadcrumb track in meters.
    [[nodiscard]] double GetTotalTrackDistanceMeters() const noexcept { return m_totalDistanceMeters; }

private:
    bool PollAndroidLocation();
    void StepSimulatedRoute(double dtSeconds);
    void AppendTrackPoint(GeoCoord pt);

    LocationFix m_latestFix{};
    GeoCoord m_simCenter{44.6020, 33.4950}; ///< Matches embedded demo_bay.geojson chart by default
    std::vector<GeoCoord> m_trackHistory;
    double m_totalDistanceMeters{0.0};
    double m_simTimeSec{0.0};
    double m_trackSampleTimerSec{0.0};
    bool m_initialized{false};
    bool m_enabled{true};
    bool m_hasAndroidLocation{false};
};

} // namespace guikit::mobile
