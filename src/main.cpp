#include "camera/TestPatternSource.hpp"
#include "util/Clock.hpp"
#include "util/Log.hpp"

#include <exception>

namespace camview {

// Temporary smoke test for step 3; replaced by App in later steps.
int run() {
    TestPatternSource source(640, 480, 30.0);
    source.open();
    log::info("camview ", CAMVIEW_VERSION);
    log::info("Source: ", source.description());
    log::info("Mode:   ", toString(source.mode()));

    constexpr int kFramesToCapture = 90;
    constexpr auto kCaptureTimeout = std::chrono::milliseconds(1000);

    Frame frame;
    const TimePoint start = Clock::now();
    for (int i = 0; i < kFramesToCapture; ++i) {
        if (source.capture(frame, kCaptureTimeout) != CaptureStatus::Ok) {
            log::error("Capture failed at frame ", i);
            return 1;
        }
        if (frame.sequence % 30 == 0) {
            log::info("Frame #", frame.sequence, " ", frame.width, "x", frame.height, ", ",
                      frame.data.size(), " bytes, age ",
                      toMilliseconds(frame.dequeueTime - frame.captureTime), " ms");
        }
    }
    const double seconds = std::chrono::duration<double>(Clock::now() - start).count();
    log::info("Captured ", kFramesToCapture, " frames in ", seconds, " s -> ",
              kFramesToCapture / seconds, " fps");

    source.close();
    return 0;
}

} // namespace camview

int main() {
    try {
        return camview::run();
    } catch (const std::exception& e) {
        camview::log::error("Fatal: ", e.what());
        return 1;
    }
}