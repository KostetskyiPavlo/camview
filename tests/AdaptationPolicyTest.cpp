#include "app/AdaptationPolicy.hpp"

#include <gtest/gtest.h>

namespace camview {
namespace {

// Measured on a Raspberry Pi 2 with a 20 fps camera (frame interval 50 ms).
constexpr double kCameraFps = 20.0;

TEST(AdaptationPolicy, DowngradesWhenSlowProcessingLosesFrames) {
    // 1280x720: display 13 fps, decode 67 + render 10 ms.
    AdaptationPolicy policy;
    EXPECT_FALSE(policy.update(kCameraFps, 13.0, 77.0));
    EXPECT_FALSE(policy.update(kCameraFps, 13.0, 77.0));
    EXPECT_TRUE(policy.update(kCameraFps, 13.0, 77.0));
}

TEST(AdaptationPolicy, StaysWhenTheDisplayKeepsUpEvenIfProcessingIsTight) {
    // 800x600: decode 32 + render 18 = 50 ms, yet every frame is shown.
    AdaptationPolicy policy;
    for (int window = 0; window < 10; ++window) {
        EXPECT_FALSE(policy.update(kCameraFps, 20.0, 50.0));
    }
}

TEST(AdaptationPolicy, StaysForASlowConsumerWhoseProcessingIsFast) {
    // --extra-render-delay 200: display 4 fps, but decode 30 + render 6 = 36 ms.
    AdaptationPolicy policy;
    for (int window = 0; window < 10; ++window) {
        EXPECT_FALSE(policy.update(kCameraFps, 4.2, 36.0));
    }
}

TEST(AdaptationPolicy, OneGoodWindowRestartsTheCount) {
    AdaptationPolicy policy;
    EXPECT_FALSE(policy.update(kCameraFps, 13.0, 77.0));
    EXPECT_FALSE(policy.update(kCameraFps, 13.0, 77.0));
    EXPECT_FALSE(policy.update(kCameraFps, 20.0, 26.0));
    EXPECT_FALSE(policy.update(kCameraFps, 13.0, 77.0));
    EXPECT_FALSE(policy.update(kCameraFps, 13.0, 77.0));
    EXPECT_TRUE(policy.update(kCameraFps, 13.0, 77.0));
}

TEST(AdaptationPolicy, IgnoresAnIdleCamera) {
    AdaptationPolicy policy;
    for (int window = 0; window < 5; ++window) {
        EXPECT_FALSE(policy.update(0.0, 0.0, 77.0));
    }
}

TEST(AdaptationPolicy, NeedsFreshEvidenceAfterADowngrade) {
    AdaptationPolicy policy;
    static_cast<void>(policy.update(kCameraFps, 13.0, 77.0));
    static_cast<void>(policy.update(kCameraFps, 13.0, 77.0));
    ASSERT_TRUE(policy.update(kCameraFps, 13.0, 77.0));
    EXPECT_FALSE(policy.update(kCameraFps, 13.0, 77.0));
    EXPECT_FALSE(policy.update(kCameraFps, 13.0, 77.0));
    EXPECT_TRUE(policy.update(kCameraFps, 13.0, 77.0));
}

} // namespace
} // namespace camview