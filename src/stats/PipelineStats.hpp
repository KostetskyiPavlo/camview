#pragma once

#include "util/Clock.hpp"

#include <optional>

namespace camview {

// Where the time of one displayed frame went, from the driver timestamp to the screen.
struct FrameTimings {
    Duration transfer; // Driver timestamp -> dequeued by the capture thread (USB transfer etc.).
    Duration queue;    // Dequeued -> taken by the render thread (waiting in the mailbox).
    Duration decode;   // MJPEG decoding; zero for YUYV.
    Duration render;   // Texture upload and present.
    Duration total;    // Driver timestamp -> presented: the frame age the viewer sees.
};

struct StatsSummary {
    double windowSeconds{0.0};
    double displayFps{0.0};
    double avgTotalMs{0.0};
    double maxTotalMs{0.0};
    double avgTransferMs{0.0};
    double avgQueueMs{0.0};
    double avgDecodeMs{0.0};
    double avgRenderMs{0.0};
};

// Aggregates per-frame timings into one summary per time window.
// Used by the render thread only, so it needs no synchronisation.
class PipelineStats {
public:
    explicit PipelineStats(Duration window = std::chrono::seconds(1)) : window_(window) {}

    void addFrame(const FrameTimings& timings, TimePoint presented);

    // Returns the summary of the current window once it is `window` long, and starts a new one.
    [[nodiscard]] std::optional<StatsSummary> poll(TimePoint now);

private:
    void reset(TimePoint windowStart);

    Duration window_;
    std::optional<TimePoint> windowStart_;
    long long frames_{0};
    Duration sumTotal_{};
    Duration maxTotal_{};
    Duration sumTransfer_{};
    Duration sumQueue_{};
    Duration sumDecode_{};
    Duration sumRender_{};
};

} // namespace camview