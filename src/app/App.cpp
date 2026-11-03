#include "app/App.hpp"
#include "app/AdaptationPolicy.hpp"

#include "camera/TestPatternSource.hpp"
#include "camera/V4l2Camera.hpp"
#include "decode/MjpegDecoder.hpp"
#include "stats/CsvLogger.hpp"
#include "stats/PipelineStats.hpp"
#include "util/Clock.hpp"
#include "util/Log.hpp"

#include <condition_variable>
#include <csignal>
#include <cstdio>
#include <exception>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

namespace camview {

namespace {

// Short timeouts keep both loops responsive to quit requests even when no frames arrive.
constexpr auto kCaptureTimeout = std::chrono::milliseconds(100);
constexpr auto kTakeTimeout = std::chrono::milliseconds(100);

// Without a single valid frame for this long, tell the user instead of staying silent.
constexpr auto kStallThreshold = std::chrono::seconds(2);

// How often to try reopening a camera that has disappeared.
constexpr auto kReconnectInterval = std::chrono::seconds(1);

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
        config.device, CameraMode{config.format, config.width, config.height, config.fps},
        config.preferResolution ? ModePriority::Resolution : ModePriority::FrameRate);
}

double toSeconds(Duration duration) {
    return std::chrono::duration<double>(duration).count();
}

std::vector<std::string> makeOverlayLines(double captureFps, const StatsSummary& summary,
                                          std::uint64_t dropped) {
    char line[64];
    std::vector<std::string> lines;
    std::snprintf(line, sizeof(line), "CAP %.1f FPS  DISP %.1f FPS", captureFps,
                  summary.displayFps);
    lines.emplace_back(line);
    std::snprintf(line, sizeof(line), "AGE %.0f MS  MAX %.0f MS", summary.avgTotalMs,
                  summary.maxTotalMs);
    lines.emplace_back(line);
    std::snprintf(line, sizeof(line), "DEC %.1f MS  DROP %llu", summary.avgDecodeMs,
                  static_cast<unsigned long long>(dropped));
    lines.emplace_back(line);
    return lines;
}

const std::vector<std::string> kStartingLines{"STARTING"};
const std::vector<std::string> kNoSignalLines{"NO SIGNAL", "RECONNECTING"};
const std::vector<std::string> kNoFramesLines{"NO FRAMES"};

} // namespace

App::App(Config config) : config_(std::move(config)) {}

App::~App() = default;

int App::run() {
    // Window first, camera second: streaming starts only once the consumer is ready, so the
    // driver does not fill up with stale frames while SDL initialises.
    if (config_.display) {
        renderer_.emplace("camview", config_.width, config_.height);
        // Wayland shows a window only once something has been drawn into it.
        renderer_->renderMessage(kStartingLines);
    } else {
        std::signal(SIGINT, handleStopSignal);
        std::signal(SIGTERM, handleStopSignal);
        log::info("Display disabled; stop with Ctrl+C");
    }

    std::optional<CsvLogger> csv;
    if (!config_.statsCsvPath.empty()) {
        csv.emplace(config_.statsCsvPath);
        log::info("Writing per-frame timings to ", config_.statsCsvPath);
    }

    // Fail fast at start-up: a camera that is missing right away most likely means a wrong
    // --device, and a clear error is more useful than waiting forever. Once streaming has
    // started, a lost camera is treated as temporary and reopened by the capture thread.
    source_ = makeSource(config_);
    source_->open();
    cameraConnected_.store(true);
    log::info("Source: ", source_->description(), ", mode ", toString(source_->mode()));

    // From here on only the capture thread touches source_, until it is joined below.
    captureThread_ = std::jthread([this](std::stop_token token) { captureLoop(token); });

    MjpegDecoder decoder;
    PipelineStats stats;
    AdaptationPolicy adaptation;
    Frame received;
    Frame decoded;
    std::vector<std::string> overlayLines;
    std::uint32_t capturedAtWindowStart = capturedFrames_.load(std::memory_order_relaxed);
    TimePoint lastFrameTime = Clock::now();
    bool stallReported = false;
    bool wasConnected = true;
    int exitCode = 0;
    const auto extraDelay = std::chrono::milliseconds(config_.extraRenderDelayMs);

    while (!quitRequested()) {
        const bool connected = cameraConnected_.load();
        if (connected && !wasConnected) {
            // The camera has just come back: measure stalls and statistics from now on, not
            // from the last frame before the outage.
            lastFrameTime = Clock::now();
            stats = PipelineStats{};
            capturedAtWindowStart = capturedFrames_.load(std::memory_order_relaxed);
            stallReported = false;
            adaptation.reset();
        }
        wasConnected = connected;

        if (!slot_.take(received, kTakeTimeout)) {
            if (captureFailed_.load()) {
                log::error("Capture stopped after an unexpected error");
                exitCode = 1;
                break;
            }
            const Duration sinceLastFrame = Clock::now() - lastFrameTime;
            if (connected && !stallReported && sinceLastFrame > kStallThreshold) {
                log::warning("No valid frames for ", toSeconds(sinceLastFrame),
                             " s: the camera stalled or every frame is being rejected");
                stallReported = true;
            }
            if (renderer_ && (!connected || sinceLastFrame > kStallThreshold)) {
                renderer_->renderMessage(connected ? kNoFramesLines : kNoSignalLines);
            }
            continue;
        }

        const TimePoint taken = Clock::now();
        if (stallReported) {
            log::info("Frames are flowing again after ", toSeconds(taken - lastFrameTime), " s");
            stallReported = false;
        }
        lastFrameTime = taken;

        const Frame* frameToShow = &received;
        Duration decodeTime{};
        if (received.format == PixelFormat::Mjpeg) {
            if (!decoder.decode(received, decoded)) {
                continue;
            }
            decodeTime = Clock::now() - taken;
            frameToShow = &decoded;
        }

        const TimePoint renderStart = Clock::now();
        if (renderer_) {
            renderer_->render(*frameToShow, overlayLines);
        }
        const TimePoint presented = Clock::now();

        const FrameTimings timings{
            received.dequeueTime - received.captureTime, // transfer
            taken - received.dequeueTime,                // queue
            decodeTime,                                  // decode
            presented - renderStart,                     // render
            presented - received.captureTime,            // total
        };
        stats.addFrame(timings, presented);
        if (csv) {
            csv->write(received.sequence, timings, slot_.dropped());
        }

        if (extraDelay.count() > 0) {
            std::this_thread::sleep_for(extraDelay); // Test aid: simulate a slow consumer.
        }

        if (const auto summary = stats.poll(presented)) {
            // Unsigned subtraction stays correct even after the counter wraps around.
            const std::uint32_t captured = capturedFrames_.load(std::memory_order_relaxed);
            const double captureFps =
                static_cast<double>(captured - capturedAtWindowStart) / summary->windowSeconds;
            capturedAtWindowStart = captured;
            const std::uint64_t dropped = slot_.dropped();

            log::info("Capture ", captureFps, " fps, display ", summary->displayFps,
                      " fps | age avg ", summary->avgTotalMs, " max ", summary->maxTotalMs,
                      " ms = transfer ", summary->avgTransferMs, " + queue ",
                      summary->avgQueueMs, " + decode ", summary->avgDecodeMs, " + render ",
                      summary->avgRenderMs, " | dropped ", dropped);
            overlayLines = makeOverlayLines(captureFps, *summary, dropped);

            const double processingMs = summary->avgDecodeMs + summary->avgRenderMs;
            if (config_.adapt && canAdapt_.load() &&
                adaptation.update(captureFps, summary->displayFps, processingMs)) {
                log::warning("Showing ", summary->displayFps, " of ", captureFps,
                             " fps: decode + render take ", processingMs,
                             " ms of the camera's ", 1000.0 / captureFps,
                             " ms frame interval; requesting a smaller mode");
                downgradeRequested_.store(true);
            }
        }
    }

    const TimePoint stopStart = Clock::now();
    captureThread_.request_stop();
    captureThread_.join();
    log::info("Capture thread stopped in ", toMilliseconds(Clock::now() - stopStart), " ms");
    log::info("Stopped: ", slot_.dropped(), " frames dropped in favour of newer ones, ",
              decoder.corruptFrames(), " corrupt MJPEG frames");
    source_->close();
    return exitCode;
}

