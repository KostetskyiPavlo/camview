#include "app/CliParser.hpp"

#include <getopt.h>

#include <charconv>
#include <cmath>
#include <optional>
#include <sstream>
#include <system_error>
#include <utility>

namespace camview {

namespace {

constexpr int kMinDimension = 16;
constexpr int kMaxWidth = 7680;
constexpr int kMaxHeight = 4320;
constexpr double kMinFps = 1.0;
constexpr double kMaxFps = 240.0;

// Codes for long-only options. Values above 255 can never clash with a short option character.
constexpr int kOptionWidth = 1000;
constexpr int kOptionHeight = 1001;
constexpr int kOptionFps = 1002;
constexpr int kOptionFormat = 1003;
constexpr int kOptionTestPattern = 1004;
constexpr int kOptionNoDisplay = 1005;
constexpr int kOptionVersion = 1006;
constexpr int kOptionExtraRenderDelay = 1007;
constexpr int kOptionStatsCsv = 1008;
constexpr int kOptionPrefer = 1009;
constexpr int kOptionListModes = 1010;
constexpr int kOptionNoAdapt = 1011;
constexpr int kMaxExtraRenderDelayMs = 1000;

const option kLongOptions[] = {
    {"device", required_argument, nullptr, 'd'},
    {"width", required_argument, nullptr, kOptionWidth},
    {"height", required_argument, nullptr, kOptionHeight},
    {"fps", required_argument, nullptr, kOptionFps},
    {"format", required_argument, nullptr, kOptionFormat},
    {"test-pattern", no_argument, nullptr, kOptionTestPattern},
    {"no-display", no_argument, nullptr, kOptionNoDisplay},
    {"extra-render-delay", required_argument, nullptr, kOptionExtraRenderDelay},
    {"stats-csv", required_argument, nullptr, kOptionStatsCsv},
    {"prefer", required_argument, nullptr, kOptionPrefer},
    {"list-modes", no_argument, nullptr, kOptionListModes},
    {"no-adapt", no_argument, nullptr, kOptionNoAdapt},
    {"help", no_argument, nullptr, 'h'},
    {"version", no_argument, nullptr, kOptionVersion},
    {nullptr, 0, nullptr, 0},
};

// "+": stop at the first non-option instead of reordering argv.
// ":": report a missing value as ':' instead of printing getopt's own message.
constexpr const char* kShortOptions = "+:d:h";

// Strict integer parsing: the whole string must be a number ("30x" is rejected).
std::optional<int> parseInt(std::string_view text) {
    int value = 0;
    const char* end = text.data() + text.size();
    const auto [last, error] = std::from_chars(text.data(), end, value);
    if (error != std::errc{} || last != end || text.empty()) {
        return std::nullopt;
    }
    return value;
}

std::optional<double> parseDouble(std::string_view text) {
    double value = 0.0;
    const char* end = text.data() + text.size();
    const auto [last, error] = std::from_chars(text.data(), end, value);
    if (error != std::errc{} || last != end || text.empty() || !std::isfinite(value)) {
        return std::nullopt;
    }
    return value;
}

std::string toLower(std::string_view text) {
    std::string result(text);
    for (char& c : result) {
        if (c >= 'A' && c <= 'Z') {
            c = static_cast<char>(c - 'A' + 'a');
        }
    }
    return result;
}

// Accepts a path ("/dev/video2") or a bare index ("2"), as the task allows "device index 0".
std::optional<std::string> resolveDevice(std::string_view text) {
    if (text.empty()) {
        return std::nullopt;
    }
    bool allDigits = true;
    for (const char c : text) {
        allDigits = allDigits && c >= '0' && c <= '9';
    }
    return allDigits ? "/dev/video" + std::string(text) : std::string(text);
}

CliResult makeError(std::string message) {
    CliResult result;
    result.action = CliResult::Action::Error;
    result.error = std::move(message);
    return result;
}

// Name of the option getopt just complained about, for error messages.
// For a missing value the option itself is the last consumed argument, in whatever form the
// user typed it ("--device" or "-d"). For an unknown short option getopt stores it in optopt.
std::string offendingOption(char* argv[], bool missingValue) {
    if (!missingValue && optopt > 0 && optopt < 128) {
        return std::string("-") + static_cast<char>(optopt);
    }
    const std::string_view last = argv[optind - 1];
    return std::string(last.substr(0, last.find('=')));
}

} // namespace

CliResult parseCommandLine(int argc, char* argv[]) {
    CliResult result;
    Config& config = result.config;
    bool widthSet = false;
    bool heightSet = false;

    // Reset getopt's global state so parsing can run more than once (unit tests).
    optind = 0;
    opterr = 0;

    for (;;) {
        const int option = getopt_long(argc, argv, kShortOptions, kLongOptions, nullptr);
        if (option == -1) {
            break;
        }
        const std::string_view value = optarg != nullptr ? optarg : "";

        switch (option) {
        case 'd': {
            const auto device = resolveDevice(value);
            if (!device) {
                return makeError("--device must not be empty");
            }
            config.device = *device;
            break;
        }
        case kOptionWidth: {
            const auto width = parseInt(value);
            if (!width || *width < kMinDimension || *width > kMaxWidth) {
                return makeError("--width must be an integer from 16 to 7680, got '" +
                                 std::string(value) + "'");
            }
            config.width = *width;
            widthSet = true;
            break;
        }
        case kOptionHeight: {
            const auto height = parseInt(value);
            if (!height || *height < kMinDimension || *height > kMaxHeight) {
                return makeError("--height must be an integer from 16 to 4320, got '" +
                                 std::string(value) + "'");
            }
            config.height = *height;
            heightSet = true;
            break;
        }
        case kOptionFps: {
            const auto fps = parseDouble(value);
            if (!fps || *fps < kMinFps || *fps > kMaxFps) {
                return makeError("--fps must be a number from 1 to 240, got '" +
                                 std::string(value) + "'");
            }
            config.fps = *fps;
            break;
        }
        case kOptionFormat: {
            const std::string format = toLower(value);
            if (format == "mjpg" || format == "mjpeg") {
                config.format = PixelFormat::Mjpeg;
            } else if (format == "yuyv") {
                config.format = PixelFormat::Yuyv;
            } else {
                return makeError("--format must be 'mjpg' or 'yuyv', got '" + std::string(value) +
                                 "'");
            }
            break;
        }
        case kOptionTestPattern:
            config.testPattern = true;
            break;
        case kOptionNoDisplay:
            config.display = false;
            break;
        case kOptionExtraRenderDelay: {
            const auto delay = parseInt(value);
            if (!delay || *delay < 0 || *delay > kMaxExtraRenderDelayMs) {
                return makeError("--extra-render-delay must be an integer from 0 to 1000 (ms), "
                                 "got '" +
                                 std::string(value) + "'");
            }
            config.extraRenderDelayMs = *delay;
            break;
        }
        case kOptionStatsCsv:
            if (value.empty()) {
                return makeError("--stats-csv needs a file path");
            }
            config.statsCsvPath = std::string(value);
            break;
        case kOptionPrefer: {
            const std::string prefer = toLower(value);
            if (prefer == "fps") {
                config.preferResolution = false;
            } else if (prefer == "resolution") {
                config.preferResolution = true;
            } else {
                return makeError("--prefer must be 'fps' or 'resolution', got '" +
                                 std::string(value) + "'");
            }
            break;
        }
        case kOptionListModes:
            config.listModes = true;
            break;
        case kOptionNoAdapt:
            config.adapt = false;
            break;
        case 'h':
            result.action = CliResult::Action::Help;
            return result;
        case kOptionVersion:
            result.action = CliResult::Action::Version;
            return result;
        case ':':
            return makeError("option '" + offendingOption(argv, true) + "' requires a value");
        default:
            return makeError("unknown option '" + offendingOption(argv, false) + "'");
        }
    }

    if (optind < argc) {
        return makeError("unexpected argument '" + std::string(argv[optind]) + "'");
    }
    // A lone --width would be combined with the default height and give a strange aspect ratio.
    if (widthSet != heightSet) {
        return makeError("--width and --height must be given together");
    }
    if (config.testPattern && config.width % 2 != 0) {
        return makeError("--test-pattern produces YUYV, which requires an even --width");
    }
    return result;
}

std::string usageText(std::string_view programName) {
    const Config defaults;
    std::ostringstream out;
    out << "Usage: " << programName << " [options]\n"
        << "\n"
        << "Live, low-latency preview of a V4L2 (USB/UVC) camera.\n"
        << "\n"
        << "Options:\n"
        << "  -d, --device PATH|N  camera device or index (default " << defaults.device
        << "; 2 means /dev/video2)\n"
        << "      --width N        requested width, 16..7680 (default " << defaults.width << ")\n"
        << "      --height N       requested height, 16..4320 (default " << defaults.height
        << ")\n"
        << "      --fps N          requested frame rate, 1..240 (default " << defaults.fps
        << ")\n"
        << "      --format F       mjpg or yuyv (default " << toLower(toString(defaults.format))
        << ")\n"
        << "      --test-pattern   synthetic source instead of a camera\n"
        << "      --no-display     capture and decode without a window (benchmarking)\n"
        << "      --extra-render-delay MS  test aid: sleep MS after each frame to simulate a\n"
        << "                       slow consumer; frames get dropped, latency must not grow\n"
        << "      --stats-csv PATH write per-frame timings to a CSV file\n"
        << "      --prefer P       what to keep when the exact mode is unavailable:\n"
        << "                       fps (default) or resolution\n"
        << "      --list-modes     print the modes the camera offers and exit\n"
        << "      --no-adapt       keep the selected mode even if the hardware cannot keep up\n"
        << "  -h, --help           show this help and exit\n"
        << "      --version        show the version and exit\n"
        << "\n"
        << "The camera may not support the requested mode exactly; the mode it actually uses is\n"
        << "logged. Quit with Esc or q in the window, or Ctrl+C.\n"
        << "\n"
        << "Exit codes: 0 success, 1 runtime error, 2 invalid command line.\n"
        << "\n"
        << "Examples:\n"
        << "  " << programName << " --device /dev/video0 --width 1280 --height 720 --fps 30\n"
        << "  " << programName << " --format yuyv\n"
        << "  " << programName << " --test-pattern\n";
    return out.str();
}

} // namespace camview