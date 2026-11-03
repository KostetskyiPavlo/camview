#pragma once

#include "camera/CameraMode.hpp"

#include <optional>
#include <span>

namespace camview {

enum class ModePriority {
    FrameRate,  // Smooth, low-latency motion first (default: drone/FPV use).
    Resolution, // Image detail first.
};

// Picks the mode from `available` that is closest to `requested`. Rules, in order:
//   1. The requested pixel format, if the camera offers it at all; otherwise any format.
//   2. FrameRate priority: the slowest rate that still meets the request (or, if none does,
//      the fastest available), then the closest resolution.
//      Resolution priority: the closest resolution, then the frame rate as above.
//   3. Ties: MJPEG over YUYV (far less USB bandwidth).
// "Closest resolution" means the smallest |dWidth| + |dHeight|. Rates within 0.5 fps count as
// equal, so a 29.97 fps mode satisfies a 30 fps request. The result does not depend on the
// order of `available`. Returns nothing for an empty list.
[[nodiscard]] std::optional<CameraMode> selectMode(std::span<const CameraMode> available,
                                                   const CameraMode& requested,
                                                   ModePriority priority = ModePriority::FrameRate);

} // namespace camview