void App::captureLoop(const std::stop_token& stopToken) {
    try {
        Frame frame;
        TimePoint lostAt{};
        int failedAttempts = 0;
        std::string lastError;
        std::mutex waitMutex;
        std::condition_variable_any waitCondition;

        while (!stopToken.stop_requested()) {
            if (!source_->isOpen()) {
                try {
                    source_->open();
                    cameraConnected_.store(true);
                    log::info("Camera reconnected after ", toSeconds(Clock::now() - lostAt),
                              " s (", failedAttempts + 1, " attempts)");
                    failedAttempts = 0;
                    lastError.clear();
                } catch (const CameraError& e) {
                    // Log only when the reason changes: one line per phase of the outage
                    // (device gone, device busy while re-initialising...), not one per second.
                    if (lastError != e.what()) {
                        log::warning("Reopening failed: ", e.what(), "; retrying every second");
                        lastError = e.what();
                    }
                    ++failedAttempts;
                    // Sleeps for the retry interval but wakes up at once on a stop request.
                    std::unique_lock lock(waitMutex);
                    static_cast<void>(waitCondition.wait_for(lock, stopToken, kReconnectInterval,
                                                             [] { return false; }));
                }
                continue;
            }

            if (downgradeRequested_.exchange(false)) {
                const CameraMode before = source_->mode();
                if (!source_->stepDown()) {
                    log::info("Already at the smallest mode with this frame rate; "
                              "cannot adapt further");
                    canAdapt_.store(false);
                } else {
                    try {
                        source_->close();
                        source_->open();
                        log::info("Adapted: ", toString(before), " -> ",
                                  toString(source_->mode()));
                    } catch (const CameraError& e) {
                        // Hand over to the reconnect logic above.
                        log::warning("Reopening in a smaller mode failed: ", e.what());
                        cameraConnected_.store(false);
                        lostAt = Clock::now();
                        failedAttempts = 0;
                        lastError.clear();
                    }
                    continue;
                }
            }

            const CaptureStatus status = source_->capture(frame, kCaptureTimeout);
            if (status == CaptureStatus::Ok) {
                capturedFrames_.fetch_add(1, std::memory_order_relaxed);
                slot_.publish(frame);
            } else if (status == CaptureStatus::Disconnected) {
                log::warning("Camera lost; trying to reconnect");
                cameraConnected_.store(false);
                source_->close();
                lostAt = Clock::now();
                failedAttempts = 0;
                lastError.clear();
            }
        }
    } catch (const std::exception& e) {
        // An exception escaping a thread function would call std::terminate.
        log::error("Capture thread failed: ", e.what());
        captureFailed_.store(true);
        slot_.close(); // Wakes the main thread immediately instead of after a timeout.
    }
}

bool App::quitRequested() {
    // With a window, SDL turns Esc, q, closing the window and Ctrl+C into quit events.
    const bool quit = renderer_ ? renderer_->pollQuitRequested() : gStopRequested != 0;
    if (quit) {
        log::info("Quit requested");
    }
    return quit;
}

} // namespace camview