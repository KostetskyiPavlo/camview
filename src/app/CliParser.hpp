#pragma once

#include "app/Config.hpp"

#include <string>
#include <string_view>

namespace camview {

struct CliResult {
    enum class Action { Run, Help, Version, Error };

    Action action{Action::Run};
    Config config;
    std::string error; // Human-readable reason when action == Error.
};

// Parses the command line with getopt_long. Never prints and never exits: the caller decides
// what to do, which keeps the parser unit-testable. Not thread-safe (getopt has global state).
[[nodiscard]] CliResult parseCommandLine(int argc, char* argv[]);

[[nodiscard]] std::string usageText(std::string_view programName);

} // namespace camview