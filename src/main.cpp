#include "app/CliParser.hpp"
#include "camera/TestPatternSource.hpp"
#include "camera/V4l2Camera.hpp"
#include "decode/MjpegDecoder.hpp"
#include "render/Renderer.hpp"
#include "stats/FpsCounter.hpp"
#include "util/Clock.hpp"
#include "util/Log.hpp"

#include <csignal>
#include <cstdio>
#include <exception>
#include <memory>
#include <optional>

namespace camview {

namespace {

// Set from a signal handler, so only an async-signal-safe type may be used.
volatile std::sig_atomic_t gStopRequested = 0;

void handleStopSignal(int /*signal*/) {
    gStopRequested = 1;
}

std::unique_ptr<CameraSource> makeSource(const Config& config) {
    if (config.testPattern) {
        return std::make_unique<TestPatternSource>(config.width, config.height, config.fps);
    }
    return std::make_unique<V4l2Camera>(
        config.device, CameraMode{config.format, config.width, config.height, config.fps});
}

bool quitRequested(std::optional<Renderer>& renderer) {
    // With a window, SDL turns Esc, q, closing the window and Ctrl+C into quit events.
    if (renderer) {
        return renderer->pollQuitRequested();
    }
    return gStopRequested != 0;
}

// Temporary single-threaded loop; the capture thread and App arrive in step 9.
int run(const Config& config) {
    log::info("camview ", CAMVIEW_VERSION);

    const std::unique_ptr<CameraSource> source = makeSource(config);
    source->open();
    const CameraMode mode = source->mode();
    log::info("Source: ", source->description(), ", mode ", toString(mode));

    std::optional<Renderer> renderer;
    if (config.display) {
        renderer.emplace("camview", mode.width, mode.height);
    } else {
        std::signal(SIGINT, handleStopSignal);
        std::signal(SIGTERM, handleStopSignal);
        log::info("Display disabled; stop with Ctrl+C");
    }

    MjpegDecoder decoder;
    constexpr auto kCaptureTimeout = std::chrono::milliseconds(100);

    Frame captured;
    Frame decoded;
    FpsCounter captureFps;
    Duration decodeTime{};
    int decodedInWindow = 0;

    while (!quitRequested(renderer)) {
        const CaptureStatus status = source->capture(captured, kCaptureTimeout);
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

        if (renderer) {
            renderer->render(*frameToShow);
        }

        const TimePoint now = Clock::now();
        if (const auto measuredFps = captureFps.tick(now)) {
            const double ageMs = toMilliseconds(now - frameToShow->captureTime);
            if (decodedInWindow > 0) {
                log::info("Measured ", *measuredFps, " fps (driver reports ", mode.fps,
                          "), frame age ", ageMs, " ms, decode ",
                          toMilliseconds(decodeTime) / decodedInWindow, " ms");
            } else {
                log::info("Measured ", *measuredFps, " fps (driver reports ", mode.fps,
                          "), frame age ", ageMs, " ms");
            }
            decodeTime = Duration{};
            decodedInWindow = 0;
        }
    }

    log::info("Quit requested, exiting; corrupt MJPEG frames: ", decoder.corruptFrames());
    source->close();
    return 0;
}

} // namespace

} // namespace camview

int main(int argc, char** argv) {
    using camview::CliResult;

    const CliResult cli = camview::parseCommandLine(argc, argv);
    switch (cli.action) {
    case CliResult::Action::Help:
        std::fputs(camview::usageText(argv[0]).c_str(), stdout);
        return 0;
    case CliResult::Action::Version:
        std::printf("camview %s\n", CAMVIEW_VERSION);
        return 0;
    case CliResult::Action::Error:
        std::fprintf(stderr, "camview: %s\nTry '%s --help' for more information.\n",
                     cli.error.c_str(), argv[0]);
        return 2;
    case CliResult::Action::Run:
        break;
    }

    try {
        return camview::run(cli.config);
    } catch (const std::exception& e) {
        camview::log::error("Fatal: ", e.what());
        return 1;
    }
}