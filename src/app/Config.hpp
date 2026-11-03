#pragma once

#include "frame/PixelFormat.hpp"

#include <string>

namespace camview {

// Everything the user can set on the command line.
// Defaults target modern hardware and drone use: 720p at the highest frame rate the camera
// offers (60 requested). If the camera cannot do it, the closest mode is selected, frame rate
// first.
struct Config {
    std::string device{"/dev/video0"};
    PixelFormat format{PixelFormat::Mjpeg};
    int width{1280};
    int height{720};
    double fps{60.0};
    bool preferResolution{false}; // Keep resolution rather than frame rate when choosing a mode.
    bool testPattern{false};      // Synthetic source instead of a camera.
    bool display{true};           // false: capture and decode without a window (benchmarking).
    bool listModes{false};        // Print the camera's modes and exit.
    int extraRenderDelayMs{0};    // Test aid: sleep after each frame to simulate a slow consumer.
    std::string statsCsvPath;     // Empty: no per-frame CSV.
};

} // namespace camview