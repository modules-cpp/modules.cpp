// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
//
// The fonts, held long enough to be read. On a one-bit panel it renders black
// text on the white frame and writes the packed rows; on a sixteen-bit panel
// it composes the same one-bit frame and expands each row to RGB565 in the
// two colours its line names before writing, so colour is a property of the
// expansion and not of the font. Two sizes, centred, with a line that uses
// the whole Polish charset, so a person can tell a working font from a
// scrambled one. Below that, every glyph the twelve pixel table holds,
// wrapped to the panel, so a broken bitmap anywhere in the charset is on
// screen rather than hidden.
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

// The whole charset follows the four lines, one gap below them, wrapped to as
// many twelve pixel cells as the panel is wide. How many lines that takes is
// a runtime fact, so the frame is budgeted for the narrowest panel the demo
// will meet and a narrower one is refused rather than overrun.
constexpr unsigned int charset_y = block_rows + mm::fonts::kMono12.line_height;
constexpr unsigned int charset_size =
    static_cast<unsigned int>(mm::fonts::kMono12.glyphs.size());
constexpr unsigned int minimum_width = 128;
constexpr unsigned int minimum_cells = minimum_width / mm::fonts::kMono12.advance;
constexpr unsigned int maximum_charset_lines =
    (charset_size + minimum_cells - 1u) / minimum_cells;
constexpr unsigned int maximum_rows =
    charset_y + maximum_charset_lines * mm::fonts::kMono12.line_height;

// One bit per pixel for the composed frame: the widest panel this demo will
// meet, packed eight pixels to the byte, times every row it may lay out.
constexpr unsigned int maximum_width = 480;
constexpr std::size_t maximum_row_bytes = (maximum_width + 7u) / 8u;
std::array<std::byte, maximum_row_bytes * maximum_rows> frame;

// One expanded RGB565 row for the sixteen-bit path: every bit of a packed row
// becomes two bytes, so it is sized from the packed row and not the panel.
std::array<std::byte, maximum_row_bytes * 16u> row;

constexpr unsigned int hold_ms = 4'000;

