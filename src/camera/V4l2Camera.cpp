#include "camera/V4l2Camera.hpp"
#include "util/Log.hpp"

#include <fcntl.h>
#include <linux/videodev2.h>
#include <sys/ioctl.h>

#include <cerrno>
#include <cmath>
#include <stdexcept>
#include <system_error>
#include <utility>

namespace camview {

namespace {

// Our FourCC values must match the kernel's so they can be passed to V4L2 unchanged.
static_assert(makeFourcc('Y', 'U', 'Y', 'V') == V4L2_PIX_FMT_YUYV);
static_assert(makeFourcc('M', 'J', 'P', 'G') == V4L2_PIX_FMT_MJPEG);

// ioctl() that transparently retries when interrupted by a signal.
int xioctl(int fd, unsigned long request, void* argument) {
    int result = 0;
    do {
        result = ::ioctl(fd, request, argument);
    } while (result == -1 && errno == EINTR);
    return result;
}

std::string errnoText(int error) {
    // std::error_category::message is thread-safe, unlike std::strerror.
    return std::generic_category().message(error);
}

[[noreturn]] void throwErrno(const std::string& context, int error) {
    throw CameraError(context + ": " + errnoText(error));
}

std::string openErrorHint(int error) {
    switch (error) {
    case ENOENT:
        return " (no such device; list cameras with 'v4l2-ctl --list-devices')";
    case EACCES:
        return " (permission denied; add the user to the 'video' group)";
    case EBUSY:
        return " (device is busy; another process is using the camera)";
    case ENODEV:
    case ENXIO:
        return " (device is not present; was it unplugged?)";
    default:
        return "";
    }
}

// V4L2 fills fixed-size, NUL-terminated byte arrays for names.
std::string cString(const __u8* bytes) {
    return reinterpret_cast<const char*>(bytes);
}

} // namespace

V4l2Camera::V4l2Camera(std::string devicePath, CameraMode requested)
    : devicePath_(std::move(devicePath)), requested_(requested) {
    if (requested_.format != PixelFormat::Yuyv && requested_.format != PixelFormat::Mjpeg) {
        throw std::invalid_argument("V4l2Camera: only YUYV and MJPG can be requested from a camera");
    }
    if (requested_.width <= 0 || requested_.height <= 0 || requested_.fps <= 0.0) {
        throw std::invalid_argument("V4l2Camera: width, height and fps must be positive");
    }
}

V4l2Camera::~V4l2Camera() {
    close();
}

void V4l2Camera::open() {
    if (isOpen()) {
        return;
    }

    // O_NONBLOCK: dequeuing a frame returns EAGAIN instead of blocking forever; waiting is done
    // with poll() and a timeout, so a dead camera can never hang the capture loop.
    UniqueFd fd(::open(devicePath_.c_str(), O_RDWR | O_NONBLOCK | O_CLOEXEC));
    if (!fd) {
        const int error = errno;
        throw CameraError("Cannot open " + devicePath_ + ": " + errnoText(error) +
                          openErrorHint(error));
    }
    fd_ = std::move(fd);

    try {
        queryCapabilities();
        negotiateFormat();
        negotiateFrameRate();
    } catch (...) {
        close();
        throw;
    }

    log::info("Opened ", description());
    log::info("Requested:  ", toString(requested_));
    log::info("Negotiated: ", toString(mode_), " (stride ", stride_, " B, max frame ",
              maxFrameBytes_, " B)");
    if (mode_ != requested_) {
        log::warning("The driver adjusted the requested mode");
    }
}

void V4l2Camera::close() noexcept {
    fd_.reset();
}

bool V4l2Camera::isOpen() const noexcept {
    return fd_.isValid();
}

CameraMode V4l2Camera::mode() const {
    return mode_;
}

std::string V4l2Camera::description() const {
    if (cardName_.empty()) {
        return devicePath_;
    }
    return devicePath_ + " - " + cardName_ + " (driver " + driverName_ + ", " + busInfo_ + ")";
}

CaptureStatus V4l2Camera::capture(Frame& /*frame*/, std::chrono::milliseconds /*timeout*/) {
    throw std::logic_error("V4l2Camera::capture: streaming is not implemented yet");
}

void V4l2Camera::queryCapabilities() {
    v4l2_capability capability{};
    if (xioctl(fd_.get(), VIDIOC_QUERYCAP, &capability) == -1) {
        const int error = errno;
        if (error == ENOTTY) {
            throw CameraError(devicePath_ + " is not a V4L2 device");
        }
        throwErrno("VIDIOC_QUERYCAP on " + devicePath_, error);
    }

    driverName_ = cString(capability.driver);
    cardName_ = cString(capability.card);
    busInfo_ = cString(capability.bus_info);

    // One physical camera can expose several nodes (video, metadata). `device_caps` describes
    // this particular node; `capabilities` describes the whole device.
    const std::uint32_t caps = (capability.capabilities & V4L2_CAP_DEVICE_CAPS) != 0
                                   ? capability.device_caps
                                   : capability.capabilities;

    if ((caps & V4L2_CAP_VIDEO_CAPTURE) == 0) {
        throw CameraError(devicePath_ + " (" + cardName_ +
                          ") is not a video capture node; it may be a metadata or codec node, "
                          "try another /dev/videoN");
    }
    if ((caps & V4L2_CAP_STREAMING) == 0) {
        throw CameraError(devicePath_ + " does not support streaming I/O");
    }
}

void V4l2Camera::negotiateFormat() {
    v4l2_format format{};
    format.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    format.fmt.pix.width = static_cast<__u32>(requested_.width);
    format.fmt.pix.height = static_cast<__u32>(requested_.height);
    format.fmt.pix.pixelformat = toFourcc(requested_.format);
    format.fmt.pix.field = V4L2_FIELD_ANY;

    // S_FMT is a negotiation: the driver replaces unsupported values with the closest ones it
    // supports and writes back what it actually set.
    if (xioctl(fd_.get(), VIDIOC_S_FMT, &format) == -1) {
        const int error = errno;
        throwErrno("VIDIOC_S_FMT on " + devicePath_ + openErrorHint(error), error);
    }

    const auto pixelFormat = pixelFormatFromFourcc(format.fmt.pix.pixelformat);
    if (!pixelFormat || (*pixelFormat != PixelFormat::Yuyv && *pixelFormat != PixelFormat::Mjpeg)) {
        throw CameraError("The driver selected unsupported pixel format " +
                          fourccToString(format.fmt.pix.pixelformat) +
                          "; camview supports YUYV and MJPG");
    }

    mode_.format = *pixelFormat;
    mode_.width = static_cast<int>(format.fmt.pix.width);
    mode_.height = static_cast<int>(format.fmt.pix.height);
    stride_ = static_cast<int>(format.fmt.pix.bytesperline);
    maxFrameBytes_ = format.fmt.pix.sizeimage;
}

void V4l2Camera::negotiateFrameRate() {
    v4l2_streamparm parameters{};
    parameters.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    if (xioctl(fd_.get(), VIDIOC_G_PARM, &parameters) == -1) {
        const int error = errno;
        log::warning("VIDIOC_G_PARM failed (", errnoText(error), "); frame rate is unknown");
        mode_.fps = 0.0;
        return;
    }

    if ((parameters.parm.capture.capability & V4L2_CAP_TIMEPERFRAME) != 0) {
        // The frame interval is a fraction of a second (1/30). Scaling by 1000 keeps
        // fractional rates such as 29.97 fps exact enough.
        parameters.parm.capture.timeperframe.numerator = 1000;
        parameters.parm.capture.timeperframe.denominator =
            static_cast<__u32>(std::lround(requested_.fps * 1000.0));
        if (xioctl(fd_.get(), VIDIOC_S_PARM, &parameters) == -1) {
            const int error = errno;
            log::warning("VIDIOC_S_PARM failed (", errnoText(error),
                         "); keeping the driver's frame rate");
        }
    } else {
        log::warning("The driver does not allow setting the frame rate");
    }

    // Read back what the driver actually uses.
    parameters = v4l2_streamparm{};
    parameters.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    if (xioctl(fd_.get(), VIDIOC_G_PARM, &parameters) == -1) {
        mode_.fps = 0.0;
        return;
    }
    const v4l2_fract& interval = parameters.parm.capture.timeperframe;
    mode_.fps = interval.numerator != 0
                    ? static_cast<double>(interval.denominator) / interval.numerator
                    : 0.0;
}

} // namespace camview