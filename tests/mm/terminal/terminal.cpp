#include <array>
#include <cstddef>
#include <span>
#include <string>

import mm.test;
import mm.terminal;
import mm.display;
import mm.gfx;
import mm.fonts;

namespace {
using mm::test::expect;
using mm::terminal::Status;

void lines() {
    std::array<char, 8> cells;
    mm::terminal::Terminal term;
    expect(term.write("x") == Status::NotInitialized, "initialization is required");
    expect(term.initialize(4, 2, cells) == Status::Ok, "initialize grid");
    expect(term.write("abcd\ne") == Status::Ok, "full line followed by newline");
    expect(std::string(cells.data(), cells.size()) == "abcde   ", "newline advances only once");
    expect(term.write("fghI") == Status::Ok, "wrap scrolls");
    expect(std::string(cells.data(), cells.size()) == "efghI   ", "oldest line is discarded");
    expect(term.initialize(9, 1, cells) == Status::BadArgument, "short storage rejected");
    expect(term.cell(0, 0) == 'e', "failed initialize preserves content");
    expect(term.cell(4, 0) == 0 && term.cell(0, 2) == 0, "out of range lookup");
    expect(term.initialize(0, 1, cells) == Status::BadArgument, "zero columns rejected");
    expect(term.initialize(~0u, ~0u, cells) == Status::BadArgument, "overflow dimensions rejected");
}

void controls() {
    std::array<char, 32> cells;
    mm::terminal::Terminal term;
    expect(term.initialize(16, 2, cells) == Status::Ok, "initialize");
    expect(term.write("abc\bD\rX\tY") == Status::Ok, "edit and tab");
    expect(term.cell(0, 0) == 'X' && term.cell(8, 0) == 'Y', "tab stops at column eight");
    expect(term.write("\fZ\x80") == Status::Ok, "clear and replacement");
    expect(term.cell(0, 0) == 'Z' && term.cell(1, 0) == '?' && term.row() == 0,
           "form feed homes cursor and non ASCII is replaced");
    expect(term.initialize(1, 1, cells) == Status::Ok, "single cell");
    expect(term.write("A\bB") == Status::Ok && term.cell(0, 0) == 'B',
           "backspace cancels pending wrap");
    expect(term.write("\n") == Status::Ok && term.cell(0, 0) == ' ', "one row scroll clears");
}

class Display final : public mm::display::Display {
public:
    unsigned int depth = 1, writes = 0, refreshes = 0;
    bool fail = false, ink = false;
    mm::display::Geometry geometry() const override { return {32, 24, depth}; }
    Status write(mm::display::Rectangle, std::span<const std::byte> bytes) override {
        ++writes;
        for (auto byte : bytes) if (byte != std::byte{0x00}) ink = true;
        return fail ? Status::TransportError : Status::Ok;
    }
    Status refresh(mm::display::Refresh) override { ++refreshes; return Status::Ok; }
};

void rendering() {
    std::array<char, 2> cells;
    std::array<std::byte, 96> pixels;
    std::array<std::byte, 64> scratch;
    mm::terminal::Terminal term;
    Display display;
    const mm::gfx::Surface surface{32, 24, 1, pixels};
    expect(term.initialize(2, 1, cells) == Status::Ok && term.write("Hi") == Status::Ok,
           "prepare text");
    display.fail = true;
    expect(term.present(display, surface, mm::fonts::kMono12, scratch) == Status::TransportError &&
           term.dirty(), "failed write keeps dirty state");
    display.fail = false;
    expect(term.present(display, surface, mm::fonts::kMono12, scratch) == Status::Ok &&
           !term.dirty() && display.ink, "render draws ink and clears dirty");
    const auto writes = display.writes;
    expect(term.present(display, surface, mm::fonts::kMono12, scratch) == Status::Ok &&
           display.writes == writes, "unchanged frame is not transmitted");
    display.depth = 16;
    expect(term.present(display, surface, mm::fonts::kMono12, {}, true) == Status::BadArgument,
           "RGB565 needs scratch storage");
    expect(term.present(display, surface, mm::fonts::kMono12, scratch, true) == Status::Ok &&
           display.writes > writes, "RGB565 expansion supports forced repaint");
}

const mm::test::case_ cases[] = {
    {"lines and bounds", &lines}, {"controls", &controls}, {"rendering", &rendering},
};
const mm::test::registrar reg{"mm.terminal", cases};
}
