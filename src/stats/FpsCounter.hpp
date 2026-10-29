#pragma once

#include "util/Clock.hpp"

#include <optional>

namespace camview {

// Measures the average frame rate over fixed time windows.
// The first tick only starts the window; later ticks count frame intervals, so the result is
// intervals / elapsed time and is not inflated by the first frame.
class FpsCounter {
public:
    explicit FpsCounter(Duration window = std::chrono::seconds(1)) : window_(window) {}

    // Registers one frame. Returns the average fps once a full window has elapsed.
    std::optional<double> tick(TimePoint now) {
        if (!windowStart_) {
            windowStart_ = now;
            intervals_ = 0;
            return std::nullopt;
        }

        ++intervals_;
        const Duration elapsed = now - *windowStart_;
        if (elapsed < window_) {
            return std::nullopt;
        }

        const double seconds = std::chrono::duration<double>(elapsed).count();
        const double fps = static_cast<double>(intervals_) / seconds;
        windowStart_ = now;
        intervals_ = 0;
        return fps;
    }

private:
    Duration window_;
    std::optional<TimePoint> windowStart_;
    long long intervals_{0};
};

} // namespace camview