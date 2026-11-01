#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <string>
#include <vector>
#include "guikit/Mobile.hpp"
#include "guikit/backend/ThorCanvas.hpp"
#include "guikit/geo/GeoJsonIO.hpp"
#include "guikit/geo/GisRenderEngine.hpp"

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
    bool showSimulatorDrawer{true};
    bool compactAdiMode{true};
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

void RenderStatusBar(Context& ui, MobileRuntime& runtime, MobileAppState& app) {
    const auto& dev = runtime.System().GetDeviceInfo();
    const auto& bat = runtime.System().GetBatteryStatus();
    const auto& fix = runtime.Location().GetLatestFix();
    const auto& snap = runtime.Sensors().GetSnapshot();

    if (auto bar = ui.Box({
        .widthMode = SizeMode::Stretch,
        .heightMode = SizeMode::Fit,
        .padding = Padding::Symmetric(10.0f, 5.0f),
        .background = {.normal = Color(13, 18, 28, 255)},
        .border = {.width = 1.0f, .radius = 0.0f}
    })) {
        if (auto row = ui.Row({.gap = 8.0f, .align = Alignment::Center, .widthMode = SizeMode::Stretch})) {
            ui.Text(ui.Format("{}", dev.model), {
                .role = TextRole::LabelSmall,
                .variant = UiVariant::Info
            });

            ui.Spacer();

            ui.Text(ui.Format("{} sat | {:.0f} lx",
                              fix.satellitesUsed,
                              snap.ambientLightLux), {
                .role = TextRole::CodeSmall,
                .variant = UiVariant::Muted
            });

            ui.Text(ui.Format("{}{:.0f}%", bat.isCharging ? "CHG " : "BAT ", bat.levelPct), {
                .role = TextRole::CodeSmall,
                .variant = bat.levelPct > 25.0f ? UiVariant::Success : UiVariant::Warning
            });

            if (ui.Size(UiSize::Xs).Button({
                .text = app.showSimulatorDrawer ? "Hide Sim" : "Sim",
                .icon = Icon::Settings(),
                .color = app.showSimulatorDrawer ? StateColor::Primary() : StateColor{}
            }).clicked) {
                app.showSimulatorDrawer = !app.showSimulatorDrawer;
                runtime.System().TriggerHaptic(HapticStyle::Selection);
            }
        }
    }
}

