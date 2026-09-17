// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
#include <array>
#include <climits>
#include <cstddef>
#include <span>

import mm.display;
import mm.gfx;
import mm.test;

mm::display::Display& mm_gfx_recording_display(unsigned int, unsigned int,
                                                unsigned int);
std::size_t mm_gfx_write_count();
mm::display::Rectangle mm_gfx_rectangle(std::size_t);
std::size_t mm_gfx_write_size(std::size_t);
std::byte mm_gfx_write_byte(std::size_t, std::size_t);
unsigned int mm_gfx_refresh_count();

namespace {
using mm::display::Color;
using mm::display::Status;
using mm::gfx::Surface;
using mm::test::expect;

void surface_sizes_are_total() {
    const Surface empty{};
    expect(empty.row_bytes() == 0 && empty.size() == 0 && !empty.valid(),
           "default surface has no size");
    const Surface huge{UINT_MAX, 1, 16, {}};
    expect(huge.row_bytes() == 0 && huge.size() == 0 && !huge.valid(),
           "a width whose doubled value could overflow is rejected first");
    const Surface too_tall{1, UINT_MAX, 1, {}};
    expect(too_tall.size() == 0, "an over-bound height has no size");
    std::array<std::byte, 5> storage{};
    const Surface padded{9, 2, 1, storage};
    expect(padded.row_bytes() == 2 && padded.size() == 4 && padded.valid(),
           "a surface may have spare storage");
}

void one_bit_drawing_and_clipping() {
    std::array<std::byte, 4> storage{};
    const Surface surface{10, 2, 1, storage};
    expect(mm::gfx::fill(surface, Color::White) == Status::Ok &&
               storage == std::array{std::byte{0xff}, std::byte{0xc0},
                                     std::byte{0xff}, std::byte{0xc0}},
           "fill clears one-bit padding");
    storage[1] = std::byte{0xff};
    expect(mm::gfx::pixel(surface, 8, 0, Color::Black) == Status::Ok &&
               storage[1] == std::byte{0x7f},
           "pixel changes only its bit and preserves padding");
    const auto before = storage;
    expect(mm::gfx::fill_rectangle(surface, -5, -5, 2, 2, Color::Black) ==
               Status::Ok && storage == before,
           "an off-surface rectangle is a no-op");
    expect(mm::gfx::rectangle(surface, 0, 0, 0, 2, Color::Black) == Status::Ok &&
               storage == before,
           "a zero extent is a no-op");
    expect(mm::gfx::line(surface, INT_MIN, 1, INT_MAX, 1, Color::Black) ==
               Status::Ok && storage[2] == std::byte{0x00} &&
               storage[3] == std::byte{0x00},
           "a far crossing line clips to the visible row");
    expect(mm::gfx::fill_rectangle(surface, 2, 0, UINT_MAX, 1,
                                   Color::Black) == Status::Ok,
           "a far rectangle edge does not wrap");
    const auto red_before = storage;
    expect(mm::gfx::pixel(surface, 1, 0, Color::Red) == Status::Unsupported &&
               storage == red_before,
           "red is rejected before changing a one-bit surface");
    expect(mm::gfx::pixel(surface, 1, 0, mm::gfx::rgb565_red) ==
               Status::BadArgument,
           "a sixteen-bit overload rejects a one-bit surface");
}

void lines_and_rectangle_edges() {
    std::array<std::byte, 4> storage{};
    const Surface surface{4, 4, 1, storage};
    expect(mm::gfx::fill(surface, Color::White) == Status::Ok &&
               mm::gfx::line(surface, 0, 0, 3, 3, Color::Black) == Status::Ok &&
               storage == std::array{std::byte{0x70}, std::byte{0xb0},
                                     std::byte{0xd0}, std::byte{0xe0}},
           "a diagonal line includes both endpoints");
    expect(mm::gfx::fill(surface, Color::White) == Status::Ok &&
               mm::gfx::rectangle(surface, 0, 0, 4, 4, Color::Black) ==
                   Status::Ok &&
               storage == std::array{std::byte{0x00}, std::byte{0x60},
                                     std::byte{0x60}, std::byte{0x00}},
           "an outline preserves its interior");
    expect(mm::gfx::fill_rectangle(surface, 1, 1, 2, 2, Color::Black) ==
               Status::Ok && storage == std::array<std::byte, 4>{},
           "a filled rectangle overwrites the interior");
}

void rgb565_drawing() {
    std::array<std::byte, 12> bytes{};
    const Surface surface{3, 2, 16, bytes};
    expect(mm::gfx::rgb(255, 0, 0).value == 0xf800 &&
               mm::gfx::rgb(0, 255, 0).value == 0x07e0 &&
               mm::gfx::rgb(0, 0, 255).value == 0x001f,
           "rgb truncates channels to RGB565");
    expect(mm::gfx::fill(surface, mm::gfx::rgb565_white) == Status::Ok &&
               mm::gfx::rectangle(surface, 0, 0, 3, 2,
                                  mm::gfx::rgb565_red) == Status::Ok &&
               bytes[0] == std::byte{0xf8} && bytes[1] == std::byte{0x00},
           "RGB565 writes its most significant byte first");
    expect(mm::gfx::pixel(surface, 1, 0, Color::Black) == Status::BadArgument,
           "a one-bit overload rejects a sixteen-bit surface");
}

void expansion_ignores_padding() {
    const std::array packed{std::byte{0x80}, std::byte{0x0f}};
    std::array<std::byte, 26> out{};
    const mm::gfx::Palette colors{mm::gfx::rgb565_red,
                                   mm::gfx::rgb565_white};
    expect(mm::gfx::expand_row(packed, 13, colors, out) == Status::Ok &&
               out[0] == std::byte{0xff} && out[1] == std::byte{0xff} &&
               out[2] == std::byte{0xf8} && out[3] == std::byte{0x00} &&
               out[24] == std::byte{0xff} && out[25] == std::byte{0xff},
           "thirteen visible pixels use twenty-six output bytes");
    expect(mm::gfx::expand_row(packed, 13, colors,
                               std::span<std::byte>{out.data(), 25}) ==
               Status::BadArgument,
           "short expansion scratch is rejected");
    expect(mm::gfx::expand_row(std::span<const std::byte>{packed.data(), 1},
                               13, colors, out) == Status::BadArgument,
           "short packed input is rejected");
}

void rotation_and_regions() {
    std::array source_bytes{std::byte{0xa0}, std::byte{0x40}};
    std::array turned_bytes{std::byte{0xff}, std::byte{0xff},
                            std::byte{0xff}};
    const Surface source{3, 2, 1, source_bytes};
    const Surface turned{2, 3, 1, turned_bytes};
    expect(mm::gfx::rotate(source, 1, turned) == Status::Ok &&
               turned_bytes == std::array{std::byte{0x40}, std::byte{0x80},
                                          std::byte{0x40}},
           "clockwise rotation maps pixels and clears padding");
    const auto inside = mm::gfx::rotate({1, 0, 2, 2}, 3, 2, 1);
    expect(inside.x == 0 && inside.y == 1 && inside.width == 2 &&
               inside.height == 2,
           "rectangle rotation covers the corresponding turned pixels");
    const auto clipped = mm::gfx::rotate({2, 1, 3, 3}, 3, 2, 1);
    expect(clipped.x == 0 && clipped.y == 2 && clipped.width == 1 &&
               clipped.height == 1,
           "a crossing rectangle clips before it turns");
    const auto outside = mm::gfx::rotate({7, 0, 1, 1}, 3, 2, 1);
    expect(outside.width == 0 && outside.height == 0,
           "a wholly outside rectangle turns into an empty one");

    const std::array golden{
        std::array{std::byte{0xa0}, std::byte{0x40}, std::byte{0x00}},
        std::array{std::byte{0x40}, std::byte{0x80}, std::byte{0x40}},
        std::array{std::byte{0x40}, std::byte{0xa0}, std::byte{0x00}},
        std::array{std::byte{0x80}, std::byte{0x40}, std::byte{0x80}},
    };
    for (unsigned int turns = 0; turns < 4; ++turns) {
        const unsigned int width = turns % 2u ? 2u : 3u;
        const unsigned int height = turns % 2u ? 3u : 2u;
        std::array<std::byte, 3> result{};
        const Surface target{width, height, 1, result};
        expect(mm::gfx::rotate(source, turns, target) == Status::Ok,
               "each quarter turn is accepted");
        bool exact = true;
        for (std::size_t i = 0; i < target.size(); ++i)
            exact = exact && result[i] == golden[turns][i];
        expect(exact, "each quarter turn matches its golden frame");
        bool mapped = true;
        for (unsigned int y = 0; y < 2; ++y)
            for (unsigned int x = 0; x < 3; ++x) {
                const auto destination = mm::gfx::rotate({x, y, 1, 1},
                                                           3, 2, turns);
                const bool before = (source_bytes[y] &
                    static_cast<std::byte>(0x80u >> x)) != std::byte{0};
                const bool after = (result[destination.y] &
                    static_cast<std::byte>(0x80u >> destination.x)) !=
                    std::byte{0};
                mapped = mapped && before == after;
            }
        expect(mapped, "rectangle and surface rotation agree for every pixel");
    }
    std::array<std::byte, 1> short_target{};
    expect(mm::gfx::rotate(source, 1, Surface{2, 3, 1, short_target}) ==
               Status::BadArgument,
           "a short destination is rejected");
    expect(mm::gfx::rotate(source, 2, source) == Status::BadArgument,
           "overlapping source and destination are rejected");
}

void writes_exact_bytes_and_checks_alignment() {
    auto& mono = mm_gfx_recording_display(10, 3, 1);
    std::array bytes{std::byte{0x80}, std::byte{0x40},
                     std::byte{0x00}, std::byte{0xc0}, std::byte{0xff}};
    const Surface surface{10, 2, 1, bytes};
    expect(mm::gfx::write(mono, surface, 0, 0) == Status::Ok &&
               mm_gfx_write_count() == 1 && mm_gfx_write_size(0) == 4 &&
               mm_gfx_write_byte(0, 0) == std::byte{0x80} &&
               mm_gfx_refresh_count() == 0,
           "same-depth write sends only the surface's pixel bytes");
    auto& aligned = mm_gfx_recording_display(16, 3, 1);
    std::array<std::byte, 1> one{};
    expect(mm::gfx::write(aligned, Surface{8, 1, 1, one}, 1, 0) ==
               Status::BadArgument && mm_gfx_write_count() == 0,
           "misaligned one-bit tile is rejected before the provider");
    expect(mm::gfx::write(aligned, Surface{8, 1, 1, one}, 8, 0) ==
               Status::Ok,
           "an aligned tile at the panel edge is accepted");
    mm_gfx_recording_display(16, 3, 1);
    expect(mm::gfx::write(aligned, Surface{8, 1, 1, one}, 16, 0) ==
               Status::BadArgument && mm_gfx_write_count() == 0,
           "panel bounds are checked before the provider");
}

void write_rejects_depth_mismatches() {
    std::array<std::byte, 1> packed{};
    std::array<std::byte, 16> scratch{};
    const Surface surface{8, 1, 1, packed};
    const mm::gfx::Palette palette{mm::gfx::rgb565_black,
                                    mm::gfx::rgb565_white};

    auto& color = mm_gfx_recording_display(8, 1, 16);
    expect(mm::gfx::write(color, surface, 0, 0) == Status::BadArgument &&
               mm_gfx_write_count() == 0,
           "same-depth write rejects a surface of another depth");

    auto& mono = mm_gfx_recording_display(8, 1, 1);
    expect(mm::gfx::write(mono, surface, 0, 0, palette, {}, scratch) ==
               Status::BadArgument && mm_gfx_write_count() == 0,
           "expanding write rejects a one-bit display");

    auto& unknown = mm_gfx_recording_display(8, 1, 2);
    expect(mm::gfx::write(unknown, surface, 0, 0) ==
               Status::Unsupported && mm_gfx_write_count() == 0,
           "same-depth write rejects an unsupported display depth");
    expect(mm::gfx::write(unknown, surface, 0, 0, palette, {}, scratch) ==
               Status::Unsupported && mm_gfx_write_count() == 0,
           "expanding write rejects an unsupported display depth");
}

void expanding_write_applies_regions() {
    auto& display = mm_gfx_recording_display(5, 3, 16);
    std::array packed{std::byte{0xa0}, std::byte{0x40}};
    std::array<std::byte, 6> scratch{};
    const Surface surface{3, 2, 1, packed};
    const mm::gfx::Palette base{mm::gfx::rgb565_black,
                                 mm::gfx::rgb565_white};
    const std::array regions{
        mm::gfx::Region{{1, 0, 2, 1}, {mm::gfx::rgb565_red,
                                        mm::gfx::rgb565_white}},
        mm::gfx::Region{{1, 0, 2, 1}, {{0x001f},
                                        mm::gfx::rgb565_white}},
    };
    expect(mm::gfx::write(display, surface, 1, 1, base, regions,
                          scratch) == Status::Ok &&
               mm_gfx_write_count() == 2 && mm_gfx_refresh_count() == 0,
           "expanding write makes one transfer per row without refresh");
    const auto first = mm_gfx_rectangle(0);
    expect(first.x == 1 && first.y == 1 && first.width == 3 &&
               first.height == 1 && mm_gfx_write_size(0) == 6,
           "expanded row has the panel origin and exact byte count");
    expect(mm_gfx_write_byte(0, 0) == std::byte{0xff} &&
               mm_gfx_write_byte(0, 1) == std::byte{0xff} &&
               mm_gfx_write_byte(0, 2) == std::byte{0xf8} &&
               mm_gfx_write_byte(0, 3) == std::byte{0x00},
           "first matching region supplies red ink");
    expect(mm_gfx_write_byte(1, 0) == std::byte{0x00} &&
               mm_gfx_write_byte(1, 1) == std::byte{0x00},
           "base palette applies on the next row");
    mm_gfx_recording_display(5, 3, 16);
    expect(mm::gfx::write(display, surface, 1, 1, base, regions,
                          std::span<std::byte>{scratch.data(), 5}) ==
               Status::BadArgument && mm_gfx_write_count() == 0,
           "short scratch is rejected before the first write");
    expect(mm::gfx::write(display, surface, 1, 1, base,
                          std::span<const mm::gfx::Region>{}, scratch) ==
               Status::Ok && mm_gfx_write_count() == 2 &&
               mm_gfx_write_byte(0, 2) == std::byte{0x00},
           "an empty region list expands every row with the base palette");
    mm_gfx_recording_display(5, 3, 16);
    const std::array crossing{
        mm::gfx::Region{{2, 1, UINT_MAX, UINT_MAX},
                        {mm::gfx::rgb565_red, mm::gfx::rgb565_white}},
    };
    expect(mm::gfx::write(display, surface, 1, 1, base, crossing,
                          scratch) == Status::Ok &&
               mm_gfx_write_byte(1, 4) == std::byte{0xf8} &&
               mm_gfx_write_byte(1, 5) == std::byte{0x00},
           "an oversized region colors only its visible intersection");
}

const mm::test::case_ cases[] = {
    {"surface sizes", &surface_sizes_are_total},
    {"one-bit drawing", &one_bit_drawing_and_clipping},
    {"lines and rectangles", &lines_and_rectangle_edges},
    {"RGB565 drawing", &rgb565_drawing},
    {"row expansion", &expansion_ignores_padding},
    {"rotation and regions", &rotation_and_regions},
    {"same-depth write", &writes_exact_bytes_and_checks_alignment},
    {"write depth checks", &write_rejects_depth_mismatches},
    {"expanding write", &expanding_write_applies_regions},
};
const mm::test::registrar reg{"mm.gfx", cases};
}
