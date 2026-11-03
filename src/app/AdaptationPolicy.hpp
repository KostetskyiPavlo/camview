#pragma once

namespace camview {

// Decides when the pipeline is too slow for the camera and a smaller mode should be tried.
//
// A window counts as overloaded only when both hold:
//   1. frames are really being lost: the display runs noticeably slower than the camera;
//   2. processing (decode + render) is the cause: it takes almost the whole frame interval.
// Condition 1 alone would also fire for a consumer that is slow for unrelated reasons
// (e.g. --extra-render-delay), where a smaller mode would not help. Condition 2 alone fires
// when processing is close to the interval but still keeps up, which would step down for
// nothing.
class AdaptationPolicy {
public:
    // Call once per statistics window. Returns true after kWindowsBeforeDowngrade overloaded
    // windows in a row; the count then restarts, so the next decision needs fresh evidence
    // from the new mode.
    [[nodiscard]] bool update(double captureFps, double displayFps, double processingMs) {
        if (captureFps < 1.0) {
            overloadedWindows_ = 0; // Camera idle or reconnecting: no evidence either way.
            return false;
        }
        const double frameIntervalMs = 1000.0 / captureFps;
        const bool losingFrames = displayFps < kMinDisplayRatio * captureFps;
        const bool processingBound = processingMs > kLoadThreshold * frameIntervalMs;

        if (losingFrames && processingBound) {
            ++overloadedWindows_;
        } else {
            overloadedWindows_ = 0;
        }
        if (overloadedWindows_ >= kWindowsBeforeDowngrade) {
            overloadedWindows_ = 0;
            return true;
        }
        return false;
    }

    void reset() noexcept { overloadedWindows_ = 0; }

private:
    static constexpr double kMinDisplayRatio = 0.95;  // Showing <95% of frames = losing them.
    static constexpr double kLoadThreshold = 0.9;     // Processing uses >90% of the interval.
    static constexpr int kWindowsBeforeDowngrade = 3; // ~3 s of consistent overload.

    int overloadedWindows_{0};
};

} // namespace camview