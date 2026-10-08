#include <array>
#include <chrono>
#include <cstddef>
#include <iostream>
#include <string_view>
#include <thread>
#include <vector>

import mm.terminal;
import mm.stdio;
import mm.display;
import mm.gfx;
import mm.fonts;

int main(int argc, char** argv) {
    bool once = false;
    if (argc == 2 && std::string_view(argv[1]) == "--help") {
        std::cout << "Usage: terminal [--once]\n";
        return 0;
    }
    if (argc == 2 && std::string_view(argv[1]) == "--once") once = true;
    else if (argc != 1) {
        std::cerr << "Usage: terminal [--once]\n";
        return 64;
    }
    auto& display = mm::display::selected_display();
    auto& console = mm::stdio::selected_console();
    if (display.initialize() != mm::display::Status::Ok ||
        console.initialize() != mm::stdio::Status::Ok) {
        std::cerr << "terminal: cannot initialize the configured display and console\n";
        return 1;
    }
    const auto geometry = display.geometry();
    const auto& font = mm::fonts::kMono12;
    if (geometry.width < font.advance || geometry.height < font.line_height ||
        geometry.width > 4096 || geometry.height > 4096 ||
        (geometry.bits_per_pixel != 1 && geometry.bits_per_pixel != 16)) {
        std::cerr << "terminal: unsupported display geometry\n";
        return 1;
    }
    const auto columns = geometry.width / font.advance;
    const auto rows = geometry.height / font.line_height;
    std::vector<char> cells(static_cast<std::size_t>(columns) * rows);
    std::vector<std::byte> frame(((geometry.width + 7u) / 8u) * geometry.height);
    std::vector<std::byte> scratch(geometry.width * 2u);
    const mm::gfx::Surface surface{geometry.width, geometry.height, 1, frame};
    mm::terminal::Terminal terminal;
    if (terminal.initialize(columns, rows, cells) != mm::terminal::Status::Ok)
        return 1;
    std::array<std::byte, 256> input;
    do {
        std::size_t count = 0;
        const auto status = console.read(input, count);
        if (status != mm::stdio::Status::Ok || count > input.size()) {
            std::cerr << "terminal: console read failed\n";
            return 1;
        }
        const auto text = std::string_view(reinterpret_cast<const char*>(input.data()), count);
        const bool changed = terminal.dirty() || count != 0;
        if (terminal.write(text) != mm::terminal::Status::Ok ||
            terminal.present(display, surface, font, scratch) != mm::terminal::Status::Ok ||
            (!changed && display.refresh(mm::display::Refresh::Full) != mm::display::Status::Ok)) {
            std::cerr << "terminal: display update failed\n";
            return 1;
        }
        if (!once) std::this_thread::sleep_for(std::chrono::milliseconds(10));
    } while (!once);
    return 0;
}
