#pragma once

#include "frame/PixelFormat.hpp"
#include "util/Clock.hpp"

#include <cstdint>
#include <vector>

namespace camview {

// One video frame plus the metadata needed to measure latency.
// `data` is reused between captures: resize() within existing capacity does not allocate,
// so a long-lived Frame costs no heap allocations per frame.
struct Frame {
    PixelFormat format{PixelFormat::Yuyv};
    int width{0};
    int height{0};
    int stride{0}; // Bytes per row for packed formats (may exceed width * 2). 0 for MJPEG.

    std::uint32_t sequence{0}; // Driver frame counter. Gaps mean frames were lost upstream.
    TimePoint captureTime{};   // When the device produced the frame.
    TimePoint dequeueTime{};   // When our process received it.

    std::vector<std::uint8_t> data;
};

} // namespace camview