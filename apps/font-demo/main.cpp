// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
//
// The fonts, held long enough to be read. On a one-bit panel it renders black
// text on the white frame and writes the packed rows; on a sixteen-bit panel
// it composes the same one-bit frame and expands each row to RGB565 before
// writing. Two sizes, centred, with a line that uses the whole Polish
// charset, so a person can tell a working font from a scrambled one.
#include <array>
#include <cstddef>
#include <span>

import mm.display;
import mm.fonts;
import mm.mcu;

namespace {

// The committed tables' geometry, in the line order the frame lays them out.
constexpr unsigned int y16_first = 0;
constexpr unsigned int y16_second = mm::fonts::kMono16.height;
constexpr unsigned int y12_first = 2u * mm::fonts::kMono16.height;
constexpr unsigned int y12_second = y12_first + mm::fonts::kMono12.line_height;
constexpr unsigned int block_rows = y12_second + mm::fonts::kMono12.height;

// One bit per pixel for the composed frame: the widest target panel times the
// whole text block.
constexpr std::size_t maximum_frame_bytes = 240u * block_rows;
std::array<std::byte, maximum_frame_bytes> frame;

// One expanded RGB565 row for the sixteen-bit path.
std::array<std::byte, 2u * 240u> row;

constexpr unsigned int hold_ms = 4'000;

// ŁóżźĆ żółć, the committed charset's Latin Extended block. Named in bytes so
// the source carries no encoding of its own.
const char8_t polish[] = {
    0xc5, 0x82,  // Ł
    0xc3, 0xb3,  // ó
    0xc5, 0xbc,  // ż
    0xc5, 0xba,  // ź
    0xc4, 0x86,  // Ć
    u8' ',
    0xc5, 0xbc,  // ż
    0xc3, 0xb3,  // ó
    0xc5, 0x82,  // ł
    0xc4, 0x87,  // ć
};

struct Line {
    const char8_t* text;
    std::size_t size;
    const mm::fonts::Font* font;
    unsigned int y;
};

const Line lines[4] = {
    {u8"modules.cpp", 11, &mm::fonts::kMono16, y16_first},
    {u8"16px mono", 9, &mm::fonts::kMono16, y16_second},
    {u8"12px mono 0123", 14, &mm::fonts::kMono12, y12_first},
    {polish, 18, &mm::fonts::kMono12, y12_second},
};

}

int main() {
    auto& display = mm::display::selected_display();
    if (display.initialize() != mm::display::Status::Ok) return 1;

    const auto geometry = display.geometry();
    if (geometry.width == 0 || geometry.height == 0 ||
        (geometry.bits_per_pixel != 1 && geometry.bits_per_pixel != 16))
        return 2;

    const std::size_t frame_width = geometry.width;  // one bit per pixel
    if (frame_width * block_rows > frame.size() || frame_width * 2u > row.size())
        return 3;

    // White is 1 in a one-bit frame: clear the panel, then mark the composed
    // frame to match before drawing black text over it.
    if (display.clear(mm::display::Color::White) != mm::display::Status::Ok)
        return 4;
    frame.fill(std::byte{0xff});

    for (const Line& line : lines) {
        const auto metrics = mm::fonts::measure(line.text, line.size, *line.font);
        const unsigned int x = metrics.width < geometry.width
            ? (geometry.width - metrics.width) / 2
            : 0;
        if (mm::fonts::render(line.text, line.size, *line.font,
                              mm::display::Color::Black,
                              mm::display::Color::White, x, line.y, frame,
                              frame_width) != mm::display::Status::Ok)
            return 5;
    }

    for (unsigned int y = 0; y < block_rows; ++y) {
        const std::span<const std::byte> packed{frame.data() + y * frame_width,
                                                frame_width};
        if (geometry.bits_per_pixel == 1) {
            if (display.write({0, y, geometry.width, 1}, packed) !=
                mm::display::Status::Ok)
                return 6;
        } else {
            if (mm::fonts::expand_row(packed, frame_width, 0x0000, 0xffff, row) !=
                mm::display::Status::Ok)
                return 6;
            if (display.write({0, y, geometry.width, 1},
                              std::span<const std::byte>{row.data(), frame_width * 2}) !=
                mm::display::Status::Ok)
                return 6;
        }
    }

    if (display.refresh(mm::display::Refresh::Full) != mm::display::Status::Ok)
        return 7;

    // Without this the frame is gone before anyone sees it: a provider that
    // owns the display releases it when this process ends.
    if (mm::mcu::delay_ms(hold_ms) != mm::mcu::Status::Ok) return 8;

    if (display.sleep() != mm::display::Status::Ok) return 9;
    return 0;
}
