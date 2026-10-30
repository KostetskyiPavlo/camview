#pragma once

#include "frame/PixelFormat.hpp"
#include "util/Clock.hpp"

#include <cstddef>
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
    int stride{0}; // Bytes per row of the first plane (YUYV: may exceed width * 2). 0 for MJPEG.

    std::uint32_t sequence{0}; // Driver frame counter. Gaps mean frames were lost upstream.
    TimePoint captureTime{};   // When the device produced the frame.
    TimePoint dequeueTime{};   // When our process received it.

    std::vector<std::uint8_t> data;
};

// Memory layout of an I420 frame stored contiguously: Y plane, then U, then V.
struct I420Layout {
    int chromaWidth;
    int chromaHeight;
    std::size_t lumaBytes;
    std::size_t chromaBytes; // Size of one chroma plane.
    std::size_t totalBytes;
};

inline I420Layout i420Layout(int width, int height) {
    const int chromaWidth = (width + 1) / 2;
    const int chromaHeight = (height + 1) / 2;
    const auto lumaBytes = static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
    const auto chromaBytes =
        static_cast<std::size_t>(chromaWidth) * static_cast<std::size_t>(chromaHeight);
    return {chromaWidth, chromaHeight, lumaBytes, chromaBytes, lumaBytes + 2 * chromaBytes};
}

} // namespace camview