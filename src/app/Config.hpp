#pragma once

#include "frame/PixelFormat.hpp"

#include <string>

namespace camview {

// Everything the user can set on the command line. Defaults target the weakest platform we
// measured (Raspberry Pi 2): MJPEG 640x480 keeps the camera's full frame rate there.
struct Config {
    std::string device{"/dev/video0"};
    PixelFormat format{PixelFormat::Mjpeg};
    int width{640};
    int height{480};
    double fps{30.0};
    bool testPattern{false}; // Synthetic source instead of a camera.
    bool display{true};      // false: capture and decode without a window (benchmarking).
};

} // namespace camview