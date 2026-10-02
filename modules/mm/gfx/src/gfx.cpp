// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
module;

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <limits>
#include <span>

module mm.gfx;

namespace mm::gfx {
namespace {

[[nodiscard]] Status check(Surface surface, unsigned int depth) {
    if (!surface.valid() || surface.bits_per_pixel != depth)
        return mm::display::Status::BadArgument;
    return mm::display::Status::Ok;
}

[[nodiscard]] bool inside(Surface surface, long long x, long long y) {
    return x >= 0 && y >= 0 && x < surface.width && y < surface.height;
}

void set(Surface surface, unsigned int x, unsigned int y, unsigned short color) {
    const auto row = static_cast<std::size_t>(y) * surface.row_bytes();
    if (surface.bits_per_pixel == 1) {
        auto& byte = surface.pixels[row + x / 8u];
        const auto mask = static_cast<std::byte>(0x80u >> (x % 8u));
        byte = color == 0xffff ? byte | mask : byte & ~mask;
    } else {
        const auto at = row + static_cast<std::size_t>(x) * 2u;
        surface.pixels[at] = static_cast<std::byte>(color >> 8u);
        surface.pixels[at + 1u] = static_cast<std::byte>(color & 0xffu);
    }
}

[[nodiscard]] Status draw_check(Surface surface, unsigned int depth,
                                unsigned short color) {
    const auto status = check(surface, depth);
    if (status != mm::display::Status::Ok) return status;
    if (depth == 1 && color != 0 && color != 0xffff)
        return mm::display::Status::Unsupported;
    return mm::display::Status::Ok;
}

[[nodiscard]] unsigned short value(mm::display::Color color) {
    return color == mm::display::Color::White ? 0xffffu :
           color == mm::display::Color::Black ? 0u : 0xf800u;
}

[[nodiscard]] Status fill_impl(Surface surface, unsigned int depth,
                               unsigned short color) {
    const auto status = draw_check(surface, depth, color);
    if (status != mm::display::Status::Ok) return status;
    if (depth == 1) {
        const auto full = static_cast<std::byte>(color == 0 ? 0 : 0xff);
        const auto bytes = surface.size();
        for (std::size_t i = 0; i < bytes; ++i)
            surface.pixels[i] = full;
        if (surface.width % 8u != 0)
            for (unsigned int y = 0; y < surface.height; ++y)
                surface.pixels[static_cast<std::size_t>(y + 1u) * surface.row_bytes() - 1u]
                    &= static_cast<std::byte>(0xffu << (8u - surface.width % 8u));
    } else {
        for (unsigned int y = 0; y < surface.height; ++y)
            for (unsigned int x = 0; x < surface.width; ++x)
                set(surface, x, y, color);
    }
    return mm::display::Status::Ok;
}

[[nodiscard]] Status pixel_impl(Surface surface, int x, int y,
                                unsigned int depth, unsigned short color) {
    const auto status = draw_check(surface, depth, color);
    if (status != mm::display::Status::Ok) return status;
    if (inside(surface, x, y))
        set(surface, static_cast<unsigned int>(x), static_cast<unsigned int>(y), color);
    return mm::display::Status::Ok;
}

[[nodiscard]] unsigned int outcode(Surface surface, long long x, long long y) {
    return (x < 0 ? 1u : x >= surface.width ? 2u : 0u) |
           (y < 0 ? 4u : y >= surface.height ? 8u : 0u);
}

// Interpolation during clipping. Its factors are bounded by differences of
// int endpoints and a panel boundary; their product fits unsigned long long.
[[nodiscard]] long long mul_div(long long a, long long b, long long c) {
    const auto magnitude = [](long long n) {
        return static_cast<unsigned long long>(n < 0 ? -n : n);
    };
    const auto quotient = magnitude(a) * magnitude(b) / magnitude(c);
    return ((a < 0) != (b < 0)) != (c < 0)
               ? -static_cast<long long>(quotient)
               : static_cast<long long>(quotient);
}

[[nodiscard]] bool clip_line(Surface surface, long long& x0, long long& y0,
                             long long& x1, long long& y1) {
    for (;;) {
        const auto a = outcode(surface, x0, y0);
        const auto b = outcode(surface, x1, y1);
        if ((a | b) == 0) return true;
        if ((a & b) != 0) return false;
        const auto outside = a != 0 ? a : b;
        long long x = 0;
        long long y = 0;
        if ((outside & 4u) != 0) {
            y = 0;
            x = x0 + mul_div(x1 - x0, y - y0, y1 - y0);
        } else if ((outside & 8u) != 0) {
            y = surface.height - 1u;
            x = x0 + mul_div(x1 - x0, y - y0, y1 - y0);
        } else if ((outside & 2u) != 0) {
            x = surface.width - 1u;
            y = y0 + mul_div(y1 - y0, x - x0, x1 - x0);
        } else {
            x = 0;
            y = y0 + mul_div(y1 - y0, x - x0, x1 - x0);
        }
        if (outside == a) { x0 = x; y0 = y; }
        else { x1 = x; y1 = y; }
    }
}

[[nodiscard]] Status line_impl(Surface surface, int x0, int y0, int x1, int y1,
                               unsigned int depth, unsigned short color) {
    const auto status = draw_check(surface, depth, color);
    if (status != mm::display::Status::Ok) return status;
    long long ax = x0, ay = y0, bx = x1, by = y1;
    if (!clip_line(surface, ax, ay, bx, by)) return mm::display::Status::Ok;
    int x = static_cast<int>(ax), y = static_cast<int>(ay);
    const int tx = static_cast<int>(bx), ty = static_cast<int>(by);
    const int dx = std::abs(tx - x), dy = std::abs(ty - y);
    const int sx = x < tx ? 1 : -1, sy = y < ty ? 1 : -1;
    int error = dx - dy;
    for (;;) {
        set(surface, static_cast<unsigned int>(x), static_cast<unsigned int>(y), color);
        if (x == tx && y == ty) break;
        const int twice = 2 * error;
        if (twice > -dy) { error -= dy; x += sx; }
        if (twice < dx) { error += dx; y += sy; }
    }
    return mm::display::Status::Ok;
}

struct Box { long long left, top, right, bottom; };
[[nodiscard]] Box box(Surface surface, int x, int y,
                      unsigned int width, unsigned int height) {
    return {std::max(0ll, static_cast<long long>(x)),
            std::max(0ll, static_cast<long long>(y)),
            std::min(static_cast<long long>(surface.width),
                     static_cast<long long>(x) + width),
            std::min(static_cast<long long>(surface.height),
                     static_cast<long long>(y) + height)};
}

[[nodiscard]] Status rectangle_impl(Surface surface, int x, int y,
                                    unsigned int width, unsigned int height,
                                    unsigned int depth, unsigned short color,
                                    bool filled) {
    const auto status = draw_check(surface, depth, color);
    if (status != mm::display::Status::Ok) return status;
    if (width == 0 || height == 0) return mm::display::Status::Ok;
    const auto bounds = box(surface, x, y, width, height);
    if (bounds.left >= bounds.right || bounds.top >= bounds.bottom)
        return mm::display::Status::Ok;
    const auto far_x = static_cast<long long>(x) + width - 1;
    const auto far_y = static_cast<long long>(y) + height - 1;
    for (auto py = bounds.top; py < bounds.bottom; ++py)
        for (auto px = bounds.left; px < bounds.right; ++px)
            if (filled || py == y || py == far_y || px == x || px == far_x)
                set(surface, static_cast<unsigned int>(px),
                    static_cast<unsigned int>(py), color);
    return mm::display::Status::Ok;
}

[[nodiscard]] Status display_check(mm::display::Display& display,
                                   Surface surface, unsigned int x,
                                   unsigned int y) {
    if (!surface.valid()) return mm::display::Status::BadArgument;
    const auto geometry = display.geometry();
    if (geometry.bits_per_pixel != 1 && geometry.bits_per_pixel != 16)
        return mm::display::Status::Unsupported;
    if (x >= geometry.width || y >= geometry.height ||
        surface.width > geometry.width - x ||
        surface.height > geometry.height - y)
        return mm::display::Status::BadArgument;
    return mm::display::Status::Ok;
}

[[nodiscard]] bool covers(mm::display::Rectangle area,
                          unsigned int x, unsigned int y) {
    return area.width != 0 && area.height != 0 && x >= area.x && y >= area.y &&
           static_cast<unsigned long long>(x) <
               static_cast<unsigned long long>(area.x) + area.width &&
           static_cast<unsigned long long>(y) <
               static_cast<unsigned long long>(area.y) + area.height;
}

}

Status fill(Surface s, mm::display::Color c) { return fill_impl(s, 1, value(c)); }
Status fill(Surface s, Rgb565 c) { return fill_impl(s, 16, c.value); }
Status pixel(Surface s, int x, int y, mm::display::Color c) {
    return pixel_impl(s, x, y, 1, value(c));
}
Status pixel(Surface s, int x, int y, Rgb565 c) {
    return pixel_impl(s, x, y, 16, c.value);
}
Status line(Surface s, int x0, int y0, int x1, int y1,
            mm::display::Color c) {
    return line_impl(s, x0, y0, x1, y1, 1, value(c));
}
Status line(Surface s, int x0, int y0, int x1, int y1, Rgb565 c) {
    return line_impl(s, x0, y0, x1, y1, 16, c.value);
}
Status rectangle(Surface s, int x, int y, unsigned int w, unsigned int h,
                 mm::display::Color c) {
    return rectangle_impl(s, x, y, w, h, 1, value(c), false);
}
Status rectangle(Surface s, int x, int y, unsigned int w, unsigned int h,
                 Rgb565 c) {
    return rectangle_impl(s, x, y, w, h, 16, c.value, false);
}
Status fill_rectangle(Surface s, int x, int y, unsigned int w, unsigned int h,
                      mm::display::Color c) {
    return rectangle_impl(s, x, y, w, h, 1, value(c), true);
}
Status fill_rectangle(Surface s, int x, int y, unsigned int w, unsigned int h,
                      Rgb565 c) {
    return rectangle_impl(s, x, y, w, h, 16, c.value, true);
}

Status expand_row(std::span<const std::byte> packed, unsigned int width,
                  Palette palette, std::span<std::byte> out) {
    const auto row_bytes = static_cast<std::size_t>(width / 8u) + (width % 8u != 0);
    if (width == 0 || static_cast<std::size_t>(width) >
                          std::numeric_limits<std::size_t>::max() / 2u ||
        packed.size() < row_bytes || out.size() < static_cast<std::size_t>(width) * 2u)
        return mm::display::Status::BadArgument;
    for (unsigned int x = 0; x < width; ++x) {
        const bool paper = (packed[x / 8u] &
                            static_cast<std::byte>(0x80u >> (x % 8u))) != std::byte{0};
        const auto color = paper ? palette.paper.value : palette.ink.value;
        out[static_cast<std::size_t>(x) * 2u] = static_cast<std::byte>(color >> 8u);
        out[static_cast<std::size_t>(x) * 2u + 1u] =
            static_cast<std::byte>(color & 0xffu);
    }
    return mm::display::Status::Ok;
}

Status rotate(Surface source, unsigned int quarter_turns, Surface turned) {
    if (check(source, 1) != mm::display::Status::Ok || check(turned, 1) != mm::display::Status::Ok)
        return mm::display::Status::BadArgument;
    const auto turns = quarter_turns % 4u;
    if (turned.width != (turns % 2u ? source.height : source.width) ||
        turned.height != (turns % 2u ? source.width : source.height))
        return mm::display::Status::BadArgument;
    const auto a = reinterpret_cast<std::uintptr_t>(source.pixels.data());
    const auto b = reinterpret_cast<std::uintptr_t>(turned.pixels.data());
    if ((a <= b && b - a < source.size()) ||
        (b < a && a - b < turned.size()))
        return mm::display::Status::BadArgument;
    const auto turned_bytes = turned.size();
    for (std::size_t i = 0; i < turned_bytes; ++i)
        turned.pixels[i] = std::byte{0};
    for (unsigned int y = 0; y < turned.height; ++y)
        for (unsigned int x = 0; x < turned.width; ++x) {
            unsigned int sx = x, sy = y;
            switch (turns) {
                case 1: sx = y; sy = source.height - 1u - x; break;
                case 2: sx = source.width - 1u - x;
                        sy = source.height - 1u - y; break;
                case 3: sx = source.width - 1u - y; sy = x; break;
                default: break;
            }
            const auto bit = source.pixels[
                static_cast<std::size_t>(sy) * source.row_bytes() + sx / 8u] &
                static_cast<std::byte>(0x80u >> (sx % 8u));
            if (bit != std::byte{0})
                turned.pixels[static_cast<std::size_t>(y) * turned.row_bytes() + x / 8u]
                    |= static_cast<std::byte>(0x80u >> (x % 8u));
        }
    return mm::display::Status::Ok;
}

Status write(mm::display::Display& display, Surface surface,
             unsigned int x, unsigned int y) {
    const auto status = display_check(display, surface, x, y);
    if (status != mm::display::Status::Ok) return status;
    const auto geometry = display.geometry();
    if (surface.bits_per_pixel != geometry.bits_per_pixel)
        return mm::display::Status::BadArgument;
    if (geometry.bits_per_pixel == 1 &&
        (x % 8u != 0 || (surface.width % 8u != 0 &&
                         surface.width != geometry.width - x)))
        return mm::display::Status::BadArgument;
    return display.write({x, y, surface.width, surface.height},
                         surface.pixels.first(surface.size()));
}

Status write(mm::display::Display& display, Surface surface,
             unsigned int x, unsigned int y, Palette base,
             std::span<const Region> regions, std::span<std::byte> scratch) {
    const auto status = display_check(display, surface, x, y);
    if (status != mm::display::Status::Ok) return status;
    if (surface.bits_per_pixel != 1 || display.geometry().bits_per_pixel != 16 ||
        scratch.size() < static_cast<std::size_t>(surface.width) * 2u)
        return mm::display::Status::BadArgument;
    const auto row_bytes = surface.row_bytes();
    const auto out = scratch.first(static_cast<std::size_t>(surface.width) * 2u);
    for (unsigned int row = 0; row < surface.height; ++row) {
        const auto packed = std::span<const std::byte>{
            surface.pixels.data() + static_cast<std::size_t>(row) * row_bytes,
            row_bytes};
        if (regions.empty()) {
            const auto result = expand_row(packed, surface.width, base, out);
            if (result != mm::display::Status::Ok) return result;
        } else {
            for (unsigned int col = 0; col < surface.width; ++col) {
                auto palette = base;
                for (const auto& region : regions)
                    if (covers(region.area, col, row)) {
                        palette = region.palette;
                        break;
                    }
                const bool paper = (packed[col / 8u] &
                    static_cast<std::byte>(0x80u >> (col % 8u))) != std::byte{0};
                const auto value = paper ? palette.paper.value : palette.ink.value;
                out[static_cast<std::size_t>(col) * 2u] =
                    static_cast<std::byte>(value >> 8u);
                out[static_cast<std::size_t>(col) * 2u + 1u] =
                    static_cast<std::byte>(value & 0xffu);
            }
        }
        const auto result = display.write({x, y + row, surface.width, 1}, out);
        if (result != mm::display::Status::Ok) return result;
    }
    return mm::display::Status::Ok;
}

}
