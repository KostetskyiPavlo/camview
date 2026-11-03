#include "camera/ModeSelector.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>

namespace camview {

namespace {

constexpr double kFpsTolerance = 0.5;

// Lower is better; compared element by element (lexicographically).
using ModeKey = std::array<double, 5>;

} // namespace

std::optional<CameraMode> selectMode(std::span<const CameraMode> available,
                                     const CameraMode& requested, ModePriority priority) {
    if (available.empty()) {
        return std::nullopt;
    }

    const bool formatOffered =
        std::any_of(available.begin(), available.end(),
                    [&](const CameraMode& mode) { return mode.format == requested.format; });

    const auto keyOf = [&](const CameraMode& mode) {
        const double formatMismatch = formatOffered && mode.format != requested.format ? 1.0 : 0.0;
        const auto resolutionDistance = static_cast<double>(
            std::abs(mode.width - requested.width) + std::abs(mode.height - requested.height));
        // Modes that meet the requested rate beat slower ones; within each group the rate
        // closest to the request wins (the slowest that is fast enough, or the fastest of the
        // too-slow ones).
        const double belowRequestedFps = mode.fps >= requested.fps - kFpsTolerance ? 0.0 : 1.0;
        const double fpsDistance = std::abs(mode.fps - requested.fps);
        const double notMjpeg = mode.format == PixelFormat::Mjpeg ? 0.0 : 1.0;

        if (priority == ModePriority::FrameRate) {
            return ModeKey{formatMismatch, belowRequestedFps, fpsDistance, resolutionDistance,
                           notMjpeg};
        }
        return ModeKey{formatMismatch, resolutionDistance, belowRequestedFps, fpsDistance,
                       notMjpeg};
    };

    return *std::min_element(
        available.begin(), available.end(),
        [&](const CameraMode& a, const CameraMode& b) { return keyOf(a) < keyOf(b); });
}

std::optional<CameraMode> selectLowerMode(std::span<const CameraMode> available,
                                          const CameraMode& current) {
    const auto area = [](const CameraMode& mode) {
        return static_cast<long long>(mode.width) * mode.height;
    };

    std::optional<CameraMode> best;
    for (const CameraMode& mode : available) {
        const bool candidate = mode.format == current.format && area(mode) < area(current) &&
                               mode.fps >= current.fps - kFpsTolerance;
        if (!candidate) {
            continue;
        }
        const bool larger = !best || area(mode) > area(*best);
        const bool sameSizeCloserFps =
            best && area(mode) == area(*best) &&
            std::abs(mode.fps - current.fps) < std::abs(best->fps - current.fps);
        if (larger || sameSizeCloserFps) {
            best = mode;
        }
    }
    return best;
}

} // namespace camview