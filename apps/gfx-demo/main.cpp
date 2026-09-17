// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
//
// A panel picture for checking gfx packing, drawing, rotation, and colour.
#include <array>
#include <cstddef>

import mm.display;
import mm.gfx;
import mm.mcu;

namespace {

constexpr unsigned int maximum_side = 480;
constexpr std::size_t maximum_row_bytes = (maximum_side + 7u) / 8u;
std::array<std::byte, maximum_row_bytes * maximum_side> frame;
std::array<std::byte, maximum_row_bytes * maximum_side> turned;
std::array<std::byte, maximum_side * 2u> row;
constexpr unsigned int swatch_side = 32;
std::array<std::byte, swatch_side * swatch_side * 2u> swatch_bytes;
constexpr unsigned int hold_ms = 4'000;

using mm::display::Color;
using mm::display::Status;
using mm::gfx::Surface;

// The three bands colour the same black marks on RGB565 panels. On a one-bit
// panel they remain black; white pixels remain white in either case.
constexpr mm::gfx::Palette black_on_white{
    mm::gfx::rgb565_black, mm::gfx::rgb565_white};
constexpr std::array<mm::gfx::Palette, 3> band_palettes{{
    {mm::gfx::rgb565_red, mm::gfx::rgb565_white},
    {mm::gfx::rgb(0, 255, 0), mm::gfx::rgb565_white},
    {{0x001f}, mm::gfx::rgb565_white},
}};

[[nodiscard]] Status draw(Surface surface) {
    const int width = static_cast<int>(surface.width);
    const int height = static_cast<int>(surface.height);
    const int short_side = width < height ? width : height;
    const int margin = short_side / 12;
    const int box_width = short_side / 3;
    const int box_height = short_side / 6;

    if (mm::gfx::fill(surface, Color::White) != Status::Ok)
        return Status::BadArgument;
    if (mm::gfx::line(surface, 0, 0, width - 1, height - 1,
                      Color::Black) != Status::Ok ||
        mm::gfx::line(surface, width - 1, 0, 0, height - 1,
                      Color::Black) != Status::Ok ||
        mm::gfx::rectangle(surface, margin, margin,
                            surface.width - 2u * margin,
                            surface.height - 2u * margin,
                            Color::Black) != Status::Ok ||
        mm::gfx::fill_rectangle(surface, (width - box_width) / 2,
                                (height - box_height) / 2, box_width,
                                box_height, Color::Black) != Status::Ok ||
        mm::gfx::rectangle(surface, (width - box_width) / 2 - margin,
                            (height - box_height) / 2 - margin,
                            box_width + 2u * margin,
                            box_height + 2u * margin,
                            Color::Black) != Status::Ok)
        return Status::BadArgument;

    // Corner dots make the orientation visible even on a monochrome panel.
    for (int i = 0; i < 4; ++i)
        if (mm::gfx::pixel(surface, 3 * margin + i, 2 * margin,
                            Color::Black) != Status::Ok)
            return Status::BadArgument;
    return Status::Ok;
}

[[nodiscard]] Status draw_swatch(mm::display::Display& display,
                                  mm::display::Geometry geometry) {
    if (geometry.width < swatch_side || geometry.height < swatch_side)
        return Status::Ok;
    const Surface swatch{swatch_side, swatch_side, 16, swatch_bytes};
    if (mm::gfx::fill(swatch, mm::gfx::rgb(0, 0, 255)) != Status::Ok ||
        mm::gfx::line(swatch, 0, 0, swatch_side - 1, swatch_side - 1,
                      mm::gfx::rgb565_white) != Status::Ok ||
        mm::gfx::rectangle(swatch, 2, 2, swatch_side - 4,
                            swatch_side - 4,
                            mm::gfx::rgb565_red) != Status::Ok ||
        mm::gfx::fill_rectangle(swatch, 12, 12, 8, 8,
                                mm::gfx::rgb(0, 255, 0)) != Status::Ok ||
        mm::gfx::pixel(swatch, 16, 16, mm::gfx::rgb565_white) != Status::Ok)
        return Status::BadArgument;
    return mm::gfx::write(display, swatch,
                           (geometry.width - swatch_side) / 2u,
                           (geometry.height - swatch_side) / 2u);
}

[[nodiscard]] int show(mm::display::Display& display,
                       mm::display::Geometry geometry, unsigned int turns) {
    const bool sideways = turns % 2u != 0;
    const unsigned int width = sideways ? geometry.height : geometry.width;
    const unsigned int height = sideways ? geometry.width : geometry.height;
    const Surface source{width, height, 1, frame};
    const Surface destination{geometry.width, geometry.height, 1, turned};

    if (display.clear(Color::White) != Status::Ok) return 4;
    if (draw(source) != Status::Ok ||
        mm::gfx::rotate(source, turns, destination) != Status::Ok)
        return 5;

    const unsigned int band = width / 3u;
    std::array<mm::gfx::Region, 3> regions{};
    for (unsigned int i = 0; i < regions.size(); ++i) {
        const unsigned int x = i * band;
        const unsigned int extent = i == 2 ? width - x : band;
        regions[i] = {
            mm::gfx::rotate({x, 0, extent, height}, width, height, turns),
            band_palettes[i]};
    }
    const auto result = geometry.bits_per_pixel == 1
        ? mm::gfx::write(display, destination, 0, 0)
        : mm::gfx::write(display, destination, 0, 0,
                         black_on_white, regions, row);
    if (result != Status::Ok) return 6;
    if (geometry.bits_per_pixel == 16 &&
        draw_swatch(display, geometry) != Status::Ok)
        return 6;
    if (display.refresh(mm::display::Refresh::Full) != Status::Ok) return 7;
    return 0;
}

}

int main() {
    auto& display = mm::display::selected_display();
    if (display.initialize() != Status::Ok) return 1;
    const auto geometry = display.geometry();
    if (geometry.width == 0 || geometry.height == 0 ||
        (geometry.bits_per_pixel != 1 && geometry.bits_per_pixel != 16))
        return 2;
    if (geometry.width > maximum_side || geometry.height > maximum_side)
        return 3;

    for (unsigned int turns = 0; turns < 4; ++turns) {
        if (const int step = show(display, geometry, turns); step != 0)
            return step;
        if (mm::mcu::delay_ms(hold_ms) != mm::mcu::Status::Ok) return 8;
    }
    if (display.sleep() != Status::Ok) return 9;
    return 0;
}
