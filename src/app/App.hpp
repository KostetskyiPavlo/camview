#pragma once

#include "app/Config.hpp"
#include "camera/CameraSource.hpp"
#include "frame/LatestFrameSlot.hpp"
#include "render/Renderer.hpp"

#include <atomic>
#include <cstdint>
#include <memory>
#include <optional>
#include <stop_token>
#include <thread>

namespace camview {

// Owns the whole pipeline:
//
//   capture thread:  camera --> LatestFrameSlot   (newest frame wins, older ones are dropped)
//   main thread:     LatestFrameSlot --> MJPEG decode --> Renderer
//
// Frames are dropped before decoding, so frames nobody will see cost no CPU time.
class App {
public:
    explicit App(Config config);
    ~App();

    App(const App&) = delete;
    App& operator=(const App&) = delete;
    App(App&&) = delete;
    App& operator=(App&&) = delete;

    // Runs until the user quits or the camera fails. Returns the process exit code.
    // Throws CameraError or RenderError if the pipeline cannot start.
    int run();

private:
    void captureLoop(const std::stop_token& stopToken);
    [[nodiscard]] bool quitRequested();

    Config config_;
    LatestFrameSlot slot_;
    std::unique_ptr<CameraSource> source_;
    std::optional<Renderer> renderer_;

    // Statistic only, so relaxed ordering is enough. 32-bit on purpose: on 32-bit ARM (ARMv6
    // target) 64-bit atomics go through libatomic and a lock; 32-bit ones are always lock-free.
    std::atomic<std::uint32_t> capturedFrames_{0};
    std::atomic<bool> cameraConnected_{false}; // false while the capture thread reconnects.
    std::atomic<bool> captureFailed_{false};   // Unexpected error: the capture thread gave up.
    std::atomic<bool> downgradeRequested_{false}; // Main thread asks for a smaller mode.
    std::atomic<bool> canAdapt_{true};            // false once the smallest mode is reached.

    // Last member on purpose: destroyed first, so the thread is stopped and joined before the
    // camera and the slot it uses go away, even when run() exits with an exception.
    std::jthread captureThread_;
};

} // namespace camview