void RenderHorizonTab(Context& ui, MobileRuntime& runtime, MobileAppState& app) {
    const auto& snap = runtime.Sensors().GetSnapshot();

    if (auto col = ui.Column({
        .gap = 8.0f,
        .padding = Padding::All(8.0f),
        .widthMode = SizeMode::Stretch,
        .heightMode = SizeMode::Stretch
    })) {
        if (auto card = ui.Card({
            .gap = 8.0f,
            .padding = Padding::All(10.0f),
            .widthMode = SizeMode::Stretch,
            .heightMode = SizeMode::Stretch
        })) {
            if (auto hdr = ui.Row({.gap = 6.0f, .align = Alignment::Center, .widthMode = SizeMode::Stretch})) {
                ui.Size(UiSize::Sm).Heading({
                    .label = "Artificial Horizon",
                    .icon = Icon::Play()
                });
                ui.Spacer();
                const bool isHandheld = (runtime.Sensors().GetAttitudeReference() == AttitudeReference::Handheld);
                if (ui.Size(UiSize::Xs).Button({
                    .text = isHandheld ? "Ref: Hand" : "Ref: Table",
                    .color = isHandheld ? StateColor::Primary() : StateColor{}
                }).clicked) {
                    runtime.Sensors().SetAttitudeReference(
                        isHandheld ? AttitudeReference::FlatTable : AttitudeReference::Handheld
                    );
                    runtime.System().TriggerHaptic(HapticStyle::Selection);
                }
                if (ui.Size(UiSize::Xs).Button({
                    .text = runtime.Sensors().HasAttitudeTare() ? "Reset 0°" : "Zero 0°",
                    .color = runtime.Sensors().HasAttitudeTare() ? StateColor::Warning() : StateColor{}
                }).clicked) {
                    if (runtime.Sensors().HasAttitudeTare()) {
                        runtime.Sensors().ResetAttitudeTare();
                    } else {
                        runtime.Sensors().TareAttitude();
                    }
                    runtime.System().TriggerHaptic(HapticStyle::Medium);
                }
                if (ui.Size(UiSize::Xs).Button({
                    .text = app.compactAdiMode ? "ADI" : "PFD",
                    .icon = Icon::Refresh()
                }).clicked) {
                    app.compactAdiMode = !app.compactAdiMode;
                    runtime.System().TriggerHaptic(HapticStyle::Light);
                }
            }

            ui.AttitudeView({
                .displayMode = app.compactAdiMode ? AttitudeDisplayMode::CompactADI : AttitudeDisplayMode::FullPFD,
                .colorScheme = AttitudeColorScheme::GlassCockpit,
                .rollDeg = snap.attitude.rollDeg,
                .pitchDeg = snap.attitude.pitchDeg,
                .headingDeg = snap.attitude.headingDeg,
                .airspeedKnots = runtime.Location().GetLatestFix().SpeedKnots(),
                .altitudeFeet = snap.barometricAltitudeMeters * 3.28084f,
                .verticalSpeedFpm = snap.verticalSpeedMps * 196.85f,
                .slipSkid = snap.slipSkid,
                .gLoad = snap.gLoad,
                .showIlsDiamonds = false,
                .heightMode = SizeMode::Stretch
            });

            if (auto metrics = ui.Row({.gap = 6.0f, .widthMode = SizeMode::Stretch})) {
                auto metricBox = [&](std::string_view title, std::string_view val, UiVariant var) {
                    if (auto b = ui.Card({.gap = 2.0f, .padding = Padding::Symmetric(8.0f, 6.0f), .widthMode = SizeMode::Stretch})) {
                        ui.Text(title, {.role = TextRole::LabelSmall, .variant = UiVariant::Muted});
                        ui.Text(val, {.role = TextRole::CodeSmall, .variant = var});
                    }
                };
                metricBox("PITCH / ROLL", ui.Format("{:+.1f}° / {:+.1f}°", snap.attitude.pitchDeg, snap.attitude.rollDeg), UiVariant::Info);
                metricBox("HEADING", ui.Format("{:05.1f}°", snap.attitude.headingDeg), UiVariant::Primary);
                metricBox("G-LOAD / ALT", ui.Format("{:.2f}G | {:.0f}m", snap.gLoad, snap.barometricAltitudeMeters), UiVariant::Success);
            }
        }
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
                    .description = ui.Format("{}x{} @ {:.0f} FPS | Photos: {}",
                                             stats.width, stats.height, stats.fps,
                                             cam.GetCapturedPhotoCount()),
                    .icon = Icon::Eye()
                });
                ui.Spacer();

                if (ui.Size(UiSize::Xs).Button({
                    .text = "Switch",
                    .icon = Icon::Refresh()
                }).clicked) {
                    cam.SwitchCameraFacing();
                    runtime.System().TriggerHaptic(HapticStyle::Medium);
                }

                bool torch = cam.IsTorchEnabled();
                if (ui.Size(UiSize::Xs).Button({
                    .text = torch ? "Torch ON" : "Torch",
                    .icon = Icon::Flash(),
                    .color = torch ? StateColor::Warning() : StateColor{}
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
                .minHeight = 180.0f,
                .heightMode = SizeMode::Stretch,
                .showSidebar = false,
                .showTopToolbar = true,
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

void RenderSimulatorDrawer(Context& ui, MobileRuntime& runtime) {
    auto& sim = runtime.Sensors().GetSimConfig();

    if (auto panel = ui.Card({
        .minWidth = 280.0f,
        .gap = 10.0f,
        .padding = Padding::All(14.0f),
        .widthMode = SizeMode::Fit,
        .heightMode = SizeMode::Stretch
    })) {
        ui.Size(UiSize::Sm).Heading({
            .label = "Mobile Hardware Sim",
            .description = "Real-time IMU, Baro & Light injection",
            .icon = Icon::Settings(),
            .divider = true
        });

        if (auto el = ui.Size(UiSize::Sm).Element({.label = "Auto IMU Motion"})) {
            ui.Size(UiSize::Sm).Switch(sim.animateMotion);
        }

        if (auto el = ui.Size(UiSize::Sm).Element({.label = "Pitch Bias (deg)"})) {
            ui.Size(UiSize::Sm).Slider(sim.manualPitchDeg, -45.0f, 45.0f, {.valueFormat = "%+.1f deg"});
        }

        if (auto el = ui.Size(UiSize::Sm).Element({.label = "Roll Bias (deg)"})) {
            ui.Size(UiSize::Sm).Slider(sim.manualRollDeg, -75.0f, 75.0f, {.valueFormat = "%+.1f deg"});
        }

        if (auto el = ui.Size(UiSize::Sm).Element({.label = "Compass Heading"})) {
            ui.Size(UiSize::Sm).Slider(sim.manualHeadingDeg, 0.0f, 359.0f, {.valueFormat = "%03.0f deg"});
        }

        if (auto el = ui.Size(UiSize::Sm).Element({.label = "Vibration Freq"})) {
            ui.Size(UiSize::Sm).Slider(sim.vibrationFreqHz, 2.0f, 45.0f, {.valueFormat = "%.1f Hz"});
        }

        if (auto el = ui.Size(UiSize::Sm).Element({.label = "Vibration Amp"})) {
            ui.Size(UiSize::Sm).Slider(sim.vibrationAmpG, 0.0f, 1.0f, {.valueFormat = "%.2f G"});
        }

        if (auto el = ui.Size(UiSize::Sm).Element({.label = "Barometer (hPa)"})) {
            ui.Size(UiSize::Sm).Slider(sim.simPressureHpa, 950.0f, 1045.0f, {.valueFormat = "%.1f hPa"});
        }

        if (auto el = ui.Size(UiSize::Sm).Element({.label = "Ambient Light"})) {
            ui.Size(UiSize::Sm).Slider(sim.simLightLux, 5.0f, 2000.0f, {.valueFormat = "%.0f lx"});
        }

        if (auto el = ui.Size(UiSize::Sm).Element({.label = "Proximity Near"})) {
            ui.Size(UiSize::Sm).Switch(sim.simProximityNear);
        }
    }
}

void RenderBottomNavigationBar(Context& ui, MobileRuntime& runtime, MobileAppState& app) {
    if (auto bar = ui.Box({
        .widthMode = SizeMode::Stretch,
        .heightMode = SizeMode::Fit,
        .padding = Padding::Symmetric(8.0f, 6.0f),
        .background = {.normal = Color(15, 23, 42, 255)},
        .border = {.width = 1.0f, .radius = 0.0f}
    })) {
        static constexpr std::array<std::string_view, 5> kTabs = {
            "Horizon",
            "Sensors",
            "Camera",
            "Audio",
            "GNSS"
        };
        if (ui.Size(UiSize::Sm).Segmented(app.activeTab, kTabs, {
            .stretch = StretchMode::Fill
        }).changed) {
            runtime.System().TriggerHaptic(HapticStyle::Selection);
        }
    }
}

} // namespace

int main(int /*argc*/, char* /*argv*/[]) {
    if (!SDL_Init(SDL_INIT_VIDEO)) return 1;

    int winW = 1180;
    int winH = 820;
    SDL_Window* window = nullptr;
    SDL_Renderer* renderer = nullptr;
    if (!SDL_CreateWindowAndRenderer(
            "GuiKit Mobile — Android & iOS Sensor / Camera / GNSS Workstation",
            winW,
            winH,
            SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY,
            &window,
            &renderer)) {
        SDL_Quit();
        return 1;
    }
    SDL_SetRenderVSync(renderer, 1);

    Context ui;
    ThorCanvas canvas;
    std::vector<uint32_t> pixels(static_cast<size_t>(winW * winH), 0);
    canvas.SetTarget(pixels.data(), winW, winH, winW);

    SDL_Texture* tex = SDL_CreateTexture(
        renderer,
        SDL_PIXELFORMAT_ABGR8888,
        SDL_TEXTUREACCESS_STREAMING,
        winW,
        winH
    );
    SDL_SetTextureScaleMode(tex, SDL_SCALEMODE_NEAREST);

    MobileRuntime mobile;
    mobile.Initialize(window, renderer);

    MobileAppState app{};
#ifdef __ANDROID__
    app.showSimulatorDrawer = false;
#endif

    bool running = true;
    auto prevTime = std::chrono::steady_clock::now();

    while (running) {
        const auto now = std::chrono::steady_clock::now();
        const double dt = std::clamp(std::chrono::duration<double>(now - prevTime).count(), 0.001, 0.1);
        prevTime = now;

        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
            if (ev.type == SDL_EVENT_QUIT) {
                running = false;
                break;
            }
            if (ev.type == SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED) {
                winW = std::max(1, ev.window.data1);
                winH = std::max(1, ev.window.data2);
                pixels.assign(static_cast<size_t>(winW * winH), 0);
                canvas.SetTarget(pixels.data(), winW, winH, winW);
                SDL_DestroyTexture(tex);
                tex = SDL_CreateTexture(
                    renderer,
                    SDL_PIXELFORMAT_ABGR8888,
                    SDL_TEXTUREACCESS_STREAMING,
                    winW,
                    winH
                );
                SDL_SetTextureScaleMode(tex, SDL_SCALEMODE_NEAREST);
                ui.InvalidateAll();
                continue;
            }

            if (mobile.ProcessEvent(ev, ui, {static_cast<float>(winW), static_cast<float>(winH)})) {
                continue;
            }

            if (ev.type == SDL_EVENT_MOUSE_MOTION) {
                ui.OnMouseMove({ev.motion.x, ev.motion.y});
            } else if (ev.type == SDL_EVENT_MOUSE_BUTTON_DOWN) {
                ui.OnMouseDown(ev.button.button);
            } else if (ev.type == SDL_EVENT_MOUSE_BUTTON_UP) {
                ui.OnMouseUp(ev.button.button);
            } else if (ev.type == SDL_EVENT_MOUSE_WHEEL) {
                ui.OnMouseWheel(ev.wheel.x, ev.wheel.y);
            } else if (ev.type == SDL_EVENT_TEXT_INPUT) {
                ui.OnTextInput(ev.text.text);
            }
        }

        mobile.UpdateFrame(dt, ui);

        const float dpi = SDL_GetWindowDisplayScale(window);
        ui.BeginFrame({static_cast<float>(winW), static_cast<float>(winH)}, dpi > 0.0f ? dpi : 1.0f);

        if (auto root = ui.Column({
            .widthMode = SizeMode::Stretch,
            .heightMode = SizeMode::Stretch
        })) {
            RenderStatusBar(ui, mobile, app);

            if (auto body = ui.Row({
                .gap = 0.0f,
                .widthMode = SizeMode::Stretch,
                .heightMode = SizeMode::Stretch
            })) {
                if (auto stage = ui.Column({
                    .widthMode = SizeMode::Stretch,
                    .heightMode = SizeMode::Stretch
                })) {
                    switch (static_cast<MobileTab>(app.activeTab)) {
                        case MobileTab::Horizon:
                            RenderHorizonTab(ui, mobile, app);
                            break;
                        case MobileTab::Sensors:
                            RenderSensorsTab(ui, mobile, app);
                            break;
                        case MobileTab::CameraVision:
                            RenderCameraVisionTab(ui, mobile, app);
                            break;
                        case MobileTab::AudioSpectrum:
                            RenderAudioSpectrumTab(ui, mobile, app);
                            break;
                        case MobileTab::GnssAndSystem:
                            RenderGnssAndSystemTab(ui, mobile, app);
                            break;
                    }
                }

                if (app.showSimulatorDrawer) {
                    RenderSimulatorDrawer(ui, mobile);
                }
            }

            RenderBottomNavigationBar(ui, mobile, app);
        }

        ui.EndFrame(canvas);

        if (ui.HasDirtyDamage()) {
            SDL_UpdateTexture(tex, nullptr, pixels.data(), winW * static_cast<int>(sizeof(uint32_t)));
            SDL_RenderClear(renderer);
            SDL_RenderTexture(renderer, tex, nullptr, nullptr);
            SDL_RenderPresent(renderer);
        } else {
            SDL_Delay(4);
        }
    }

    mobile.Shutdown();
    SDL_DestroyTexture(tex);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
