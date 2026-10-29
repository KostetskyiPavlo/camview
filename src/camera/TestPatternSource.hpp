#pragma once

#include "camera/CameraSource.hpp"
#include "util/Clock.hpp"

#include <cstdint>

namespace camview {

// Synthetic YUYV camera: moving color bars at a steady frame rate.
// Lets the whole pipeline run without hardware (WSL, CI, demos).
class TestPatternSource final : public CameraSource {
public:
    // Throws std::invalid_argument for non-positive sizes, odd width or fps outside (0, 240].
    TestPatternSource(int width, int height, double fps);

    void open() override;
    void close() noexcept override;
    [[nodiscard]] bool isOpen() const noexcept override;
    [[nodiscard]] CameraMode mode() const override;
    [[nodiscard]] std::string description() const override;
    [[nodiscard]] CaptureStatus capture(Frame& frame, std::chrono::milliseconds timeout) override;

private:
    void renderPattern(Frame& frame) const;

    CameraMode mode_;
    Duration frameInterval_;
    TimePoint nextFrameTime_{};
    std::uint32_t sequence_{0};
    bool open_{false};
};

} // namespace camview