#ifdef __ANDROID__
#include <SDL3/SDL_main.h>
#endif
#include <algorithm>
#include <array>
#include <cmath>
#include <string>
#include <vector>
#include "guikit/mobile.hpp"
#include "guikit/geo/geo_json_io.hpp"
#include "guikit/geo/gis_render_engine.hpp"

namespace {

using namespace guikit;
using namespace guikit::mobile;

enum class MobileTab : size_t {
    Horizon       = 0,
    Sensors       = 1,
    CameraVision  = 2,
    AudioSpectrum = 3,
    GnssAndSystem = 4
};

struct MobileAppState {
    size_t activeTab{0};
    bool compactAdiMode{true};
    bool fullBleedHorizon{true};
    int imuSignalAxis{3}; // 0=AccX, 1=AccY, 2=AccZ, 3=|G|, 4=Gyro, 5=Mag, 6=Baro
    ImageViewParams cameraViewParams{};
    GisRenderConfig gisConfig{};
    std::vector<GeoLayer> chartLayers;
    GeoCoord chartCenter{44.6020, 33.4950};
    bool chartLoaded{false};
    bool followGpsOnMap{false};
    std::string lastPermissionToast{"Ready"};

    void EnsureChartLoaded() {
        if (chartLoaded) return;
        chartLoaded = true;
        auto res = GeoJsonIO::LoadFromFile("assets/geo/demo_bay.geojson", true);
        if (res.ok && !res.layers.empty()) {
            chartLayers = std::move(res.layers);
            if (res.totalBounds.IsValid()) {
                chartCenter = res.totalBounds.Center();
            }
        }
    }
};

void RenderHorizonTab(Context& ui, MobileRuntime& runtime, MobileAppState& /*app*/) {
    const auto& snap = runtime.Sensors().GetSnapshot();

    if (auto col = ui.Column({
        .padding = Padding::All(6.0f),
        .widthMode = SizeMode::Stretch,
        .heightMode = SizeMode::Stretch
    })) {
        ui.AttitudeView({
            .displayMode = AttitudeDisplayMode::FullPFD,
            .viewportMode = AttitudeViewportMode::FullBleed,
            .colorScheme = AttitudeColorScheme::GlassCockpit,
            .flightDirectorStyle = FlightDirectorStyle::Off,
            .rollDeg = snap.attitude.rollDeg,
            .pitchDeg = snap.attitude.pitchDeg,
            .headingDeg = snap.attitude.headingDeg,
            .slipSkid = snap.slipSkid,
            .gLoad = snap.gLoad,
            .radioAltitudeFeet = 99999.0f,
            .showFlightPathVector = false,
            .showIlsDiamonds = false,
            .showPitchLadder = true,
            .showBankScale = true,
            .showSpeedAndAltTapes = false,
            .showHeadingTape = true,
            .showFmaBar = false,
            .showMouseStickOverlay = false,
            .heightMode = SizeMode::Stretch
        });
    }
}

void RenderSensorsTab(Context& ui, MobileRuntime& runtime, MobileAppState& app) {
    const auto& snap = runtime.Sensors().GetSnapshot();

    if (auto col = ui.Column({
        .gap = 8.0f,
        .padding = Padding::All(8.0f),
        .widthMode = SizeMode::Stretch,
        .heightMode = SizeMode::Stretch
    })) {
        // 1. Live Sensor Oscilloscope & STFT Card (stretches to fill available height)
        if (auto card = ui.Card({
            .gap = 8.0f,
            .padding = Padding::All(10.0f),
            .widthMode = SizeMode::Stretch,
            .heightMode = SizeMode::Stretch
        })) {
            if (auto hdr = ui.Row({.gap = 6.0f, .align = Alignment::Center, .widthMode = SizeMode::Stretch})) {
                ui.Size(UiSize::Sm).Heading({
                    .label = "MEMS Sensor Waveforms & Vibration STFT",
                    .icon = Icon::Flash()
                });
                ui.Spacer();
                ui.Text(ui.Format("{:.0f} Hz", runtime.Sensors().GetEstimatedSampleRateHz()), {
                    .role = TextRole::CodeSmall,
                    .variant = UiVariant::Info
                });
            }

            static constexpr std::array<std::string_view, 7> kChannelLabels = {
                "Acc X", "Acc Y", "Acc Z", "|G|", "Gyro", "Mag", "Baro"
            };
            ui.Size(UiSize::Xs).Segmented(app.imuSignalAxis, kChannelLabels, {
                .stretch = StretchMode::Fill
            });

            std::span<const float> trace = runtime.Sensors().GetAccelMagnitudeHistory();
            if (app.imuSignalAxis == 0)      trace = runtime.Sensors().GetAccelHistoryX();
            else if (app.imuSignalAxis == 1) trace = runtime.Sensors().GetAccelHistoryY();
            else if (app.imuSignalAxis == 2) trace = runtime.Sensors().GetAccelHistoryZ();
            else if (app.imuSignalAxis == 4) trace = runtime.Sensors().GetGyroMagnitudeHistory();
            else if (app.imuSignalAxis == 5) trace = runtime.Sensors().GetMagMagnitudeHistory();
            else if (app.imuSignalAxis == 6) trace = runtime.Sensors().GetBaroAltitudeHistory();

            ui.SignalView(trace, {
                .sampleRate = runtime.Sensors().GetEstimatedSampleRateHz(),
                .amplitudeScale = AmplitudeScale::Linear,
                .colormap = Colormap::Plasma,
                .fftSize = 256,
                .showOverviewBar = false,
                .heightMode = SizeMode::Stretch
            });
        }

        // 2. Compact 9-DOF & Environmental Digital Telemetry Grid
        auto metricCell = [&](std::string_view title, std::string_view val, UiVariant var) {
            if (auto b = ui.Card({.gap = 2.0f, .padding = Padding::Symmetric(8.0f, 6.0f), .widthMode = SizeMode::Stretch})) {
                ui.Text(title, {.role = TextRole::LabelSmall, .variant = UiVariant::Muted});
                ui.Text(val, {.role = TextRole::CodeSmall, .variant = var});
            }
        };

        if (auto row1 = ui.Row({.gap = 6.0f, .widthMode = SizeMode::Stretch})) {
            metricCell("ACCEL (m/s²)",
                       ui.Format("{:+.1f} {:+.1f} {:+.1f}", snap.accelerometer.x, snap.accelerometer.y, snap.accelerometer.z),
                       UiVariant::Info);
            metricCell("GYRO (rad/s)",
                       ui.Format("{:+.2f} {:+.2f} {:+.2f}", snap.gyroscope.x, snap.gyroscope.y, snap.gyroscope.z),
                       UiVariant::Primary);
        }
        if (auto row2 = ui.Row({.gap = 6.0f, .widthMode = SizeMode::Stretch})) {
            metricCell("MAG (uT)",
                       ui.Format("{:+.0f} {:+.0f} {:+.0f}", snap.magnetometer.x, snap.magnetometer.y, snap.magnetometer.z),
                       UiVariant::Warning);
            metricCell("ENV & STEPS",
                       ui.Format("{:.0f}lx | {} | {} stp",
                                 snap.ambientLightLux,
                                 snap.proximityNear ? "NEAR" : "FAR",
                                 snap.stepCount),
                       UiVariant::Success);
        }
    }
}

void RenderCameraVisionTab(Context& ui, MobileRuntime& runtime, MobileAppState& app) {
    auto& cam = runtime.Camera();
    const auto& stats = cam.GetFrameStats();
    const auto* activeDev = cam.GetActiveDevice();

    if (auto col = ui.Column({
        .gap = 8.0f,
        .padding = Padding::All(8.0f),
        .widthMode = SizeMode::Stretch,
        .heightMode = SizeMode::Stretch
    })) {
        // 1. Compact Camera Toolbar & Hardware Controls
        if (auto card = ui.Card({
            .gap = 6.0f,
            .padding = Padding::All(10.0f),
            .widthMode = SizeMode::Stretch
        })) {
            if (auto row = ui.Row({.gap = 6.0f, .align = Alignment::Center, .widthMode = SizeMode::Stretch})) {
                ui.Size(UiSize::Sm).Heading({
                    .label = activeDev ? activeDev->name : "Camera",
                    .description = ui.Format("{}x{} @ {:.0f} FPS | #{}",
                                             stats.width, stats.height, stats.fps,
                                             cam.GetCapturedPhotoCount()),
                    .icon = Icon::Eye()
                });

                if (ui.Size(UiSize::Xs).Button({
                    .text = "Switch",
                    .icon = Icon::Refresh()
                }).clicked) {
                    cam.SwitchCameraFacing();
                    runtime.System().TriggerHaptic(HapticStyle::Medium);
                }

                const bool hasFlash = !activeDev || activeDev->isSynthetic || activeDev->hasFlashUnit;
                bool torch = cam.IsTorchEnabled();
                if (ui.Size(UiSize::Xs).Button({
                    .text = torch ? "On" : "Torch",
                    .icon = Icon::Flash(),
                    .color = torch ? StateColor::Warning() : StateColor{},
                    .disabled = !hasFlash
                }).clicked) {
                    cam.SetTorchEnabled(!torch);
                    runtime.System().TriggerHaptic(HapticStyle::Light);
                }

                if (ui.Size(UiSize::Xs).Button({
                    .text = "Shot",
                    .icon = Icon::Save(),
                    .color = StateColor::Primary()
                }).clicked) {
                    cam.CaptureSnapshot();
                    runtime.System().TriggerHaptic(HapticStyle::Success);
                }
            }

            if (auto sliders = ui.Row({.gap = 10.0f, .align = Alignment::Center, .widthMode = SizeMode::Stretch})) {
                float ev = cam.GetExposureCompensationEv();
                if (auto el = ui.Size(UiSize::Xs).Element({.label = "EV", .stretch = StretchMode::Fill})) {
                    if (ui.Size(UiSize::Xs).Slider(ev, -2.0f, 2.0f, {.valueFormat = "%+.1f"}).changed) {
                        cam.SetExposureCompensationEv(ev);
                    }
                }

                float zoom = cam.GetZoomFactor();
                if (auto el = ui.Size(UiSize::Xs).Element({.label = "Zoom", .stretch = StretchMode::Fill})) {
                    if (ui.Size(UiSize::Xs).Slider(zoom, 1.0f, 5.0f, {.valueFormat = "%.1fx"}).changed) {
                        cam.SetZoomFactor(zoom);
                    }
                }
            }
        }

        // 2. Live ImageView Workstation (stretches to fill remaining screen without scrolling)
        const ImageViewData imgData = cam.GetImageViewData();
        if (!imgData.Empty()) {
            ui.ImageView(imgData, app.cameraViewParams, {
                .height = 180.0f,
                .minHeight = 180.0f,
                .heightMode = SizeMode::Stretch,
                .showSidebar = false,
                .showTopToolbar = true,
                .toolbarSize = UiSize::Xs,
                .showStatusBar = true
            });
        }
    }
}

void RenderAudioSpectrumTab(Context& ui, MobileRuntime& runtime, MobileAppState&) {
    auto& audio = runtime.Audio();

    if (auto col = ui.Column({
        .gap = 8.0f,
        .padding = Padding::All(8.0f),
        .widthMode = SizeMode::Stretch,
        .heightMode = SizeMode::Stretch
    })) {
        // 1. Microphone Input & Stereo Level Meter
        if (auto card = ui.Card({
            .gap = 6.0f,
            .padding = Padding::All(10.0f),
            .widthMode = SizeMode::Stretch
        })) {
            if (auto row = ui.Row({.gap = 6.0f, .align = Alignment::Center, .widthMode = SizeMode::Stretch})) {
                ui.Size(UiSize::Sm).Heading({
                    .label = audio.IsUsingSyntheticSignal() ? "Mic Analyzer (Synth)" : "Mic Analyzer (Live PCM)",
                    .description = ui.Format("Peak: {:.1f} dB | RMS: {:.1f} dB | Tone: {:.0f} Hz",
                                             audio.GetPeakDbFs(), audio.GetRmsDbFs(),
                                             audio.GetDominantFreqHz()),
                    .icon = Icon::Play()
                });
                ui.Spacer();

                bool capturing = audio.IsCapturing();
                if (ui.Size(UiSize::Xs).Button({
                    .text = capturing ? "Pause" : "Resume",
                    .icon = capturing ? Icon::Pause() : Icon::Play(),
                    .color = capturing ? StateColor::Primary() : StateColor{}
                }).clicked) {
                    audio.SetCapturing(!capturing);
                    runtime.System().TriggerHaptic(HapticStyle::Selection);
                }
            }

            static constexpr std::array<std::string_view, 2> kChLabels = {"L", "R"};
            ui.LevelMeter(audio.GetStereoMeterDb(), -60.0f, 0.0f, {
                .segments = 24,
                .channelLabels = kChLabels
            });

            if (audio.IsUsingSyntheticSignal()) {
                float f0 = audio.GetSynthFundamentalHz();
                if (auto el = ui.Size(UiSize::Xs).Element({.label = "Tone (Hz)"})) {
                    if (ui.Size(UiSize::Xs).Slider(f0, 120.0f, 8000.0f, {.valueFormat = "%.0f Hz"}).changed) {
                        audio.SetSynthFundamentalHz(f0);
                    }
                }
            }
        }

        // 2. Real-Time Spectrum Waterfall (stretches to fill remaining height without scrolling)
        if (auto card = ui.Card({
            .gap = 6.0f,
            .padding = Padding::All(8.0f),
            .widthMode = SizeMode::Stretch,
            .heightMode = SizeMode::Stretch
        })) {
            ui.WaterfallView(audio.GetSpectrumSliceDb(), {
                .mode = WaterfallMode::SpectrumRtsa,
                .viewLayout = WaterfallLayout::SplitTraceAndWaterfall,
                .colormap = Colormap::Turbo,
                .numBins = static_cast<uint32_t>(AudioCapture::kSpectrumBins),
                .historyRows = 180,
                .historyDurationSec = 10.0f,
                .minX = 0.0f,
                .maxX = audio.GetSampleRate() * 0.5f,
                .sliceSequence = audio.GetSpectrumSliceSequence(),
                .dbFloor = -95.0f,
                .dbCeil = 0.0f,
                .heightMode = SizeMode::Stretch
            });
        }
    }
}

void RenderGnssAndSystemTab(Context& ui, MobileRuntime& runtime, MobileAppState& app) {
    app.EnsureChartLoaded();
    const auto& fix = runtime.Location().GetLatestFix();

    if (auto col = ui.Column({
        .gap = 8.0f,
        .padding = Padding::All(8.0f),
        .widthMode = SizeMode::Stretch,
        .heightMode = SizeMode::Stretch
    })) {
        // 1. Live GNSS MapView & Track (stretches to fill available screen height)
        if (auto card = ui.Card({
            .gap = 6.0f,
            .padding = Padding::All(10.0f),
            .widthMode = SizeMode::Stretch,
            .heightMode = SizeMode::Stretch
        })) {
            if (auto row = ui.Row({.gap = 6.0f, .align = Alignment::Center, .widthMode = SizeMode::Stretch})) {
                ui.Size(UiSize::Sm).Heading({
                    .label = "GNSS & Marine Chart",
                    .description = ui.Format("{:.4f}, {:.4f} | {:.1f} kn | {:03.0f}°",
                                             fix.coord.lat, fix.coord.lon,
                                             fix.SpeedKnots(), fix.courseDeg),
                    .icon = Icon::Search()
                });
                ui.Spacer();
                ui.Text("Follow", {.role = TextRole::LabelSmall, .variant = UiVariant::Muted});
                ui.Size(UiSize::Xs).Checkbox(app.followGpsOnMap);
                if (ui.Size(UiSize::Xs).Button({.text = "Clear", .icon = Icon::Trash()}).clicked) {
                    runtime.Location().ClearBreadcrumbTrack();
                    runtime.System().TriggerHaptic(HapticStyle::Light);
                }
            }

            std::optional<GeoCoord> reqCenter = app.followGpsOnMap
                ? std::optional<GeoCoord>(fix.coord)
                : std::nullopt;

            if (auto map = ui.MapView({
                .initialCenter = app.chartCenter,
                .initialTileZoom = 13.3f,
                .requestCenterGeo = reqCenter,
                .showTileGrid = false,
                .showNauticalGraticule = true,
                .showScaleBarOverlay = true,
                .heightMode = SizeMode::Stretch,
                .minHeight = 160.0f
            })) {
                if (!app.chartLayers.empty()) {
                    uint64_t selUid = 0;
                    GisRenderEngine::RenderLayers(map, app.chartLayers, app.gisConfig, selUid, false);
                }

                // Draw recorded GNSS breadcrumb trail and live vessel/device beacon
                const auto track = runtime.Location().GetBreadcrumbTrack();
                if (track.size() >= 2) {
                    map.DrawGeoPolyline(track, Color(56, 189, 248, 230), 2.5f, false);
                }
                map.DrawGeoCircle(fix.coord, 7.0f, Color(16, 185, 129, 255), Color(255, 255, 255, 255), 2.0f);
                map.DrawGeoText("GNSS POS", fix.coord, {10.0f, -10.0f}, 11.0f, Color(52, 211, 153, 255), FontStyle::Bold);
            }
        }

        // 2. Compact Permissions & Haptics Bar
        if (auto sysCard = ui.Card({
            .gap = 6.0f,
            .padding = Padding::All(10.0f),
            .widthMode = SizeMode::Stretch
        })) {
            if (auto permRow = ui.Wrap({.gap = 6.0f})) {
                auto permBtn = [&](Permission p, std::string_view label) {
                    const auto st = runtime.System().GetPermissionStatus(p);
                    if (ui.Size(UiSize::Xs).Button({
                        .text = label,
                        .icon = (st == PermissionStatus::Granted) ? Icon::Check() : Icon::Warning(),
                        .color = (st == PermissionStatus::Granted) ? StateColor::Success() : StateColor::Primary()
                    }).clicked) {
                        runtime.System().RequestPermission(p, [&](Permission reqP, PermissionStatus res) {
                            app.lastPermissionToast = std::string(SystemBridge::GetPermissionDisplayName(reqP)) +
                                (res == PermissionStatus::Granted ? " -> GRANTED" : " -> DENIED");
                        });
                        runtime.System().TriggerHaptic(HapticStyle::Selection);
                    }
                };
                permBtn(Permission::Camera, "Camera");
                permBtn(Permission::RecordAudio, "Mic");
                permBtn(Permission::FineLocation, "GPS");
                permBtn(Permission::BodySensors, "Sensors");
            }

            if (auto hapRow = ui.Row({.gap = 6.0f, .align = Alignment::Center, .widthMode = SizeMode::Stretch})) {
                ui.Text("Haptic:", {.role = TextRole::LabelSmall, .variant = UiVariant::Muted});
                if (ui.Size(UiSize::Xs).Button({.text = "Light"}).clicked) {
                    runtime.System().TriggerHaptic(HapticStyle::Light);
                }
                if (ui.Size(UiSize::Xs).Button({.text = "Med"}).clicked) {
                    runtime.System().TriggerHaptic(HapticStyle::Medium);
                }
                if (ui.Size(UiSize::Xs).Button({.text = "Heavy"}).clicked) {
                    runtime.System().TriggerHaptic(HapticStyle::Heavy);
                }
                ui.Spacer();
                ui.Text(ui.Format("{} | #{}", app.lastPermissionToast, runtime.System().GetHapticPulseCount()), {
                    .role = TextRole::CodeSmall,
                    .variant = UiVariant::Info
                });
            }
        }
    }
}

class MobileDemoWindow final : public MobileWindow {
public:
    MobileDemoWindow()
        : MobileWindow({
            .title = "GuiKit Mobile — Android & iOS Sensor / Camera / GNSS Workstation",
            .width = 1180,
            .height = 820,
            .resizable = true,
            .highDpi = true,
            .vsync = true,
            .reactiveMode = true,
            .applySafeAreaPadding = true
        }) {}

protected:
    void OnRender(Context& ui, MobileRuntime& mobile) override {
        RenderStatusBar(ui);

        if (auto body = ui.Row({
            .gap = 0.0f,
            .widthMode = SizeMode::Stretch,
            .heightMode = SizeMode::Stretch
        })) {
            if (auto stage = ui.Column({
                .widthMode = SizeMode::Stretch,
                .heightMode = SizeMode::Stretch
            })) {
                switch (static_cast<MobileTab>(m_app.activeTab)) {
                    case MobileTab::Horizon:
                        RenderHorizonTab(ui, mobile, m_app);
                        break;
                    case MobileTab::Sensors:
                        RenderSensorsTab(ui, mobile, m_app);
                        break;
                    case MobileTab::CameraVision:
                        RenderCameraVisionTab(ui, mobile, m_app);
                        break;
                    case MobileTab::AudioSpectrum:
                        RenderAudioSpectrumTab(ui, mobile, m_app);
                        break;
                    case MobileTab::GnssAndSystem:
                        RenderGnssAndSystemTab(ui, mobile, m_app);
                        break;
                }
            }

            RenderSimulatorDrawer(ui);
        }

        static constexpr std::array<std::string_view, 5> kTabs = {
            "Horizon",
            "Sensors",
            "Camera",
            "Audio",
            "GNSS"
        };
        RenderBottomNavigationBar(ui, m_app.activeTab, kTabs);
    }

private:
    MobileAppState m_app{};
};

} // namespace

int main(int /*argc*/, char* /*argv*/[]) {
    MobileDemoWindow window;
    return window.Run();
}
