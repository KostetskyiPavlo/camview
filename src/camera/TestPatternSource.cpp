#include "camera/TestPatternSource.hpp"

#include "util/Log.hpp"

#include <algorithm>
#include <array>
#include <cstdlib>
#include <stdexcept>
#include <thread>

namespace camview {

namespace {

struct YuvColor {
    std::uint8_t y;
    std::uint8_t u;
    std::uint8_t v;
};

// Classic SMPTE-like color bars in BT.601 limited range.
constexpr std::array<YuvColor, 8> kBars{{
    {235, 128, 128}, // white
    {210, 16, 146},  // yellow
    {170, 166, 16},  // cyan
    {145, 54, 34},   // green
    {106, 202, 222}, // magenta
    {81, 90, 240},   // red
    {41, 240, 110},  // blue
    {16, 128, 128},  // black
}};

constexpr YuvColor kLineColor{235, 128, 128};
constexpr std::uint32_t kScrollPixelsPerFrame = 4;
constexpr std::uint32_t kLinePixelsPerFrame = 3;
constexpr int kLineHalfThickness = 4;
constexpr double kMaxFps = 240.0;

} // namespace

TestPatternSource::TestPatternSource(int width, int height, double fps)
    : mode_{PixelFormat::Yuyv, width, height, fps} {
    if (width <= 0 || height <= 0) {
        throw std::invalid_argument("TestPatternSource: width and height must be positive");
    }
    if (width % 2 != 0) {
        throw std::invalid_argument("TestPatternSource: YUYV requires an even width");
    }
    if (fps <= 0.0 || fps > kMaxFps) {
        throw std::invalid_argument("TestPatternSource: fps must be in (0, 240]");
    }
    frameInterval_ = std::chrono::duration_cast<Duration>(std::chrono::duration<double>(1.0 / fps));
}

void TestPatternSource::open() {
    open_ = true;
    sequence_ = 0;
    nextFrameTime_ = Clock::now();
    log::info("Opened ", description(), ", mode ", toString(mode_));
}

void TestPatternSource::close() noexcept {
    open_ = false;
}

bool TestPatternSource::isOpen() const noexcept {
    return open_;
}

CameraMode TestPatternSource::mode() const {
    return mode_;
}

std::string TestPatternSource::description() const {
    return "test pattern (synthetic YUYV)";
}

CaptureStatus TestPatternSource::capture(Frame& frame, std::chrono::milliseconds timeout) {
    if (!open_) {
        return CaptureStatus::Disconnected;
    }

    const TimePoint deadline = Clock::now() + timeout;
    if (nextFrameTime_ > deadline) {
        std::this_thread::sleep_until(deadline);
        return CaptureStatus::Timeout;
    }
    std::this_thread::sleep_until(nextFrameTime_);

    frame.format = PixelFormat::Yuyv;
    frame.width = mode_.width;
    frame.height = mode_.height;
    frame.stride = mode_.width * 2;
    frame.sequence = sequence_;
    frame.captureTime = nextFrameTime_;
    renderPattern(frame);
    frame.dequeueTime = Clock::now();

    // Behave like a real sensor: it never waits for a slow reader. Missed frame slots are
    // skipped and still advance the sequence number, so the consumer can detect the gap.
    ++sequence_;
    nextFrameTime_ += frameInterval_;
    const TimePoint now = Clock::now();
    while (nextFrameTime_ <= now) {
        nextFrameTime_ += frameInterval_;
        ++sequence_;
    }

    return CaptureStatus::Ok;
}

void TestPatternSource::renderPattern(Frame& frame) const {
    const auto stride = static_cast<std::size_t>(frame.stride);
    const auto width = static_cast<std::uint32_t>(frame.width);
    const auto height = static_cast<std::uint32_t>(frame.height);
    frame.data.resize(stride * height);

    const std::uint32_t barWidth = std::max<std::uint32_t>(1U, width / kBars.size());
    const std::uint32_t scroll = (frame.sequence * kScrollPixelsPerFrame) % width;
    const auto lineY = static_cast<int>((frame.sequence * kLinePixelsPerFrame) % height);

    for (std::uint32_t y = 0; y < height; ++y) {
        std::uint8_t* row = frame.data.data() + static_cast<std::size_t>(y) * stride;
        const bool onLine = std::abs(static_cast<int>(y) - lineY) < kLineHalfThickness;

        // One YUYV macropixel = 2 pixels = 4 bytes: Y0 U Y1 V.
        for (std::uint32_t x = 0; x < width; x += 2) {
            const std::uint32_t bar =
                std::min<std::uint32_t>(((x + scroll) % width) / barWidth, kBars.size() - 1);
            const YuvColor& color = onLine ? kLineColor : kBars[bar];

            std::uint8_t* pixelPair = row + static_cast<std::size_t>(x) * 2;
            pixelPair[0] = color.y;
            pixelPair[1] = color.u;
            pixelPair[2] = color.y;
            pixelPair[3] = color.v;
        }
    }
}

} // namespace camview