// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
module;

#include <cstddef>
#include <limits>
#include <span>

export module mm.gfx;

import mm.display;

export namespace mm::gfx {

inline constexpr unsigned int maximum_side = 1u << 20;

struct Surface {
    unsigned int width = 0;
    unsigned int height = 0;
    unsigned int bits_per_pixel = 0;
    std::span<std::byte> pixels;

    [[nodiscard]] constexpr std::size_t row_bytes() const {
        if (width == 0 || width > maximum_side) return 0;
        if (bits_per_pixel == 1) return (static_cast<std::size_t>(width) + 7u) / 8u;
        if (bits_per_pixel == 16) return static_cast<std::size_t>(width) * 2u;
        return 0;
    }
    [[nodiscard]] constexpr std::size_t size() const {
        if (height == 0 || height > maximum_side) return 0;
        const auto row = row_bytes();
        if (row == 0 || row > std::numeric_limits<std::size_t>::max() / height)
            return 0;
        return row * height;
    }
    [[nodiscard]] constexpr bool valid() const {
        return size() != 0 && pixels.size() >= size();
    }
};

struct Rgb565 { unsigned short value; };
inline constexpr Rgb565 rgb565_black{0x0000};
inline constexpr Rgb565 rgb565_white{0xffff};
inline constexpr Rgb565 rgb565_red{0xf800};
[[nodiscard]] constexpr Rgb565 rgb(unsigned char r, unsigned char g,
                                    unsigned char b) {
    return {static_cast<unsigned short>(((r >> 3u) << 11u) |
                                        ((g >> 2u) << 5u) | (b >> 3u))};
}

struct Palette { Rgb565 ink; Rgb565 paper; };
struct Region { mm::display::Rectangle area; Palette palette; };

using Status = mm::display::Status;
[[nodiscard]] Status fill(Surface, mm::display::Color);
[[nodiscard]] Status fill(Surface, Rgb565);
[[nodiscard]] Status pixel(Surface, int x, int y, mm::display::Color);
[[nodiscard]] Status pixel(Surface, int x, int y, Rgb565);
[[nodiscard]] Status line(Surface, int x0, int y0, int x1, int y1,
                          mm::display::Color);
[[nodiscard]] Status line(Surface, int x0, int y0, int x1, int y1, Rgb565);
[[nodiscard]] Status rectangle(Surface, int x, int y, unsigned int width,
                               unsigned int height, mm::display::Color);
[[nodiscard]] Status rectangle(Surface, int x, int y, unsigned int width,
                               unsigned int height, Rgb565);
[[nodiscard]] Status fill_rectangle(Surface, int x, int y, unsigned int width,
                                    unsigned int height, mm::display::Color);
[[nodiscard]] Status fill_rectangle(Surface, int x, int y, unsigned int width,
                                    unsigned int height, Rgb565);

[[nodiscard]] Status expand_row(std::span<const std::byte> packed_row,
                                unsigned int width, Palette palette,
                                std::span<std::byte> rgb565_row);
[[nodiscard]] Status rotate(Surface source, unsigned int quarter_turns,
                            Surface turned);

// Clip before turning. An empty intersection returns an empty rectangle.
[[nodiscard]] constexpr mm::display::Rectangle rotate(
    mm::display::Rectangle area, unsigned int width, unsigned int height,
    unsigned int quarter_turns) {
    const auto x0 = static_cast<unsigned long long>(area.x);
    const auto y0 = static_cast<unsigned long long>(area.y);
    const auto x1 = x0 + area.width;
    const auto y1 = y0 + area.height;
    if (area.width == 0 || area.height == 0 || x0 >= width || y0 >= height)
        return {};
    const auto right = x1 < width ? x1 : width;
    const auto bottom = y1 < height ? y1 : height;
    const auto x = static_cast<unsigned int>(x0);
    const auto y = static_cast<unsigned int>(y0);
    const auto w = static_cast<unsigned int>(right - x0);
    const auto h = static_cast<unsigned int>(bottom - y0);
    switch (quarter_turns % 4u) {
        case 1: return {height - y - h, x, h, w};
        case 2: return {width - x - w, height - y - h, w, h};
        case 3: return {y, width - x - w, h, w};
        default: return {x, y, w, h};
    }
}

[[nodiscard]] Status write(mm::display::Display&, Surface,
                           unsigned int display_x, unsigned int display_y);
[[nodiscard]] Status write(mm::display::Display&, Surface,
                           unsigned int display_x, unsigned int display_y,
                           Palette base, std::span<const Region> regions,
                           std::span<std::byte> scratch);

}
