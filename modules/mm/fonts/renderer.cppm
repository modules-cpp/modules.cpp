// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
module;

#include <cstddef>
#include <span>

export module mm.fonts:renderer;

import :types;
import mm.display;
import mm.gfx;

export namespace mm::fonts {

// Transparent UTF-8 text on a one-bit Surface. Glyph pixels take ink; all
// other pixels are preserved. Missing glyphs render blank. Text is clipped
// at the right edge and rejected if it extends past the bottom.
[[nodiscard]] mm::display::Status render(
    const char8_t* text, std::size_t text_size, const Font& font,
    mm::display::Color ink, unsigned int x, unsigned int y,
    mm::gfx::Surface surface);

// Monospace measurement: the width is the count of well-formed code points
// times the advance, and the height is the font's line height. A malformed
// sequence ends the count; nothing is measured past it.
[[nodiscard]] TextMetrics measure(const char8_t* text,
                                  std::size_t text_size, const Font& font);

}
