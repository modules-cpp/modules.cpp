// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
module;

#include <cstddef>
#include <span>

export module mm.fonts:renderer;

import :types;
import mm.display;

export namespace mm::fonts {

// UTF-8 text composed into a one-bit packed frame. frame is row-major, MSB
// left, one bit per pixel, one is white and zero is black -- mm.display's
// one-bit packing, byte stride frame_width_bytes. Glyph pixels set the
// foreground, everything else is left untouched, so text can be composed
// over an existing frame. Code points absent from the font render blank.
// foreground Red has no one-bit value and is rejected; the composition is
// transparent, so background is advisory (a caller that wants a cleared
// region clears it before rendering). Text that reaches the right edge of
// the frame is clipped, not an error.
[[nodiscard]] mm::display::Status render(
    const char8_t* text, std::size_t text_size, const Font& font,
    mm::display::Color foreground, mm::display::Color background,
    unsigned int x, unsigned int y,
    std::span<std::byte> frame, unsigned int frame_width_bytes);

// Monospace measurement: the width is the count of well-formed code points
// times the advance, and the height is the font's line height. A malformed
// sequence ends the count; nothing is measured past it.
[[nodiscard]] TextMetrics measure(const char8_t* text,
                                  std::size_t text_size, const Font& font);

// One packed one-bit row into RGB565, most significant byte first, for the
// sixteen-bit panels: bit 1 becomes foreground, bit 0 background.
[[nodiscard]] mm::display::Status expand_row(
    std::span<const std::byte> packed_row, unsigned int row_width_bytes,
    unsigned short foreground, unsigned short background,
    std::span<std::byte> rgb565_row);

}
