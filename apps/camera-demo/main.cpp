// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
#include <array>
#include <cstddef>
#include <span>
import mm.camera;
import mm.display;
namespace { std::array<std::byte, 324 * 244> frame; }
int main() {
    auto& display = mm::display::selected_display();
    auto& camera = mm::camera::selected_camera();
    if (display.initialize() != mm::display::Status::Ok) return 2;
    if (camera.initialize() != mm::camera::Status::Ok) return 3;
    const auto captured = camera.capture(frame, 2000);
    const auto released = camera.sleep();
    if (captured != mm::camera::Status::Ok) return 4;
    if (released != mm::camera::Status::Ok) return 8;
    const auto sensor = camera.geometry();
    const auto panel = display.geometry();
    if (sensor.width != 324 || sensor.height != 244 || panel.width != 240 ||
        panel.height != 135 || panel.bits_per_pixel != 16) return 5;
    std::array<std::byte, 240 * 2> row;
    for (unsigned int y = 0; y < panel.height; ++y) {
        for (unsigned int x = 0; x < panel.width; ++x) {
            const auto grey = std::to_integer<unsigned int>(frame[
                (y * sensor.height / panel.height) * sensor.width +
                x * sensor.width / panel.width]);
            const auto pixel = ((grey & 0xf8) << 8) | ((grey & 0xfc) << 3) | (grey >> 3);
            row[x * 2] = static_cast<std::byte>(pixel >> 8);
            row[x * 2 + 1] = static_cast<std::byte>(pixel & 0xff);
        }
        if (display.write({0, y, panel.width, 1}, row) != mm::display::Status::Ok) return 6;
    }
    return display.refresh(mm::display::Refresh::Full) == mm::display::Status::Ok ? 0 : 7;
}
