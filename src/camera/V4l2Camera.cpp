#include "camera/V4l2Camera.hpp"
#include "util/Log.hpp"

#include <fcntl.h>
#include <linux/videodev2.h>
#include <poll.h>
#include <sys/ioctl.h>
#include <sys/mman.h>

#include <algorithm>
#include <cerrno>
#include <cmath>
#include <cstring>
#include <optional>
#include <stdexcept>
#include <system_error>
#include <utility>
#include <vector>

namespace camview {

namespace {

// Our FourCC values must match the kernel's so they can be passed to V4L2 unchanged.
static_assert(makeFourcc('Y', 'U', 'Y', 'V') == V4L2_PIX_FMT_YUYV);
static_assert(makeFourcc('M', 'J', 'P', 'G') == V4L2_PIX_FMT_MJPEG);

// Enough for the driver to keep filling buffers while we hold one, few enough that a stale
// queue cannot grow long.
constexpr std::uint32_t kRequestedBufferCount = 4;
constexpr std::uint32_t kMinBufferCount = 2;

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

// Works because libstdc++'s steady_clock and V4L2 monotonic timestamps share CLOCK_MONOTONIC.
TimePoint toTimePoint(const timeval& time) {
    return TimePoint{std::chrono::duration_cast<Duration>(std::chrono::seconds{time.tv_sec} +
                                                          std::chrono::microseconds{time.tv_usec})};
}

v4l2_buffer makeBuffer() {
    v4l2_buffer buffer{};
    buffer.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    buffer.memory = V4L2_MEMORY_MMAP;
    return buffer;
}

UniqueFd openDevice(const std::string& path) {
    // O_NONBLOCK: dequeuing a frame returns EAGAIN instead of blocking forever; waiting is done
    // with poll() and a timeout, so a dead camera can never hang the capture loop.
    UniqueFd fd(::open(path.c_str(), O_RDWR | O_NONBLOCK | O_CLOEXEC));
    if (!fd) {
        const int error = errno;
        throw CameraError("Cannot open " + path + ": " + errnoText(error) + openErrorHint(error));
    }
    return fd;
}

std::uint32_t snapToRange(std::uint32_t value, std::uint32_t minimum, std::uint32_t maximum,
                          std::uint32_t step) {
    const std::uint32_t clamped = std::clamp(value, minimum, maximum);
    if (step <= 1) {
        return clamped;
    }
    return minimum + (clamped - minimum) / step * step;
}

// Appends one mode per frame rate the driver lists for this format and size.
void appendFrameRates(int fd, PixelFormat format, std::uint32_t fourcc, std::uint32_t width,
                      std::uint32_t height, std::vector<CameraMode>& modes) {
    const auto addMode = [&](double fps) {
        modes.push_back({format, static_cast<int>(width), static_cast<int>(height), fps});
    };
    const auto toFps = [](const v4l2_fract& interval) {
        return interval.numerator != 0
                   ? static_cast<double>(interval.denominator) / interval.numerator
                   : 0.0;
    };

    bool listed = false;
    for (std::uint32_t index = 0;; ++index) {
        v4l2_frmivalenum interval{};
        interval.index = index;
        interval.pixel_format = fourcc;
        interval.width = width;
        interval.height = height;
        if (xioctl(fd, VIDIOC_ENUM_FRAMEINTERVALS, &interval) == -1) {
            break; // EINVAL marks the end of the list.
        }
        listed = true;
        if (interval.type == V4L2_FRMIVAL_TYPE_DISCRETE) {
            addMode(toFps(interval.discrete));
            continue;
        }
        // Stepwise or continuous: one entry describes a range. Offer its fastest rate (the
        // shortest interval) and its slowest one.
        addMode(toFps(interval.stepwise.min));
        addMode(toFps(interval.stepwise.max));
        break;
    }
    if (!listed) {
        addMode(0.0); // The driver does not enumerate rates: frame rate unknown.
    }
}

// Every mode the device offers in a format camview can display.
std::vector<CameraMode> enumerateModes(int fd, const CameraMode& requested) {
    std::vector<CameraMode> modes;
    for (std::uint32_t formatIndex = 0;; ++formatIndex) {
        v4l2_fmtdesc description{};
        description.index = formatIndex;
        description.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        if (xioctl(fd, VIDIOC_ENUM_FMT, &description) == -1) {
            break; // EINVAL marks the end of the list.
        }
        const auto format = pixelFormatFromFourcc(description.pixelformat);
        if (!format || (*format != PixelFormat::Yuyv && *format != PixelFormat::Mjpeg)) {
            continue;
        }

        for (std::uint32_t sizeIndex = 0;; ++sizeIndex) {
            v4l2_frmsizeenum size{};
            size.index = sizeIndex;
            size.pixel_format = description.pixelformat;
            if (xioctl(fd, VIDIOC_ENUM_FRAMESIZES, &size) == -1) {
                break;
            }
            if (size.type == V4L2_FRMSIZE_TYPE_DISCRETE) {
                appendFrameRates(fd, *format, description.pixelformat, size.discrete.width,
                                 size.discrete.height, modes);
                continue;
            }
            // Stepwise or continuous: one entry describes a whole range of sizes. Offer the
            // requested size snapped into the range, plus the range's extremes.
            const v4l2_frmsize_stepwise& range = size.stepwise;
            appendFrameRates(fd, *format, description.pixelformat,
                             snapToRange(static_cast<std::uint32_t>(requested.width),
                                         range.min_width, range.max_width, range.step_width),
                             snapToRange(static_cast<std::uint32_t>(requested.height),
                                         range.min_height, range.max_height, range.step_height),
                             modes);
            appendFrameRates(fd, *format, description.pixelformat, range.min_width,
                             range.min_height, modes);
            appendFrameRates(fd, *format, description.pixelformat, range.max_width,
                             range.max_height, modes);
            break;
        }
    }
    return modes;
}

} // namespace

V4l2Camera::V4l2Camera(std::string devicePath, CameraMode requested, ModePriority priority)
    : devicePath_(std::move(devicePath)), requested_(requested), priority_(priority) {
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

    fd_ = openDevice(devicePath_);
    try {
        queryCapabilities();
        selectTargetMode();
        negotiateFormat();
        negotiateFrameRate();
        startStreaming();
    } catch (...) {
        close();
        throw;
    }

    log::info("Opened ", description());
    log::info("Requested:  ", toString(requested_));
    if (target_ != requested_) {
        log::info("Selected:   ", toString(target_), " (closest mode the camera offers)");
    }
    log::info("Negotiated: ", toString(mode_), " (stride ", stride_, " B, max frame ",
              maxFrameBytes_, " B, ", buffers_.size(), " buffers)");
    if (mode_ != target_) {
        log::warning("The driver adjusted the selected mode");
    }
}

void V4l2Camera::close() noexcept {
    stopStreaming();
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

CaptureStatus V4l2Camera::capture(Frame& frame, std::chrono::milliseconds timeout) {
    if (!streaming_) {
        return CaptureStatus::Disconnected;
    }

    pollfd descriptor{};
    descriptor.fd = fd_.get();
    descriptor.events = POLLIN;
    int ready = 0;
    do {
        ready = ::poll(&descriptor, 1, static_cast<int>(timeout.count()));
    } while (ready == -1 && errno == EINTR);

    if (ready == -1) {
        const int error = errno;
        log::error("poll on ", devicePath_, " failed: ", errnoText(error));
        return CaptureStatus::Disconnected;
    }
    if (ready == 0) {
        return CaptureStatus::Timeout;
    }
    if ((descriptor.revents & (POLLERR | POLLHUP | POLLNVAL)) != 0) {
        log::error(devicePath_, " reported an error condition (was it unplugged?)");
        return CaptureStatus::Disconnected;
    }

    // Drain the queue: keep only the newest filled buffer and give older ones straight back.
    std::optional<v4l2_buffer> newest;
    for (;;) {
        v4l2_buffer buffer = makeBuffer();
        if (xioctl(fd_.get(), VIDIOC_DQBUF, &buffer) == -1) {
            const int error = errno;
            if (error == EAGAIN) {
                break;
            }
            log::error("VIDIOC_DQBUF on ", devicePath_, " failed: ", errnoText(error));
            return CaptureStatus::Disconnected;
        }
        if (newest) {
            ++staleFramesSkipped_;
            if (!queueBuffer(newest->index)) {
                return CaptureStatus::Disconnected;
            }
        }
        newest = buffer;
    }
    if (!newest) {
        return CaptureStatus::Timeout; // Woken up without a ready frame.
    }

    const TimePoint dequeueTime = Clock::now();
    const v4l2_buffer& buffer = *newest;
    if (buffer.index >= buffers_.size()) {
        log::error("Driver returned an invalid buffer index ", buffer.index);
        return CaptureStatus::Disconnected;
    }

    const MappedBuffer& mapped = buffers_[buffer.index];
    const std::size_t bytesUsed = buffer.bytesused;
    const std::size_t minimumBytes =
        mode_.format == PixelFormat::Yuyv
            ? static_cast<std::size_t>(stride_) * static_cast<std::size_t>(mode_.height)
            : 1;
    const bool errorFlag = (buffer.flags & V4L2_BUF_FLAG_ERROR) != 0;
    const bool badSize = bytesUsed < minimumBytes || bytesUsed > mapped.length;
    if (errorFlag || badSize) {
        if (errorFlag) {
            ++errorFlagFrames_;
        } else {
            ++truncatedFrames_;
        }
        // Details of the first few bad frames are enough to see the pattern.
        if (errorFlagFrames_ + truncatedFrames_ <= 5) {
            log::warning("Skipping frame #", buffer.sequence, ": ",
                         errorFlag ? "driver error flag" : "wrong size", ", ", bytesUsed,
                         " of ", minimumBytes, " bytes");
        }
        return queueBuffer(buffer.index) ? CaptureStatus::Timeout : CaptureStatus::Disconnected;
    }

    frame.format = mode_.format;
    frame.width = mode_.width;
    frame.height = mode_.height;
    frame.stride = stride_;
    frame.sequence = buffer.sequence;
    frame.dequeueTime = dequeueTime;

    const bool monotonic = (buffer.flags & V4L2_BUF_FLAG_TIMESTAMP_MASK) ==
                           V4L2_BUF_FLAG_TIMESTAMP_MONOTONIC;
    if (monotonic) {
        frame.captureTime = toTimePoint(buffer.timestamp);
    } else {
        frame.captureTime = dequeueTime;
        if (!warnedAboutTimestamps_) {
            log::warning("Driver timestamps are not monotonic; frame age will read as zero");
            warnedAboutTimestamps_ = true;
        }
    }

    // Copy out and return the buffer immediately so the driver never runs out of buffers
    // while we decode or render.
    frame.data.resize(bytesUsed);
    std::memcpy(frame.data.data(), mapped.start, bytesUsed);

    return queueBuffer(buffer.index) ? CaptureStatus::Ok : CaptureStatus::Disconnected;
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
    format.fmt.pix.width = static_cast<__u32>(target_.width);
    format.fmt.pix.height = static_cast<__u32>(target_.height);
    format.fmt.pix.pixelformat = toFourcc(target_.format);
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
            static_cast<__u32>(std::lround(target_.fps * 1000.0));
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

void V4l2Camera::startStreaming() {
    v4l2_requestbuffers request{};
    request.count = kRequestedBufferCount;
    request.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    request.memory = V4L2_MEMORY_MMAP;
    if (xioctl(fd_.get(), VIDIOC_REQBUFS, &request) == -1) {
        const int error = errno;
        throwErrno("VIDIOC_REQBUFS on " + devicePath_ + openErrorHint(error), error);
    }
    if (request.count < kMinBufferCount) {
        throw CameraError("The driver allocated only " + std::to_string(request.count) +
                          " buffer(s); at least 2 are required");
    }

    buffers_.reserve(request.count);
    for (std::uint32_t index = 0; index < request.count; ++index) {
        v4l2_buffer buffer = makeBuffer();
        buffer.index = index;
        if (xioctl(fd_.get(), VIDIOC_QUERYBUF, &buffer) == -1) {
            const int error = errno;
            throwErrno("VIDIOC_QUERYBUF on " + devicePath_, error);
        }

        void* start = ::mmap(nullptr, buffer.length, PROT_READ | PROT_WRITE, MAP_SHARED,
                             fd_.get(), static_cast<off_t>(buffer.m.offset));
        if (start == MAP_FAILED) {
            const int error = errno;
            throwErrno("mmap of buffer " + std::to_string(index), error);
        }
        buffers_.push_back(MappedBuffer{start, buffer.length});

        if (!queueBuffer(index)) {
            throw CameraError("Cannot queue buffer " + std::to_string(index));
        }
    }

    v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    if (xioctl(fd_.get(), VIDIOC_STREAMON, &type) == -1) {
        const int error = errno;
        throwErrno("VIDIOC_STREAMON on " + devicePath_ + openErrorHint(error), error);
    }
    streaming_ = true;
    staleFramesSkipped_ = 0;
    errorFlagFrames_ = 0;
    truncatedFrames_ = 0;
}

void V4l2Camera::stopStreaming() noexcept {
    if (!fd_) {
        return;
    }

    // Errors are ignored on purpose: this also runs after the device has disappeared.
    if (streaming_) {
        v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        static_cast<void>(xioctl(fd_.get(), VIDIOC_STREAMOFF, &type));
        streaming_ = false;

        try {
            log::info("Streaming stopped: ", staleFramesSkipped_, " stale frames skipped, ",
                      errorFlagFrames_, " with driver error flag, ", truncatedFrames_,
                      " truncated");
        } catch (...) {
            // Logging must never turn cleanup into a crash.
        }
    }

    for (const MappedBuffer& buffer : buffers_) {
        ::munmap(buffer.start, buffer.length);
    }
    if (!buffers_.empty()) {
        v4l2_requestbuffers request{};
        request.count = 0; // Releases the driver's buffers.
        request.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        request.memory = V4L2_MEMORY_MMAP;
        static_cast<void>(xioctl(fd_.get(), VIDIOC_REQBUFS, &request));
    }
    buffers_.clear();
}

bool V4l2Camera::queueBuffer(std::uint32_t index) {
    v4l2_buffer buffer = makeBuffer();
    buffer.index = index;
    if (xioctl(fd_.get(), VIDIOC_QBUF, &buffer) == -1) {
        const int error = errno;
        log::error("VIDIOC_QBUF on ", devicePath_, " failed: ", errnoText(error));
        return false;
    }
    return true;
}

void V4l2Camera::selectTargetMode() {
    const std::vector<CameraMode> modes = enumerateModes(fd_.get(), requested_);
    if (const auto best = selectMode(modes, requested_, priority_)) {
        target_ = *best;
        log::info("Camera offers ", modes.size(), " modes in supported formats");
    } else {
        target_ = requested_;
        log::warning("The camera lists no modes in supported formats; "
                     "asking the driver for the requested one");
    }
}

std::vector<CameraMode> V4l2Camera::listModes(const std::string& devicePath) {
    V4l2Camera camera(devicePath, CameraMode{PixelFormat::Mjpeg, 640, 480, 30.0});
    camera.fd_ = openDevice(devicePath);
    camera.queryCapabilities();
    return enumerateModes(camera.fd_.get(), camera.requested_);
}

} // namespace camview