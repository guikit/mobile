#include "guikit/mobile/Camera.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <numbers>

#ifdef __ANDROID__
#include <SDL3/SDL_system.h>
#include <android/log.h>
#include <camera/NdkCameraCaptureSession.h>
#include <camera/NdkCameraDevice.h>
#include <camera/NdkCameraManager.h>
#include <camera/NdkCameraMetadata.h>
#include <media/NdkImage.h>
#include <media/NdkImageReader.h>
#include <atomic>
#include <mutex>
#endif

namespace guikit::mobile {

#ifdef __ANDROID__
struct CameraService::AndroidCameraState {
    ACameraManager* manager{nullptr};
    ACameraDevice* device{nullptr};
    AImageReader* reader{nullptr};
    ANativeWindow* readerWindow{nullptr};
    ACaptureSessionOutputContainer* outputContainer{nullptr};
    ACaptureSessionOutput* sessionOutput{nullptr};
    ACameraOutputTarget* outputTarget{nullptr};
    ACaptureRequest* request{nullptr};
    ACameraCaptureSession* session{nullptr};

    std::atomic<bool> closing{false};
    std::atomic<bool> deviceError{false};
    std::atomic<bool> permissionRequested{false};
    int captureW{640};
    int captureH{480};
    int sensorOrientationDeg{90};
    CameraFacing facing{CameraFacing::Back};

    std::mutex frameMutex;
    std::vector<uint8_t> stagingRgb;
    int stagingW{0};
    int stagingH{0};
    uint64_t stagingSeq{0};
    uint64_t consumedSeq{0};

