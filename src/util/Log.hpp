#pragma once

#include <iomanip>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>

namespace camview::log {

enum class Level { Info, Warning, Error };

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
void info(Args&&... args) {
    write(Level::Info, detail::concat(std::forward<Args>(args)...));
}

template <typename... Args>
void warning(Args&&... args) {
    write(Level::Warning, detail::concat(std::forward<Args>(args)...));
}

template <typename... Args>
void error(Args&&... args) {
    write(Level::Error, detail::concat(std::forward<Args>(args)...));
}

} // namespace camview::log