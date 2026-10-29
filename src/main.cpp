#include "camera/V4l2Camera.hpp"
#include "util/Log.hpp"

#include <exception>
#include <string>
#include <string_view>

namespace camview {

// Temporary probe for step 5. Usage: camview [device] [yuyv|mjpg] [width] [height] [fps]
// Proper CLI parsing and validation come in step 8.
int run(int argc, char** argv) {
    const std::string device = argc > 1 ? argv[1] : "/dev/video0";
    const PixelFormat format = (argc > 2 && std::string_view(argv[2]) == "yuyv")
                                   ? PixelFormat::Yuyv
                                   : PixelFormat::Mjpeg;
    const int width = argc > 3 ? std::stoi(argv[3]) : 640;
    const int height = argc > 4 ? std::stoi(argv[4]) : 480;
    const double fps = argc > 5 ? std::stod(argv[5]) : 30.0;

    log::info("camview ", CAMVIEW_VERSION);

    V4l2Camera camera(device, CameraMode{format, width, height, fps});
    camera.open();
    camera.close();
    log::info("Device closed");
    return 0;
}

} // namespace camview

int main(int argc, char** argv) {
    try {
        return camview::run(argc, argv);
    } catch (const std::exception& e) {
        camview::log::error("Fatal: ", e.what());
        return 1;
    }
}