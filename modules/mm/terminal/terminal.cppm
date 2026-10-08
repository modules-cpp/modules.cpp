module;
#include <cstddef>
#include <span>
#include <string_view>

export module mm.terminal;
import mm.display;
import mm.gfx;
import mm.fonts;

export namespace mm::terminal {
using Status = mm::display::Status;

class Terminal {
public:
    [[nodiscard]] Status initialize(unsigned int columns, unsigned int rows,
                                    std::span<char> cells);
    void clear();
    [[nodiscard]] Status write(std::string_view text);
    [[nodiscard]] Status present(mm::display::Display&, mm::gfx::Surface,
                                const mm::fonts::Font&, std::span<std::byte> scratch,
                                bool force = false);
    [[nodiscard]] unsigned int columns() const { return columns_; }
    [[nodiscard]] unsigned int rows() const { return rows_; }
    [[nodiscard]] unsigned int column() const { return column_; }
    [[nodiscard]] unsigned int row() const { return row_; }
    [[nodiscard]] bool dirty() const { return dirty_; }
    [[nodiscard]] char cell(unsigned int column, unsigned int row) const;
private:
    void newline();
    void put(char);
    std::span<char> cells_;
    unsigned int columns_ = 0, rows_ = 0, column_ = 0, row_ = 0;
    bool pending_wrap_ = false, dirty_ = false;
};
}
