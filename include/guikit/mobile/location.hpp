#pragma once

#include <cstdint>
#include <span>
#include <vector>
#include "guikit/geo/map_grid.hpp"
#include "guikit/mobile/types.hpp"
#include "keel/mobile/location.h"

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

/// Mobile GNSS / Location service backed by `keel::LocationService` (`keelcpp/mobile`).
/// Directly produces `guikit::GeoCoord` trajectories for rendering on `guikit::MapView`.
class LocationService {
public:
    static constexpr size_t kMaxTrackHistory = keel::LocationService::kMaxTrackHistory;

    LocationService();
    ~LocationService();

    LocationService(const LocationService&) = delete;
    LocationService& operator=(const LocationService&) = delete;

    bool Initialize();
    void Shutdown();

    void SetEnabled(bool enabled) { m_hal.set_enabled(enabled); }
    [[nodiscard]] bool IsEnabled() const noexcept { return m_hal.is_enabled(); }

    void UpdateFrame(double dtSeconds);

    [[nodiscard]] const LocationFix& GetLatestFix() const noexcept { return m_latestFix; }
    [[nodiscard]] std::span<const GeoCoord> GetBreadcrumbTrack() const noexcept { return m_trackHistory; }

    void ClearBreadcrumbTrack() noexcept {
        m_hal.clear_breadcrumb_track();
        m_trackHistory.clear();
    }
    void SetSimulatedRouteCenter(GeoCoord center) noexcept;

    /// Computes great-circle Haversine distance in meters between two WGS-84 coordinates.
    [[nodiscard]] static double DistanceMeters(GeoCoord a, GeoCoord b) noexcept {
        return keel::LocationService::distance_meters(
            keel::GeoCoord{a.lat, a.lon},
            keel::GeoCoord{b.lat, b.lon}
        );
    }

    /// Computes initial forward azimuth (bearing) in degrees [0..360) from coordinate `a` to `b`.
    [[nodiscard]] static float BearingDegrees(GeoCoord a, GeoCoord b) noexcept {
        return keel::LocationService::bearing_degrees(
            keel::GeoCoord{a.lat, a.lon},
            keel::GeoCoord{b.lat, b.lon}
        );
    }

    /// Total distance traveled along the recorded breadcrumb track in meters.
    [[nodiscard]] double GetTotalTrackDistanceMeters() const noexcept { return m_hal.total_track_distance_meters(); }

    [[nodiscard]] keel::LocationService& Hal() noexcept { return m_hal; }
    [[nodiscard]] const keel::LocationService& Hal() const noexcept { return m_hal; }

private:
    void SyncFromHal();

    keel::LocationService m_hal{};
    LocationFix m_latestFix{};
    std::vector<GeoCoord> m_trackHistory{};
};

} // namespace guikit::mobile
