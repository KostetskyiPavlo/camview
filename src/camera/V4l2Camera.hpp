#pragma once

#include "camera/CameraSource.hpp"
#include "util/UniqueFd.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace camview {

// Captures frames from a V4L2 device (USB/UVC cameras) using memory-mapped streaming I/O.
class V4l2Camera final : public CameraSource {
public:
    // `requested` is a wish: the driver may adjust size, format or fps; mode() reports the result.
    // Throws std::invalid_argument for a mode that cannot be requested from a camera.
    V4l2Camera(std::string devicePath, CameraMode requested);
    ~V4l2Camera() override;

    void open() override;
    void close() noexcept override;
    [[nodiscard]] bool isOpen() const noexcept override;
    [[nodiscard]] CameraMode mode() const override;
    [[nodiscard]] std::string description() const override;

    // Returns the newest available frame. Older frames waiting in the driver queue are handed
    // straight back to the driver: showing them would only add latency. Returns Timeout also
    // when the only available frame was corrupted and had to be skipped.
    [[nodiscard]] CaptureStatus capture(Frame& frame,
                                        std::chrono::milliseconds timeout) override;

private:
    struct MappedBuffer {
        void* start;
        std::size_t length;
    };

    void queryCapabilities();
    void negotiateFormat();
    void negotiateFrameRate();
    void startStreaming();
    void stopStreaming() noexcept;
    [[nodiscard]] bool queueBuffer(std::uint32_t index);

    std::string devicePath_;
    CameraMode requested_;
    CameraMode mode_{};
    UniqueFd fd_;

    std::string driverName_;
    std::string cardName_;
    std::string busInfo_;

    int stride_{0};                // Bytes per row reported by the driver (0 for MJPEG).
    std::size_t maxFrameBytes_{0}; // Largest possible frame size reported by the driver.

    std::vector<MappedBuffer> buffers_;
    bool streaming_{false};
    bool warnedAboutTimestamps_{false};

    std::uint64_t staleFramesSkipped_{0};   // Older frames replaced by a newer one in the queue.
    std::uint64_t errorFlagFrames_{0}; // Frames flagged with V4L2_BUF_FLAG_ERROR by the driver.
    std::uint64_t truncatedFrames_{0}; // Frames with fewer (or more) bytes than a full image.
};

} // namespace camview