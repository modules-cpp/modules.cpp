// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
#include <array>
#include <cstddef>
#include <span>

import mm.display;

namespace {

constexpr std::size_t maximum_frame_bytes = 8'192;
std::array<std::byte, maximum_frame_bytes> frame;

}

int main() {
    auto& display = mm::display::selected_display();
    const auto geometry = display.geometry();
    if (geometry.width == 0 || geometry.height == 0 ||
        geometry.bits_per_pixel != 1)
        return 1;

    const std::size_t row_bytes = (geometry.width + 7) / 8;
    const std::size_t frame_bytes = row_bytes * geometry.height;
    if (frame_bytes > frame.size()) return 2;

    for (unsigned int y = 0; y < geometry.height; ++y) {
        for (std::size_t x = 0; x < row_bytes; ++x) {
            const bool white = ((x + y / 8) % 2) != 0;
            frame[y * row_bytes + x] = white ? std::byte{0xff} : std::byte{0x00};
        }
    }

    if (display.initialize() != mm::display::Status::Ok) return 3;
    if (display.write({0, 0, geometry.width, geometry.height},
                      std::span<const std::byte>{frame.data(), frame_bytes}) !=
        mm::display::Status::Ok)
        return 4;
    if (display.refresh(mm::display::Refresh::Full) != mm::display::Status::Ok)
        return 5;
    if (display.sleep() != mm::display::Status::Ok) return 6;
    return 0;
}
