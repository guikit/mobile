#include "guikit/mobile/AudioCapture.hpp"
#include <SDL3/SDL.h>
#include <algorithm>
#include <vector>

namespace guikit::mobile {

AudioCapture::AudioCapture() = default;

AudioCapture::~AudioCapture() {
    Shutdown();
}

bool AudioCapture::Initialize(uint32_t sampleRate) {
    const uint32_t rate = sampleRate > 0 ? sampleRate : kDefaultSampleRate;

    if (SDL_InitSubSystem(SDL_INIT_AUDIO)) {
        SDL_AudioSpec spec{};
        spec.format = SDL_AUDIO_F32;
        spec.channels = 1;
        spec.freq = static_cast<int>(rate);

        m_audioStream = SDL_OpenAudioDeviceStream(
            SDL_AUDIO_DEVICE_DEFAULT_RECORDING,
            &spec,
            nullptr,
            nullptr
        );
        if (m_audioStream) {
            SDL_ResumeAudioStreamDevice(static_cast<SDL_AudioStream*>(m_audioStream));
        }
    }

    return m_hal.initialize(rate);
}

void AudioCapture::Shutdown() {
    if (m_audioStream) {
        SDL_DestroyAudioStream(static_cast<SDL_AudioStream*>(m_audioStream));
        m_audioStream = nullptr;
    }
    m_hal.shutdown();
}

bool AudioCapture::SetCapturing(bool enabled) {
    if (m_audioStream) {
        auto* stream = static_cast<SDL_AudioStream*>(m_audioStream);
        if (enabled) {
            SDL_ResumeAudioStreamDevice(stream);
        } else {
            SDL_PauseAudioStreamDevice(stream);
        }
    }
    return m_hal.set_capturing(enabled);
}

void AudioCapture::UpdateFrame(double dtSeconds) {
    if (!m_hal.is_capturing()) return;

    if (m_audioStream && !m_hal.is_using_synthetic_signal()) {
        auto* stream = static_cast<SDL_AudioStream*>(m_audioStream);
        const int availBytes = SDL_GetAudioStreamAvailable(stream);
        if (availBytes > 0) {
            const size_t availFloats = static_cast<size_t>(availBytes) / sizeof(float);
            std::vector<float> temp(std::min<size_t>(availFloats, kWaveformWindowSamples));
            const int readBytes = SDL_GetAudioStreamData(
                stream,
                temp.data(),
                static_cast<int>(temp.size() * sizeof(float))
            );
            if (readBytes > 0) {
                const size_t count = static_cast<size_t>(readBytes) / sizeof(float);
                m_hal.push_hardware_samples(keel::ArrayRef<float>(temp.data(), count));
            }
        }
    }

    m_hal.update_frame(dtSeconds);
}

} // namespace guikit::mobile
