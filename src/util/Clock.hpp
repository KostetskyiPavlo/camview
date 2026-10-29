#pragma once

#include <chrono>

namespace camview {

// On Linux, libstdc++'s steady_clock reads CLOCK_MONOTONIC. V4L2 stamps buffers with the
// same clock (V4L2_BUF_FLAG_TIMESTAMP_MONOTONIC), so driver timestamps can be compared
// with Clock::now() directly to get the real age of a frame.
using Clock = std::chrono::steady_clock;
using TimePoint = Clock::time_point;
using Duration = Clock::duration;

inline double toMilliseconds(Duration duration) {
    return std::chrono::duration<double, std::milli>(duration).count();
}

} // namespace camview