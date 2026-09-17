// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
//
// A panel picture for checking gfx packing, drawing, rotation, and colour,
// made of nothing but the primitives mm.gfx has: fill, pixel, line, the two
// rectangles, rotate, and the two writes.
//
// The scene is composed one bit deep: a dithered glow, a tunnel of nested
// frames, a haloed porthole with a starburst inside it, and an off-centre
// tick so the turn is visible. Ordered dithering is pixel() in a loop with a
// Bayer matrix, the circle is pixel() on the midpoint walk, its fill is
// one-row rectangles, and the rays are lines from a sine table. The frame
// turns a quarter clockwise four times, as font-demo does.
//
// A one-bit panel gets exactly that, black on white. A sixteen-bit panel gets
// the same bits expanded through six rainbow bands that turn with the scene
// and cycle their hues frame by frame, and a plasma drawn directly in RGB565
// and written through the porthole, masked to a disc, moving with the
// bands. The bits never change between the two; only the write does.
#include <array>
#include <cstddef>

import mm.display;
import mm.gfx;
import mm.mcu;

namespace {

using mm::display::Color;
using mm::display::Status;
using mm::gfx::Rgb565;
using mm::gfx::Surface;

// Frame budget: the longest side either role of the panel may take, packed
// eight pixels to the byte, for the composed frame and the turned one.
constexpr unsigned int maximum_side = 480;
constexpr std::size_t maximum_row_bytes = (maximum_side + 7u) / 8u;
std::array<std::byte, maximum_row_bytes * maximum_side> frame;
std::array<std::byte, maximum_row_bytes * maximum_side> turned;

// One expanded RGB565 row, and the porthole tile drawn in RGB565 directly.
std::array<std::byte, maximum_side * 2u> row;
constexpr unsigned int porthole_side = 96;
std::array<std::byte, porthole_side * porthole_side * 2u> porthole_bytes;

// A one-bit panel is an e-paper: one frame per orientation, held to be read.
// A sixteen-bit panel is fast enough to animate the colour in place.
constexpr unsigned int still_hold_ms = 4'000;
constexpr unsigned int animated_steps = 16;
constexpr unsigned int animated_hold_ms = 200;

// sin over a 256-step turn, scaled to -127..127, generated at compile time
// from its series so that no <cmath> is needed at runtime or in constexpr.
[[nodiscard]] constexpr double series_sine(double a) {
    double term = a;
    double sum = a;
    for (int n = 1; n < 12; ++n) {
        term *= -a * a / static_cast<double>((2 * n) * (2 * n + 1));
        sum += term;
    }
    return sum;
}

[[nodiscard]] constexpr std::array<int, 256> make_sines() {
    constexpr double pi = 3.14159265358979323846;
    std::array<int, 256> table{};
    for (int i = 0; i < 256; ++i) {
        const int centred = i < 128 ? i : i - 256;  // keep the series near zero
        const double value = series_sine(centred * 2.0 * pi / 256.0) * 127.0;
        table[static_cast<std::size_t>(i)] =
            static_cast<int>(value + (value >= 0 ? 0.5 : -0.5));
    }
    return table;
}

constexpr std::array<int, 256> sines = make_sines();
[[nodiscard]] constexpr int sine(unsigned int angle) { return sines[angle & 255u]; }
[[nodiscard]] constexpr int cosine(unsigned int angle) { return sine(angle + 64u); }

// The classic 4 by 4 ordered dither: a pixel is black when its grey level,
// 0..16, exceeds the matrix entry under it.
constexpr std::array<std::array<unsigned int, 4>, 4> bayer{{
    {0, 8, 2, 10},
    {12, 4, 14, 6},
    {3, 11, 1, 9},
    {15, 7, 13, 5},
}};

// Octagonal distance: close enough to Euclidean for a vignette and a
// plasma, with no square root per pixel.
[[nodiscard]] constexpr unsigned int distance(int dx, int dy) {
    const unsigned int ax = static_cast<unsigned int>(dx < 0 ? -dx : dx);
    const unsigned int ay = static_cast<unsigned int>(dy < 0 ? -dy : dy);
    return ax > ay ? ax + ay / 2u : ay + ax / 2u;
}

// A saturated hue on a 256-step wheel, red at zero.
[[nodiscard]] constexpr Rgb565 rainbow(unsigned int hue) {
    const unsigned int h = hue & 255u;
    const unsigned int sector = h / 43u;
    const unsigned int t = (h % 43u) * 255u / 42u;
    unsigned int r = 0;
    unsigned int g = 0;
    unsigned int b = 0;
    switch (sector) {
        case 0: r = 255; g = t; break;
        case 1: r = 255 - t; g = 255; break;
        case 2: g = 255; b = t; break;
        case 3: g = 255 - t; b = 255; break;
        case 4: r = t; b = 255; break;
        default: r = 255; b = 255 - t; break;
    }
    return mm::gfx::rgb(static_cast<unsigned char>(r), static_cast<unsigned char>(g),
                        static_cast<unsigned char>(b));
}

// The paper the bands share: deep blue, so the inks read as neon and the
// glow fades into the dark. A one-bit panel never sees it.
constexpr Rgb565 night = mm::gfx::rgb(0, 0, 48);
constexpr unsigned int bands = 6;
constexpr unsigned int rays = 24;

// The shared geometry of a frame this wide and tall: everything hangs off
// the shorter side so the picture keeps its proportions in both roles.
struct Layout {
    int width;
    int height;
    int short_side;
    int centre_x;
    int centre_y;
    int margin;
    int radius;
};

[[nodiscard]] Layout layout(Surface surface) {
    const int width = static_cast<int>(surface.width);
    const int height = static_cast<int>(surface.height);
    const int short_side = width < height ? width : height;
    return {width,          height,          short_side, width / 2,
            height / 2,     short_side / 16, short_side / 4};
}

// A glow: ink densest around the porthole and thinning to nothing at the
// corners, dithered. Every pixel is visited once; the porthole and its halo
// are filled white afterwards, so the loop skips nothing and stays simple.
[[nodiscard]] Status glow(Surface surface, const Layout& at) {
    const unsigned int reach = distance(at.centre_x, at.centre_y);
    for (int y = 0; y < at.height; ++y)
        for (int x = 0; x < at.width; ++x) {
            const unsigned int away = distance(x - at.centre_x, y - at.centre_y);
            const unsigned int grey = (reach - away) * 14u / reach;
            const bool black = grey > bayer[static_cast<std::size_t>(y % 4)]
                                            [static_cast<std::size_t>(x % 4)];
            if (black && mm::gfx::pixel(surface, x, y, Color::Black) != Status::Ok)
                return Status::BadArgument;
        }
    return Status::Ok;
}

// Nested outlines from the edge inward, one margin apart, stopping before
// they would touch the porthole's outer ring.
[[nodiscard]] Status tunnel(Surface surface, const Layout& at) {
    for (int inset = at.margin; at.short_side / 2 - inset > at.radius + 4 + at.margin;
         inset += at.margin) {
        const auto status = mm::gfx::rectangle(
            surface, inset, inset, static_cast<unsigned int>(at.width - 2 * inset),
            static_cast<unsigned int>(at.height - 2 * inset), Color::Black);
        if (status != Status::Ok) return status;
    }
    return Status::Ok;
}

// A disc of one-row rectangles, the half-width narrowing as the rows move
// away from the centre, mirrored above and below; and, around it, a circle
// walked by the midpoint rule with pixel().
[[nodiscard]] Status disc(Surface surface, int cx, int cy, int r, Color ink) {
    int half = r;
    for (int dy = 0; dy <= r; ++dy) {
        while (half > 0 && half * half + dy * dy > r * r) --half;
        const auto width = static_cast<unsigned int>(2 * half + 1);
        if (mm::gfx::fill_rectangle(surface, cx - half, cy + dy, width, 1, ink) !=
                Status::Ok ||
            mm::gfx::fill_rectangle(surface, cx - half, cy - dy, width, 1, ink) !=
                Status::Ok)
            return Status::BadArgument;
    }
    return Status::Ok;
}

[[nodiscard]] Status circle(Surface surface, int cx, int cy, int r, Color ink) {
    int x = r;
    int y = 0;
    int error = 1 - r;
    while (x >= y) {
        const int points[8][2] = {{x, y}, {y, x}, {-y, x}, {-x, y},
                                  {-x, -y}, {-y, -x}, {y, -x}, {x, -y}};
        for (const auto& p : points)
            if (mm::gfx::pixel(surface, cx + p[0], cy + p[1], ink) != Status::Ok)
                return Status::BadArgument;
        ++y;
        if (error < 0) error += 2 * y + 1;
        else { --x; error += 2 * (y - x) + 1; }
    }
    return Status::Ok;
}

// Rays from the centre to the porthole's rim, the whole fan turned by phase
// so each orientation gets a different starburst.
[[nodiscard]] Status starburst(Surface surface, const Layout& at, unsigned int phase) {
    for (unsigned int i = 0; i < rays; ++i) {
        const unsigned int angle = i * 256u / rays + phase;
        const int x = at.centre_x + (at.radius - 2) * cosine(angle) / 127;
        const int y = at.centre_y + (at.radius - 2) * sine(angle) / 127;
        const auto status = mm::gfx::line(surface, at.centre_x, at.centre_y, x, y,
                                          Color::Black);
        if (status != Status::Ok) return status;
    }
    return Status::Ok;
}

// The whole one-bit scene for one orientation.
[[nodiscard]] Status compose(Surface surface, unsigned int turns) {
    const Layout at = layout(surface);
    if (mm::gfx::fill(surface, Color::White) != Status::Ok) return Status::BadArgument;
    if (glow(surface, at) != Status::Ok) return Status::BadArgument;
    if (tunnel(surface, at) != Status::Ok) return Status::BadArgument;
    // A white halo carries the two rings, so they read against the glow.
    if (disc(surface, at.centre_x, at.centre_y, at.radius + 3 + at.margin / 2,
             Color::White) != Status::Ok)
        return Status::BadArgument;
    if (circle(surface, at.centre_x, at.centre_y, at.radius, Color::Black) != Status::Ok)
        return Status::BadArgument;
    if (circle(surface, at.centre_x, at.centre_y, at.radius + 3, Color::Black) != Status::Ok)
        return Status::BadArgument;
    if (starburst(surface, at, turns * 256u / (4u * rays)) != Status::Ok)
        return Status::BadArgument;
    // The tick: a filled square in the top-left quadrant, so that the turn is
    // visible even where the scene is otherwise symmetric.
    const unsigned int tick = static_cast<unsigned int>(at.margin);
    return mm::gfx::fill_rectangle(surface, 3 * at.margin, 3 * at.margin, tick, tick,
                                   Color::Black);
}

// Six bands across the composed frame, each expanding the black bits to one
// hue on the night paper, turned with the frame and shifted by the step.
void colour_bands(std::array<mm::gfx::Region, bands>& regions, unsigned int width,
                  unsigned int height, unsigned int turns, unsigned int step) {
    const unsigned int band = height / bands;
    for (unsigned int i = 0; i < bands; ++i) {
        const unsigned int y = i * band;
        const unsigned int extent = i + 1 == bands ? height - y : band;
        regions[i] = {mm::gfx::rotate({0, y, width, extent}, width, height, turns),
                      {rainbow(i * 256u / bands + step * 16u), night}};
    }
}

// The plasma: four sines summed per pixel, hue from the sum, masked to a
// disc that sits inside the porthole's rim. Drawn in RGB565 directly, which
// is the one thing a one-bit composition cannot do.
[[nodiscard]] Status plasma(Surface tile, unsigned int t) {
    constexpr int centre = static_cast<int>(porthole_side) / 2;
    constexpr int radius = static_cast<int>(porthole_side) / 2 - 2;
    for (int y = 0; y < static_cast<int>(porthole_side); ++y)
        for (int x = 0; x < static_cast<int>(porthole_side); ++x) {
            const int dx = x - centre;
            const int dy = y - centre;
            const unsigned int d = distance(dx, dy);
            Rgb565 colour = night;
            if (dx * dx + dy * dy <= radius * radius) {
                const int sum = sine(static_cast<unsigned int>(x) * 4u + t * 3u) +
                                sine(static_cast<unsigned int>(y) * 3u - t * 2u) +
                                sine(static_cast<unsigned int>(x + y) * 2u + t) +
                                sine(d * 5u - t * 4u);
                colour = rainbow(static_cast<unsigned int>(sum / 4 + 128) + t);
            }
            if (mm::gfx::pixel(tile, x, y, colour) != Status::Ok)
                return Status::BadArgument;
        }
    return Status::Ok;
}

// One orientation: composed upright on the logical panel, turned into
// place, then written once per step. Returns the step of main's exit codes
// that failed, or zero.
[[nodiscard]] int show(mm::display::Display& display,
                       mm::display::Geometry geometry, unsigned int turns) {
    const bool sideways = turns % 2u != 0;
    const unsigned int width = sideways ? geometry.height : geometry.width;
    const unsigned int height = sideways ? geometry.width : geometry.height;
    const Surface source{width, height, 1, frame};
    const Surface destination{geometry.width, geometry.height, 1, turned};
    const bool colour = geometry.bits_per_pixel == 16;
    const bool porthole = colour && geometry.width >= porthole_side &&
                          geometry.height >= porthole_side;

    if (display.clear(Color::White) != Status::Ok) return 4;
    if (compose(source, turns) != Status::Ok ||
        mm::gfx::rotate(source, turns, destination) != Status::Ok)
        return 5;

    const unsigned int steps = colour ? animated_steps : 1;
    for (unsigned int step = 0; step < steps; ++step) {
        if (colour) {
            std::array<mm::gfx::Region, bands> regions{};
            colour_bands(regions, width, height, turns, step);
            if (mm::gfx::write(display, destination, 0, 0, {mm::gfx::rgb565_white, night},
                               regions, row) != Status::Ok)
                return 6;
        } else if (mm::gfx::write(display, destination, 0, 0) != Status::Ok) {
            return 6;
        }
        if (porthole) {
            const Surface tile{porthole_side, porthole_side, 16, porthole_bytes};
            if (plasma(tile, step * 6u + turns * 64u) != Status::Ok) return 5;
            if (mm::gfx::write(display, tile, (geometry.width - porthole_side) / 2u,
                               (geometry.height - porthole_side) / 2u) != Status::Ok)
                return 6;
        }
        if (display.refresh(mm::display::Refresh::Full) != Status::Ok) return 7;
        if (mm::mcu::delay_ms(colour ? animated_hold_ms : still_hold_ms) !=
            mm::mcu::Status::Ok)
            return 8;
    }
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

    for (unsigned int turns = 0; turns < 4; ++turns)
        if (const int step = show(display, geometry, turns); step != 0)
            return step;
    if (display.sleep() != Status::Ok) return 9;
    return 0;
}
