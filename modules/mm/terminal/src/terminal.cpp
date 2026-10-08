module;
#include <algorithm>
#include <cstddef>
#include <span>
#include <string_view>

module mm.terminal;
import mm.display;
import mm.gfx;
import mm.fonts;

namespace mm::terminal {
Status Terminal::initialize(unsigned int columns, unsigned int rows,
                            std::span<char> cells) {
    if (columns == 0 || rows == 0 || columns > cells.size() / rows)
        return Status::BadArgument;
    columns_ = columns;
    rows_ = rows;
    cells_ = cells.first(static_cast<std::size_t>(columns) * rows);
    clear();
    return Status::Ok;
}

void Terminal::clear() {
    std::fill(cells_.begin(), cells_.end(), ' ');
    column_ = row_ = 0;
    pending_wrap_ = false;
    dirty_ = !cells_.empty();
}

char Terminal::cell(unsigned int column, unsigned int row) const {
    if (column >= columns_ || row >= rows_) return '\0';
    return cells_[static_cast<std::size_t>(row) * columns_ + column];
}

void Terminal::newline() {
    column_ = 0;
    pending_wrap_ = false;
    if (++row_ == rows_) {
        // Forward copying is safe when the destination precedes the source.
        for (std::size_t i = columns_; i < cells_.size(); ++i)
            cells_[i - columns_] = cells_[i];
        std::fill(cells_.end() - columns_, cells_.end(), ' ');
        row_ = rows_ - 1;
        dirty_ = true;
    }
}

void Terminal::put(char ch) {
    if (pending_wrap_) newline();
    cells_[static_cast<std::size_t>(row_) * columns_ + column_] = ch;
    dirty_ = true;
    if (column_ + 1 == columns_) pending_wrap_ = true;
    else ++column_;
}

Status Terminal::write(std::string_view text) {
    if (cells_.empty()) return Status::NotInitialized;
    for (unsigned char ch : text) {
        switch (ch) {
        case '\n': newline(); break;
        case '\r': column_ = 0; pending_wrap_ = false; break;
        case '\f': clear(); break;
        case '\b':
            if (pending_wrap_) pending_wrap_ = false;
            else if (column_ != 0) --column_;
            else break;
            cells_[static_cast<std::size_t>(row_) * columns_ + column_] = ' ';
            dirty_ = true;
            break;
        case '\t': {
            if (pending_wrap_) newline();
            const auto spaces = 8u - column_ % 8u;
            for (unsigned int i = 0; i < spaces; ++i) put(' ');
            break;
        }
        default:
            if (ch >= 32 && ch != 127) put(ch < 127 ? static_cast<char>(ch) : '?');
            break;
        }
    }
    return Status::Ok;
}

Status Terminal::present(mm::display::Display& display, mm::gfx::Surface surface,
                         const mm::fonts::Font& font, std::span<std::byte> scratch,
                         bool force) {
    if (cells_.empty()) return Status::NotInitialized;
    const auto geometry = display.geometry();
    if (!surface.valid() || surface.bits_per_pixel != 1 ||
        font.advance == 0 || font.height == 0 || font.line_height < font.height ||
        columns_ > surface.width / font.advance ||
        rows_ > surface.height / font.line_height ||
        surface.width > geometry.width || surface.height > geometry.height ||
        (geometry.bits_per_pixel != 1 && geometry.bits_per_pixel != 16) ||
        (geometry.bits_per_pixel == 16 && scratch.size() < surface.width * 2u))
        return Status::BadArgument;
    if (!dirty_ && !force) return Status::Ok;
    dirty_ = true;
    auto status = mm::gfx::fill(surface, mm::display::Color::Black);
    if (status != Status::Ok) return status;
    for (unsigned int y = 0; y < rows_; ++y) {
        for (unsigned int x = 0; x < columns_; ++x) {
            const char8_t ch = static_cast<char8_t>(cell(x, y));
            status = mm::fonts::render(&ch, 1, font, mm::display::Color::White,
                                       x * font.advance, y * font.line_height, surface);
            if (status != Status::Ok) return status;
        }
    }
    status = geometry.bits_per_pixel == 1
        ? mm::gfx::write(display, surface, 0, 0)
        : mm::gfx::write(display, surface, 0, 0,
                         {mm::gfx::rgb565_black, mm::gfx::rgb(255, 176, 0)}, {}, scratch);
    if (status != Status::Ok) return status;
    status = display.refresh(mm::display::Refresh::Full);
    if (status == Status::Ok) dirty_ = false;
    return status;
}
}
