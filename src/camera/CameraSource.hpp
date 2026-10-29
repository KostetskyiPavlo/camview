#pragma once

#include "camera/CameraMode.hpp"
#include "frame/Frame.hpp"

#include <chrono>
#include <stdexcept>
#include <string>

namespace camview {

// Thrown when a camera cannot be opened or configured.
class CameraError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

enum class CaptureStatus {
    Ok,           // `frame` holds a new frame.
    Timeout,      // No frame within the timeout. Normal; the caller may retry.
    Disconnected, // The device is gone. The caller should close() and reopen later.
};

// Source of video frames. Implementations: V4l2Camera (real device), TestPatternSource.
class CameraSource {
public:
    CameraSource() = default;
    virtual ~CameraSource() = default;

    CameraSource(const CameraSource&) = delete;
    CameraSource& operator=(const CameraSource&) = delete;
    CameraSource(CameraSource&&) = delete;
    CameraSource& operator=(CameraSource&&) = delete;

    // Opens the device and starts streaming. Throws CameraError on failure.
    virtual void open() = 0;

    // Stops streaming and releases the device. Safe to call more than once.
    virtual void close() noexcept = 0;

    [[nodiscard]] virtual bool isOpen() const noexcept = 0;

    // Mode actually negotiated with the device (may differ from the requested one).
    [[nodiscard]] virtual CameraMode mode() const = 0;

    // Human-readable description for logs: device path, driver, card name.
    [[nodiscard]] virtual std::string description() const = 0;

    // Waits up to `timeout` for the next frame and copies it into `frame`,
    // reusing frame.data's capacity.
    [[nodiscard]] virtual CaptureStatus capture(Frame& frame,
                                                std::chrono::milliseconds timeout) = 0;
};

} // namespace camview