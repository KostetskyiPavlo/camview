#pragma once

#include "frame/Frame.hpp"

#include <cstdint>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

// Forward declarations keep SDL headers out of every file that includes Renderer.hpp.
struct SDL_Window;
struct SDL_Renderer;
struct SDL_Texture;

namespace camview {

class RenderError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

// Owns the SDL window and draws frames into it.
// SDL requires the window to be created, used and destroyed on one thread; in camview that is
// the main thread.
class Renderer {
public:
    // Throws RenderError if SDL, the window or the renderer cannot be created.
    Renderer(const std::string& title, int width, int height);
    ~Renderer();

    Renderer(const Renderer&) = delete;
    Renderer& operator=(const Renderer&) = delete;
    Renderer(Renderer&&) = delete;
    Renderer& operator=(Renderer&&) = delete;

    // Drains pending window and keyboard events. Returns true when the user asked to quit:
    // Esc, q, closing the window or SIGINT (SDL turns Ctrl+C into a quit event).
    [[nodiscard]] bool pollQuitRequested();

    // Uploads the frame to the GPU, draws `overlayLines` on top and presents the result.
    // Throws RenderError on SDL failures or when the frame's format cannot be displayed.
    void render(const Frame& frame, const std::vector<std::string>& overlayLines = {});
    // Clears the window to black and shows `lines` (e.g. "NO SIGNAL") instead of video.
    void renderMessage(const std::vector<std::string>& lines);

private:
    // Calls SDL_Init in the constructor and SDL_Quit in the destructor.
    class SdlLibrary {
    public:
        SdlLibrary();
        ~SdlLibrary();
        SdlLibrary(const SdlLibrary&) = delete;
        SdlLibrary& operator=(const SdlLibrary&) = delete;
        SdlLibrary(SdlLibrary&&) = delete;
        SdlLibrary& operator=(SdlLibrary&&) = delete;
    };

    struct SdlDeleter {
        void operator()(SDL_Window* window) const noexcept;
        void operator()(SDL_Renderer* renderer) const noexcept;
        void operator()(SDL_Texture* texture) const noexcept;
    };

    void ensureTexture(const Frame& frame);
    void uploadFrame(const Frame& frame);
    void drawOverlay(const std::vector<std::string>& lines);
    [[nodiscard]] bool supportsTextureFormat(std::uint32_t sdlFormat) const;

    // Declaration order matters: members are destroyed in reverse order, so the texture goes
    // first, then the renderer, the window and finally SDL itself.
    SdlLibrary sdl_;
    std::unique_ptr<SDL_Window, SdlDeleter> window_;
    std::unique_ptr<SDL_Renderer, SdlDeleter> renderer_;
    std::unique_ptr<SDL_Texture, SdlDeleter> texture_;

    PixelFormat textureFormat_{PixelFormat::Yuyv};
    int textureWidth_{0};
    int textureHeight_{0};
};

} // namespace camview