    static void OnImageAvailable(void* context, AImageReader* reader);
    static void OnDeviceDisconnected(void* context, ACameraDevice* device);
    static void OnDeviceError(void* context, ACameraDevice* device, int error);
};

void CameraService::AndroidCameraState::OnImageAvailable(void* context, AImageReader* reader) {
    auto* st = static_cast<CameraService::AndroidCameraState*>(context);
    if (!st || st->closing.load(std::memory_order_acquire)) {
        AImage* img = nullptr;
        if (reader && AImageReader_acquireLatestImage(reader, &img) == AMEDIA_OK && img) {
            AImage_delete(img);
        }
        return;
    }

    AImage* image = nullptr;
    if (AImageReader_acquireLatestImage(reader, &image) != AMEDIA_OK || !image) {
        return;
    }

    int32_t width = 0, height = 0, format = 0;
    AImage_getWidth(image, &width);
    AImage_getHeight(image, &height);
    AImage_getFormat(image, &format);

    if (width > 0 && height > 0 && format == AIMAGE_FORMAT_YUV_420_888) {
        uint8_t* yData = nullptr;
        uint8_t* uData = nullptr;
        uint8_t* vData = nullptr;
        int yLen = 0, uLen = 0, vLen = 0;
        int32_t yRowStride = 0, uvRowStride = 0, uvPixelStride = 0;

        AImage_getPlaneData(image, 0, &yData, &yLen);
        AImage_getPlaneData(image, 1, &uData, &uLen);
        AImage_getPlaneData(image, 2, &vData, &vLen);
        AImage_getPlaneRowStride(image, 0, &yRowStride);
        AImage_getPlaneRowStride(image, 1, &uvRowStride);
        AImage_getPlanePixelStride(image, 1, &uvPixelStride);

        if (yData && uData && vData && yRowStride > 0 && uvRowStride > 0 && uvPixelStride > 0) {
            std::vector<uint8_t> decoded(static_cast<size_t>(width) * height * 3);
            for (int32_t y = 0; y < height; ++y) {
                const uint8_t* yRow = yData + y * yRowStride;
                const uint8_t* uRow = uData + (y >> 1) * uvRowStride;
                const uint8_t* vRow = vData + (y >> 1) * uvRowStride;
                uint8_t* dstRow = decoded.data() + static_cast<size_t>(y) * width * 3;

                for (int32_t x = 0; x < width; ++x) {
                    const int32_t uvOffset = (x >> 1) * uvPixelStride;
                    const int32_t Y = static_cast<int32_t>(yRow[x]);
                    const int32_t U = static_cast<int32_t>(uRow[uvOffset]) - 128;
                    const int32_t V = static_cast<int32_t>(vRow[uvOffset]) - 128;

                    const int32_t r = Y + ((359 * V) >> 8);
                    const int32_t g = Y - ((88 * U + 183 * V) >> 8);
                    const int32_t b = Y + ((454 * U) >> 8);

                    dstRow[x * 3 + 0] = static_cast<uint8_t>(std::clamp(r, 0, 255));
                    dstRow[x * 3 + 1] = static_cast<uint8_t>(std::clamp(g, 0, 255));
                    dstRow[x * 3 + 2] = static_cast<uint8_t>(std::clamp(b, 0, 255));
                }
            }

            // Release AImage before locking frameMutex so cameraserver buffers never stall
            AImage_delete(image);
            image = nullptr;

            {
                std::lock_guard<std::mutex> lock(st->frameMutex);
                st->stagingRgb = std::move(decoded);
                st->stagingW = width;
                st->stagingH = height;
                ++st->stagingSeq;
            }
            return;
        }
    }

    if (image) {
        AImage_delete(image);
    }
}

void CameraService::AndroidCameraState::OnDeviceDisconnected(void* context, ACameraDevice* /*device*/) {
    auto* st = static_cast<CameraService::AndroidCameraState*>(context);
    if (st && !st->closing.load(std::memory_order_acquire)) {
        st->deviceError.store(true, std::memory_order_release);
    }
}

void CameraService::AndroidCameraState::OnDeviceError(void* context, ACameraDevice* /*device*/, int /*error*/) {
    auto* st = static_cast<CameraService::AndroidCameraState*>(context);
    if (st && !st->closing.load(std::memory_order_acquire)) {
        st->deviceError.store(true, std::memory_order_release);
    }
}

namespace {

void OnAndroidSessionClosed(void* /*context*/, ACameraCaptureSession* /*session*/) {}
void OnAndroidSessionReady(void* /*context*/, ACameraCaptureSession* /*session*/) {}
void OnAndroidSessionActive(void* /*context*/, ACameraCaptureSession* /*session*/) {}

int GetDisplayRotationDegrees() noexcept {
    const SDL_DisplayID primaryDisplay = SDL_GetPrimaryDisplay();
    const SDL_DisplayOrientation orient = SDL_GetCurrentDisplayOrientation(primaryDisplay);
    switch (orient) {
        case SDL_ORIENTATION_LANDSCAPE:         return 90;
        case SDL_ORIENTATION_PORTRAIT_FLIPPED:  return 180;
        case SDL_ORIENTATION_LANDSCAPE_FLIPPED: return 270;
        case SDL_ORIENTATION_PORTRAIT:
        default:                                return 0;
    }
}

} // namespace
#endif // __ANDROID__

CameraService::CameraService() = default;

CameraService::~CameraService() {
    Shutdown();
}

bool CameraService::Initialize() {
    if (m_initialized) return true;

    m_devices.clear();

#ifdef __ANDROID__
    if (!m_androidCam) {
        m_androidCam = new AndroidCameraState();
        m_androidCam->manager = ACameraManager_create();
    }

    if (m_androidCam && m_androidCam->manager) {
        ACameraIdList* idList = nullptr;
        if (ACameraManager_getCameraIdList(m_androidCam->manager, &idList) == ACAMERA_OK && idList) {
            bool foundPrimaryBack = false;
            bool foundPrimaryFront = false;

            for (int i = 0; i < idList->numCameras; ++i) {
                const char* camId = idList->cameraIds[i];
                if (!camId) continue;

                ACameraMetadata* chars = nullptr;
                if (ACameraManager_getCameraCharacteristics(m_androidCam->manager, camId, &chars) != ACAMERA_OK || !chars) {
                    continue;
                }

                CameraFacing facing = CameraFacing::Back;
                ACameraMetadata_const_entry entry{};
                if (ACameraMetadata_getConstEntry(chars, ACAMERA_LENS_FACING, &entry) == ACAMERA_OK && entry.count > 0) {
                    const uint8_t f = entry.data.u8[0];
                    if (f == ACAMERA_LENS_FACING_FRONT) {
                        facing = CameraFacing::Front;
                    } else if (f == ACAMERA_LENS_FACING_BACK) {
                        facing = CameraFacing::Back;
                    } else {
                        facing = CameraFacing::External;
                    }
                }

                // Register primary Rear and primary Front cameras so Switch toggles cleanly between Rear & Front
                if (facing == CameraFacing::Back && foundPrimaryBack) {
                    ACameraMetadata_free(chars);
                    continue;
                }
                if (facing == CameraFacing::Front && foundPrimaryFront) {
                    ACameraMetadata_free(chars);
                    continue;
                }

                int sensorOrient = (facing == CameraFacing::Front) ? 270 : 90;
                if (ACameraMetadata_getConstEntry(chars, ACAMERA_SENSOR_ORIENTATION, &entry) == ACAMERA_OK && entry.count > 0) {
                    sensorOrient = entry.data.i32[0];
                }

                bool hasFlash = false;
                if (ACameraMetadata_getConstEntry(chars, ACAMERA_FLASH_INFO_AVAILABLE, &entry) == ACAMERA_OK && entry.count > 0) {
                    hasFlash = (entry.data.u8[0] != 0);
                }

                int bestW = 640;
                int bestH = 480;
                if (ACameraMetadata_getConstEntry(chars, ACAMERA_SCALER_AVAILABLE_STREAM_CONFIGURATIONS, &entry) == ACAMERA_OK) {
                    int bestDiff = 100000000;
                    for (uint32_t j = 0; j + 3 < entry.count; j += 4) {
                        const int32_t fmt = entry.data.i32[j + 0];
                        const int32_t w   = entry.data.i32[j + 1];
                        const int32_t h   = entry.data.i32[j + 2];
                        const int32_t inp = entry.data.i32[j + 3];
                        if (inp == 0 && fmt == AIMAGE_FORMAT_YUV_420_888 && w >= 320 && h >= 240 && w <= 1280 && h <= 960) {
                            const int diff = std::abs(w - 640) + std::abs(h - 480);
                            if (diff < bestDiff) {
                                bestDiff = diff;
                                bestW = w;
                                bestH = h;
                            }
                        }
                    }
                }

                ACameraMetadata_free(chars);

                CameraDeviceInfo info{};
                info.deviceIndex = static_cast<uint32_t>(m_devices.size());
                info.androidCameraId = camId;
                info.facing = facing;
                info.sensorOrientationDeg = sensorOrient;
                info.hasFlashUnit = hasFlash;
                info.preferredWidth = bestW;
                info.preferredHeight = bestH;
                info.preferredFps = 30.0f;
                info.isSynthetic = false;

                if (facing == CameraFacing::Back) {
                    info.name = "Rear Camera";
                    foundPrimaryBack = true;
                } else if (facing == CameraFacing::Front) {
                    info.name = "Front Camera";
                    foundPrimaryFront = true;
                } else {
                    info.name = "External Camera";
                }

                m_devices.push_back(std::move(info));
            }
            ACameraManager_deleteCameraIdList(idList);
        }
    }
#else
    // Desktop / non-Android: try initializing SDL3 camera subsystem
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
                info.preferredHeight = 480;
                info.preferredFps = 30.0f;
                info.isSynthetic = false;
                m_devices.push_back(std::move(info));
            }
            SDL_free(camIds);
        }
    }
