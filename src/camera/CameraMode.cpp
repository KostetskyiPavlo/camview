#include "camera/CameraMode.hpp"

#include <cstdio>

namespace camview {

std::string toString(const CameraMode& mode) {
    char buffer[64];
    const std::string_view format = toString(mode.format);
    std::snprintf(buffer, sizeof(buffer), "%.*s %dx%d @ %.2f fps", static_cast<int>(format.size()),
                  format.data(), mode.width, mode.height, mode.fps);
    return buffer;
}

} // namespace camview