// ŁóżźĆ żółć, the committed charset's Latin Extended block. Named in bytes so
// the source carries no encoding of its own.
const char8_t polish[] = {
    0xc5, 0x81,  // Ł
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

// The two RGB565 colours a band of rows expands to on a sixteen-bit panel.
// The composed frame is one bit deep whatever the panel, so a one-bit panel
// shows black on white and never reads these.
struct Palette {
    unsigned short ink;
    unsigned short paper;
};

constexpr Palette black_on_white{0x0000, 0xffff};

struct Line {
    const char8_t* text;
    std::size_t size;
    const mm::fonts::Font* font;
    unsigned int y;
    Palette palette;
};

constexpr Palette white_on_blue{0xffff, 0x001f};
constexpr Palette red_on_white{0xf800, 0xffff};
constexpr Palette green_on_white{0x03e0, 0xffff};

const Line lines[4] = {
    {u8"modules.cpp", 11, &mm::fonts::kMono16, y16_first, black_on_white},
    {u8"16px mono", 9, &mm::fonts::kMono16, y16_second, white_on_blue},
    {u8"12px mono 0123", 14, &mm::fonts::kMono12, y12_first, red_on_white},
    {polish, sizeof polish, &mm::fonts::kMono12, y12_second, green_on_white},
};

constexpr Palette charset_palette = black_on_white;

// The palette of the band a row falls in: a line's rows take its palette, the
// charset's rows its own, and the gaps between them are plain paper.
[[nodiscard]] Palette palette_for(unsigned int y) {
    for (const Line& line : lines)
        if (y >= line.y && y < line.y + line.font->height) return line.palette;
    if (y >= charset_y) return charset_palette;
    return black_on_white;
}

// One code point as UTF-8. The charset ends below U+0800, so two bytes are
// enough, but the three byte form costs nothing to carry.
[[nodiscard]] std::size_t encode(unsigned int code_point, char8_t (&out)[3]) {
    if (code_point < 0x80u) {
        out[0] = static_cast<char8_t>(code_point);
        return 1;
    }
    if (code_point < 0x800u) {
        out[0] = static_cast<char8_t>(0xc0u | (code_point >> 6));
        out[1] = static_cast<char8_t>(0x80u | (code_point & 0x3fu));
        return 2;
    }
    out[0] = static_cast<char8_t>(0xe0u | (code_point >> 12));
    out[1] = static_cast<char8_t>(0x80u | ((code_point >> 6) & 0x3fu));
    out[2] = static_cast<char8_t>(0x80u | (code_point & 0x3fu));
    return 3;
}

// Every glyph of the twelve pixel table in index order, cells wide per line,
// each line centred. The glyphs that ink nothing are skipped so the first
// line does not open with an invisible cell. Returns false if a glyph does
// not compose.
[[nodiscard]] bool render_charset(unsigned int cells, unsigned int panel_width,
                                  std::span<std::byte> composed,
                                  unsigned int frame_width) {
    const auto& font = mm::fonts::kMono12;
    unsigned int inked = 0;
    for (const auto& glyph : font.glyphs)
        inked += glyph.width != 0;
    unsigned int cell = 0;
    for (const auto& glyph : font.glyphs) {
        if (glyph.width == 0) continue;
        const unsigned int line = cell / cells;
        const unsigned int column = cell % cells;
        const unsigned int on_line = inked - line * cells < cells
            ? inked - line * cells
            : cells;
        const unsigned int x = (panel_width - on_line * font.advance) / 2
                             + column * font.advance;
        const unsigned int y = charset_y + line * font.line_height;
        char8_t text[3];
        const std::size_t size = encode(glyph.code_point, text);
        if (mm::fonts::render(text, size, font, mm::display::Color::Black,
                              mm::display::Color::White, x, y, composed,
                              frame_width) != mm::display::Status::Ok)
            return false;
        ++cell;
    }
    return true;
}

}

int main() {
    auto& display = mm::display::selected_display();
    if (display.initialize() != mm::display::Status::Ok) return 1;

    const auto geometry = display.geometry();
    if (geometry.width == 0 || geometry.height == 0 ||
        (geometry.bits_per_pixel != 1 && geometry.bits_per_pixel != 16))
        return 2;

    // The packed row stride: eight pixels to the byte, the last byte padded.
    const unsigned int frame_width = (geometry.width + 7u) / 8u;
    const unsigned int cells = geometry.width / mm::fonts::kMono12.advance;
    if (cells == 0) return 3;
    const unsigned int charset_lines = (charset_size + cells - 1u) / cells;
    const unsigned int rows =
        charset_y + charset_lines * mm::fonts::kMono12.line_height;
    if (static_cast<std::size_t>(frame_width) * rows > frame.size() ||
        static_cast<std::size_t>(frame_width) * 16u > row.size())
        return 3;
    // A short panel shows what fits; the charset is the part that gets cut.
    const unsigned int visible_rows = rows < geometry.height ? rows : geometry.height;

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
    if (!render_charset(cells, geometry.width, frame, frame_width)) return 5;

    for (unsigned int y = 0; y < visible_rows; ++y) {
        const std::span<const std::byte> packed{
            frame.data() + static_cast<std::size_t>(y) * frame_width, frame_width};
        if (geometry.bits_per_pixel == 1) {
            if (display.write({0, y, geometry.width, 1}, packed) !=
                mm::display::Status::Ok)
                return 6;
        } else {
            // The frame holds paper as set bits and ink as cleared bits, so
            // the set-bit colour is the paper and the cleared-bit colour the
            // ink.
            const Palette palette = palette_for(y);
            if (mm::fonts::expand_row(packed, frame_width, palette.paper,
                                      palette.ink, row) != mm::display::Status::Ok)
                return 6;
            if (display.write({0, y, geometry.width, 1},
                              std::span<const std::byte>{
                                  row.data(), static_cast<std::size_t>(geometry.width) * 2u}) !=
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
