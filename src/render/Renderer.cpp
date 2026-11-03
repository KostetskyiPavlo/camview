#include "render/Renderer.hpp"

#include "render/Overlay.hpp"
#include "util/Log.hpp"

#include <SDL2/SDL.h>

#include <algorithm>
#include <string>

namespace camview {

namespace {

[[noreturn]] void throwSdlError(const char* operation) {
    throw RenderError(std::string(operation) + " failed: " + SDL_GetError());
}

Uint32 toSdlPixelFormat(PixelFormat format) {
    switch (format) {
    case PixelFormat::Yuyv:
        return SDL_PIXELFORMAT_YUY2;
    case PixelFormat::I420:
        return SDL_PIXELFORMAT_IYUV;
    case PixelFormat::Mjpeg:
        break;
    }
    throw RenderError("Renderer: pixel format " + std::string(toString(format)) +
                      " must be decoded before rendering");
}

const char* orUnknown(const char* text) {
    return text != nullptr ? text : "unknown";
}

} // namespace

Renderer::SdlLibrary::SdlLibrary() {
    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        throwSdlError("SDL_Init");
    }
}

Renderer::SdlLibrary::~SdlLibrary() {
    SDL_Quit();
}

void Renderer::SdlDeleter::operator()(SDL_Window* window) const noexcept {
    SDL_DestroyWindow(window);
}

void Renderer::SdlDeleter::operator()(SDL_Renderer* renderer) const noexcept {
    SDL_DestroyRenderer(renderer);
}

void Renderer::SdlDeleter::operator()(SDL_Texture* texture) const noexcept {
    SDL_DestroyTexture(texture);
}

Renderer::Renderer(const std::string& title, int width, int height) {
    // Smooth scaling when the window size differs from the video size.
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "linear");

    window_.reset(SDL_CreateWindow(title.c_str(), SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                                   width, height, SDL_WINDOW_RESIZABLE));
    if (!window_) {
        throwSdlError("SDL_CreateWindow");
    }

    // No SDL_RENDERER_PRESENTVSYNC on purpose: waiting for vblank adds up to one display refresh
    // of latency and blocks the render thread. Low latency matters more than tear-free output.
    renderer_.reset(SDL_CreateRenderer(window_.get(), -1, SDL_RENDERER_ACCELERATED));
    if (!renderer_) {
        log::warning("Accelerated renderer unavailable (", SDL_GetError(),
                     "), falling back to software rendering");
        renderer_.reset(SDL_CreateRenderer(window_.get(), -1, SDL_RENDERER_SOFTWARE));
        if (!renderer_) {
            throwSdlError("SDL_CreateRenderer");
        }
    }

    if (SDL_SetRenderDrawColor(renderer_.get(), 0, 0, 0, 255) != 0) {
        throwSdlError("SDL_SetRenderDrawColor");
    }

    SDL_RendererInfo info{};
    if (SDL_GetRendererInfo(renderer_.get(), &info) == 0) {
        const bool accelerated = (info.flags & static_cast<Uint32>(SDL_RENDERER_ACCELERATED)) != 0;
        log::info("Video driver: ", orUnknown(SDL_GetCurrentVideoDriver()),
                  ", renderer: ", orUnknown(info.name), accelerated ? " (GPU)" : " (software)");
    }
}

Renderer::~Renderer() = default;

bool Renderer::pollQuitRequested() {
    bool quitRequested = false;
    SDL_Event event;
    // Drain the whole queue every call so events never pile up behind a slow frame.
    while (SDL_PollEvent(&event) != 0) {
        if (event.type == SDL_QUIT) {
            quitRequested = true;
        } else if (event.type == SDL_KEYDOWN) {
            const SDL_Keycode key = event.key.keysym.sym;
            if (key == SDLK_ESCAPE || key == SDLK_q) {
                quitRequested = true;
            }
        }
    }
    return quitRequested;
}

void Renderer::render(const Frame& frame, const std::vector<std::string>& overlayLines) {
    ensureTexture(frame);
    uploadFrame(frame);

    if (SDL_RenderClear(renderer_.get()) != 0) {
        throwSdlError("SDL_RenderClear");
    }
    if (SDL_RenderCopy(renderer_.get(), texture_.get(), nullptr, nullptr) != 0) {
        throwSdlError("SDL_RenderCopy");
    }
    drawOverlay(overlayLines);
    SDL_RenderPresent(renderer_.get());
}

void Renderer::renderMessage(const std::vector<std::string>& lines) {
    static_cast<void>(SDL_SetRenderDrawColor(renderer_.get(), 0, 0, 0, 255));
    if (SDL_RenderClear(renderer_.get()) != 0) {
        throwSdlError("SDL_RenderClear");
    }
    drawOverlay(lines);
    SDL_RenderPresent(renderer_.get());
}

