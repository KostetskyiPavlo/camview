#include "camera/V4l2Camera.hpp"
#include "decode/MjpegDecoder.hpp"
#include "render/Renderer.hpp"
#include "stats/FpsCounter.hpp"
#include "util/Clock.hpp"
#include "util/Log.hpp"

#include <exception>
#include <string>
#include <string_view>

namespace camview {

// Temporary loop for step 7. Usage: camview [device] [yuyv|mjpg] [width] [height] [fps]
// Proper CLI parsing and validation come in step 8.
int run(int argc, char** argv) {
    const std::string device = argc > 1 ? argv[1] : "/dev/video0";
    const PixelFormat format = (argc > 2 && std::string_view(argv[2]) == "yuyv")
                                   ? PixelFormat::Yuyv
                                   : PixelFormat::Mjpeg;
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
    MjpegDecoder decoder;

    constexpr auto kCaptureTimeout = std::chrono::milliseconds(100);

    Frame captured;
    Frame decoded;
    FpsCounter captureFps;
    Duration decodeTime{};
    int decodedInWindow = 0;

    while (!renderer.pollQuitRequested()) {
        const CaptureStatus status = camera.capture(captured, kCaptureTimeout);
        if (status == CaptureStatus::Disconnected) {
            log::error("Camera disconnected");
            return 1;
        }
        if (status == CaptureStatus::Timeout) {
            continue;
        }

        const Frame* frameToShow = &captured;
        if (captured.format == PixelFormat::Mjpeg) {
            const TimePoint decodeStart = Clock::now();
            if (!decoder.decode(captured, decoded)) {
                continue;
            }
            decodeTime += Clock::now() - decodeStart;
            ++decodedInWindow;
            frameToShow = &decoded;
        }

        if (renderEnabled) {
            renderer.render(*frameToShow);
        }

        const TimePoint now = Clock::now();
        if (const auto measuredFps = captureFps.tick(now)) {
            const double decodeMs =
                decodedInWindow > 0 ? toMilliseconds(decodeTime) / decodedInWindow : 0.0;
            log::info("Measured ", *measuredFps, " fps (driver reports ", mode.fps,
                      "), frame age ", toMilliseconds(now - frameToShow->captureTime),
                      " ms, decode ", decodeMs, " ms");
            decodeTime = Duration{};
            decodedInWindow = 0;
        }
    }

    log::info("Quit requested, exiting; corrupt MJPEG frames: ", decoder.corruptFrames());
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