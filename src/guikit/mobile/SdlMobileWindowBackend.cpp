#include "guikit/mobile/SdlMobileWindowBackend.hpp"
#include <SDL3/SDL.h>
#include <algorithm>
#include <cmath>
#include <iostream>
#include "guikit/Context.hpp"
#include "guikit/backend/SdlCanvas.hpp"
#include "guikit/backend/ThorCanvas.hpp"
#include "guikit/mobile/MobileRuntime.hpp"

namespace guikit::mobile {

namespace {

[[nodiscard]] SDL_PixelFormat ResolveSdlPixelFormat(SurfacePixelFormat fmt) noexcept {
    switch (fmt) {
        case SurfacePixelFormat::ARGB8888_Premul:
            return SDL_PIXELFORMAT_ARGB8888;
        case SurfacePixelFormat::ABGR8888_Premul:
        default:
            return SDL_PIXELFORMAT_ABGR8888;
    }
}

} // namespace

std::unique_ptr<IMobileWindowBackend> CreateSdlMobileWindowBackend() {
    return std::make_unique<SdlMobileWindowBackend>();
}

std::unique_ptr<IMobileWindowBackend> CreateSdlMobileWindowBackend(std::unique_ptr<ICanvas> canvas) {
    return std::make_unique<SdlMobileWindowBackend>(std::move(canvas));
}

void RegisterSdlMobileWindowBackend() noexcept {
    RegisterDefaultMobileWindowBackendFactory(&CreateSdlMobileWindowBackend);
}

SdlMobileWindowBackend::SdlMobileWindowBackend(std::unique_ptr<ICanvas> canvas)
    : m_canvas(std::move(canvas))
    , m_hasExplicitCanvas(m_canvas != nullptr) {
    if (!m_canvas) {
        m_canvas = CreateVectorCanvas();
        if (!m_canvas) {
            m_canvas = std::make_unique<ThorCanvas>();
        }
    }
}

SdlMobileWindowBackend::~SdlMobileWindowBackend() {
    Shutdown();
}

bool SdlMobileWindowBackend::Initialize(
    const MobileWindowConfig& config,
    Context& ui,
    MobileRuntime& runtime,
    const MobileWindowHostCallbacks& callbacks
) {
    Shutdown();

    m_config = config;
    m_callbacks = callbacks;
    m_width = std::max(1, config.width);
    m_height = std::max(1, config.height);
    m_vsyncEnabled = config.vsync;
    m_shouldExit = false;
    m_minimized = false;

    if (!SDL_InitSubSystem(SDL_INIT_VIDEO)) {
        std::cerr << "Failed to initialize SDL3 video subsystem: " << SDL_GetError() << "\n";
        return false;
    }
    m_sdlVideoInitialized = true;

    SDL_WindowFlags windowFlags = 0;
    if (m_config.resizable) {
        windowFlags |= SDL_WINDOW_RESIZABLE;
    }
    if (m_config.highDpi) {
        windowFlags |= SDL_WINDOW_HIGH_PIXEL_DENSITY;
    }

    if (!SDL_CreateWindowAndRenderer(
            m_config.title.c_str(),
            m_width,
            m_height,
            windowFlags,
            &m_window,
            &m_renderer)) {
        std::cerr << "Failed to create SDL window & renderer: " << SDL_GetError() << "\n";
        Shutdown();
        return false;
    }

    if (m_config.minWidth > 0 && m_config.minHeight > 0) {
        SDL_SetWindowMinimumSize(m_window, m_config.minWidth, m_config.minHeight);
    }
    SDL_SetRenderVSync(m_renderer, m_vsyncEnabled ? 1 : 0);

    int actualW = m_width;
    int actualH = m_height;
    if (SDL_GetWindowSizeInPixels(m_window, &actualW, &actualH) && actualW > 0 && actualH > 0) {
        m_width = actualW;
        m_height = actualH;
    }

    if (!m_hasExplicitCanvas && m_config.canvasFactory != nullptr) {
        if (auto custom = m_config.canvasFactory(true)) {
            m_canvas = std::move(custom);
        }
    }
    if (auto* sdlCanvas = dynamic_cast<SdlCanvas*>(m_canvas.get()); sdlCanvas != nullptr) {
        sdlCanvas->SetRenderer(m_renderer);
    }

    m_bufWidth = m_width;
    m_bufHeight = m_height;
    if (m_canvas->UsesCpuFramebuffer()) {
        m_pixelBuffer.assign(static_cast<size_t>(m_bufWidth) * static_cast<size_t>(m_bufHeight), 0);
        m_canvas->SetTarget(m_pixelBuffer.data(), m_bufWidth, m_bufHeight, m_bufWidth);

        m_screenTexture = SDL_CreateTexture(
            m_renderer,
            ResolveSdlPixelFormat(m_canvas->GetPixelFormat()),
            SDL_TEXTUREACCESS_STREAMING,
            m_bufWidth,
            m_bufHeight
        );
        SDL_SetTextureScaleMode(m_screenTexture, SDL_SCALEMODE_NEAREST);
    } else {
        m_pixelBuffer.clear();
        m_pixelBuffer.shrink_to_fit();
    }

    UpdateDisplayMetrics();

    ui.SetClipboardCallbacks(
        []() -> std::string {
            char* clip = SDL_GetClipboardText();
            if (!clip) return {};
            std::string res(clip);
            SDL_free(clip);
            return res;
        },
        [](std::string_view text) {
            std::string s(text);
            SDL_SetClipboardText(s.c_str());
        }
    );

    runtime.Initialize(m_window, m_renderer);
    return true;
}

void SdlMobileWindowBackend::Shutdown() noexcept {
    if (auto* sdlCanvas = dynamic_cast<SdlCanvas*>(m_canvas.get()); sdlCanvas != nullptr) {
        sdlCanvas->SetRenderer(nullptr);
    }
    if (m_screenTexture != nullptr) {
        SDL_DestroyTexture(m_screenTexture);
        m_screenTexture = nullptr;
    }
    if (m_renderer != nullptr) {
        SDL_DestroyRenderer(m_renderer);
        m_renderer = nullptr;
    }
    if (m_window != nullptr) {
        SDL_DestroyWindow(m_window);
        m_window = nullptr;
    }
    if (m_sdlVideoInitialized) {
        SDL_QuitSubSystem(SDL_INIT_VIDEO);
        m_sdlVideoInitialized = false;
    }
}

void SdlMobileWindowBackend::UpdateDisplayMetrics() noexcept {
    const float dpi = (m_window != nullptr) ? SDL_GetWindowDisplayScale(m_window) : 1.0f;
    m_displayScale = (dpi > 0.1f) ? dpi : 1.0f;
    m_displayPxPerMm = (96.0f * m_displayScale) / 25.4f;
}

bool SdlMobileWindowBackend::PumpEvents(
    Context& ui,
    MobileRuntime& runtime,
    bool /*reactiveMode*/,
    int /*sleepTimeoutMs*/
) {
    if (m_shouldExit) {
        return false;
    }

    SDL_Event ev;
    while (SDL_PollEvent(&ev)) {
        ProcessEvent(ev, ui, runtime);
        if (m_shouldExit) {
            return false;
        }
    }

    return !m_shouldExit;
}

void SdlMobileWindowBackend::ProcessEvent(const SDL_Event& ev, Context& ui, MobileRuntime& runtime) {
    if (ev.type == SDL_EVENT_QUIT || ev.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED) {
        if (!m_callbacks.onCloseRequested || m_callbacks.onCloseRequested()) {
            m_shouldExit = true;
        }
        return;
    }

    if (ev.type == SDL_EVENT_WINDOW_MINIMIZED || ev.type == SDL_EVENT_WINDOW_HIDDEN) {
        m_minimized = true;
        return;
    }

    if (ev.type == SDL_EVENT_WINDOW_RESTORED ||
        ev.type == SDL_EVENT_WINDOW_SHOWN ||
        ev.type == SDL_EVENT_WINDOW_MAXIMIZED ||
        ev.type == SDL_EVENT_WINDOW_EXPOSED) {
        m_minimized = false;
        UpdateDisplayMetrics();
        ui.InvalidateAll();
        ui.RequestRepaint(2);
        return;
    }

    if (ev.type == SDL_EVENT_WINDOW_DISPLAY_CHANGED ||
        ev.type == SDL_EVENT_WINDOW_DISPLAY_SCALE_CHANGED) {
        UpdateDisplayMetrics();
        ui.InvalidateAll();
        ui.RequestRepaint(2);
        return;
    }

    if (ev.type == SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED) {
        if (ev.window.data1 > 0 && ev.window.data2 > 0) {
            m_width = std::max(1, ev.window.data1);
            m_height = std::max(1, ev.window.data2);
            UpdateDisplayMetrics();
            ui.InvalidateAll();
            ui.RequestRepaint(3);
            if (m_callbacks.onResize) {
                m_callbacks.onResize(m_width, m_height);
            }
        }
        return;
    }

    // Route mobile lifecycle, back button, and multi-touch gestures through MobileRuntime
    if (runtime.ProcessEvent(&ev, ui, {static_cast<float>(m_width), static_cast<float>(m_height)})) {
        return;
    }

    if (ev.type == SDL_EVENT_MOUSE_MOTION) {
        ui.OnMouseMove({ev.motion.x, ev.motion.y});
    } else if (ev.type == SDL_EVENT_MOUSE_BUTTON_DOWN) {
        const SDL_Keymod mod = SDL_GetModState();
        ui.SetKeyModifiers({
            .ctrl  = (mod & SDL_KMOD_CTRL) != 0,
            .shift = (mod & SDL_KMOD_SHIFT) != 0,
            .alt   = (mod & SDL_KMOD_ALT) != 0
        });
        ui.OnMouseDown(ev.button.button);
    } else if (ev.type == SDL_EVENT_MOUSE_BUTTON_UP) {
        ui.OnMouseUp(ev.button.button);
    } else if (ev.type == SDL_EVENT_MOUSE_WHEEL) {
        const SDL_Keymod mod = SDL_GetModState();
        ui.SetKeyModifiers({
            .ctrl  = (mod & SDL_KMOD_CTRL) != 0,
            .shift = (mod & SDL_KMOD_SHIFT) != 0,
            .alt   = (mod & SDL_KMOD_ALT) != 0
        });
        ui.OnMouseWheel(ev.wheel.x, ev.wheel.y);
    } else if (ev.type == SDL_EVENT_TEXT_INPUT) {
        ui.OnTextInput(ev.text.text);
    } else if (ev.type == SDL_EVENT_KEY_DOWN) {
        const bool ctrl  = (ev.key.mod & SDL_KMOD_CTRL) != 0;
        const bool shift = (ev.key.mod & SDL_KMOD_SHIFT) != 0;
        const bool alt   = (ev.key.mod & SDL_KMOD_ALT) != 0;
        const KeyModifiers mods{ctrl, shift, alt};
        ui.SetKeyModifiers(mods);

        Key key = Key::None;
        switch (ev.key.key) {
            case SDLK_BACKSPACE: key = Key::Backspace; break;
            case SDLK_DELETE:    key = Key::Delete; break;
            case SDLK_RETURN:
            case SDLK_KP_ENTER:  key = Key::Enter; break;
            case SDLK_ESCAPE:    key = Key::Escape; break;
            case SDLK_TAB:       key = Key::Tab; break;
            case SDLK_SPACE:     key = Key::Space; break;
            case SDLK_LEFT:      key = Key::Left; break;
            case SDLK_RIGHT:     key = Key::Right; break;
            case SDLK_UP:        key = Key::Up; break;
            case SDLK_DOWN:      key = Key::Down; break;
            case SDLK_HOME:      key = Key::Home; break;
            case SDLK_END:       key = Key::End; break;
            case SDLK_PAGEUP:    key = Key::PageUp; break;
            case SDLK_PAGEDOWN:  key = Key::PageDown; break;
            default: break;
        }
        if (key != Key::None) {
            ui.OnKeyDown(key, mods);
        }
    }
}

void SdlMobileWindowBackend::PrepareFrameTarget(Context& ui) {
    if (!m_canvas) {
        return;
    }
    if (m_width != m_bufWidth || m_height != m_bufHeight) {
        m_bufWidth = m_width;
        m_bufHeight = m_height;
        if (m_canvas->UsesCpuFramebuffer()) {
            m_pixelBuffer.assign(static_cast<size_t>(m_bufWidth) * static_cast<size_t>(m_bufHeight), 0);
            m_canvas->SetTarget(m_pixelBuffer.data(), m_bufWidth, m_bufHeight, m_bufWidth);
            if (m_screenTexture != nullptr) {
                SDL_DestroyTexture(m_screenTexture);
            }
            m_screenTexture = SDL_CreateTexture(
                m_renderer,
                ResolveSdlPixelFormat(m_canvas->GetPixelFormat()),
                SDL_TEXTUREACCESS_STREAMING,
                m_bufWidth,
                m_bufHeight
            );
            SDL_SetTextureScaleMode(m_screenTexture, SDL_SCALEMODE_NEAREST);
        } else {
            m_pixelBuffer.clear();
            m_pixelBuffer.shrink_to_fit();
        }
        ui.InvalidateAll();
    }
}

void SdlMobileWindowBackend::PresentFrame(const Context& ui, FrameMetrics& metrics, bool /*forcePresent*/) {
    if (!m_renderer || !m_canvas) {
        return;
    }

    metrics.MarkGpuStart();
    if (m_canvas->UsesCpuFramebuffer()) {
        if (!m_screenTexture) {
            return;
        }
        if (ui.HasDirtyDamage()) {
            SDL_UpdateTexture(
                m_screenTexture,
                nullptr,
                m_pixelBuffer.data(),
                m_width * static_cast<int>(sizeof(uint32_t))
            );
        }
        SDL_RenderClear(m_renderer);
        SDL_RenderTexture(m_renderer, m_screenTexture, nullptr, nullptr);
    }
    metrics.MarkGpuDone();

    SDL_RenderPresent(m_renderer);
    metrics.MarkFramePresented();
}

void SdlMobileWindowBackend::PaceFrame(double /*elapsedMs*/, bool presented) noexcept {
    if (!presented) {
        SDL_Delay(4);
    }
}

void SdlMobileWindowBackend::SetVsyncEnabled(bool enabled) noexcept {
    m_vsyncEnabled = enabled;
    if (m_renderer != nullptr) {
        SDL_SetRenderVSync(m_renderer, m_vsyncEnabled ? 1 : 0);
    }
}

void SdlMobileWindowBackend::OpenUrl(std::string_view url) const noexcept {
    std::string urlStr(url);
    SDL_OpenURL(urlStr.c_str());
}

} // namespace guikit::mobile
