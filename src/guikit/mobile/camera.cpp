#include "guikit/mobile/camera.hpp"
#include <SDL3/SDL.h>
#include <cstring>

namespace guikit::mobile {

CameraService::CameraService() = default;

CameraService::~CameraService() {
    Shutdown();
}

bool CameraService::Initialize() {
    const bool ok = m_hal.initialize();
    SyncDevicesFromHal();
    SyncStatsFromHal();
    return ok;
}

void CameraService::Shutdown() {
    CloseCamera();
    if (m_previewTexture) {
        SDL_DestroyTexture(static_cast<SDL_Texture*>(m_previewTexture));
        m_previewTexture = nullptr;
        m_texWidth = 0;
        m_texHeight = 0;
    }
    m_hal.shutdown();
    m_devices.clear();
}

bool CameraService::OpenCamera(size_t deviceIndex, int width, int height) {
    const bool ok = m_hal.open_camera(deviceIndex, width, height);
    SyncDevicesFromHal();
    SyncStatsFromHal();
    return ok;
}

void CameraService::CloseCamera() {
    m_hal.close_camera();
}

bool CameraService::SwitchCameraFacing() {
    if (m_devices.size() <= 1) return false;
    const size_t nextIdx = (m_hal.active_device_index() + 1) % m_devices.size();
    const auto& nextDev = m_devices[nextIdx];
    return OpenCamera(nextIdx, nextDev.preferredWidth, nextDev.preferredHeight);
}

const CameraDeviceInfo* CameraService::GetActiveDevice() const noexcept {
    const size_t idx = m_hal.active_device_index();
    if (idx < m_devices.size()) {
        return &m_devices[idx];
    }
    return nullptr;
}

void CameraService::UpdateFrame(double dtSeconds, void* nativeRenderer) {
    if (!m_hal.is_streaming() || m_hal.is_paused()) return;

    const uint64_t prevSeq = m_hal.frame_stats().frame_sequence;
    m_hal.update_frame(dtSeconds);
    SyncStatsFromHal();

    auto* renderer = static_cast<SDL_Renderer*>(nativeRenderer);
    if (renderer && m_hal.frame_stats().frame_sequence != prevSeq) {
        const uint32_t w = m_hal.frame_width();
        const uint32_t h = m_hal.frame_height();
        const auto rgba = m_hal.rgba_pixels();
        if (w > 0 && h > 0 && rgba.size() == static_cast<size_t>(w) * static_cast<size_t>(h)) {
            if (m_previewTexture && (m_texWidth != w || m_texHeight != h)) {
                SDL_DestroyTexture(static_cast<SDL_Texture*>(m_previewTexture));
                m_previewTexture = nullptr;
            }
            if (!m_previewTexture) {
                m_previewTexture = SDL_CreateTexture(
                    renderer,
                    SDL_PIXELFORMAT_ABGR8888,
                    SDL_TEXTUREACCESS_STREAMING,
                    static_cast<int>(w),
                    static_cast<int>(h)
                );
                m_texWidth = w;
                m_texHeight = h;
            }
            if (m_previewTexture) {
                SDL_UpdateTexture(
                    static_cast<SDL_Texture*>(m_previewTexture),
                    nullptr,
                    rgba.data(),
                    static_cast<int>(w * sizeof(uint32_t))
                );
            }
        }
    }
}

ImageViewData CameraService::GetImageViewData() const noexcept {
    const auto rgb = m_hal.rgb_pixels();
    return ImageViewData::FromU8(
        std::span<const uint8_t>(rgb.data(), rgb.size()),
        m_hal.frame_width(),
        m_hal.frame_height(),
        3,
        m_stats.frameSequence
    );
}

void CameraService::SyncDevicesFromHal() {
    const auto halDevs = m_hal.devices();
    m_devices.resize(halDevs.size());
    for (size_t i = 0; i < halDevs.size(); ++i) {
        const auto& src = halDevs[i];
        auto& dst = m_devices[i];
        dst.deviceIndex = src.device_index;
        dst.sdlCameraId = src.platform_camera_id;
        dst.androidCameraId.assign(src.android_camera_id.data(), src.android_camera_id.size());
        dst.name.assign(src.name.data(), src.name.size());
        dst.facing = static_cast<CameraFacing>(src.facing);
        dst.sensorOrientationDeg = src.sensor_orientation_deg;
        dst.hasFlashUnit = src.has_flash_unit;
        dst.preferredWidth = src.preferred_width;
        dst.preferredHeight = src.preferred_height;
        dst.preferredFps = src.preferred_fps;
        dst.isSynthetic = src.is_synthetic;
    }
}

void CameraService::SyncStatsFromHal() {
    const auto& hs = m_hal.frame_stats();
    m_stats.width = hs.width;
    m_stats.height = hs.height;
    m_stats.frameSequence = hs.frame_sequence;
    m_stats.fps = hs.fps;
    m_stats.meanLuminance = hs.mean_luminance;
    m_stats.contrastRms = hs.contrast_rms;
    std::memcpy(m_stats.histLuma.data(), hs.hist_luma, sizeof(hs.hist_luma));
    std::memcpy(m_stats.histR.data(), hs.hist_r, sizeof(hs.hist_r));
    std::memcpy(m_stats.histG.data(), hs.hist_g, sizeof(hs.hist_g));
    std::memcpy(m_stats.histB.data(), hs.hist_b, sizeof(hs.hist_b));
}

} // namespace guikit::mobile
