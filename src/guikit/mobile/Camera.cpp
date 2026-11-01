#include "guikit/mobile/Camera.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <numbers>

#ifdef __ANDROID__
#include <jni.h>
#endif

namespace guikit::mobile {

CameraService::CameraService() = default;

CameraService::~CameraService() {
    Shutdown();
}

bool CameraService::Initialize() {
    if (m_initialized) return true;

    m_devices.clear();

    // Try initializing SDL3 camera subsystem (Android Camera2 NDK / Windows MediaFoundation / V4L2)
    if (SDL_InitSubSystem(SDL_INIT_CAMERA)) {
        int count = 0;
        if (SDL_CameraID* camIds = SDL_GetCameras(&count)) {
            for (int i = 0; i < count; ++i) {
                CameraDeviceInfo info{};
                info.deviceIndex = static_cast<uint32_t>(m_devices.size());
                info.sdlCameraId = camIds[i];
                const char* cname = SDL_GetCameraName(camIds[i]);
                info.name = cname ? cname : "Hardware Camera";

                const SDL_CameraPosition pos = SDL_GetCameraPosition(camIds[i]);
                if (pos == SDL_CAMERA_POSITION_FRONT_FACING) {
                    info.facing = CameraFacing::Front;
                } else if (pos == SDL_CAMERA_POSITION_BACK_FACING) {
                    info.facing = CameraFacing::Back;
                } else {
                    info.facing = CameraFacing::External;
                }
                info.preferredWidth = 640;
                info.preferredHeight = 360;
                info.preferredFps = 30.0f;
                info.isSynthetic = false;
                m_devices.push_back(std::move(info));
            }
            SDL_free(camIds);
        }
    }

    // Always append synthetic Back & Front test cameras so switching & vision tools work on any machine
    {
        CameraDeviceInfo rearSim{};
        rearSim.deviceIndex = static_cast<uint32_t>(m_devices.size());
        rearSim.sdlCameraId = 0;
        rearSim.name = "Synthetic Rear Wide (IMX890 Sim)";
        rearSim.facing = CameraFacing::Back;
        rearSim.preferredWidth = 480;
        rearSim.preferredHeight = 300;
        rearSim.preferredFps = 60.0f;
        rearSim.isSynthetic = true;
        m_devices.push_back(std::move(rearSim));

        CameraDeviceInfo frontSim{};
        frontSim.deviceIndex = static_cast<uint32_t>(m_devices.size());
        frontSim.sdlCameraId = 0;
        frontSim.name = "Synthetic Front Selfie (ToF Sim)";
        frontSim.facing = CameraFacing::Front;
        frontSim.preferredWidth = 480;
        frontSim.preferredHeight = 300;
        frontSim.preferredFps = 60.0f;
        frontSim.isSynthetic = true;
        m_devices.push_back(std::move(frontSim));
    }

    m_initialized = true;
    OpenCamera(0, 480, 300);
    return true;
}

void CameraService::Shutdown() {
    CloseCamera();
    if (m_previewTexture) {
        SDL_DestroyTexture(m_previewTexture);
        m_previewTexture = nullptr;
    }
    m_devices.clear();
    m_initialized = false;
}

bool CameraService::OpenCamera(size_t deviceIndex, int width, int height) {
    if (!m_initialized && !Initialize()) return false;
    if (m_devices.empty()) return false;

    CloseCamera();

    m_activeDeviceIndex = std::min(deviceIndex, m_devices.size() - 1);
    const auto& dev = m_devices[m_activeDeviceIndex];

    m_frameWidth = static_cast<uint32_t>(std::max(64, width));
    m_frameHeight = static_cast<uint32_t>(std::max(64, height));
    m_rgbPixels.assign(static_cast<size_t>(m_frameWidth) * m_frameHeight * 3, 0);
    m_rgbaPixels.assign(static_cast<size_t>(m_frameWidth) * m_frameHeight, 0xFF000000u);

    m_usingSynthetic = dev.isSynthetic;
    if (!dev.isSynthetic && dev.sdlCameraId != 0) {
        SDL_CameraSpec spec{};
        spec.format = SDL_PIXELFORMAT_RGB24;
        spec.colorspace = SDL_COLORSPACE_SRGB;
        spec.width = static_cast<int>(m_frameWidth);
        spec.height = static_cast<int>(m_frameHeight);
        spec.framerate_numerator = 30;
        spec.framerate_denominator = 1;

        m_sdlCamera = SDL_OpenCamera(dev.sdlCameraId, &spec);
        if (!m_sdlCamera) {
            m_usingSynthetic = true;
        }
    }

    m_streaming = true;
    GenerateProceduralFrame(0.016);
    ComputeFrameHistograms();
    return true;
}

void CameraService::CloseCamera() {
    if (m_sdlCamera) {
        SDL_CloseCamera(m_sdlCamera);
        m_sdlCamera = nullptr;
    }
    m_streaming = false;
}

bool CameraService::SwitchCameraFacing() {
    if (m_devices.size() <= 1) return false;
    const size_t nextIdx = (m_activeDeviceIndex + 1) % m_devices.size();
    return OpenCamera(nextIdx, static_cast<int>(m_frameWidth), static_cast<int>(m_frameHeight));
}

const CameraDeviceInfo* CameraService::GetActiveDevice() const noexcept {
    if (m_activeDeviceIndex < m_devices.size()) {
        return &m_devices[m_activeDeviceIndex];
    }
    return nullptr;
}

void CameraService::UpdateFrame(double dtSeconds, SDL_Renderer* renderer) {
    if (!m_streaming) return;

    bool gotHardwareFrame = false;

    if (m_sdlCamera && !m_usingSynthetic) {
        Uint64 timestampNs = 0;
        SDL_Surface* frameSurf = SDL_AcquireCameraFrame(m_sdlCamera, &timestampNs);
        if (frameSurf) {
            SDL_Surface* rgbSurf = SDL_ConvertSurface(frameSurf, SDL_PIXELFORMAT_RGB24);
            if (rgbSurf) {
                if (static_cast<uint32_t>(rgbSurf->w) != m_frameWidth ||
                    static_cast<uint32_t>(rgbSurf->h) != m_frameHeight) {
                    m_frameWidth = static_cast<uint32_t>(std::max(1, rgbSurf->w));
                    m_frameHeight = static_cast<uint32_t>(std::max(1, rgbSurf->h));
                    m_rgbPixels.resize(static_cast<size_t>(m_frameWidth) * m_frameHeight * 3);
                    m_rgbaPixels.resize(static_cast<size_t>(m_frameWidth) * m_frameHeight);
                    if (m_previewTexture) {
                        SDL_DestroyTexture(m_previewTexture);
                        m_previewTexture = nullptr;
                    }
                }

                const float evGain = std::pow(2.0f, m_exposureEv);
                const auto* srcBase = static_cast<const uint8_t*>(rgbSurf->pixels);
                for (uint32_t y = 0; y < m_frameHeight; ++y) {
                    const uint8_t* srcRow = srcBase + static_cast<size_t>(y) * rgbSurf->pitch;
                    uint8_t* dstRgb = m_rgbPixels.data() + static_cast<size_t>(y) * m_frameWidth * 3;
                    uint32_t* dstRgba = m_rgbaPixels.data() + static_cast<size_t>(y) * m_frameWidth;
                    for (uint32_t x = 0; x < m_frameWidth; ++x) {
                        const uint8_t r = static_cast<uint8_t>(std::clamp(srcRow[x * 3 + 0] * evGain, 0.0f, 255.0f));
                        const uint8_t g = static_cast<uint8_t>(std::clamp(srcRow[x * 3 + 1] * evGain, 0.0f, 255.0f));
                        const uint8_t b = static_cast<uint8_t>(std::clamp(srcRow[x * 3 + 2] * evGain, 0.0f, 255.0f));
                        dstRgb[x * 3 + 0] = r;
                        dstRgb[x * 3 + 1] = g;
                        dstRgb[x * 3 + 2] = b;
                        dstRgba[x] = (0xFFu << 24) | (static_cast<uint32_t>(b) << 16) |
                                     (static_cast<uint32_t>(g) << 8) | static_cast<uint32_t>(r);
                    }
                }
                SDL_DestroySurface(rgbSurf);
                gotHardwareFrame = true;
            }
            SDL_ReleaseCameraFrame(m_sdlCamera, frameSurf);
        }
    }

    if (!gotHardwareFrame && m_usingSynthetic) {
        GenerateProceduralFrame(dtSeconds);
        gotHardwareFrame = true;
    }

    if (gotHardwareFrame) {
        ++m_stats.frameSequence;
        ++m_fpsFrameCount;
        m_fpsAccumSec += dtSeconds;
        if (m_fpsAccumSec >= 0.25) {
            m_stats.fps = static_cast<float>(m_fpsFrameCount / m_fpsAccumSec);
            m_fpsFrameCount = 0;
            m_fpsAccumSec = 0.0;
        }
        ComputeFrameHistograms();

        if (renderer) {
            if (!m_previewTexture) {
                m_previewTexture = SDL_CreateTexture(
                    renderer,
                    SDL_PIXELFORMAT_ABGR8888,
                    SDL_TEXTUREACCESS_STREAMING,
                    static_cast<int>(m_frameWidth),
                    static_cast<int>(m_frameHeight)
                );
            }
            if (m_previewTexture) {
                SDL_UpdateTexture(
                    m_previewTexture,
                    nullptr,
                    m_rgbaPixels.data(),
                    static_cast<int>(m_frameWidth * sizeof(uint32_t))
                );
            }
        }
    }
}

ImageViewData CameraService::GetImageViewData() const noexcept {
    return ImageViewData::FromU8(
        m_rgbPixels,
        m_frameWidth,
        m_frameHeight,
        3,
        m_stats.frameSequence
    );
}

void CameraService::SetFlashMode(FlashMode mode) {
    m_flashMode = mode;
    SetTorchEnabled(mode == FlashMode::Torch);
}

void CameraService::SetTorchEnabled(bool enabled) {
    m_torchEnabled = enabled;
    if (enabled) {
        m_flashMode = FlashMode::Torch;
    } else if (m_flashMode == FlashMode::Torch) {
        m_flashMode = FlashMode::Off;
    }
}

void CameraService::TriggerTapToFocus(Vec2 normalizedPoint) {
    m_focusPoint.x = std::clamp(normalizedPoint.x, 0.05f, 0.95f);
    m_focusPoint.y = std::clamp(normalizedPoint.y, 0.05f, 0.95f);
}

void CameraService::SetExposureCompensationEv(float ev) noexcept {
    m_exposureEv = std::clamp(ev, -2.5f, 2.5f);
}

void CameraService::SetZoomFactor(float zoom) noexcept {
    m_zoomFactor = std::clamp(zoom, 1.0f, 8.0f);
}

bool CameraService::CaptureSnapshot() {
    if (m_rgbPixels.empty()) return false;
    m_lastCapturedRgb = m_rgbPixels;
    ++m_capturedPhotos;
    return true;
}

void CameraService::GenerateProceduralFrame(double dtSeconds, float /*pitchDeg*/, float /*rollDeg*/) {
    m_simTimeSec += dtSeconds;
    const float t = static_cast<float>(m_simTimeSec);

    const uint32_t w = m_frameWidth;
    const uint32_t h = m_frameHeight;
    if (w == 0 || h == 0) return;

    const auto* activeDev = GetActiveDevice();
    const bool isFront = activeDev && (activeDev->facing == CameraFacing::Front);
    const float evGain = std::pow(2.0f, m_exposureEv) * (m_torchEnabled ? 1.28f : 1.0f);
    const float invZoom = 1.0f / std::max(1.0f, m_zoomFactor);

    // Moving optical target & reticle around tap-to-focus point
    const float targetCx = m_focusPoint.x + 0.14f * std::cos(t * 0.9f);
    const float targetCy = m_focusPoint.y + 0.12f * std::sin(t * 1.1f);

    for (uint32_t y = 0; y < h; ++y) {
        const float v = ((static_cast<float>(y) / static_cast<float>(h)) - 0.5f) * invZoom + 0.5f;
        uint8_t* dstRgb = m_rgbPixels.data() + static_cast<size_t>(y) * w * 3;
        uint32_t* dstRgba = m_rgbaPixels.data() + static_cast<size_t>(y) * w;

        for (uint32_t x = 0; x < w; ++x) {
            const float u = ((static_cast<float>(x) / static_cast<float>(w)) - 0.5f) * invZoom + 0.5f;

            // Synthetic camera scene: horizon sky/terrain + Siemens star / ISO test target + color bars
            float r = 0.0f;
            float g = 0.0f;
            float b = 0.0f;

            const float dx = (u - targetCx) * (static_cast<float>(w) / static_cast<float>(h));
            const float dy = (v - targetCy);
            const float dist = std::sqrt(dx * dx + dy * dy);

            if (v < 0.16f) {
                // Top SMPTE/Macbeth calibration color strip
                const int bar = std::clamp(static_cast<int>(u * 7.0f), 0, 6);
                constexpr float kBars[7][3] = {
                    {215.0f, 215.0f, 215.0f},
                    {215.0f, 215.0f,  25.0f},
                    { 25.0f, 215.0f, 215.0f},
                    { 25.0f, 215.0f,  25.0f},
                    {215.0f,  25.0f, 215.0f},
                    {215.0f,  25.0f,  25.0f},
                    { 25.0f,  60.0f, 215.0f}
                };
                r = kBars[bar][0];
                g = kBars[bar][1];
                b = kBars[bar][2];
            } else {
                // Smooth gradient & Siemens optical resolution star
                const float angle = std::atan2(dy, dx);
                const float spokes = 0.5f + 0.5f * std::cos(angle * 16.0f + t * 2.0f);
                const float ring = std::exp(-dist * 5.2f);

                if (isFront) {
                    r = 45.0f + 135.0f * (1.0f - v) + 75.0f * ring * spokes;
                    g = 55.0f + 110.0f * u + 85.0f * ring;
                    b = 95.0f + 120.0f * v + 60.0f * ring * (1.0f - spokes);
                } else {
                    r = 28.0f + 95.0f * u + 125.0f * ring * spokes;
                    g = 48.0f + 130.0f * (1.0f - std::abs(v - 0.55f)) + 110.0f * ring * spokes;
                    b = 72.0f + 140.0f * (1.0f - v) + 90.0f * ring;
                }
            }

            // Focus reticle box around m_focusPoint
            const float fdx = std::abs(u - m_focusPoint.x);
            const float fdy = std::abs(v - m_focusPoint.y);
            if ((fdx < 0.055f && fdy < 0.075f) && (fdx > 0.048f || fdy > 0.066f)) {
                r = 34.0f;
                g = 235.0f;
                b = 115.0f;
            }

            const uint8_t ur = static_cast<uint8_t>(std::clamp(r * evGain, 0.0f, 255.0f));
            const uint8_t ug = static_cast<uint8_t>(std::clamp(g * evGain, 0.0f, 255.0f));
            const uint8_t ub = static_cast<uint8_t>(std::clamp(b * evGain, 0.0f, 255.0f));

            dstRgb[x * 3 + 0] = ur;
            dstRgb[x * 3 + 1] = ug;
            dstRgb[x * 3 + 2] = ub;
            dstRgba[x] = (0xFFu << 24) | (static_cast<uint32_t>(ub) << 16) |
                         (static_cast<uint32_t>(ug) << 8) | static_cast<uint32_t>(ur);
        }
    }
}

void CameraService::ComputeFrameHistograms() {
    m_stats.width = m_frameWidth;
    m_stats.height = m_frameHeight;
    m_stats.histLuma.fill(0);
    m_stats.histR.fill(0);
    m_stats.histG.fill(0);
    m_stats.histB.fill(0);

    const size_t totalPixels = static_cast<size_t>(m_frameWidth) * m_frameHeight;
    if (totalPixels == 0 || m_rgbPixels.size() < totalPixels * 3) return;

    double sumLuma = 0.0;
    double sumLumaSq = 0.0;

    for (size_t i = 0; i < totalPixels; ++i) {
        const uint8_t r = m_rgbPixels[i * 3 + 0];
        const uint8_t g = m_rgbPixels[i * 3 + 1];
        const uint8_t b = m_rgbPixels[i * 3 + 2];
        const uint8_t y = static_cast<uint8_t>((77u * r + 150u * g + 29u * b) >> 8);

        ++m_stats.histR[r];
        ++m_stats.histG[g];
        ++m_stats.histB[b];
        ++m_stats.histLuma[y];

        sumLuma += y;
        sumLumaSq += static_cast<double>(y) * y;
    }

    const double invN = 1.0 / static_cast<double>(totalPixels);
    const double mean = sumLuma * invN;
    const double variance = std::max(0.0, (sumLumaSq * invN) - (mean * mean));
    m_stats.meanLuminance = static_cast<float>(mean);
    m_stats.contrastRms = static_cast<float>(std::sqrt(variance));
}

} // namespace guikit::mobile
