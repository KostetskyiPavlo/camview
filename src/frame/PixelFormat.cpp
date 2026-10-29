#include "frame/PixelFormat.hpp"

namespace camview {

namespace {

constexpr std::uint32_t kFourccYuyv = makeFourcc('Y', 'U', 'Y', 'V');
constexpr std::uint32_t kFourccMjpeg = makeFourcc('M', 'J', 'P', 'G');
constexpr std::uint32_t kFourccI420 = makeFourcc('Y', 'U', '1', '2');

} // namespace

std::uint32_t toFourcc(PixelFormat format) {
    switch (format) {
    case PixelFormat::Yuyv:
        return kFourccYuyv;
    case PixelFormat::Mjpeg:
        return kFourccMjpeg;
    case PixelFormat::I420:
        return kFourccI420;
    }
    return 0;
}

std::optional<PixelFormat> pixelFormatFromFourcc(std::uint32_t fourcc) {
    switch (fourcc) {
    case kFourccYuyv:
        return PixelFormat::Yuyv;
    case kFourccMjpeg:
        return PixelFormat::Mjpeg;
    case kFourccI420:
        return PixelFormat::I420;
    default:
        return std::nullopt;
    }
}

std::string_view toString(PixelFormat format) {
    switch (format) {
    case PixelFormat::Yuyv:
        return "YUYV";
    case PixelFormat::Mjpeg:
        return "MJPG";
    case PixelFormat::I420:
        return "I420";
    }
    return "????";
}

std::string fourccToString(std::uint32_t fourcc) {
    std::string result(4, '.');
    for (std::size_t i = 0; i < result.size(); ++i) {
        const auto byte = static_cast<unsigned char>((fourcc >> (8U * i)) & 0xFFU);
        if (byte >= 0x20U && byte < 0x7FU) {
            result[i] = static_cast<char>(byte);
        }
    }
    return result;
}

} // namespace camview