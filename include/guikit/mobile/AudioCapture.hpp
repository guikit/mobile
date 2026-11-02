#pragma once

#include <array>
#include <cstdint>
#include <mutex>
#include <span>
#include <vector>
#include "guikit/mobile/Types.hpp"

namespace guikit::mobile {

/// Real-time Microphone PCM Audio Capture and Spectral Analyzer.
/// Streams audio from the device microphone via SDL3 Audio (Android AAudio/OpenSLES, Windows WASAPI, etc.)
/// or generates a calibrated multi-tone + noise test signal when no physical microphone is available.
/// Directly feeds `guikit::SignalView`, `guikit::WaterfallView`, and `guikit::LevelMeter`.
class AudioCapture {
public:
    static constexpr uint32_t kDefaultSampleRate = 48000;
    static constexpr size_t kWaveformWindowSamples = 4096;
    static constexpr size_t kSpectrumBins = 512;

    AudioCapture();
    ~AudioCapture();

    AudioCapture(const AudioCapture&) = delete;
    AudioCapture& operator=(const AudioCapture&) = delete;

    bool Initialize(uint32_t sampleRate = kDefaultSampleRate);
    void Shutdown();

    bool SetCapturing(bool enabled);
    [[nodiscard]] bool IsCapturing() const noexcept { return m_capturing; }
    [[nodiscard]] bool IsUsingSyntheticSignal() const noexcept { return m_usingSynthetic; }
    void SetForceSyntheticSignal(bool forceSynth) noexcept { m_forceSynthetic = forceSynth; }

    /// Pulls latest captured PCM samples, updates RMS/Peak dBFS meters and 512-bin FFT spectrum slice.
    void UpdateFrame(double dtSeconds);

    [[nodiscard]] float GetSampleRate() const noexcept { return static_cast<float>(m_sampleRate); }

    /// Latest contiguous waveform window (`kWaveformWindowSamples` floats in [-1..+1]) for `ui.SignalView(...)`.
    [[nodiscard]] std::span<const float> GetWaveformSamples() const noexcept { return m_waveform; }

    /// Latest 512-bin power spectrum slice in dBFS [-110..0] for `ui.WaterfallView(...)`.
    [[nodiscard]] std::span<const float> GetSpectrumSliceDb() const noexcept { return m_spectrumDb; }

    /// Monotonically increasing slice counter for `WaterfallViewOptions::sliceSequence`.
    [[nodiscard]] uint64_t GetSpectrumSliceSequence() const noexcept { return m_sliceSequence; }

    /// Instantaneous RMS and Peak levels in dBFS [-60..0] for `ui.LevelMeter(...)`.
    [[nodiscard]] float GetRmsDbFs() const noexcept { return m_rmsDbFs; }
    [[nodiscard]] float GetPeakDbFs() const noexcept { return m_peakDbFs; }
    [[nodiscard]] std::span<const float> GetStereoMeterDb() const noexcept { return m_stereoMeterDb; }

    /// Dominant detected frequency peak in Hz and its level in dBFS.
    [[nodiscard]] float GetDominantFreqHz() const noexcept { return m_dominantFreqHz; }
    [[nodiscard]] float GetDominantPeakDb() const noexcept { return m_dominantPeakDb; }

    /// Synthetic test generator controls (used in Simulator or when testing DSP filters).
    void SetSynthFundamentalHz(float freqHz) noexcept { m_synthFreqHz = freqHz; }
    [[nodiscard]] float GetSynthFundamentalHz() const noexcept { return m_synthFreqHz; }
    void SetSynthNoiseFloorDb(float noiseDb) noexcept { m_synthNoiseDb = noiseDb; }
    [[nodiscard]] float GetSynthNoiseFloorDb() const noexcept { return m_synthNoiseDb; }

private:
    void ComputeMetricsAndSpectrum();

    void* m_audioStream{nullptr};
    uint32_t m_sampleRate{kDefaultSampleRate};
    std::vector<float> m_waveform;
    std::vector<float> m_spectrumDb;
    std::array<float, 2> m_stereoMeterDb{-60.0f, -60.0f};

    uint64_t m_sliceSequence{0};
    float m_rmsDbFs{-60.0f};
    float m_peakDbFs{-60.0f};
    float m_dominantFreqHz{440.0f};
    float m_dominantPeakDb{-60.0f};

    float m_synthFreqHz{1000.0f};
    float m_synthNoiseDb{-52.0f};
    double m_synthPhase{0.0};
    double m_simTimeSec{0.0};

    bool m_initialized{false};
    bool m_capturing{true};
    bool m_usingSynthetic{true};
    bool m_forceSynthetic{false};
};

} // namespace guikit::mobile