#endif

    // Only append synthetic test cameras when no physical hardware cameras were found
    if (m_devices.empty()) {
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
    const auto& firstDev = m_devices.front();
    OpenCamera(0, firstDev.preferredWidth, firstDev.preferredHeight);
    return true;
}

void CameraService::Shutdown() {
    CloseCamera();
#ifdef __ANDROID__
    if (m_androidCam) {
        if (m_androidCam->manager) {
            ACameraManager_delete(m_androidCam->manager);
            m_androidCam->manager = nullptr;
        }
        delete m_androidCam;
        m_androidCam = nullptr;
    }
#endif
    if (m_previewTexture) {
        SDL_DestroyTexture(m_previewTexture);
        m_previewTexture = nullptr;
    }
    m_devices.clear();
    m_initialized = false;
}

bool CameraService::OpenAndroidCamera(const CameraDeviceInfo& dev, int width, int height) {
#ifdef __ANDROID__
    if (!m_androidCam || !m_androidCam->manager || dev.androidCameraId.empty()) {
        return false;
    }

    CloseAndroidCamera();

    m_androidCam->closing.store(false, std::memory_order_release);
    m_androidCam->deviceError.store(false, std::memory_order_release);
    m_androidCam->captureW = (width > 0) ? width : dev.preferredWidth;
    m_androidCam->captureH = (height > 0) ? height : dev.preferredHeight;
    m_androidCam->sensorOrientationDeg = dev.sensorOrientationDeg;
    m_androidCam->facing = dev.facing;

    if (AImageReader_new(
            m_androidCam->captureW,
            m_androidCam->captureH,
            AIMAGE_FORMAT_YUV_420_888,
            4,
            &m_androidCam->reader) != AMEDIA_OK || !m_androidCam->reader) {
        CloseAndroidCamera();
        return false;
    }

    AImageReader_ImageListener imgListener{};
    imgListener.context = m_androidCam;
    imgListener.onImageAvailable = AndroidCameraState::OnImageAvailable;
    AImageReader_setImageListener(m_androidCam->reader, &imgListener);

    if (AImageReader_getWindow(m_androidCam->reader, &m_androidCam->readerWindow) != AMEDIA_OK ||
        !m_androidCam->readerWindow) {
        CloseAndroidCamera();
        return false;
    }

    ACameraDevice_StateCallbacks devCallbacks{};
    devCallbacks.context = m_androidCam;
    devCallbacks.onDisconnected = AndroidCameraState::OnDeviceDisconnected;
    devCallbacks.onError = AndroidCameraState::OnDeviceError;

    const camera_status_t openStatus = ACameraManager_openCamera(
        m_androidCam->manager,
        dev.androidCameraId.c_str(),
        &devCallbacks,
        &m_androidCam->device
    );
    if (openStatus != ACAMERA_OK || !m_androidCam->device) {
        if (openStatus == ACAMERA_ERROR_PERMISSION_DENIED &&
            !m_androidCam->permissionRequested.exchange(true)) {
            SDL_RequestAndroidPermission("android.permission.CAMERA", nullptr, nullptr);
        }
        CloseAndroidCamera();
        return false;
    }

    if (ACameraDevice_createCaptureRequest(
            m_androidCam->device,
            TEMPLATE_PREVIEW,
            &m_androidCam->request) != ACAMERA_OK || !m_androidCam->request) {
        CloseAndroidCamera();
        return false;
    }

    if (ACameraOutputTarget_create(m_androidCam->readerWindow, &m_androidCam->outputTarget) != ACAMERA_OK ||
        !m_androidCam->outputTarget) {
        CloseAndroidCamera();
        return false;
    }

    ACaptureRequest_addTarget(m_androidCam->request, m_androidCam->outputTarget);

    uint8_t afMode = ACAMERA_CONTROL_AF_MODE_CONTINUOUS_VIDEO;
    ACaptureRequest_setEntry_u8(m_androidCam->request, ACAMERA_CONTROL_AF_MODE, 1, &afMode);

    uint8_t aeMode = ACAMERA_CONTROL_AE_MODE_ON;
    ACaptureRequest_setEntry_u8(m_androidCam->request, ACAMERA_CONTROL_AE_MODE, 1, &aeMode);

    if (dev.hasFlashUnit) {
        uint8_t flashMode = m_torchEnabled ? ACAMERA_FLASH_MODE_TORCH : ACAMERA_FLASH_MODE_OFF;
        ACaptureRequest_setEntry_u8(m_androidCam->request, ACAMERA_FLASH_MODE, 1, &flashMode);
    } else {
        m_torchEnabled = false;
    }

    if (ACaptureSessionOutputContainer_create(&m_androidCam->outputContainer) != ACAMERA_OK ||
        !m_androidCam->outputContainer) {
        CloseAndroidCamera();
        return false;
    }

    if (ACaptureSessionOutput_create(m_androidCam->readerWindow, &m_androidCam->sessionOutput) != ACAMERA_OK ||
        !m_androidCam->sessionOutput) {
        CloseAndroidCamera();
        return false;
    }

    ACaptureSessionOutputContainer_add(m_androidCam->outputContainer, m_androidCam->sessionOutput);

    ACameraCaptureSession_stateCallbacks sessionCallbacks{};
    sessionCallbacks.context = m_androidCam;
    sessionCallbacks.onClosed = OnAndroidSessionClosed;
    sessionCallbacks.onReady = OnAndroidSessionReady;
    sessionCallbacks.onActive = OnAndroidSessionActive;

    if (ACameraDevice_createCaptureSession(
            m_androidCam->device,
            m_androidCam->outputContainer,
            &sessionCallbacks,
            &m_androidCam->session) != ACAMERA_OK || !m_androidCam->session) {
        CloseAndroidCamera();
        return false;
    }

    if (ACameraCaptureSession_setRepeatingRequest(
            m_androidCam->session,
            nullptr,
            1,
            &m_androidCam->request,
            nullptr) != ACAMERA_OK) {
        CloseAndroidCamera();
        return false;
    }

    return true;
#else
    (void)dev;
    (void)width;
    (void)height;
    return false;
#endif
}

