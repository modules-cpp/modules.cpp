// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
//
// Something a person can look at. The smoke applications answer with an exit
// code, which is the right shape for a test and no use at all when the question
// is whether the panel works. This draws three colour bars, holds them long
// enough to be seen, rotates them twice so a still frame cannot be mistaken for
// a working refresh, and then sleeps.
//
// It names mm.display and mm.mcu and no board, so it runs wherever both are
// bound: a panel on a controller, or a Linux framebuffer through DRM.
#include <array>
#include <cstddef>
#include <span>

import mm.display;
import mm.mcu;

namespace {

// One row at a time, so the buffer does not grow with the panel. 8 KiB spans a
// 4096-pixel row at sixteen bits, which is wider than anything this is likely
// to meet.
constexpr std::size_t maximum_row_bytes = 8'192;
std::array<std::byte, maximum_row_bytes> row;

// RGB565, most significant byte first, which is how mm.display packs sixteen
// bits and what a provider hands to its panel unchanged.
constexpr unsigned short red = 0xf800;
constexpr unsigned short green = 0x07e0;
constexpr unsigned short blue = 0x001f;

constexpr unsigned int hold_ms = 1'500;
constexpr unsigned int rotations = 3;

void fill_row(unsigned int width, const unsigned short (&colours)[3]) {
    for (unsigned int x = 0; x < width; ++x) {
        const unsigned int band = (x * 3) / width;
        const unsigned short colour = colours[band < 3 ? band : 2];
        row[x * 2] = static_cast<std::byte>(colour >> 8);
        row[x * 2 + 1] = static_cast<std::byte>(colour & 0xff);
    }
}

}

int main() {
    auto& display = mm::display::selected_display();

    // initialize first, then ask. A controller driver knows its geometry from
    // the wiring it was built with, but a provider over a Linux framebuffer
    // learns the mode from the connector it discovers, and has nothing to
    // report until it has one. mm.display orders nothing here, so an
    // application that wants both kinds of provider asks after initializing.
    if (display.initialize() != mm::display::Status::Ok) return 1;

    const auto geometry = display.geometry();
    if (geometry.width == 0 || geometry.height == 0 ||
        geometry.bits_per_pixel != 16)
        return 2;

    const std::size_t row_bytes = static_cast<std::size_t>(geometry.width) * 2;
    if (row_bytes > row.size()) return 3;

    if (display.clear(mm::display::Color::Black) != mm::display::Status::Ok)
        return 4;

    unsigned short colours[3] = {red, green, blue};
    for (unsigned int turn = 0; turn < rotations; ++turn) {
        fill_row(geometry.width, colours);
        for (unsigned int y = 0; y < geometry.height; ++y) {
            if (display.write({0, y, geometry.width, 1},
                              std::span<const std::byte>{row.data(), row_bytes}) !=
                mm::display::Status::Ok)
                return 5;
        }
        // write changes display memory; refresh is what makes it visible.
        if (display.refresh(mm::display::Refresh::Full) != mm::display::Status::Ok)
            return 6;

        // Without this the frame is gone before anyone sees it: a provider that
        // owns the display releases it when this process ends.
        if (mm::mcu::delay_ms(hold_ms) != mm::mcu::Status::Ok) return 7;

        const unsigned short first = colours[0];
        colours[0] = colours[1];
        colours[1] = colours[2];
        colours[2] = first;
    }

    if (display.sleep() != mm::display::Status::Ok) return 8;
    return 0;
}
