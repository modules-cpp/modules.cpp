// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
module;

#include <cstddef>
#include <span>

export module mm.fonts:types;

export namespace mm::fonts {

struct Glyph {
    unsigned int code_point = 0;   // sorted ascending in the index
    unsigned int x_offset = 0;     // ink left edge, pixels from the cell origin
    unsigned int width = 0;        // tight ink width in pixels
    unsigned int byte_offset = 0;  // offset into Font::data
    [[nodiscard]] constexpr unsigned int row_bytes() const { return (width + 7) / 8; }
};

struct Font {
    std::span<const std::byte> data;   // glyph bitmaps: per glyph, one bit per
                                       // pixel, MSB left, each row padded to a
                                       // byte, rows top to bottom from ascent
    std::span<const Glyph> glyphs;
    unsigned int advance = 0;          // monospace cell width, pixels
    unsigned int height = 0;           // ascent + descent
    unsigned int ascent = 0;
    unsigned int line_height = 0;      // height plus the row gap the face wants
};

struct TextMetrics {
    unsigned int width = 0;
    unsigned int height = 0;
};

}
