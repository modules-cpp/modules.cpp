// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
module;

#include <cstddef>
#include <span>

module mm.fonts;

import :types;
import :renderer;
import mm.display;

namespace mm::fonts {

namespace {

// One UTF-8 code point at index. Sets consumed (1-4) on success. Rejects
// overlong encodings, surrogates, out-of-range values, and truncated
// sequences.
[[nodiscard]] bool decode_code_point(const unsigned char* text, std::size_t size,
                                     unsigned int index, unsigned int& code_point,
                                     unsigned int& consumed)
{
    if (index >= size) return false;
    const unsigned char first = text[index];
    unsigned int extra = 0;
    unsigned int minimum = 0x7Fu;
    if (first < 0x80u) {
        code_point = first;
        consumed = 1;
        return true;
    }
    if ((first & 0xE0u) == 0xC0u) { extra = 1; code_point = first & 0x1Fu; }
    else if ((first & 0xF0u) == 0xE0u) { extra = 2; code_point = first & 0x0Fu; minimum = 0x800u; }
    else if ((first & 0xF8u) == 0xF0u) { extra = 3; code_point = first & 0x07u; minimum = 0x10000u; }
    else return false;
    if (index + extra >= size) return false;
    if (code_point < minimum) return false;
    for (unsigned int i = 0; i < extra; ++i) {
        const unsigned char c = text[index + 1 + i];
        if ((c & 0xC0u) != 0x80u) return false;
        code_point = (code_point << 6) | (c & 0x3Fu);
    }
    if (code_point >= 0xD800u && code_point <= 0xDFFFu) return false;
    if (code_point > 0x10FFFFu) return false;
    consumed = 1 + extra;
    return true;
}

// Counts well-formed code points. strict true fails on the first malformed
// sequence; strict false stops there and reports the count so far.
[[nodiscard]] bool count_code_points(const unsigned char* text, std::size_t size,
                                     bool strict, unsigned int& count)
{
    count = 0;
    unsigned int i = 0;
    while (i < size) {
        unsigned int code_point = 0;
        unsigned int consumed = 0;
        if (!decode_code_point(text, size, i, code_point, consumed)) return !strict;
        i += consumed;
        ++count;
    }
    return true;
}

// The glyph index is sorted ascending by code point.
[[nodiscard]] const Glyph* find_glyph(std::span<const Glyph> glyphs, unsigned int code_point)
{
    unsigned int lo = 0;
    unsigned int hi = static_cast<unsigned int>(glyphs.size());
    while (lo < hi) {
        const unsigned int mid = lo + (hi - lo) / 2;
        if (glyphs[mid].code_point < code_point) lo = mid + 1;
        else hi = mid;
    }
    if (lo < glyphs.size() && glyphs[lo].code_point == code_point) return &glyphs[lo];
    return nullptr;
}

}

TextMetrics measure(const char8_t* text, std::size_t text_size, const Font& font)
{
    if (text == nullptr) return {0, font.line_height};
    const auto* bytes = reinterpret_cast<const unsigned char*>(text);
    unsigned int count = 0;
    // Lenient by design: count only the well-formed prefix; the validity
    // result is intentionally discarded.
    (void)count_code_points(bytes, text_size, false, count);
    const unsigned int width =
        static_cast<unsigned int>(static_cast<std::size_t>(count) * font.advance);
    return {width, font.line_height};
}

mm::display::Status render(const char8_t* text, std::size_t text_size, const Font& font,
                           mm::display::Color foreground, mm::display::Color background,
                           unsigned int x, unsigned int y,
                           std::span<std::byte> frame, unsigned int frame_width_bytes)
{
    (void)background;
    if (text == nullptr && text_size != 0) return mm::display::Status::BadArgument;
    if (frame_width_bytes == 0) return mm::display::Status::BadArgument;
    if (frame.size() < frame_width_bytes) return mm::display::Status::BadArgument;
    if (foreground == mm::display::Color::Red) return mm::display::Status::BadArgument;
    const std::size_t frame_rows = frame.size() / frame_width_bytes;
    if (static_cast<std::size_t>(y) + font.height > frame_rows)
        return mm::display::Status::BadArgument;

    const auto* bytes = reinterpret_cast<const unsigned char*>(text);
    unsigned int code_points = 0;
    if (!count_code_points(bytes, text_size, true, code_points))
        return mm::display::Status::BadArgument;

    const std::size_t frame_width_bits = static_cast<std::size_t>(frame_width_bytes) * 8u;
    const bool white = foreground == mm::display::Color::White;
    unsigned int text_pos = 0;
    for (unsigned int cell = 0; cell < code_points; ++cell) {
        const std::size_t cell_x = static_cast<std::size_t>(x)
                                  + static_cast<std::size_t>(cell) * font.advance;
        unsigned int code_point = 0;
        unsigned int consumed = 0;
        // The whole text was validated above, so this cannot fail.
        if (!decode_code_point(bytes, text_size, text_pos, code_point, consumed))
            return mm::display::Status::BadArgument;
        text_pos += consumed;
        if (cell_x >= frame_width_bits) break;
        const Glyph* glyph = find_glyph(font.glyphs, code_point);
        if (glyph == nullptr || glyph->width == 0) continue;
        const unsigned int ink_x = static_cast<unsigned int>(cell_x) + glyph->x_offset;
        if (ink_x >= frame_width_bits) continue;
        const std::size_t remaining = frame_width_bits - ink_x;
        const unsigned int width =
            glyph->width < remaining ? glyph->width : static_cast<unsigned int>(remaining);
        const unsigned int row_bytes = glyph->row_bytes();
        for (unsigned int row = 0; row < font.height; ++row) {
            const std::byte* src = font.data.data() + glyph->byte_offset
                                 + static_cast<std::size_t>(row) * row_bytes;
            std::byte* dst = frame.data()
                           + static_cast<std::size_t>(y + row) * frame_width_bytes
                           + ink_x / 8u;
            const unsigned int shift = ink_x % 8u;
            const unsigned int out_bytes = (width + 7) / 8u;
            for (unsigned int b = 0; b < out_bytes; ++b) {
                unsigned int value = static_cast<unsigned int>(src[b]);
                if (b + 1 == out_bytes && width % 8u) value &= 0xFFu << (8u - (width % 8u));
                const unsigned int low = value >> shift;
                const unsigned int high = value << (8u - shift);
                if (white) {
                    dst[b] = dst[b] | static_cast<std::byte>(low);
                    if (b + 1 < out_bytes) dst[b + 1] = dst[b + 1] | static_cast<std::byte>(high);
                } else {
                    dst[b] = dst[b] & static_cast<std::byte>(~low);
                    if (b + 1 < out_bytes) dst[b + 1] = dst[b + 1] & static_cast<std::byte>(~high);
                }
            }
        }
    }
    return mm::display::Status::Ok;
}

mm::display::Status expand_row(std::span<const std::byte> packed_row,
                               unsigned int row_width_bytes,
                               unsigned short foreground, unsigned short background,
                               std::span<std::byte> rgb565_row)
{
    if (packed_row.size() < row_width_bytes) return mm::display::Status::BadArgument;
    if (rgb565_row.size() < static_cast<std::size_t>(row_width_bytes) * 16u)
        return mm::display::Status::BadArgument;
    for (unsigned int i = 0; i < row_width_bytes; ++i) {
        const unsigned int value = static_cast<unsigned int>(packed_row[i]);
        for (unsigned int bit = 0; bit < 8u; ++bit) {
            const unsigned short color =
                (value & (0x80u >> bit)) ? foreground : background;
            const std::size_t off = (static_cast<std::size_t>(i) * 8u + bit) * 2u;
            rgb565_row[off] = static_cast<std::byte>(color >> 8);
            rgb565_row[off + 1] = static_cast<std::byte>(color & 0xFFu);
        }
    }
    return mm::display::Status::Ok;
}

}
