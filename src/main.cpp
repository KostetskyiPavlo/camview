#include "camera/TestPatternSource.hpp"
#include "render/Renderer.hpp"
#include "util/Clock.hpp"
#include "util/Log.hpp"

#include <exception>

namespace camview {

// Temporary single-threaded loop for step 4; replaced by App in later steps.
int run() {
    log::info("camview ", CAMVIEW_VERSION);

    TestPatternSource source(640, 480, 30.0);
    source.open();
    log::info("Source: ", source.description());
    log::info("Mode:   ", toString(source.mode()));

    const CameraMode mode = source.mode();
    Renderer renderer("camview", mode.width, mode.height);

    // Short timeout keeps the window responsive even if the source stalls.
    constexpr auto kCaptureTimeout = std::chrono::milliseconds(100);

    Frame frame;
    int framesInWindow = 0;
    TimePoint windowStart = Clock::now();

    while (!renderer.pollQuitRequested()) {
        const CaptureStatus status = source.capture(frame, kCaptureTimeout);
        if (status == CaptureStatus::Disconnected) {
            log::error("Source disconnected");
            return 1;
        }
        if (status == CaptureStatus::Timeout) {
            continue;
        }

        renderer.render(frame);
        ++framesInWindow;

        const TimePoint now = Clock::now();
        if (now - windowStart >= std::chrono::seconds(1)) {
            const double seconds = std::chrono::duration<double>(now - windowStart).count();
            log::info("Rendered ", framesInWindow / seconds, " fps");
            framesInWindow = 0;
            windowStart = now;
        }
    }

    log::info("Quit requested, exiting");
    source.close();
    return 0;
}

} // namespace camview

int main() {
    try {
        return camview::run();
    } catch (const std::exception& e) {
        camview::log::error("Fatal: ", e.what());
        return 1;
    }
}