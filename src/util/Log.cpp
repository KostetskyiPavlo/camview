#include "util/Log.hpp"

#include "util/Clock.hpp"

#include <cstdio>
#include <mutex>

namespace camview::log {

namespace {

std::mutex gOutputMutex;
const TimePoint gStartTime = Clock::now();

const char* toTag(Level level) {
    switch (level) {
    case Level::Info:
        return "INFO ";
    case Level::Warning:
        return "WARN ";
    case Level::Error:
        return "ERROR";
    }
    return "?????";
}

} // namespace

void write(Level level, std::string_view message) {
    const double seconds = std::chrono::duration<double>(Clock::now() - gStartTime).count();

    const std::lock_guard lock(gOutputMutex);
    std::fprintf(stderr, "[%9.3f] %s %.*s\n", seconds, toTag(level),
                 static_cast<int>(message.size()), message.data());
}

} // namespace camview::log