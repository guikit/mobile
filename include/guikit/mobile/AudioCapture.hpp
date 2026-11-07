#pragma once

#include <cstdint>
#include <span>
#include "guikit/mobile/Types.hpp"
#include "keel/mobile/audio.h"

namespace guikit::mobile {

/// Real-time Microphone PCM Audio Capture and Spectral Analyzer backed by `keel::AudioCapture` (`keelcpp/mobile`).
/// Streams audio from the device microphone via SDL3 Audio (Android AAudio/OpenSLES, Windows WASAPI, etc.)
/// or generates a calibrated multi-tone + noise test signal inside `keel::AudioCapture`.
/// Directly feeds `guikit::SignalView`, `guikit::WaterfallView`, and `guikit::LevelMeter`.
class AudioCapture {
public:
    static constexpr uint32_t kDefaultSampleRate = keel::AudioCapture::kDefaultSampleRate;
    static constexpr size_t kWaveformWindowSamples = keel::AudioCapture::kWaveformWindowSamples;
    static constexpr size_t kSpectrumBins = keel::AudioCapture::kSpectrumBins;

    AudioCapture();
    ~AudioCapture();

    AudioCapture(const AudioCapture&) = delete;
    AudioCapture& operator=(const AudioCapture&) = delete;

    bool Initialize(uint32_t sampleRate = kDefaultSampleRate);
    void Shutdown();

    bool SetCapturing(bool enabled);
    [[nodiscard]] bool IsCapturing() const noexcept { return m_hal.is_capturing(); }
    [[nodiscard]] bool IsUsingSyntheticSignal() const noexcept { return m_hal.is_using_synthetic_signal(); }
    void SetForceSyntheticSignal(bool forceSynth) noexcept { m_hal.set_force_synthetic_signal(forceSynth); }

    /// Pulls latest captured PCM samples, updates RMS/Peak dBFS meters and 512-bin FFT spectrum slice.
    void UpdateFrame(double dtSeconds);

    [[nodiscard]] float GetSampleRate() const noexcept { return m_hal.sample_rate(); }

    /// Latest contiguous waveform window (`kWaveformWindowSamples` floats in [-1..+1]) for `ui.SignalView(...)`.
    [[nodiscard]] std::span<const float> GetWaveformSamples() const noexcept {
        const auto s = m_hal.waveform_samples();
        return {s.data(), s.size()};
    }

    /// Latest 512-bin power spectrum slice in dBFS [-110..0] for `ui.WaterfallView(...)`.
    [[nodiscard]] std::span<const float> GetSpectrumSliceDb() const noexcept {
        const auto s = m_hal.spectrum_slice_db();
        return {s.data(), s.size()};
    }

    /// Monotonically increasing slice counter for `WaterfallViewOptions::sliceSequence`.
    [[nodiscard]] uint64_t GetSpectrumSliceSequence() const noexcept { return m_hal.spectrum_slice_sequence(); }

    /// Instantaneous RMS and Peak levels in dBFS [-60..0] for `ui.LevelMeter(...)`.
    [[nodiscard]] float GetRmsDbFs() const noexcept { return m_hal.rms_dbfs(); }
    [[nodiscard]] float GetPeakDbFs() const noexcept { return m_hal.peak_dbfs(); }
    [[nodiscard]] std::span<const float> GetStereoMeterDb() const noexcept {
        const auto s = m_hal.stereo_meter_db();
        return {s.data(), s.size()};
    }

    /// Dominant detected frequency peak in Hz and its level in dBFS.
    [[nodiscard]] float GetDominantFreqHz() const noexcept { return m_hal.dominant_freq_hz(); }
    [[nodiscard]] float GetDominantPeakDb() const noexcept { return m_hal.dominant_peak_db(); }

    /// Synthetic test generator controls (used in Simulator or when testing DSP filters).
    void SetSynthFundamentalHz(float freqHz) noexcept { m_hal.set_synth_fundamental_hz(freqHz); }
    [[nodiscard]] float GetSynthFundamentalHz() const noexcept { return m_hal.synth_fundamental_hz(); }
    void SetSynthNoiseFloorDb(float noiseDb) noexcept { m_hal.set_synth_noise_floor_db(noiseDb); }
    [[nodiscard]] float GetSynthNoiseFloorDb() const noexcept { return m_hal.synth_noise_floor_db(); }

    [[nodiscard]] keel::AudioCapture& Hal() noexcept { return m_hal; }
    [[nodiscard]] const keel::AudioCapture& Hal() const noexcept { return m_hal; }

private:
    keel::AudioCapture m_hal{};
    void* m_audioStream{nullptr};
};

} // namespace guikit::mobile
