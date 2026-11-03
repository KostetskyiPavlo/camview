#include "camera/ModeSelector.hpp"

#include <algorithm>
#include <gtest/gtest.h>
#include <ostream>
#include <vector>

namespace camview {

// Lets GoogleTest print modes readably in failure messages.
void PrintTo(const CameraMode& mode, std::ostream* out) {
    *out << toString(mode);
}

namespace {

CameraMode mjpg(int width, int height, double fps) {
    return {PixelFormat::Mjpeg, width, height, fps};
}

CameraMode yuyv(int width, int height, double fps) {
    return {PixelFormat::Yuyv, width, height, fps};
}

// Exactly what the development camera (USB 32e6:9221) reports, in the driver's order.
// 1920x1080 comes last, so nothing may rely on the list being sorted.
std::vector<CameraMode> developmentCamera() {
    return {mjpg(1280, 720, 30), mjpg(960, 720, 30), mjpg(800, 600, 30),   mjpg(640, 480, 30),
            mjpg(640, 360, 30),  mjpg(320, 240, 30), mjpg(1920, 1080, 30), yuyv(640, 480, 30)};
}

TEST(ModeSelector, ReturnsNothingForAnEmptyList) {
    EXPECT_FALSE(selectMode({}, mjpg(640, 480, 30)).has_value());
}

TEST(ModeSelector, ReturnsAnExactMatchUnchanged) {
    EXPECT_EQ(selectMode(developmentCamera(), mjpg(800, 600, 30)), mjpg(800, 600, 30));
}

TEST(ModeSelector, TaskExample720p60FallsBackTo720p30) {
    EXPECT_EQ(selectMode(developmentCamera(), mjpg(1280, 720, 60)), mjpg(1280, 720, 30));
}

TEST(ModeSelector, PrefersFrameRateOverResolutionByDefault) {
    const std::vector<CameraMode> modes{mjpg(1280, 720, 30), mjpg(640, 480, 60)};
    EXPECT_EQ(selectMode(modes, mjpg(1280, 720, 60)), mjpg(640, 480, 60));
}

TEST(ModeSelector, ResolutionPriorityKeepsTheResolution) {
    const std::vector<CameraMode> modes{mjpg(1280, 720, 30), mjpg(640, 480, 60)};
    EXPECT_EQ(selectMode(modes, mjpg(1280, 720, 60), ModePriority::Resolution),
              mjpg(1280, 720, 30));
}

TEST(ModeSelector, KeepsTheRequestedFormatWhenTheCameraOffersIt) {
    EXPECT_EQ(selectMode(developmentCamera(), yuyv(1280, 720, 30)), yuyv(640, 480, 30));
}

TEST(ModeSelector, FallsBackToAnotherFormatWhenTheRequestedOneIsMissing) {
    const std::vector<CameraMode> modes{mjpg(1280, 720, 30), mjpg(640, 480, 30)};
    EXPECT_EQ(selectMode(modes, yuyv(640, 480, 30)), mjpg(640, 480, 30));
}

TEST(ModeSelector, PrefersMjpegWhenTheRequestedFormatIsNotOffered) {
    const std::vector<CameraMode> modes{yuyv(640, 480, 30), mjpg(640, 480, 30)};
    const CameraMode requested{PixelFormat::I420, 640, 480, 30};
    EXPECT_EQ(selectMode(modes, requested), mjpg(640, 480, 30));
}

TEST(ModeSelector, PicksTheSlowestRateThatMeetsTheRequest) {
    const std::vector<CameraMode> modes{mjpg(640, 480, 15), mjpg(640, 480, 30), mjpg(640, 480, 60)};
    EXPECT_EQ(selectMode(modes, mjpg(640, 480, 25)), mjpg(640, 480, 30));
}

TEST(ModeSelector, PicksTheFastestRateWhenNoneMeetsTheRequest) {
    const std::vector<CameraMode> modes{mjpg(640, 480, 15), mjpg(640, 480, 30), mjpg(640, 480, 60)};
    EXPECT_EQ(selectMode(modes, mjpg(640, 480, 90)), mjpg(640, 480, 60));
}

TEST(ModeSelector, TreatsNearlyEqualRatesAsMeetingTheRequest) {
    const std::vector<CameraMode> modes{mjpg(640, 480, 15), mjpg(640, 480, 29.97),
                                        mjpg(640, 480, 60)};
    EXPECT_EQ(selectMode(modes, mjpg(640, 480, 30)), mjpg(640, 480, 29.97));
}

TEST(ModeSelector, UnknownFrameRateIsLeastPreferred) {
    const std::vector<CameraMode> modes{mjpg(640, 480, 0.0), mjpg(640, 480, 15)};
    EXPECT_EQ(selectMode(modes, mjpg(640, 480, 30)), mjpg(640, 480, 15));
}

TEST(ModeSelector, ClosestResolutionUsesWidthPlusHeightDistance) {
    // |1024-960| + |576-720| = 208 beats 800x600 (248) and 1280x720 (400).
    EXPECT_EQ(selectMode(developmentCamera(), mjpg(1024, 576, 30)), mjpg(960, 720, 30));
}

TEST(ModeSelector, ResultDoesNotDependOnListOrder) {
    const std::vector<CameraMode> requests{mjpg(1280, 720, 60), mjpg(1024, 576, 30),
                                           yuyv(1920, 1080, 30), mjpg(100, 100, 5)};
    std::vector<CameraMode> reversed = developmentCamera();
    std::reverse(reversed.begin(), reversed.end());
    std::vector<CameraMode> rotated = developmentCamera();
    std::rotate(rotated.begin(), rotated.begin() + 3, rotated.end());

    for (const CameraMode& requested : requests) {
        const auto expected = selectMode(developmentCamera(), requested);
        EXPECT_EQ(selectMode(reversed, requested), expected) << toString(requested);
        EXPECT_EQ(selectMode(rotated, requested), expected) << toString(requested);
    }
}

TEST(ModeSelectorLower, StepsDownTheResolutionLadder) {
    const std::vector<CameraMode> modes = developmentCamera();
    EXPECT_EQ(selectLowerMode(modes, mjpg(1280, 720, 30)), mjpg(960, 720, 30));
    EXPECT_EQ(selectLowerMode(modes, mjpg(960, 720, 30)), mjpg(800, 600, 30));
    EXPECT_EQ(selectLowerMode(modes, mjpg(800, 600, 30)), mjpg(640, 480, 30));
    EXPECT_EQ(selectLowerMode(modes, mjpg(640, 480, 30)), mjpg(640, 360, 30));
    EXPECT_EQ(selectLowerMode(modes, mjpg(640, 360, 30)), mjpg(320, 240, 30));
}

TEST(ModeSelectorLower, ReturnsNothingAtTheSmallestMode) {
    EXPECT_FALSE(selectLowerMode(developmentCamera(), mjpg(320, 240, 30)).has_value());
}

TEST(ModeSelectorLower, KeepsThePixelFormat) {
    // The only YUYV mode is 640x480, so there is nowhere to go without switching formats.
    EXPECT_FALSE(selectLowerMode(developmentCamera(), yuyv(640, 480, 30)).has_value());
}

TEST(ModeSelectorLower, DoesNotTradeFrameRateForResolution) {
    const std::vector<CameraMode> modes{mjpg(1280, 720, 60), mjpg(640, 480, 30),
                                        mjpg(320, 240, 60)};
    EXPECT_EQ(selectLowerMode(modes, mjpg(1280, 720, 60)), mjpg(320, 240, 60));
}

} // namespace

} // namespace camview