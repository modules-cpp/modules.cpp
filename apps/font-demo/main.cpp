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
// screen rather than hidden. The whole block is shown four times, upright
// and then turned a quarter clockwise each time: the text is always composed
// upright, and the packed frame is what turns.
#include <array>
#include <cstddef>

import mm.display;
import mm.fonts;
import mm.gfx;
import mm.mcu;

namespace {

// The committed tables' geometry, in the line order the frame lays them out.
constexpr unsigned int y16_first = 0;
constexpr unsigned int y16_second = mm::fonts::kMono16.height;
constexpr unsigned int y12_first = 2u * mm::fonts::kMono16.height;
constexpr unsigned int y12_second = y12_first + mm::fonts::kMono12.line_height;
constexpr unsigned int block_rows = y12_second + mm::fonts::kMono12.height;

// The whole charset follows the four lines, one gap below them, wrapped to as
// many twelve pixel cells as the frame is wide.
constexpr unsigned int charset_y = block_rows + mm::fonts::kMono12.line_height;
constexpr unsigned int charset_size =
    static_cast<unsigned int>(mm::fonts::kMono12.glyphs.size());

// The block is composed upright on a logical panel that is the physical one
// turned back, so either side may be the width. Both one-bit frames -- the
// composed one and the turned one -- are budgeted for the longest side the
// demo will meet in either role, packed eight pixels to the byte. A panel
// that exceeds it is refused rather than overrun.
constexpr unsigned int maximum_side = 480;
constexpr std::size_t maximum_row_bytes = (maximum_side + 7u) / 8u;
std::array<std::byte, maximum_row_bytes * maximum_side> frame;
std::array<std::byte, maximum_row_bytes * maximum_side> turned;

// One expanded RGB565 row for the sixteen-bit path.
std::array<std::byte, maximum_side * 2u> row;

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
using Palette = mm::gfx::Palette;
constexpr Palette black_on_white{mm::gfx::rgb565_black, mm::gfx::rgb565_white};

struct Line {
    const char8_t* text;
    std::size_t size;
    const mm::fonts::Font* font;
    unsigned int y;
    Palette palette;
};

constexpr Palette white_on_blue{mm::gfx::rgb565_white, {0x001f}};
constexpr Palette red_on_white{mm::gfx::rgb565_red, mm::gfx::rgb565_white};
constexpr Palette green_on_white{{0x03e0}, mm::gfx::rgb565_white};

const Line lines[4] = {
    {u8"modules.cpp", 11, &mm::fonts::kMono16, y16_first, black_on_white},
    {u8"16px mono", 9, &mm::fonts::kMono16, y16_second, white_on_blue},
    {u8"12px mono 0123", 14, &mm::fonts::kMono12, y12_first, red_on_white},
    {polish, sizeof polish, &mm::fonts::kMono12, y12_second, green_on_white},
};

constexpr Palette charset_palette = black_on_white;

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
                                  mm::gfx::Surface composed) {
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
                              x, y, composed) != mm::display::Status::Ok)
            return false;
        ++cell;
    }
    return true;
}

// The whole block at one orientation: composed upright on the logical panel,
// turned into place, and written row by row. Returns the step that failed,
// numbered as main's exit codes, or zero.
[[nodiscard]] int show(mm::display::Display& display,
                       const mm::display::Geometry& geometry, unsigned int turns) {
    const bool sideways = turns % 2u == 1u;
    const unsigned int width = sideways ? geometry.height : geometry.width;
    const unsigned int height = sideways ? geometry.width : geometry.height;

    // Packed row strides, eight pixels to the byte, the last byte padded: one
    // for the logical frame and one for the panel it is turned onto.
    const unsigned int stride = (width + 7u) / 8u;
    const unsigned int panel_stride = (geometry.width + 7u) / 8u;
    const unsigned int cells = width / mm::fonts::kMono12.advance;
    if (cells == 0) return 3;
    const unsigned int charset_lines = (charset_size + cells - 1u) / cells;
    const unsigned int rows =
        charset_y + charset_lines * mm::fonts::kMono12.line_height;
    // The logical frame holds the whole panel, or the whole block when that
    // is taller: a short panel shows what fits, and the charset is the part
    // that gets cut, because only the panel's rows are turned.
    const unsigned int logical_rows = rows > height ? rows : height;
    if (static_cast<std::size_t>(stride) * logical_rows > frame.size() ||
        static_cast<std::size_t>(panel_stride) * geometry.height > turned.size() ||
        static_cast<std::size_t>(panel_stride) * 16u > row.size())
        return 3;

    // White is 1 in a one-bit frame: clear the panel, then mark the composed
    // frame to match before drawing black text over it.
    if (display.clear(mm::display::Color::White) != mm::display::Status::Ok)
        return 4;
    const mm::gfx::Surface composed{width, logical_rows, 1, frame};
    if (mm::gfx::fill(composed, mm::display::Color::White) !=
        mm::display::Status::Ok)
        return 4;

    for (const Line& line : lines) {
        const auto metrics = mm::fonts::measure(line.text, line.size, *line.font);
        const unsigned int x = metrics.width < width ? (width - metrics.width) / 2 : 0;
        if (mm::fonts::render(line.text, line.size, *line.font,
                              mm::display::Color::Black, x, line.y,
                              composed) != mm::display::Status::Ok)
            return 5;
    }
    if (!render_charset(cells, width, composed)) return 5;

    // Into place. No turn is a copy, which keeps one path for every
    // orientation.
    const mm::gfx::Surface source{width, height, 1, frame};
    const mm::gfx::Surface destination{
        geometry.width, geometry.height, 1, turned};
    if (mm::gfx::rotate(source, turns, destination) !=
        mm::display::Status::Ok)
        return 5;
    std::array<mm::gfx::Region, 4> regions{};
    for (unsigned int i = 0; i < regions.size(); ++i)
        regions[i] = {mm::gfx::rotate({0, lines[i].y, width,
                                      lines[i].font->height}, width, height,
                                     turns), lines[i].palette};

    const auto write_status = geometry.bits_per_pixel == 1
        ? mm::gfx::write(display, destination, 0, 0)
        : mm::gfx::write(display, destination, 0, 0,
                         charset_palette, regions, row);
    if (write_status != mm::display::Status::Ok) return 6;

    if (display.refresh(mm::display::Refresh::Full) != mm::display::Status::Ok)
        return 7;
    return 0;
}

}

int main() {
    auto& display = mm::display::selected_display();
    if (display.initialize() != mm::display::Status::Ok) return 1;

    const auto geometry = display.geometry();
    if (geometry.width == 0 || geometry.height == 0 ||
        (geometry.bits_per_pixel != 1 && geometry.bits_per_pixel != 16))
        return 2;

    // Upright, then a quarter turn clockwise three times, each held long
    // enough to be read. Without the hold the frame is gone before anyone
    // sees it: a provider that owns the display releases it when this
    // process ends.
    for (unsigned int turns = 0; turns < 4; ++turns) {
        if (const int step = show(display, geometry, turns); step != 0) return step;
        if (mm::mcu::delay_ms(hold_ms) != mm::mcu::Status::Ok) return 8;
    }

    if (display.sleep() != mm::display::Status::Ok) return 9;
    return 0;
}
