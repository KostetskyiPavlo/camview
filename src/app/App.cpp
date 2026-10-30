#include "app/App.hpp"

#include "camera/TestPatternSource.hpp"
#include "camera/V4l2Camera.hpp"
#include "decode/MjpegDecoder.hpp"
#include "stats/FpsCounter.hpp"
#include "util/Clock.hpp"
#include "util/Log.hpp"

#include <csignal>
#include <exception>
#include <utility>

namespace camview {

namespace {

// Short timeouts keep both loops responsive to quit requests even when no frames arrive.
constexpr auto kCaptureTimeout = std::chrono::milliseconds(100);
constexpr auto kTakeTimeout = std::chrono::milliseconds(100);

static_assert(std::atomic<std::uint32_t>::is_always_lock_free,
              "the frame counter must be lock-free on every target");

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

} // namespace

App::App(Config config) : config_(std::move(config)) {}

App::~App() = default;

int App::run() {
    // Window first, camera second: streaming starts only once the consumer is ready, so the
    // driver does not fill up with stale frames while SDL initialises.
    if (config_.display) {
        renderer_.emplace("camview", config_.width, config_.height);
    } else {
        std::signal(SIGINT, handleStopSignal);
        std::signal(SIGTERM, handleStopSignal);
        log::info("Display disabled; stop with Ctrl+C");
    }

    source_ = makeSource(config_);
    source_->open();
    const CameraMode mode = source_->mode();
    log::info("Source: ", source_->description(), ", mode ", toString(mode));

    // From here on only the capture thread touches source_, until it is joined below.
    captureThread_ = std::jthread([this](std::stop_token token) { captureLoop(token); });

    MjpegDecoder decoder;
    Frame received;
    Frame decoded;
    FpsCounter displayFps;
    std::uint32_t capturedAtWindowStart = 0;
    TimePoint windowStart = Clock::now();
    Duration decodeTime{};
    int decodedInWindow = 0;
    int exitCode = 0;
    const auto extraDelay = std::chrono::milliseconds(config_.extraRenderDelayMs);

    while (!quitRequested()) {
        if (!slot_.take(received, kTakeTimeout)) {
            if (cameraLost_.load()) {
                log::error("Camera disconnected");
                exitCode = 1;
                break;
            }
            continue;
        }

        const Frame* frameToShow = &received;
        if (received.format == PixelFormat::Mjpeg) {
            const TimePoint decodeStart = Clock::now();
            if (!decoder.decode(received, decoded)) {
                continue;
            }
            decodeTime += Clock::now() - decodeStart;
            ++decodedInWindow;
            frameToShow = &decoded;
        }

        if (renderer_) {
            renderer_->render(*frameToShow);
        }
        const TimePoint presented = Clock::now();
        const double ageMs = toMilliseconds(presented - frameToShow->captureTime);

        if (extraDelay.count() > 0) {
            std::this_thread::sleep_for(extraDelay); // Test aid: simulate a slow consumer.
        }

        if (const auto displayedFps = displayFps.tick(presented)) {
            // Unsigned subtraction stays correct even after the counter wraps around.
            const std::uint32_t captured = capturedFrames_.load(std::memory_order_relaxed);
            const double seconds = std::chrono::duration<double>(presented - windowStart).count();
            const double captureFps =
                static_cast<double>(captured - capturedAtWindowStart) / seconds;

            if (decodedInWindow > 0) {
                log::info("Capture ", captureFps, " fps, display ", *displayedFps,
                          " fps (driver reports ", mode.fps, "), frame age ", ageMs,
                          " ms, decode ", toMilliseconds(decodeTime) / decodedInWindow,
                          " ms, dropped so far ", slot_.dropped());
            } else {
                log::info("Capture ", captureFps, " fps, display ", *displayedFps,
                          " fps (driver reports ", mode.fps, "), frame age ", ageMs,
                          " ms, dropped so far ", slot_.dropped());
            }

            capturedAtWindowStart = captured;
            windowStart = presented;
            decodeTime = Duration{};
            decodedInWindow = 0;
        }
    }

    captureThread_.request_stop();
    captureThread_.join();
    log::info("Stopped: ", slot_.dropped(), " frames dropped in favour of newer ones, ",
              decoder.corruptFrames(), " corrupt MJPEG frames");
    source_->close();
    return exitCode;
}

void App::captureLoop(const std::stop_token& stopToken) {
    try {
        Frame frame;
        while (!stopToken.stop_requested()) {
            const CaptureStatus status = source_->capture(frame, kCaptureTimeout);
            if (status == CaptureStatus::Ok) {
                capturedFrames_.fetch_add(1, std::memory_order_relaxed);
                slot_.publish(frame);
            } else if (status == CaptureStatus::Disconnected) {
                break;
            }
        }
        if (stopToken.stop_requested()) {
            return;
        }
    } catch (const std::exception& e) {
        // An exception escaping a thread function would call std::terminate.
        log::error("Capture thread failed: ", e.what());
    }
    cameraLost_.store(true);
    slot_.close(); // Wakes the main thread immediately instead of after a timeout.
}

bool App::quitRequested() {
    // With a window, SDL turns Esc, q, closing the window and Ctrl+C into quit events.
    if (renderer_) {
        return renderer_->pollQuitRequested();
    }
    return gStopRequested != 0;
}

} // namespace camview