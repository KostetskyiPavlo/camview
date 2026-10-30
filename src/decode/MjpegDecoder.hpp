#pragma once

#include "frame/Frame.hpp"

#include <cstdint>
#include <memory>
#include <vector>

namespace camview {

// Decodes MJPEG frames into I420 with libjpeg-turbo, skipping the costly RGB conversion.
// Not thread-safe: use one decoder per thread.
class MjpegDecoder {
public:
    // Throws std::runtime_error if the TurboJPEG decoder cannot be created.
    MjpegDecoder();
    ~MjpegDecoder();

    MjpegDecoder(const MjpegDecoder&) = delete;
    MjpegDecoder& operator=(const MjpegDecoder&) = delete;
    MjpegDecoder(MjpegDecoder&&) = delete;
    MjpegDecoder& operator=(MjpegDecoder&&) = delete;

    // Decodes `jpeg` into `i420`, reusing i420.data's capacity. Copies timestamps and sequence.
    // Returns false for a corrupt frame (counted, logged once).
    [[nodiscard]] bool decode(const Frame& jpeg, Frame& i420);

    [[nodiscard]] std::uint64_t corruptFrames() const noexcept { return corruptFrames_; }

private:
    struct HandleDeleter {
        void operator()(void* handle) const noexcept;
    };

    void reportCorrupt(const char* stage);

    std::unique_ptr<void, HandleDeleter> handle_; // TurboJPEG's tjhandle is a void*.
    std::vector<std::uint8_t> chromaScratch_;     // Chroma planes of non-4:2:0 JPEGs.
    int lastSubsampling_{-1};
    std::uint64_t corruptFrames_{0};
};

} // namespace camview