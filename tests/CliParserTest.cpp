#include "app/CliParser.hpp"

#include <gtest/gtest.h>

#include <string>
#include <vector>

namespace camview {
namespace {

// Builds a mutable argv (getopt may permute it) with the program name in front.
CliResult parse(std::vector<std::string> args) {
    args.insert(args.begin(), "camview");
    std::vector<char*> argv;
    for (std::string& arg : args) {
        argv.push_back(arg.data());
    }
    argv.push_back(nullptr);
    return parseCommandLine(static_cast<int>(argv.size() - 1), argv.data());
}

void expectError(const std::vector<std::string>& args, const std::string& fragment) {
    const CliResult result = parse(args);
    EXPECT_EQ(result.action, CliResult::Action::Error) << "args: " << ::testing::PrintToString(args);
    EXPECT_NE(result.error.find(fragment), std::string::npos) << "error: " << result.error;
}

TEST(CliParser, UsesDefaultsWithoutArguments) {
    const CliResult result = parse({});
    ASSERT_EQ(result.action, CliResult::Action::Run);
    EXPECT_EQ(result.config.device, "/dev/video0");
    EXPECT_EQ(result.config.format, PixelFormat::Mjpeg);
    EXPECT_EQ(result.config.width, 1280);
    EXPECT_EQ(result.config.height, 720);
    EXPECT_DOUBLE_EQ(result.config.fps, 60.0);
    EXPECT_TRUE(result.config.adapt);
    EXPECT_TRUE(result.config.display);
}

TEST(CliParser, ParsesTheTaskExample) {
    const CliResult result =
        parse({"--device", "/dev/video2", "--width", "1280", "--height", "720", "--fps", "30"});
    ASSERT_EQ(result.action, CliResult::Action::Run);
    EXPECT_EQ(result.config.device, "/dev/video2");
    EXPECT_EQ(result.config.width, 1280);
    EXPECT_EQ(result.config.height, 720);
    EXPECT_DOUBLE_EQ(result.config.fps, 30.0);
}

TEST(CliParser, AcceptsTheEqualsSyntax) {
    const CliResult result = parse({"--width=640", "--height=480", "--fps=29.97"});
    ASSERT_EQ(result.action, CliResult::Action::Run);
    EXPECT_EQ(result.config.width, 640);
    EXPECT_EQ(result.config.height, 480);
    EXPECT_DOUBLE_EQ(result.config.fps, 29.97);
}

TEST(CliParser, TurnsADeviceIndexIntoAPath) {
    EXPECT_EQ(parse({"-d", "2"}).config.device, "/dev/video2");
}

TEST(CliParser, ReadsFormatAndPriorityCaseInsensitively) {
    const CliResult result = parse({"--format", "YUYV", "--prefer", "Resolution"});
    ASSERT_EQ(result.action, CliResult::Action::Run);
    EXPECT_EQ(result.config.format, PixelFormat::Yuyv);
    EXPECT_TRUE(result.config.preferResolution);
}

TEST(CliParser, ReadsTheBooleanFlags) {
    const CliResult result =
        parse({"--test-pattern", "--no-display", "--no-adapt", "--list-modes"});
    ASSERT_EQ(result.action, CliResult::Action::Run);
    EXPECT_TRUE(result.config.testPattern);
    EXPECT_FALSE(result.config.display);
    EXPECT_FALSE(result.config.adapt);
    EXPECT_TRUE(result.config.listModes);
}

TEST(CliParser, ReportsHelpAndVersion) {
    EXPECT_EQ(parse({"--help"}).action, CliResult::Action::Help);
    EXPECT_EQ(parse({"-h"}).action, CliResult::Action::Help);
    EXPECT_EQ(parse({"--version"}).action, CliResult::Action::Version);
}

TEST(CliParser, RejectsNonNumericAndPartlyNumericValues) {
    expectError({"--width", "abc", "--height", "720"}, "--width");
    expectError({"--fps", "30x"}, "--fps");
    expectError({"--fps", "nan"}, "--fps");
}

TEST(CliParser, RejectsOutOfRangeValues) {
    expectError({"--fps", "0"}, "--fps");
    expectError({"--width", "8000", "--height", "720"}, "--width");
    expectError({"--width", "640", "--height", "8"}, "--height");
    expectError({"--extra-render-delay", "5000"}, "--extra-render-delay");
}

TEST(CliParser, RequiresWidthAndHeightTogether) {
    expectError({"--width", "1280"}, "together");
}

TEST(CliParser, RejectsUnknownOptionsMissingValuesAndExtraArguments) {
    expectError({"--bogus"}, "unknown option '--bogus'");
    expectError({"-x"}, "unknown option '-x'");
    expectError({"--device"}, "'--device' requires a value");
    expectError({"extra"}, "unexpected argument 'extra'");
    expectError({"--format", "h264"}, "--format");
    expectError({"--prefer", "speed"}, "--prefer");
}

TEST(CliParser, RejectsAnOddWidthForTheTestPattern) {
    expectError({"--test-pattern", "--width", "641", "--height", "480"}, "even");
}

TEST(CliParser, CanBeCalledRepeatedly) {
    // getopt keeps global state; the parser must reset it every time.
    ASSERT_EQ(parse({"--bogus"}).action, CliResult::Action::Error);
    const CliResult result = parse({"--width", "640", "--height", "480"});
    ASSERT_EQ(result.action, CliResult::Action::Run);
    EXPECT_EQ(result.config.width, 640);
}

} // namespace
} // namespace camview