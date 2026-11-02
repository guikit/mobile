#include "guikit/mobile/AudioCapture.hpp"
#include <SDL3/SDL.h>
#include <algorithm>
#include <cmath>
#include <complex>
#include <numbers>

namespace guikit::mobile {

namespace {

// In-place radix-2 Cooley-Tukey FFT for real-time 1024-point spectrum computation
void Radix2Fft(std::vector<std::complex<float>>& a) noexcept {
    const size_t n = a.size();
    if (n <= 1) return;

    for (size_t i = 1, j = 0; i < n; ++i) {
        size_t bit = n >> 1;
        for (; j & bit; bit >>= 1) {
            j ^= bit;
        }
        j ^= bit;
        if (i < j) {
            std::swap(a[i], a[j]);
        }
    }

    for (size_t len = 2; len <= n; len <<= 1) {
        const float angle = -2.0f * std::numbers::pi_v<float> / static_cast<float>(len);
        const std::complex<float> wlen(std::cos(angle), std::sin(angle));
        for (size_t i = 0; i < n; i += len) {
            std::complex<float> w(1.0f, 0.0f);
            for (size_t j = 0; j < len / 2; ++j) {
                const std::complex<float> u = a[i + j];
                const std::complex<float> v = a[i + j + len / 2] * w;
                a[i + j] = u + v;
                a[i + j + len / 2] = u - v;
                w *= wlen;
            }
        }
    }
}

} // namespace

AudioCapture::AudioCapture()
    : m_waveform(kWaveformWindowSamples, 0.0f)
    , m_spectrumDb(kSpectrumBins, -95.0f)
{
}

AudioCapture::~AudioCapture() {
    Shutdown();
}

bool AudioCapture::Initialize(uint32_t sampleRate) {
    if (m_initialized) return true;

    m_sampleRate = sampleRate > 0 ? sampleRate : kDefaultSampleRate;
    m_waveform.assign(kWaveformWindowSamples, 0.0f);
    m_spectrumDb.assign(kSpectrumBins, -95.0f);

    if (SDL_InitSubSystem(SDL_INIT_AUDIO)) {
        SDL_AudioSpec spec{};
        spec.format = SDL_AUDIO_F32;
        spec.channels = 1;
        spec.freq = static_cast<int>(m_sampleRate);

        m_audioStream = SDL_OpenAudioDeviceStream(
            SDL_AUDIO_DEVICE_DEFAULT_RECORDING,
            &spec,
            nullptr,
            nullptr
        );
        if (m_audioStream) {
            SDL_ResumeAudioStreamDevice(static_cast<SDL_AudioStream*>(m_audioStream));
            m_usingSynthetic = false;
        } else {
            m_usingSynthetic = true;
        }
    } else {
        m_usingSynthetic = true;
    }

    m_initialized = true;
    m_capturing = true;
    UpdateFrame(0.016);
    return true;
}

void AudioCapture::Shutdown() {
    if (m_audioStream) {
        SDL_DestroyAudioStream(static_cast<SDL_AudioStream*>(m_audioStream));
        m_audioStream = nullptr;
    }
    m_initialized = false;
    m_capturing = false;
}

bool AudioCapture::SetCapturing(bool enabled) {
    m_capturing = enabled;
    if (m_audioStream) {
        auto* stream = static_cast<SDL_AudioStream*>(m_audioStream);
        if (enabled) {
            SDL_ResumeAudioStreamDevice(stream);
        } else {
            SDL_PauseAudioStreamDevice(stream);
        }
    }
    return true;
}

void AudioCapture::UpdateFrame(double dtSeconds) {
    if (!m_capturing) return;

    m_simTimeSec += dtSeconds;
    bool gotHardwareAudio = false;

    if (m_audioStream && !m_forceSynthetic) {
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
                if (count >= m_waveform.size()) {
                    std::copy(temp.end() - m_waveform.size(), temp.end(), m_waveform.begin());
                } else {
                    std::move(m_waveform.begin() + count, m_waveform.end(), m_waveform.begin());
                    std::copy(temp.begin(), temp.begin() + count, m_waveform.end() - count);
                }
                m_usingSynthetic = false;
                gotHardwareAudio = true;
            }
        }
    }

    if (!gotHardwareAudio && (m_usingSynthetic || m_forceSynthetic)) {
        m_usingSynthetic = true;
        const size_t newSamples = std::clamp<size_t>(
            static_cast<size_t>(std::round(dtSeconds * m_sampleRate)),
            128,
            1024
        );
        std::move(m_waveform.begin() + newSamples, m_waveform.end(), m_waveform.begin());

        const double twoPi = 2.0 * std::numbers::pi;
        const double f0 = std::clamp(static_cast<double>(m_synthFreqHz), 60.0, 18000.0);
        const double phaseStep = twoPi * f0 / static_cast<double>(m_sampleRate);
        const float noiseAmp = std::pow(10.0f, m_synthNoiseDb / 20.0f);
        const float amMod = 0.65f + 0.25f * std::sin(static_cast<float>(m_simTimeSec * 2.2));

        uint32_t rng = static_cast<uint32_t>(m_sliceSequence * 1664525u + 1013904223u);
        for (size_t i = m_waveform.size() - newSamples; i < m_waveform.size(); ++i) {
            rng = rng * 1664525u + 1013904223u;
            const float white = (static_cast<float>(rng & 0xFFFFu) / 32768.0f) - 1.0f;

            // Fundamental + 2nd & 3rd harmonics + FM chirp beacon
            const float s1 = 0.48f * amMod * static_cast<float>(std::sin(m_synthPhase));
            const float s2 = 0.14f * static_cast<float>(std::sin(m_synthPhase * 2.0));
            const float s3 = 0.06f * static_cast<float>(std::sin(m_synthPhase * 3.0));
            m_waveform[i] = std::clamp(s1 + s2 + s3 + white * noiseAmp, -1.0f, 1.0f);

            m_synthPhase += phaseStep;
            if (m_synthPhase > twoPi) m_synthPhase -= twoPi;
        }
    }

    ComputeMetricsAndSpectrum();
}