void Renderer::ensureTexture(const Frame& frame) {
    if (texture_ && frame.format == textureFormat_ && frame.width == textureWidth_ &&
        frame.height == textureHeight_) {
        return;
    }

    const Uint32 sdlFormat = toSdlPixelFormat(frame.format);
    texture_.reset(SDL_CreateTexture(renderer_.get(), sdlFormat, SDL_TEXTUREACCESS_STREAMING,
                                     frame.width, frame.height));
    if (!texture_) {
        throwSdlError("SDL_CreateTexture");
    }

    // Keeps the video's aspect ratio when the window is resized (black bars, no stretching).
    if (SDL_RenderSetLogicalSize(renderer_.get(), frame.width, frame.height) != 0) {
        throwSdlError("SDL_RenderSetLogicalSize");
    }

    textureFormat_ = frame.format;
    textureWidth_ = frame.width;
    textureHeight_ = frame.height;

    log::info("Texture: ", toString(frame.format), " ", frame.width, "x", frame.height,
              supportsTextureFormat(sdlFormat) ? " (native, converted on GPU)"
                                               : " (not native, SDL converts on CPU)");
}

bool Renderer::supportsTextureFormat(std::uint32_t sdlFormat) const {
    SDL_RendererInfo info{};
    if (SDL_GetRendererInfo(renderer_.get(), &info) != 0) {
        return false;
    }
    for (Uint32 i = 0; i < info.num_texture_formats; ++i) {
        if (info.texture_formats[i] == sdlFormat) {
            return true;
        }
    }
    return false;
}

void Renderer::uploadFrame(const Frame& frame) {
    switch (frame.format) {
    case PixelFormat::Yuyv: {
        const auto requiredBytes =
            static_cast<std::size_t>(frame.stride) * static_cast<std::size_t>(frame.height);
        if (frame.data.size() < requiredBytes) {
            throw RenderError("Renderer: YUYV frame is smaller than stride * height");
        }
        if (SDL_UpdateTexture(texture_.get(), nullptr, frame.data.data(), frame.stride) != 0) {
            throwSdlError("SDL_UpdateTexture");
        }
        return;
    }
    case PixelFormat::I420: {
        const I420Layout layout = i420Layout(frame.width, frame.height);
        if (frame.data.size() < layout.totalBytes) {
            throw RenderError("Renderer: I420 frame is smaller than its three planes");
        }
        const std::uint8_t* planeY = frame.data.data();
        const std::uint8_t* planeU = planeY + layout.lumaBytes;
        const std::uint8_t* planeV = planeU + layout.chromaBytes;
        if (SDL_UpdateYUVTexture(texture_.get(), nullptr, planeY, frame.width, planeU,
                                 layout.chromaWidth, planeV, layout.chromaWidth) != 0) {
            throwSdlError("SDL_UpdateYUVTexture");
        }
        return;
    }
    case PixelFormat::Mjpeg:
        break;
    }
    throw RenderError("Renderer: MJPG frames must be decoded before rendering");
}

void Renderer::drawOverlay(const std::vector<std::string>& lines) {
    if (lines.empty()) {
        return;
    }

    // Coordinates are in video pixels (logical size), so the overlay scales with the window.
    const int scale = std::max(2, textureHeight_ / 240);
    const int padding = 2 * scale;
    const int lineHeight = overlay::kLineAdvance * scale;

    int width = 0;
    std::vector<OverlayRect> rects;
    for (std::size_t i = 0; i < lines.size(); ++i) {
        width = std::max(width, overlay::textWidth(lines[i], scale));
        overlay::layoutText(lines[i], padding, padding + static_cast<int>(i) * lineHeight, scale,
                            rects);
    }

    // Overlay drawing failures are not worth stopping the video for, so results are ignored.
    SDL_Renderer* sdl = renderer_.get();
    static_cast<void>(SDL_SetRenderDrawBlendMode(sdl, SDL_BLENDMODE_BLEND));
    static_cast<void>(SDL_SetRenderDrawColor(sdl, 0, 0, 0, 160));
    const SDL_Rect background{0, 0, width + 2 * padding,
                              static_cast<int>(lines.size()) * lineHeight + padding};
    static_cast<void>(SDL_RenderFillRect(sdl, &background));

    static_cast<void>(SDL_SetRenderDrawColor(sdl, 255, 255, 255, 255));
    for (const OverlayRect& rect : rects) {
        const SDL_Rect sdlRect{rect.x, rect.y, rect.width, rect.height};
        static_cast<void>(SDL_RenderFillRect(sdl, &sdlRect));
    }

    static_cast<void>(SDL_SetRenderDrawColor(sdl, 0, 0, 0, 255)); // Restore the clear colour.
}

} // namespace camview