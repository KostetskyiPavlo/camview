#pragma once

#include "frame/PixelFormat.hpp"

#include <string>

namespace camview {

struct CameraMode {
    PixelFormat format{PixelFormat::Yuyv};
    int width{0};
    int height{0};
    double fps{0.0};

    friend bool operator==(const CameraMode&, const CameraMode&) = default;
};

// e.g. "MJPG 640x480 @ 30.00 fps"
std::string toString(const CameraMode& mode);

} // namespace camview