void AudioCapture::ComputeMetricsAndSpectrum() {
    constexpr size_t kFftSize = kSpectrumBins * 2; // 1024-point FFT -> 512 positive bins
    if (m_waveform.size() < kFftSize) return;

    const float* windowStart = m_waveform.data() + (m_waveform.size() - kFftSize);

    double sumSq = 0.0;
    float peak = 0.0f;
    std::vector<std::complex<float>> fftBuf(kFftSize);

    for (size_t i = 0; i < kFftSize; ++i) {
        const float s = windowStart[i];
        sumSq += static_cast<double>(s) * s;
        peak = std::max(peak, std::abs(s));

        // Hann window
        const float w = 0.5f * (1.0f - std::cos(2.0f * std::numbers::pi_v<float> * static_cast<float>(i) / static_cast<float>(kFftSize - 1)));
        fftBuf[i] = std::complex<float>(s * w, 0.0f);
    }

    const float rms = static_cast<float>(std::sqrt(sumSq / static_cast<double>(kFftSize)));
    m_rmsDbFs = std::clamp(20.0f * std::log10(std::max(rms, 1e-5f)), -60.0f, 0.0f);
    m_peakDbFs = std::clamp(20.0f * std::log10(std::max(peak, 1e-5f)), -60.0f, 0.0f);
    m_stereoMeterDb[0] = m_rmsDbFs;
    m_stereoMeterDb[1] = std::clamp(m_rmsDbFs - 1.2f, -60.0f, 0.0f);

    Radix2Fft(fftBuf);

    const float invNorm = 2.0f / static_cast<float>(kFftSize);
    float maxBinDb = -120.0f;
    size_t maxBinIdx = 1;

    for (size_t k = 0; k < kSpectrumBins; ++k) {
        const float mag = std::abs(fftBuf[k]) * invNorm;
        const float db = std::clamp(20.0f * std::log10(std::max(mag, 1e-6f)), -110.0f, 0.0f);
        m_spectrumDb[k] = db;
        if (k > 1 && db > maxBinDb) {
            maxBinDb = db;
            maxBinIdx = k;
        }
    }

    m_dominantPeakDb = maxBinDb;
    m_dominantFreqHz = static_cast<float>(maxBinIdx) * (static_cast<float>(m_sampleRate) * 0.5f) / static_cast<float>(kSpectrumBins);
    ++m_sliceSequence;
}

} // namespace guikit::mobile
