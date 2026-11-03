#include "stats/CsvLogger.hpp"

#include <iomanip>
#include <stdexcept>

namespace camview {

CsvLogger::CsvLogger(const std::string& path) : out_(path) {
    if (!out_) {
        throw std::runtime_error("Cannot create CSV file '" + path + "'");
    }
    out_ << "sequence,transfer_ms,queue_ms,decode_ms,render_ms,total_ms,dropped_total\n";
    out_ << std::fixed << std::setprecision(3);
}

void CsvLogger::write(std::uint32_t sequence, const FrameTimings& timings,
                      std::uint64_t droppedTotal) {
    // std::ofstream buffers internally: one row costs a few formatted numbers, not a syscall.
    out_ << sequence << ',' << toMilliseconds(timings.transfer) << ','
         << toMilliseconds(timings.queue) << ',' << toMilliseconds(timings.decode) << ','
         << toMilliseconds(timings.render) << ',' << toMilliseconds(timings.total) << ','
         << droppedTotal << '\n';
}

} // namespace camview