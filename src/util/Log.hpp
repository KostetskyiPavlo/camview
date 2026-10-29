#pragma once

#include <iomanip>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>

namespace camview::log {

enum class Level { Debug, Info, Warning, Error };

void setLevel(Level level) noexcept;
bool isEnabled(Level level) noexcept;

// Writes one complete line to stderr. Thread-safe: lines from different threads never mix.
void write(Level level, std::string_view message);

namespace detail {

template <typename... Args>
std::string concat(Args&&... args) {
    std::ostringstream out;
    out << std::fixed << std::setprecision(2);
    (out << ... << std::forward<Args>(args));
    return out.str();
}

} // namespace detail

template <typename... Args>
void debug(Args&&... args) {
    if (isEnabled(Level::Debug)) {
        write(Level::Debug, detail::concat(std::forward<Args>(args)...));
    }
}

template <typename... Args>
void info(Args&&... args) {
    if (isEnabled(Level::Info)) {
        write(Level::Info, detail::concat(std::forward<Args>(args)...));
    }
}

template <typename... Args>
void warning(Args&&... args) {
    if (isEnabled(Level::Warning)) {
        write(Level::Warning, detail::concat(std::forward<Args>(args)...));
    }
}

template <typename... Args>
void error(Args&&... args) {
    if (isEnabled(Level::Error)) {
        write(Level::Error, detail::concat(std::forward<Args>(args)...));
    }
}

} // namespace camview::log