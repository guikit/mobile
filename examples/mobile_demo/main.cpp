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
    ImuAttitude = 0,
    CameraVision = 1,
    AudioSpectrum = 2,
    GnssAndSystem = 3
};

struct MobileAppState {
    size_t activeTab{0};
    bool showSimulatorDrawer{true};
    bool compactAdiMode{true};
    int imuSignalAxis{3}; // 0=X, 1=Y, 2=Z, 3=|G|
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
        .padding = Padding::Symmetric(14.0f, 6.0f),
        .background = {.normal = Color(13, 18, 28, 255)},
        .border = {.width = 1.0f, .radius = 0.0f}
    })) {
        if (auto row = ui.Row({.gap = 10.0f, .align = Alignment::Center, .widthMode = SizeMode::Stretch})) {
            ui.Text(ui.Format("{} | {}", dev.model, dev.osVersion), {
                .role = TextRole::LabelSmall,
                .variant = UiVariant::Info
            });

            ui.Spacer();

            ui.Text(ui.Format("GNSS {} sat | {:.0f} hPa | {:.0f} lx",
                              fix.satellitesUsed,
                              snap.pressureHpa,
                              snap.ambientLightLux), {
                .role = TextRole::CodeSmall,
                .variant = UiVariant::Muted
            });

            ui.Text(ui.Format("{}{:.0f}%", bat.isCharging ? "CHG " : "BAT ", bat.levelPct), {
                .role = TextRole::CodeSmall,
                .variant = bat.levelPct > 25.0f ? UiVariant::Success : UiVariant::Warning
            });

            if (ui.Size(UiSize::Xs).Button({
                .text = app.showSimulatorDrawer ? "Hide Sim" : "Sim Controls",
                .icon = Icon::Settings(),
                .color = app.showSimulatorDrawer ? StateColor::Primary() : StateColor{}
            }).clicked) {
                app.showSimulatorDrawer = !app.showSimulatorDrawer;
                runtime.System().TriggerHaptic(HapticStyle::Selection);
            }
        }
    }
}