void CameraService::CloseAndroidCamera() {
#ifdef __ANDROID__
    if (!m_androidCam) return;

    m_androidCam->closing.store(true, std::memory_order_release);

    if (m_androidCam->session) {
        ACameraCaptureSession_stopRepeating(m_androidCam->session);
        ACameraCaptureSession_close(m_androidCam->session);
        m_androidCam->session = nullptr;
    }
    if (m_androidCam->request) {
        ACaptureRequest_free(m_androidCam->request);
        m_androidCam->request = nullptr;
    }
    if (m_androidCam->outputTarget) {
        ACameraOutputTarget_free(m_androidCam->outputTarget);
        m_androidCam->outputTarget = nullptr;
    }
    if (m_androidCam->sessionOutput) {
        ACaptureSessionOutput_free(m_androidCam->sessionOutput);
        m_androidCam->sessionOutput = nullptr;
    }
    if (m_androidCam->outputContainer) {
        ACaptureSessionOutputContainer_free(m_androidCam->outputContainer);
        m_androidCam->outputContainer = nullptr;
    }
    if (m_androidCam->device) {
        ACameraDevice_close(m_androidCam->device);
        m_androidCam->device = nullptr;
    }
    if (m_androidCam->reader) {
        AImageReader_ImageListener nullListener{nullptr, nullptr};
        AImageReader_setImageListener(m_androidCam->reader, &nullListener);
        AImageReader_delete(m_androidCam->reader);
        m_androidCam->reader = nullptr;
        m_androidCam->readerWindow = nullptr;
    }
#endif
}

