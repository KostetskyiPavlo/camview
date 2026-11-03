#include "stats/PipelineStats.hpp"

#include <algorithm>

namespace camview {

void PipelineStats::addFrame(const FrameTimings& timings, TimePoint presented) {
    if (!windowStart_) {
        windowStart_ = presented;
    }
    ++frames_;
    sumTotal_ += timings.total;
    maxTotal_ = std::max(maxTotal_, timings.total);
    sumTransfer_ += timings.transfer;
    sumQueue_ += timings.queue;
    sumDecode_ += timings.decode;
    sumRender_ += timings.render;
}

std::optional<StatsSummary> PipelineStats::poll(TimePoint now) {
    if (!windowStart_ || now - *windowStart_ < window_) {
        return std::nullopt;
    }
    if (frames_ == 0) {
        reset(now); // Nothing to report: start a fresh window instead of growing a stale one.
        return std::nullopt;
    }

    const double seconds = std::chrono::duration<double>(now - *windowStart_).count();
    const auto frames = static_cast<double>(frames_);
    const auto average = [frames](Duration sum) { return toMilliseconds(sum) / frames; };

    StatsSummary summary;
    summary.windowSeconds = seconds;
    summary.displayFps = frames / seconds;
    summary.avgTotalMs = average(sumTotal_);
    summary.maxTotalMs = toMilliseconds(maxTotal_);
    summary.avgTransferMs = average(sumTransfer_);
    summary.avgQueueMs = average(sumQueue_);
    summary.avgDecodeMs = average(sumDecode_);
    summary.avgRenderMs = average(sumRender_);

    reset(now);
    return summary;
}

void PipelineStats::reset(TimePoint windowStart) {
    windowStart_ = windowStart;
    frames_ = 0;
    sumTotal_ = Duration{};
    maxTotal_ = Duration{};
    sumTransfer_ = Duration{};
    sumQueue_ = Duration{};
    sumDecode_ = Duration{};
    sumRender_ = Duration{};
}

} // namespace camview