void RenderImuAttitudeTab(Context& ui, MobileRuntime& runtime, MobileAppState& app) {
    const auto& snap = runtime.Sensors().GetSnapshot();

    if (auto scroll = ui.ScrollArea({
        .padding = Padding::All(12.0f)
    })) {
        if (auto col = ui.Column({.gap = 12.0f, .widthMode = SizeMode::Stretch})) {
            // 1. Primary Flight / Attitude Horizon Card
            if (auto card = ui.Card({
                .gap = 10.0f,
                .padding = Padding::All(14.0f),
                .widthMode = SizeMode::Stretch
            })) {
                if (auto hdr = ui.Row({.gap = 8.0f, .align = Alignment::Center, .widthMode = SizeMode::Stretch})) {
                    ui.Size(UiSize::Sm).Heading({
                        .label = "9-DOF Attitude & Artificial Horizon",
                        .description = "Driven by SensorManager (RotationVector + Barometer + G-Load)",
                        .icon = Icon::Play()
                    });
                    ui.Spacer();
                    if (ui.Size(UiSize::Xs).Button({
                        .text = app.compactAdiMode ? "Mode: Compact ADI" : "Mode: Full PFD",
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
                    .height = 310.0f,
                    .heightMode = SizeMode::Fit
                });

                if (auto metrics = ui.Row({.gap = 8.0f, .widthMode = SizeMode::Stretch})) {
                    auto metricBox = [&](std::string_view title, std::string_view val, UiVariant var) {
                        if (auto b = ui.Card({.gap = 2.0f, .padding = Padding::All(8.0f), .widthMode = SizeMode::Stretch})) {
                            ui.Text(title, {.role = TextRole::LabelSmall, .variant = UiVariant::Muted});
                            ui.Text(val, {.role = TextRole::CodeMedium, .variant = var});
                        }
                    };
                    metricBox("PITCH / ROLL", ui.Format("{:+.1f} / {:+.1f} deg", snap.attitude.pitchDeg, snap.attitude.rollDeg), UiVariant::Info);
                    metricBox("HEADING (MAG)", ui.Format("{:05.1f} deg", snap.attitude.headingDeg), UiVariant::Primary);
                    metricBox("G-LOAD / STEPS", ui.Format("{:.2f} G | {} stp", snap.gLoad, snap.stepCount), UiVariant::Success);
                }
            }

            // 2. 3-Axis IMU Readouts & Real-Time Vibration Spectrum
            if (auto card = ui.Card({
                .gap = 10.0f,
                .padding = Padding::All(14.0f),
                .widthMode = SizeMode::Stretch
            })) {
                if (auto hdr = ui.Row({.gap = 8.0f, .align = Alignment::Center, .widthMode = SizeMode::Stretch})) {
                    ui.Size(UiSize::Sm).Heading({
                        .label = "MEMS Accelerometer, Gyroscope & Vibration STFT",
                        .description = ui.Format("Accel: ({:+.2f}, {:+.2f}, {:+.2f}) m/s^2  |  Gyro: ({:+.2f}, {:+.2f}, {:+.2f}) rad/s",
                                                 snap.accelerometer.x, snap.accelerometer.y, snap.accelerometer.z,
                                                 snap.gyroscope.x, snap.gyroscope.y, snap.gyroscope.z),
                        .icon = Icon::Flash()
                    });
                    ui.Spacer();
                    static constexpr std::array<std::string_view, 4> kAxisLabels = {"Acc X", "Acc Y", "Acc Z", "|G| Net"};
                    ui.Size(UiSize::Xs).Segmented(app.imuSignalAxis, kAxisLabels);
                }

                std::span<const float> trace = runtime.Sensors().GetAccelMagnitudeHistory();
                if (app.imuSignalAxis == 0) trace = runtime.Sensors().GetAccelHistoryX();
                else if (app.imuSignalAxis == 1) trace = runtime.Sensors().GetAccelHistoryY();
                else if (app.imuSignalAxis == 2) trace = runtime.Sensors().GetAccelHistoryZ();

                ui.SignalView(trace, {
                    .sampleRate = runtime.Sensors().GetEstimatedSampleRateHz(),
                    .amplitudeScale = AmplitudeScale::Linear,
                    .colormap = Colormap::Plasma,
                    .fftSize = 256,
                    .showOverviewBar = false,
                    .height = 240.0f,
                    .heightMode = SizeMode::Fit
                });
            }
        }
    }
}

void RenderCameraVisionTab(Context& ui, MobileRuntime& runtime, MobileAppState& app) {
    auto& cam = runtime.Camera();
    const auto& stats = cam.GetFrameStats();
    const auto* activeDev = cam.GetActiveDevice();

    if (auto scroll = ui.ScrollArea({
        .padding = Padding::All(12.0f)
    })) {
        if (auto col = ui.Column({.gap = 12.0f, .widthMode = SizeMode::Stretch})) {
            // 1. Camera Toolbar & Hardware Controls
            if (auto card = ui.Card({
                .gap = 10.0f,
                .padding = Padding::All(12.0f),
                .widthMode = SizeMode::Stretch
            })) {
                if (auto row = ui.Row({.gap = 8.0f, .align = Alignment::Center, .widthMode = SizeMode::Stretch})) {
                    ui.Size(UiSize::Sm).Heading({
                        .label = activeDev ? activeDev->name : "Mobile Camera",
                        .description = ui.Format("{}x{} @ {:.1f} FPS | Mean Luma: {:.1f} | Photos: {}",
                                                 stats.width, stats.height, stats.fps,
                                                 stats.meanLuminance, cam.GetCapturedPhotoCount()),
                        .icon = Icon::Eye()
                    });
                    ui.Spacer();

                    if (ui.Size(UiSize::Sm).Button({
                        .text = "Switch Cam",
                        .icon = Icon::Refresh()
                    }).clicked) {
                        cam.SwitchCameraFacing();
                        runtime.System().TriggerHaptic(HapticStyle::Medium);
                    }

                    bool torch = cam.IsTorchEnabled();
                    if (ui.Size(UiSize::Sm).Button({
                        .text = torch ? "Torch: ON" : "Torch: OFF",
                        .icon = Icon::Flash(),
                        .color = torch ? StateColor::Warning() : StateColor{}
                    }).clicked) {
                        cam.SetTorchEnabled(!torch);
                        runtime.System().TriggerHaptic(HapticStyle::Light);
                    }

                    if (ui.Size(UiSize::Sm).Button({
                        .text = "Capture",
                        .icon = Icon::Save(),
                        .color = StateColor::Primary()
                    }).clicked) {
                        cam.CaptureSnapshot();
                        runtime.System().TriggerHaptic(HapticStyle::Success);
                    }
                }

                if (auto sliders = ui.Row({.gap = 16.0f, .align = Alignment::Center, .widthMode = SizeMode::Stretch})) {
                    float ev = cam.GetExposureCompensationEv();
                    if (auto el = ui.Size(UiSize::Sm).Element({.label = "Exposure EV", .stretch = StretchMode::Fill})) {
                        if (ui.Size(UiSize::Sm).Slider(ev, -2.0f, 2.0f, {.valueFormat = "%+.1f EV"}).changed) {
                            cam.SetExposureCompensationEv(ev);
                        }
                    }

                    float zoom = cam.GetZoomFactor();
                    if (auto el = ui.Size(UiSize::Sm).Element({.label = "Digital Zoom", .stretch = StretchMode::Fill})) {
                        if (ui.Size(UiSize::Sm).Slider(zoom, 1.0f, 5.0f, {.valueFormat = "%.1fx"}).changed) {
                            cam.SetZoomFactor(zoom);
                        }
                    }
                }
            }

            // 2. Live ImageView Workstation (with Loupe, W/L, Colormap & RGB Histogram)
            const ImageViewData imgData = cam.GetImageViewData();
            if (!imgData.Empty()) {
                ui.ImageView(imgData, app.cameraViewParams, {
                    .height = 460.0f,
                    .minHeight = 360.0f,
                    .heightMode = SizeMode::Fit,
                    .showSidebar = true,
                    .sidebarOnRight = true,
                    .sidebarWidth = 250.0f,
                    .showTopToolbar = true,
                    .showStatusBar = true
                });
            }
        }
    }
}

void RenderAudioSpectrumTab(Context& ui, MobileRuntime& runtime, MobileAppState&) {
    auto& audio = runtime.Audio();

    if (auto scroll = ui.ScrollArea({
        .padding = Padding::All(12.0f)
    })) {
        if (auto col = ui.Column({.gap = 12.0f, .widthMode = SizeMode::Stretch})) {
            // 1. Microphone Input & Stereo Level Meter
            if (auto card = ui.Card({
                .gap = 10.0f,
                .padding = Padding::All(12.0f),
                .widthMode = SizeMode::Stretch
            })) {
                if (auto row = ui.Row({.gap = 8.0f, .align = Alignment::Center, .widthMode = SizeMode::Stretch})) {
                    ui.Size(UiSize::Sm).Heading({
                        .label = audio.IsUsingSyntheticSignal()
                            ? "Microphone Audio Analyzer (Synthetic Harmonic Source)"
                            : "Microphone Audio Analyzer (Live Hardware PCM)",
                        .description = ui.Format("Peak: {:.1f} dBFS | RMS: {:.1f} dBFS | Dominant Tone: {:.0f} Hz ({:.1f} dB)",
                                                 audio.GetPeakDbFs(), audio.GetRmsDbFs(),
                                                 audio.GetDominantFreqHz(), audio.GetDominantPeakDb()),
                        .icon = Icon::Play()
                    });
                    ui.Spacer();

                    bool capturing = audio.IsCapturing();
                    if (ui.Size(UiSize::Sm).Button({
                        .text = capturing ? "Pause Mic" : "Resume Mic",
                        .icon = capturing ? Icon::Pause() : Icon::Play(),
                        .color = capturing ? StateColor::Primary() : StateColor{}
                    }).clicked) {
                        audio.SetCapturing(!capturing);
                        runtime.System().TriggerHaptic(HapticStyle::Selection);
                    }
                }

                static constexpr std::array<std::string_view, 2> kChLabels = {"MIC L", "MIC R"};
                ui.LevelMeter(audio.GetStereoMeterDb(), -60.0f, 0.0f, {
                    .segments = 28,
                    .channelLabels = kChLabels
                });

                if (audio.IsUsingSyntheticSignal()) {
                    float f0 = audio.GetSynthFundamentalHz();
                    if (auto el = ui.Size(UiSize::Sm).Element({.label = "Test Tone Frequency (Hz)"})) {
                        if (ui.Size(UiSize::Sm).Slider(f0, 120.0f, 8000.0f, {.valueFormat = "%.0f Hz"}).changed) {
                            audio.SetSynthFundamentalHz(f0);
                        }
                    }
                }
            }

            // 2. Real-Time Spectrum Waterfall (RTSA)
            if (auto card = ui.Card({
                .gap = 8.0f,
                .padding = Padding::All(12.0f),
                .widthMode = SizeMode::Stretch
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
                    .height = 360.0f,
                    .heightMode = SizeMode::Fit
                });
            }
        }
    }
}

void RenderGnssAndSystemTab(Context& ui, MobileRuntime& runtime, MobileAppState& app) {
    app.EnsureChartLoaded();
    const auto& fix = runtime.Location().GetLatestFix();
    const auto& snap = runtime.Sensors().GetSnapshot();

    if (auto scroll = ui.ScrollArea({
        .padding = Padding::All(12.0f)
    })) {
        if (auto col = ui.Column({.gap = 12.0f, .widthMode = SizeMode::Stretch})) {
            // 1. Live GNSS MapView & Track
            if (auto card = ui.Card({
                .gap = 10.0f,
                .padding = Padding::All(12.0f),
                .widthMode = SizeMode::Stretch
            })) {
                if (auto row = ui.Row({.gap = 8.0f, .align = Alignment::Center, .widthMode = SizeMode::Stretch})) {
                    ui.Size(UiSize::Sm).Heading({
                        .label = "GNSS Navigation & S-52 Marine Chart",
                        .description = ui.Format("Lat: {:.5f}  Lon: {:.5f}  |  SOG: {:.1f} kn  COG: {:05.1f} deg  |  Track: {:.0f} m",
                                                 fix.coord.lat, fix.coord.lon,
                                                 fix.SpeedKnots(), fix.courseDeg,
                                                 runtime.Location().GetTotalTrackDistanceMeters()),
                        .icon = Icon::Search()
                    });
                    ui.Spacer();
                    ui.Text("Follow GPS", {.role = TextRole::LabelSmall, .variant = UiVariant::Muted});
                    ui.Size(UiSize::Sm).Checkbox(app.followGpsOnMap);
                    if (ui.Size(UiSize::Xs).Button({.text = "Clear Track", .icon = Icon::Trash()}).clicked) {
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
                    .heightMode = SizeMode::Fit,
                    .height = 300.0f
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

            // 2. Environmental Sensors, Permissions & Haptics
            if (auto row = ui.Row({.gap = 12.0f, .widthMode = SizeMode::Stretch})) {
                // Environmental Card
                if (auto envCard = ui.Card({
                    .gap = 8.0f,
                    .padding = Padding::All(12.0f),
                    .widthMode = SizeMode::Stretch
                })) {
                    ui.Size(UiSize::Sm).Heading({
                        .label = "Environmental & Activity Sensors",
                        .icon = Icon::Info(),
                        .divider = true
                    });
                    ui.Text(ui.Format("Barometer: {:.2f} hPa (ISA Alt: {:.1f} m)",
                                      snap.pressureHpa, snap.barometricAltitudeMeters),
                            {.role = TextRole::CodeSmall});
                    ui.Text(ui.Format("Ambient Light: {:.1f} lux", snap.ambientLightLux),
                            {.role = TextRole::CodeSmall});
                    ui.Text(ui.Format("Proximity Sensor: {:.1f} cm ({})",
                                      snap.proximityCm, snap.proximityNear ? "NEAR" : "FAR"),
                            {.role = TextRole::CodeSmall});
                    ui.Text(ui.Format("Pedometer Step Counter: {} steps", snap.stepCount),
                            {.role = TextRole::CodeSmall});
                }

                // Runtime Permissions & Haptics Card
                if (auto sysCard = ui.Card({
                    .gap = 8.0f,
                    .padding = Padding::All(12.0f),
                    .widthMode = SizeMode::Stretch
                })) {
                    ui.Size(UiSize::Sm).Heading({
                        .label = "Android Permissions & Haptics",
                        .description = app.lastPermissionToast,
                        .icon = Icon::Check(),
                        .divider = true
                    });

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
                        permBtn(Permission::RecordAudio, "Microphone");
                        permBtn(Permission::FineLocation, "Fine GPS");
                        permBtn(Permission::BodySensors, "Sensors");
                    }

                    if (auto hapRow = ui.Row({.gap = 6.0f, .align = Alignment::Center})) {
                        ui.Text("Haptics:", {.role = TextRole::LabelSmall, .variant = UiVariant::Muted});
                        if (ui.Size(UiSize::Xs).Button({.text = "Light"}).clicked) {
                            runtime.System().TriggerHaptic(HapticStyle::Light);
                        }
                        if (ui.Size(UiSize::Xs).Button({.text = "Medium"}).clicked) {
                            runtime.System().TriggerHaptic(HapticStyle::Medium);
                        }
                        if (ui.Size(UiSize::Xs).Button({.text = "Heavy"}).clicked) {
                            runtime.System().TriggerHaptic(HapticStyle::Heavy);
                        }
                        ui.Spacer();
                        ui.Text(ui.Format("Pulses: {}", runtime.System().GetHapticPulseCount()), {
                            .role = TextRole::CodeSmall,
                            .variant = UiVariant::Info
                        });
                    }
                }
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
        .padding = Padding::Symmetric(16.0f, 8.0f),
        .background = {.normal = Color(15, 23, 42, 255)},
        .border = {.width = 1.0f, .radius = 0.0f}
    })) {
        static constexpr std::array<std::string_view, 4> kTabs = {
            "1. IMU & Attitude",
            "2. Camera & Vision",
            "3. Mic & Spectrum",
            "4. GNSS & System"
        };
        if (ui.Size(UiSize::Md).Segmented(app.activeTab, kTabs, {
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
                        case MobileTab::ImuAttitude:
                            RenderImuAttitudeTab(ui, mobile, app);
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