bool CameraService::OpenCamera(size_t deviceIndex, int width, int height) {
    if (!m_initialized && !Initialize()) return false;
    if (m_devices.empty()) return false;

    CloseCamera();

    m_activeDeviceIndex = std::min(deviceIndex, m_devices.size() - 1);
    const auto& dev = m_devices[m_activeDeviceIndex];

    const int reqW = (width > 0) ? width : dev.preferredWidth;
    const int reqH = (height > 0) ? height : dev.preferredHeight;

#ifdef __ANDROID__
    // Determine initial upright output dimensions based on sensor and display orientation
    const int dispRot = GetDisplayRotationDegrees();
    const int rotCw = (dev.facing == CameraFacing::Front)
        ? ((dev.sensorOrientationDeg + dispRot) % 360)
        : ((dev.sensorOrientationDeg - dispRot + 360) % 360);
    if (rotCw == 90 || rotCw == 270) {
        m_frameWidth = static_cast<uint32_t>(std::max(64, reqH));
        m_frameHeight = static_cast<uint32_t>(std::max(64, reqW));
    } else {
        m_frameWidth = static_cast<uint32_t>(std::max(64, reqW));
        m_frameHeight = static_cast<uint32_t>(std::max(64, reqH));
    }
#else
    m_frameWidth = static_cast<uint32_t>(std::max(64, reqW));
    m_frameHeight = static_cast<uint32_t>(std::max(64, reqH));
#endif

    // Initialize buffer to a clean dark viewfinder backdrop (RGB 18, 22, 30)
    const size_t numPx = static_cast<size_t>(m_frameWidth) * m_frameHeight;
    m_rgbPixels.resize(numPx * 3);
    for (size_t i = 0; i < numPx; ++i) {
        m_rgbPixels[i * 3 + 0] = 18;
        m_rgbPixels[i * 3 + 1] = 22;
        m_rgbPixels[i * 3 + 2] = 30;
    }
    m_rgbaPixels.assign(numPx, 0xFF1E1612u);

    m_usingSynthetic = dev.isSynthetic;
    m_retryTimerSec = 0.0;

    if (!dev.isSynthetic) {
#ifdef __ANDROID__
        if (!m_paused) {
            OpenAndroidCamera(dev, reqW, reqH);
        }
#else
        if (dev.sdlCameraId != 0) {
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
#endif
    }

    m_streaming = true;
    if (m_usingSynthetic) {
        GenerateProceduralFrame(0.016);
    }
    ComputeFrameHistograms();
    return true;
}

void CameraService::CloseCamera() {
#ifdef __ANDROID__
    CloseAndroidCamera();
#endif
    if (m_sdlCamera) {
        SDL_CloseCamera(m_sdlCamera);
        m_sdlCamera = nullptr;
    }
    m_streaming = false;
}

void CameraService::SetPaused(bool paused) {
    if (m_paused == paused) return;
    m_paused = paused;

#ifdef __ANDROID__
    if (m_paused) {
        CloseAndroidCamera();
    } else if (m_streaming && m_activeDeviceIndex < m_devices.size()) {
        const auto& dev = m_devices[m_activeDeviceIndex];
        if (!dev.isSynthetic) {
            OpenAndroidCamera(dev, dev.preferredWidth, dev.preferredHeight);
        }
    }
#endif
}

bool CameraService::SwitchCameraFacing() {
    if (m_devices.size() <= 1) return false;
    const size_t nextIdx = (m_activeDeviceIndex + 1) % m_devices.size();
    const auto& nextDev = m_devices[nextIdx];
    return OpenCamera(nextIdx, nextDev.preferredWidth, nextDev.preferredHeight);
}

const CameraDeviceInfo* CameraService::GetActiveDevice() const noexcept {
    if (m_activeDeviceIndex < m_devices.size()) {
        return &m_devices[m_activeDeviceIndex];
    }
    return nullptr;
}

void CameraService::UpdateFrame(double dtSeconds, SDL_Renderer* renderer) {
    if (!m_streaming || m_paused) return;

    bool gotHardwareFrame = false;

#ifdef __ANDROID__
    const auto* activeDev = GetActiveDevice();
    if (activeDev && !activeDev->isSynthetic && m_androidCam) {
        // Automatically reopen camera if cameraserver was busy at startup or disconnected
        if (!m_androidCam->session || m_androidCam->deviceError.load(std::memory_order_acquire)) {
            m_retryTimerSec += dtSeconds;
            if (m_retryTimerSec >= 0.45) {
                m_retryTimerSec = 0.0;
                OpenAndroidCamera(*activeDev, activeDev->preferredWidth, activeDev->preferredHeight);
            }
        }

        std::vector<uint8_t> rawRgb;
        int srcW = 0;
        int srcH = 0;
        {
            std::lock_guard<std::mutex> lock(m_androidCam->frameMutex);
            if (m_androidCam->stagingSeq != m_androidCam->consumedSeq &&
                m_androidCam->stagingW > 0 && m_androidCam->stagingH > 0 &&
                !m_androidCam->stagingRgb.empty()) {
                m_androidCam->consumedSeq = m_androidCam->stagingSeq;
                srcW = m_androidCam->stagingW;
                srcH = m_androidCam->stagingH;
                rawRgb = m_androidCam->stagingRgb;
            }
        }

        if (srcW > 0 && srcH > 0 && !rawRgb.empty()) {
            const int dispRot = GetDisplayRotationDegrees();
            const bool isFront = (activeDev->facing == CameraFacing::Front);
            const int rotCw = isFront
                ? ((activeDev->sensorOrientationDeg + dispRot) % 360)
                : ((activeDev->sensorOrientationDeg - dispRot + 360) % 360);

            const uint32_t outW = (rotCw == 90 || rotCw == 270)
                ? static_cast<uint32_t>(srcH)
                : static_cast<uint32_t>(srcW);
            const uint32_t outH = (rotCw == 90 || rotCw == 270)
                ? static_cast<uint32_t>(srcW)
                : static_cast<uint32_t>(srcH);

            if (outW != m_frameWidth || outH != m_frameHeight) {
                m_frameWidth = outW;
                m_frameHeight = outH;
                m_rgbPixels.resize(static_cast<size_t>(m_frameWidth) * m_frameHeight * 3);
                m_rgbaPixels.resize(static_cast<size_t>(m_frameWidth) * m_frameHeight);
                if (m_previewTexture) {
                    SDL_DestroyTexture(m_previewTexture);
                    m_previewTexture = nullptr;
                }
            }

            const float evGain = std::pow(2.0f, m_exposureEv);
            const bool applyEv = (std::abs(m_exposureEv) > 0.02f);
            const float invZoom = 1.0f / std::max(1.0f, m_zoomFactor);
            const float invOutW = 1.0f / static_cast<float>(outW);
            const float invOutH = 1.0f / static_cast<float>(outH);

            for (uint32_t dy = 0; dy < outH; ++dy) {
                const float vBase = ((static_cast<float>(dy) + 0.5f) * invOutH - 0.5f) * invZoom + 0.5f;
                uint8_t* dstRgb = m_rgbPixels.data() + static_cast<size_t>(dy) * outW * 3;
                uint32_t* dstRgba = m_rgbaPixels.data() + static_cast<size_t>(dy) * outW;

                for (uint32_t dx = 0; dx < outW; ++dx) {
                    float u = (static_cast<float>(dx) + 0.5f) * invOutW;
                    if (isFront) {
                        u = 1.0f - u;
                    }
                    u = (u - 0.5f) * invZoom + 0.5f;
                    const float v = vBase;

                    float su = u;
                    float sv = v;
                    if (rotCw == 90) {
                        su = v;
                        sv = 1.0f - u;
                    } else if (rotCw == 180) {
                        su = 1.0f - u;
                        sv = 1.0f - v;
                    } else if (rotCw == 270) {
                        su = 1.0f - v;
                        sv = u;
                    }

                    const int sx = std::clamp(static_cast<int>(su * static_cast<float>(srcW)), 0, srcW - 1);
                    const int sy = std::clamp(static_cast<int>(sv * static_cast<float>(srcH)), 0, srcH - 1);
                    const uint8_t* srcPx = rawRgb.data() + (static_cast<size_t>(sy) * srcW + sx) * 3;

                    uint8_t r = srcPx[0];
                    uint8_t g = srcPx[1];
                    uint8_t b = srcPx[2];
                    if (applyEv) {
                        r = static_cast<uint8_t>(std::clamp(r * evGain, 0.0f, 255.0f));
                        g = static_cast<uint8_t>(std::clamp(g * evGain, 0.0f, 255.0f));
                        b = static_cast<uint8_t>(std::clamp(b * evGain, 0.0f, 255.0f));
                    }

                    dstRgb[dx * 3 + 0] = r;
                    dstRgb[dx * 3 + 1] = g;
                    dstRgb[dx * 3 + 2] = b;
                    dstRgba[dx] = (0xFFu << 24) | (static_cast<uint32_t>(b) << 16) |
                                  (static_cast<uint32_t>(g) << 8) | static_cast<uint32_t>(r);
                }
            }
            gotHardwareFrame = true;
        }
    }
#else
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
#endif

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
    const auto* dev = GetActiveDevice();
    if (dev && !dev->isSynthetic && !dev->hasFlashUnit && enabled) {
        m_torchEnabled = false;
        m_flashMode = FlashMode::Off;
        return;
    }

    m_torchEnabled = enabled;
    if (enabled) {
        m_flashMode = FlashMode::Torch;
    } else if (m_flashMode == FlashMode::Torch) {
        m_flashMode = FlashMode::Off;
    }

#ifdef __ANDROID__
    if (m_androidCam && m_androidCam->session && m_androidCam->request) {
        uint8_t flashMode = m_torchEnabled ? ACAMERA_FLASH_MODE_TORCH : ACAMERA_FLASH_MODE_OFF;
        ACaptureRequest_setEntry_u8(m_androidCam->request, ACAMERA_FLASH_MODE, 1, &flashMode);
        uint8_t aeMode = ACAMERA_CONTROL_AE_MODE_ON;
        ACaptureRequest_setEntry_u8(m_androidCam->request, ACAMERA_CONTROL_AE_MODE, 1, &aeMode);
        ACameraCaptureSession_setRepeatingRequest(
            m_androidCam->session,
            nullptr,
            1,
            &m_androidCam->request,
            nullptr
        );
    }
#endif
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
