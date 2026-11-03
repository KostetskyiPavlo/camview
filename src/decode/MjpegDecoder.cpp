#include "decode/MjpegDecoder.hpp"
#include "util/Log.hpp"

#include <turbojpeg.h>

#include <cstring>
#include <stdexcept>
#include <string>

namespace camview {

namespace {

const char* subsamplingName(int subsampling) {
    switch (subsampling) {
    case TJSAMP_444:
        return "4:4:4";
    case TJSAMP_422:
        return "4:2:2";
    case TJSAMP_420:
        return "4:2:0";
    case TJSAMP_GRAY:
        return "grayscale";
    case TJSAMP_440:
        return "4:4:0";
    case TJSAMP_411:
        return "4:1:1";
    default:
        return "unknown";
    }
}

// Nearest-neighbour resize of one chroma plane, without a division per pixel: the default
// 32-bit Raspberry Pi OS target (ARMv6) has no hardware divide, so every '/' becomes a slow
// library call. 4:2:2 -> 4:2:0, the common case for UVC cameras, keeps the width and becomes
// a plain row copy.
void resamplePlane(const std::uint8_t* source, int sourceWidth, int sourceHeight,
                   std::uint8_t* target, int targetWidth, int targetHeight) {
    // 16.16 fixed-point step through the source row, computed once per plane.
    const std::uint32_t step = (static_cast<std::uint32_t>(sourceWidth) << 16U) /
                               static_cast<std::uint32_t>(targetWidth);

    for (int y = 0; y < targetHeight; ++y) {
        const std::uint8_t* sourceRow = source + (y * sourceHeight / targetHeight) * sourceWidth;
        std::uint8_t* targetRow = target + y * targetWidth;

        if (sourceWidth == targetWidth) {
            std::memcpy(targetRow, sourceRow, static_cast<std::size_t>(targetWidth));
            continue;
        }
        std::uint32_t position = 0;
        for (int x = 0; x < targetWidth; ++x) {
            targetRow[x] = sourceRow[position >> 16U];
            position += step;
        }
    }
}

} // namespace

void MjpegDecoder::HandleDeleter::operator()(void* handle) const noexcept {
    tjDestroy(handle);
}

MjpegDecoder::MjpegDecoder() : handle_(tjInitDecompress()) {
    if (!handle_) {
        throw std::runtime_error(std::string("tjInitDecompress failed: ") +
                                 tjGetErrorStr2(nullptr));
    }
}

MjpegDecoder::~MjpegDecoder() = default;

bool MjpegDecoder::decode(const Frame& jpeg, Frame& i420) {
    tjhandle handle = handle_.get();
    const unsigned char* jpegData = jpeg.data.data();
    const auto jpegSize = static_cast<unsigned long>(jpeg.data.size());

    int width = 0;
    int height = 0;
    int subsampling = 0;
    int colorspace = 0;
    if (tjDecompressHeader3(handle, jpegData, jpegSize, &width, &height, &subsampling,
                            &colorspace) != 0) {
        reportCorrupt("header");
        return false;
    }

    if (subsampling != lastSubsampling_) {
        log::info("MJPEG stream: ", width, "x", height, ", chroma ",
                  subsamplingName(subsampling));
        lastSubsampling_ = subsampling;
    }

    const I420Layout layout = i420Layout(width, height);
    i420.data.resize(layout.totalBytes);
    std::uint8_t* planeY = i420.data.data();
    std::uint8_t* planeU = planeY + layout.lumaBytes;
    std::uint8_t* planeV = planeU + layout.chromaBytes;

    // A TJERR_WARNING means the data had minor damage but an image was still produced.
    const auto decodedOk = [handle](int result) {
        return result == 0 || tjGetErrorCode(handle) == TJERR_WARNING;
    };

    if (subsampling == TJSAMP_420) {
        // Fast path: the JPEG is already 4:2:0, decode straight into the output planes.
        unsigned char* planes[3] = {planeY, planeU, planeV};
        int strides[3] = {width, layout.chromaWidth, layout.chromaWidth};
        if (!decodedOk(tjDecompressToYUVPlanes(handle, jpegData, jpegSize, planes, width,
                                               strides, height, TJFLAG_FASTDCT))) {
            reportCorrupt("decode");
            return false;
        }
    } else if (subsampling == TJSAMP_GRAY) {
        unsigned char* planes[3] = {planeY, nullptr, nullptr};
        int strides[3] = {width, 0, 0};
        if (!decodedOk(tjDecompressToYUVPlanes(handle, jpegData, jpegSize, planes, width,
                                               strides, height, TJFLAG_FASTDCT))) {
            reportCorrupt("decode");
            return false;
        }
        std::memset(planeU, 128, 2 * layout.chromaBytes); // Neutral chroma = gray.
    } else {
        // Decode with the JPEG's own chroma layout (usually 4:2:2), then resample to 4:2:0.
        const int sourceChromaWidth = tjPlaneWidth(1, width, subsampling);
        const int sourceChromaHeight = tjPlaneHeight(1, height, subsampling);
        if (sourceChromaWidth <= 0 || sourceChromaHeight <= 0) {
            reportCorrupt("chroma layout");
            return false;
        }
        const auto sourceChromaBytes = static_cast<std::size_t>(sourceChromaWidth) *
                                       static_cast<std::size_t>(sourceChromaHeight);
        chromaScratch_.resize(2 * sourceChromaBytes);
        std::uint8_t* scratchU = chromaScratch_.data();
        std::uint8_t* scratchV = scratchU + sourceChromaBytes;

        unsigned char* planes[3] = {planeY, scratchU, scratchV};
        int strides[3] = {width, sourceChromaWidth, sourceChromaWidth};
        if (!decodedOk(tjDecompressToYUVPlanes(handle, jpegData, jpegSize, planes, width,
                                               strides, height, TJFLAG_FASTDCT))) {
            reportCorrupt("decode");
            return false;
        }
        resamplePlane(scratchU, sourceChromaWidth, sourceChromaHeight, planeU,
                      layout.chromaWidth, layout.chromaHeight);
        resamplePlane(scratchV, sourceChromaWidth, sourceChromaHeight, planeV,
                      layout.chromaWidth, layout.chromaHeight);
    }

    i420.format = PixelFormat::I420;
    i420.width = width;
    i420.height = height;
    i420.stride = width;
    i420.sequence = jpeg.sequence;
    i420.captureTime = jpeg.captureTime;
    i420.dequeueTime = jpeg.dequeueTime;
    return true;
}

void MjpegDecoder::reportCorrupt(const char* stage) {
    ++corruptFrames_;
    if (corruptFrames_ == 1) {
        log::warning("Corrupt MJPEG frame (", stage, "): ", tjGetErrorStr2(handle_.get()),
                     "; further corrupt frames are only counted");
    }
}

} // namespace camview