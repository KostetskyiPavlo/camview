#pragma once

#include "stats/PipelineStats.hpp"

#include <cstdint>
#include <fstream>
#include <string>

namespace camview {

// Writes one CSV row per displayed frame, for offline analysis (histograms, percentiles).
class CsvLogger {
public:
    // Throws std::runtime_error if the file cannot be created.
    explicit CsvLogger(const std::string& path);

    void write(std::uint32_t sequence, const FrameTimings& timings, std::uint64_t droppedTotal);

private:
    std::ofstream out_;
};

} // namespace camview