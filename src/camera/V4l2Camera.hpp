#pragma once

#include "camera/CameraSource.hpp"
#include "util/UniqueFd.hpp"

#include <cstddef>
#include <string>

namespace camview {

// Captures frames from a V4L2 device (USB/UVC cameras).
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
    [[nodiscard]] CaptureStatus capture(Frame& frame,
                                        std::chrono::milliseconds timeout) override;

private:
    void queryCapabilities();
    void negotiateFormat();
    void negotiateFrameRate();

    std::string devicePath_;
    CameraMode requested_;
    CameraMode mode_{};
    UniqueFd fd_;

    std::string driverName_;
    std::string cardName_;
    std::string busInfo_;

    int stride_{0};                // Bytes per row reported by the driver (0 for MJPEG).
    std::size_t maxFrameBytes_{0}; // Largest possible frame size reported by the driver.
};

} // namespace camview