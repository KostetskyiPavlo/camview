#include "util/SystemInfo.hpp"

#include "util/Log.hpp"

#include <fstream>
#include <string>

namespace camview {

namespace {

std::string readFirstLine(const char* path) {
    std::ifstream file(path);
    std::string line;
    std::getline(file, line);
    // Device-tree strings end with a NUL byte rather than a newline.
    while (!line.empty() && (line.back() == '\0' || line.back() == '\n')) {
        line.pop_back();
    }
    return line;
}

} // namespace

void logSystemInfo() {
    const std::string model = readFirstLine("/proc/device-tree/model");
    const std::string governor =
        readFirstLine("/sys/devices/system/cpu/cpu0/cpufreq/scaling_governor");

    log::info("System: ", model.empty() ? "unknown board" : model, ", CPU governor ",
              governor.empty() ? "unknown" : governor);
    if (!governor.empty() && governor != "performance") {
        log::info("Processing times vary with CPU frequency; "
                  "use the 'performance' governor for stable measurements");
    }
}

} // namespace camview