#include "camera/V4l2Camera.hpp"
#include "render/Renderer.hpp"
#include "stats/FpsCounter.hpp"
#include "util/Clock.hpp"
#include "util/Log.hpp"

#include <cstdint>
#include <exception>
#include <string>
#include <string_view>

namespace camview {

// Temporary loop for step 6. Usage: camview [device] [yuyv|mjpg] [width] [height] [fps]
// Proper CLI parsing and validation come in step 8.
int run(int argc, char** argv) {
    const std::string device = argc > 1 ? argv[1] : "/dev/video0";
    const PixelFormat format = (argc > 2 && std::string_view(argv[2]) == "mjpg")
                                   ? PixelFormat::Mjpeg
                                   : PixelFormat::Yuyv;
    const int width = argc > 3 ? std::stoi(argv[3]) : 640;
    const int height = argc > 4 ? std::stoi(argv[4]) : 480;
    const double fps = argc > 5 ? std::stod(argv[5]) : 30.0;
        // Temporary diagnostic switch for the corrupt-frame investigation.
    const bool renderEnabled = !(argc > 6 && std::string_view(argv[6]) == "norender");

    log::info("camview ", CAMVIEW_VERSION);

    V4l2Camera camera(device, CameraMode{format, width, height, fps});
    camera.open();
    const CameraMode mode = camera.mode();

    Renderer renderer("camview", mode.width, mode.height);
    if (mode.format != PixelFormat::Yuyv) {
        log::warning("MJPG frames are captured and counted but not displayed yet");
    }

    constexpr auto kCaptureTimeout = std::chrono::milliseconds(100);

    Frame frame;
    FpsCounter captureFps;
    std::uint32_t lastSequence = 0;
    bool haveSequence = false;
    std::uint64_t lostByDriver = 0;

    while (!renderer.pollQuitRequested()) {
        const CaptureStatus status = camera.capture(frame, kCaptureTimeout);
        if (status == CaptureStatus::Disconnected) {
            log::error("Camera disconnected");
            return 1;
        }
        if (status == CaptureStatus::Timeout) {
            continue;
        }

        // A gap in driver sequence numbers means frames were lost before reaching us.
        if (haveSequence && frame.sequence != lastSequence + 1) {
            lostByDriver += frame.sequence - lastSequence - 1;
        }
        lastSequence = frame.sequence;
        haveSequence = true;

        if (frame.format == PixelFormat::Yuyv) {
            if (renderEnabled) {
                renderer.render(frame);
            }
        }

        const TimePoint now = Clock::now();
        if (const auto measuredFps = captureFps.tick(now)) {
            log::info("Measured ", *measuredFps, " fps (driver reports ", mode.fps,
                      "), frame age ", toMilliseconds(now - frame.captureTime),
                      " ms, gaps in sequence ", lostByDriver);
        }
    }

    log::info("Quit requested, exiting");
    camera.close();
    return 0;
}

} // namespace camview

int main(int argc, char** argv) {
    try {
        return camview::run(argc, argv);
    } catch (const std::exception& e) {
        camview::log::error("Fatal: ", e.what());
        return 1;
    }
}