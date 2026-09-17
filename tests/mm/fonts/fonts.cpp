// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
#include <array>
#include <cstddef>
#include <span>

import mm.display;
import mm.fonts;
import mm.test;

namespace {

using mm::test::expect;

// Golden bitmaps from tools/fonts/gen_font.py's committed provenance: IBM Plex
// Mono, rasterized at 16 and 12 pixels, most significant bit first.

// 'A' at 16 pixels: ten wide, twenty-two rows, two bytes per row.
constexpr std::array<std::byte, 44> a_16 = {
    std::byte{0x00}, std::byte{0x00}, std::byte{0x00}, std::byte{0x00}, std::byte{0x00}, std::byte{0x00}, std::byte{0x00}, std::byte{0x00}, std::byte{0x00}, std::byte{0x00}, std::byte{0x00}, std::byte{0x00},
    std::byte{0x0c}, std::byte{0x00}, std::byte{0x0c}, std::byte{0x00}, std::byte{0x1e}, std::byte{0x00}, std::byte{0x12}, std::byte{0x00}, std::byte{0x12}, std::byte{0x00}, std::byte{0x33}, std::byte{0x00},
    std::byte{0x23}, std::byte{0x00}, std::byte{0x3f}, std::byte{0x00}, std::byte{0x61}, std::byte{0x80}, std::byte{0x61}, std::byte{0x80}, std::byte{0x40}, std::byte{0x80}, std::byte{0x00}, std::byte{0x00},
    std::byte{0x00}, std::byte{0x00}, std::byte{0x00}, std::byte{0x00}, std::byte{0x00}, std::byte{0x00}, std::byte{0x00}, std::byte{0x00},
};

// The cedilla z at 16 pixels: the last glyph in the committed charset.
constexpr std::array<std::byte, 44> z_16 = {
    std::byte{0x00}, std::byte{0x00}, std::byte{0x00}, std::byte{0x00}, std::byte{0x00}, std::byte{0x00}, std::byte{0x00}, std::byte{0x00}, std::byte{0x00}, std::byte{0x00}, std::byte{0x0c}, std::byte{0x00},
    std::byte{0x0c}, std::byte{0x00}, std::byte{0x00}, std::byte{0x00}, std::byte{0x00}, std::byte{0x00}, std::byte{0x3f}, std::byte{0x00}, std::byte{0x01}, std::byte{0x00}, std::byte{0x02}, std::byte{0x00},
    std::byte{0x04}, std::byte{0x00}, std::byte{0x08}, std::byte{0x00}, std::byte{0x10}, std::byte{0x00}, std::byte{0x20}, std::byte{0x00}, std::byte{0x3f}, std::byte{0x80}, std::byte{0x00}, std::byte{0x00},
    std::byte{0x00}, std::byte{0x00}, std::byte{0x00}, std::byte{0x00}, std::byte{0x00}, std::byte{0x00}, std::byte{0x00}, std::byte{0x00},
};

// 'A' at 12 pixels: seven wide, seventeen rows, one byte per row.
constexpr std::array<std::byte, 17> a_12 = {
    std::byte{0x00}, std::byte{0x00}, std::byte{0x00}, std::byte{0x00}, std::byte{0x00}, std::byte{0x10}, std::byte{0x28}, std::byte{0x28},
    std::byte{0x28}, std::byte{0x44}, std::byte{0x7c}, std::byte{0x44}, std::byte{0xc6}, std::byte{0x00}, std::byte{0x00}, std::byte{0x00}, std::byte{0x00},
};

// The twenty-two rows of the sixteen pixel 'A' combined with a pre-marked
// background: white ink sets the glyph bits, black ink clears them.
[[nodiscard]] std::array<std::byte, 44> combine(unsigned short mark, bool white) {
    std::array<std::byte, 44> out;
    for (unsigned int row = 0; row < 22; ++row) {
        const unsigned short glyph = static_cast<unsigned short>(
            (static_cast<unsigned int>(a_16[row * 2]) << 8 |
             static_cast<unsigned int>(a_16[row * 2 + 1])));
        const unsigned short bits = white ? mark | glyph : mark & static_cast<unsigned short>(~glyph);
        out[row * 2] = static_cast<std::byte>(bits >> 8);
        out[row * 2 + 1] = static_cast<std::byte>(bits & 0xff);
    }
    return out;
}

[[nodiscard]] const mm::fonts::Glyph* find(const mm::fonts::Font& font,
                                           unsigned int code_point) {
    for (const auto& glyph : font.glyphs)
        if (glyph.code_point == code_point) return &glyph;
    return nullptr;
}

void golden_index_and_metrics() {
    using mm::fonts::kMono12;
    using mm::fonts::kMono16;
    expect(kMono16.glyphs.size() == 113 && kMono16.data.size() == 4928,
           "kMono16 holds the full charset in its packed bytes");
    expect(kMono16.advance == 10 && kMono16.height == 22 &&
               kMono16.ascent == 17 && kMono16.line_height == 22,
           "kMono16 keeps the face's advance, height, and line");
    expect(kMono12.glyphs.size() == 113 && kMono12.data.size() == 1904,
           "kMono12 holds the full charset in its packed bytes");
    expect(kMono12.advance == 7 && kMono12.height == 17 && kMono12.ascent == 13 &&
               kMono12.line_height == 17,
           "kMono12 keeps the face's advance, height, and line");
    expect(kMono16.glyphs[0].code_point == 0x20 && kMono16.glyphs[0].width == 0 &&
               kMono16.glyphs[0].byte_offset == 0,
           "the index starts at the space glyph");
    unsigned int ascending = 0;
    for (std::size_t i = 1; i < kMono16.glyphs.size(); ++i)
        ascending += kMono16.glyphs[i].code_point > kMono16.glyphs[i - 1].code_point;
    expect(ascending == kMono16.glyphs.size() - 1, "the index is sorted ascending");
}

void golden_glyph_bitmaps() {
    using mm::fonts::kMono12;
    using mm::fonts::kMono16;
    const auto* a16 = find(kMono16, 0x41);
    expect(a16 != nullptr && a16->x_offset == 0 && a16->width == 10 &&
               a16->byte_offset == 1408,
           "the kMono16 'A' sits where the golden index says");
    std::array<std::byte, 44> a16_bytes;
    for (unsigned int row = 0; row < 22; ++row)
        for (unsigned int b = 0; b < 2; ++b)
            a16_bytes[row * 2 + b] = kMono16.data[a16->byte_offset + row * 2 + b];
    expect(a16_bytes == a_16, "the kMono16 'A' bitmap matches the golden");
    const auto* a12 = find(kMono12, 0x41);
    expect(a12 != nullptr && a12->x_offset == 0 && a12->width == 7 &&
               a12->byte_offset == 544,
           "the kMono12 'A' sits where the golden index says");
    std::array<std::byte, 17> a12_bytes;
    for (unsigned int row = 0; row < 17; ++row)
        a12_bytes[row] = kMono12.data[a12->byte_offset + row];
    expect(a12_bytes == a_12, "the kMono12 'A' bitmap matches the golden");
    const auto* z16 = find(kMono16, 0x17c);
    expect(z16 != nullptr && z16->x_offset == 0 && z16->width == 10 &&
               z16->byte_offset == 4884,
           "the cedilla z is the last entry of the index");
    std::array<std::byte, 44> z_bytes;
    for (unsigned int row = 0; row < 22; ++row)
        for (unsigned int b = 0; b < 2; ++b)
            z_bytes[row * 2 + b] = kMono16.data[z16->byte_offset + row * 2 + b];
    expect(z_bytes == z_16, "the kMono16 cedilla z matches the golden");
}

void renders_both_polarities() {
    const char8_t* text = u8"A";
    std::array<std::byte, 44> frame;
    frame.fill(std::byte{0x00});
    expect(mm::fonts::render(text, 1, mm::fonts::kMono16,
                             mm::display::Color::White, mm::display::Color::Black,
                             0, 0, frame, 2) == mm::display::Status::Ok,
           "white ink on a cleared frame composes");
    expect(frame == a_16, "white ink sets exactly the glyph bits");
    frame.fill(std::byte{0xff});
    expect(mm::fonts::render(text, 1, mm::fonts::kMono16,
                             mm::display::Color::Black, mm::display::Color::White,
                             0, 0, frame, 2) == mm::display::Status::Ok,
           "black ink on a marked frame composes");
    expect(frame == combine(0xffff, false), "black ink clears exactly the glyph bits");
}

void composes_over_an_existing_frame() {
    const char8_t* text = u8"A";
    std::array<std::byte, 44> frame;
    frame.fill(std::byte{0x5a});
    expect(mm::fonts::render(text, 1, mm::fonts::kMono16,
                             mm::display::Color::White, mm::display::Color::Black,
                             0, 0, frame, 2) == mm::display::Status::Ok,
           "composition over a pre-marked frame is accepted");
    expect(frame == combine(0x5a5a, true),
           "glyph bits are set and pre-marked bits stay untouched");
    frame.fill(std::byte{0x5a});
    expect(mm::fonts::render(text, 1, mm::fonts::kMono16,
                             mm::display::Color::Black, mm::display::Color::White,
                             0, 0, frame, 2) == mm::display::Status::Ok,
           "clearing composition over a pre-marked frame is accepted");
    expect(frame == combine(0x5a5a, false),
           "glyph bits are cleared and pre-marked bits stay untouched");
}

void missing_code_points_render_blank_and_advance() {
    // e-acute is not in the charset: it renders blank, and the pen still moves
    // one advance for it.
    const char8_t text[] = {
        static_cast<char8_t>(0xc3), static_cast<char8_t>(0xa9), u8'A'};
    std::array<std::byte, 3 * 22> frame;
    frame.fill(std::byte{0x00});
    expect(mm::fonts::render(text, 3, mm::fonts::kMono16,
                             mm::display::Color::White, mm::display::Color::Black,
                             0, 0, frame, 3) == mm::display::Status::Ok,
           "a missing code point is not an error");
    bool placed = true;
    for (unsigned int row = 0; row < 22; ++row) {
        const unsigned int value =
            (static_cast<unsigned int>(a_16[row * 2]) << 8 |
             static_cast<unsigned int>(a_16[row * 2 + 1])) << 10;
        if (frame[row * 3] != static_cast<std::byte>(value >> 16) ||
            frame[row * 3 + 1] != static_cast<std::byte>(value >> 8) ||
            frame[row * 3 + 2] != static_cast<std::byte>(value))
            placed = false;
    }
    expect(placed, "'A' lands one advance past the blank cell");
}

void measure_obey_the_monospace_contract() {
    using mm::fonts::kMono16;
    const auto ab = mm::fonts::measure(u8"ab", 2, kMono16);
    expect(ab.width == 20 && ab.height == 22,
           "two code points measure twice the advance and the line height");
    const char8_t pl[] = {  // łąć
        static_cast<char8_t>(0xc5), static_cast<char8_t>(0x82),
        static_cast<char8_t>(0xc4), static_cast<char8_t>(0x85),
        static_cast<char8_t>(0xc4), static_cast<char8_t>(0x87),
    };
    const auto three = mm::fonts::measure(pl, 6, kMono16);
    expect(three.width == 30 && three.height == 22,
           "multi-byte Polish letters measure one cell each");
    expect(mm::fonts::measure(u8"w", 1, kMono16).width ==
               mm::fonts::measure(u8"W", 1, kMono16).width,
           "the advance does not depend on the code point");
    const auto empty = mm::fonts::measure(nullptr, 0, kMono16);
    expect(empty.width == 0 && empty.height == 22,
           "null text measures empty at the line height");
    const char8_t truncated[] = {u8'a', static_cast<char8_t>(0xc4)};
    const auto cut = mm::fonts::measure(truncated, 2, kMono16);
    expect(cut.width == 10 && cut.height == 22,
           "a trailing partial sequence stops the count, not the call");
}

void render_rejects_bad_arguments() {
    const char8_t* text = u8"A";
    std::array<std::byte, 44> frame;
    frame.fill(std::byte{0x00});
    expect(mm::fonts::render(text, 1, mm::fonts::kMono16,
                             mm::display::Color::Red, mm::display::Color::Black,
                             0, 0, frame, 2) == mm::display::Status::BadArgument,
           "red has no one-bit value");
    expect(mm::fonts::render(text, 1, mm::fonts::kMono16,
                             mm::display::Color::White, mm::display::Color::Black,
                             0, 0, std::span<std::byte>{frame.data(), 43}, 2) ==
               mm::display::Status::BadArgument,
           "a frame shorter than its declared width is rejected");
    expect(mm::fonts::render(text, 1, mm::fonts::kMono16,
                             mm::display::Color::White, mm::display::Color::Black,
                             0, 1, frame, 2) == mm::display::Status::BadArgument,
           "text reaching past the last row is rejected");
    const char8_t partial[] = {u8'A', static_cast<char8_t>(0xc4)};
    expect(mm::fonts::render(partial, 2, mm::fonts::kMono16,
                             mm::display::Color::White, mm::display::Color::Black,
                             0, 0, frame, 2) == mm::display::Status::BadArgument,
           "a trailing partial UTF-8 sequence is rejected");
    expect(mm::fonts::render(nullptr, 1, mm::fonts::kMono16,
                             mm::display::Color::White, mm::display::Color::Black,
                             0, 0, frame, 2) == mm::display::Status::BadArgument,
           "null text with a nonzero size is rejected");
    expect(mm::fonts::render(text, 1, mm::fonts::kMono16,
                             mm::display::Color::White, mm::display::Color::Black,
                             0, 0, frame, 0) == mm::display::Status::BadArgument,
           "a zero-width frame is rejected");
}

void render_clips_at_the_right_edge() {
    const char8_t* text = u8"A";
    std::array<std::byte, 44> frame;
    frame.fill(std::byte{0x00});
    expect(mm::fonts::render(text, 1, mm::fonts::kMono16,
                             mm::display::Color::White, mm::display::Color::Black,
                             12, 0, frame, 2) == mm::display::Status::Ok,
           "ink crossing the right edge is clipped, not an error");
    bool clipped = true;
    for (unsigned int row = 0; row < 22; ++row) {
        const unsigned int value =
            (static_cast<unsigned int>(a_16[row * 2]) << 8 |
             static_cast<unsigned int>(a_16[row * 2 + 1])) << 12;
        if (frame[row * 2] != static_cast<std::byte>(value >> 8) ||
            frame[row * 2 + 1] != static_cast<std::byte>(value))
            clipped = false;
    }
    expect(clipped, "only the four columns inside the frame are inked");
}

void expand_row_maps_one_bit_to_rgb565() {
    using mm::display::Status;
    const std::array<std::byte, 1> row{std::byte{0x81}};
    const std::array<std::byte, 16> black_on_white = {
    std::byte{0x00}, std::byte{0x00}, std::byte{0xff}, std::byte{0xff}, std::byte{0xff}, std::byte{0xff}, std::byte{0xff}, std::byte{0xff},
    std::byte{0xff}, std::byte{0xff}, std::byte{0xff}, std::byte{0xff}, std::byte{0xff}, std::byte{0xff}, std::byte{0x00}, std::byte{0x00},
    };
    const std::array<std::byte, 16> white_on_black = {
    std::byte{0xff}, std::byte{0xff}, std::byte{0x00}, std::byte{0x00}, std::byte{0x00}, std::byte{0x00}, std::byte{0x00}, std::byte{0x00},
    std::byte{0x00}, std::byte{0x00}, std::byte{0x00}, std::byte{0x00}, std::byte{0x00}, std::byte{0x00}, std::byte{0xff}, std::byte{0xff},
    };
    std::array<std::byte, 16> out;
    expect(mm::fonts::expand_row(row, 1, 0x0000, 0xffff, out) == Status::Ok &&
               out == black_on_white,
           "set bits become the foreground colour, the rest the background");
    expect(mm::fonts::expand_row(row, 1, 0xffff, 0x0000, out) == Status::Ok &&
               out == white_on_black,
           "polarity follows the two colours, not the bits");
    expect(mm::fonts::expand_row(row, 1, 0xffff, 0x0000,
                                 std::span<std::byte>{out.data(), 15}) ==
               Status::BadArgument,
           "a short output row is rejected");
    expect(mm::fonts::expand_row(std::span<const std::byte>{row.data(), 0}, 1,
                                 0xffff, 0x0000, out) == Status::BadArgument,
           "a packed row shorter than its declared width is rejected");
}

const mm::test::case_ cases[] = {
    {"golden index and metrics", &golden_index_and_metrics},
    {"golden glyph bitmaps", &golden_glyph_bitmaps},
    {"both polarities", &renders_both_polarities},
    {"transparent composition", &composes_over_an_existing_frame},
    {"missing code points advance", &missing_code_points_render_blank_and_advance},
    {"monospace measurement", &measure_obey_the_monospace_contract},
    {"render bad arguments", &render_rejects_bad_arguments},
    {"right edge clipping", &render_clips_at_the_right_edge},
    {"one bit to rgb565", &expand_row_maps_one_bit_to_rgb565},
};

const mm::test::registrar reg{"mm.fonts", cases};

}
