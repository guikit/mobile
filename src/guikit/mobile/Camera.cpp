#include "guikit/mobile/Camera.hpp"
#include <SDL3/SDL.h>
#include <cstring>

namespace guikit::mobile {

CameraService::CameraService() = default;

CameraService::~CameraService() {
    Shutdown();
}

bool CameraService::Initialize() {
    const bool ok = m_hal.initialize();

#if !defined(__ANDROID__)
    // Register any desktop SDL3 webcams into the HAL before syncing device list
    if (SDL_InitSubSystem(SDL_INIT_CAMERA)) {
        int count = 0;
        if (SDL_CameraID* camIds = SDL_GetCameras(&count)) {
            for (int i = 0; i < count; ++i) {
                keel::CameraDeviceInfo dev{};
                dev.platform_camera_id = camIds[i];
                if (const char* cname = SDL_GetCameraName(camIds[i])) {
                    dev.name = keel::InplaceString<64>(keel::StringRef(cname));
                } else {
                    dev.name = keel::InplaceString<64>("Hardware Camera");
                }
                const SDL_CameraPosition pos = SDL_GetCameraPosition(camIds[i]);
                if (pos == SDL_CAMERA_POSITION_FRONT_FACING) {
                    dev.facing = keel::CameraFacing::Front;
                } else if (pos == SDL_CAMERA_POSITION_BACK_FACING) {
                    dev.facing = keel::CameraFacing::Back;
                } else {
                    dev.facing = keel::CameraFacing::External;
                }
                dev.preferred_width = 640;
                dev.preferred_height = 480;
                dev.preferred_fps = 30.0f;
                dev.is_synthetic = false;
                m_hal.register_external_device(dev);
            }
            SDL_free(camIds);
        }
    }
#endif

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
    if (m_sdlCamera) {
        SDL_CloseCamera(static_cast<SDL_Camera*>(m_sdlCamera));
        m_sdlCamera = nullptr;
    }

    const bool ok = m_hal.open_camera(deviceIndex, width, height);
    SyncDevicesFromHal();

#if !defined(__ANDROID__)
    if (const auto* dev = GetActiveDevice(); dev && !dev->isSynthetic && dev->sdlCameraId != 0) {
        SDL_CameraSpec spec{};
        spec.format = SDL_PIXELFORMAT_RGB24;
        spec.colorspace = SDL_COLORSPACE_SRGB;
        spec.width = width > 0 ? width : dev->preferredWidth;
        spec.height = height > 0 ? height : dev->preferredHeight;
        spec.framerate_numerator = 30;
        spec.framerate_denominator = 1;
        m_sdlCamera = SDL_OpenCamera(dev->sdlCameraId, &spec);
    }
#endif

    SyncStatsFromHal();
    return ok;
}

void CameraService::CloseCamera() {
    if (m_sdlCamera) {
        SDL_CloseCamera(static_cast<SDL_Camera*>(m_sdlCamera));
        m_sdlCamera = nullptr;
    }
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

#if !defined(__ANDROID__)
    if (m_sdlCamera) {
        auto* sdlCam = static_cast<SDL_Camera*>(m_sdlCamera);
        Uint64 timestampNs = 0;
        if (SDL_Surface* frameSurf = SDL_AcquireCameraFrame(sdlCam, &timestampNs)) {
            if (SDL_Surface* rgbSurf = SDL_ConvertSurface(frameSurf, SDL_PIXELFORMAT_RGB24)) {
                if (rgbSurf->w > 0 && rgbSurf->h > 0 && rgbSurf->pixels) {
                    const size_t totalBytes = static_cast<size_t>(rgbSurf->pitch) * static_cast<size_t>(rgbSurf->h);
                    m_hal.push_external_rgb_frame(
                        keel::ArrayRef<uint8_t>(static_cast<const uint8_t*>(rgbSurf->pixels), totalBytes),
                        static_cast<uint32_t>(rgbSurf->w),
                        static_cast<uint32_t>(rgbSurf->h),
                        static_cast<size_t>(rgbSurf->pitch)
                    );
                }
                SDL_DestroySurface(rgbSurf);
            }
            SDL_ReleaseCameraFrame(sdlCam, frameSurf);
        }
    }
#endif

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
