#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace camview {

enum class PixelFormat {
    Yuyv,  // Packed 4:2:2, 2 bytes per pixel. Raw, uncompressed camera output.
    Mjpeg, // One JPEG image per frame. Compressed camera output.
    I420,  // Planar 4:2:0 (Y plane, then U, then V). Output of the MJPEG decoder.
};

constexpr std::uint32_t makeFourcc(char a, char b, char c, char d) {
    return static_cast<std::uint32_t>(static_cast<unsigned char>(a)) |
           (static_cast<std::uint32_t>(static_cast<unsigned char>(b)) << 8U) |
           (static_cast<std::uint32_t>(static_cast<unsigned char>(c)) << 16U) |
           (static_cast<std::uint32_t>(static_cast<unsigned char>(d)) << 24U);
}

std::uint32_t toFourcc(PixelFormat format);
std::optional<PixelFormat> pixelFormatFromFourcc(std::uint32_t fourcc);

std::string_view toString(PixelFormat format);

// Renders any FourCC as 4 printable characters, e.g. "MJPG". Unknown bytes become '.'.
std::string fourccToString(std::uint32_t fourcc);

} // namespace camview