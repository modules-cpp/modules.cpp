// Raspberry Pi Pico firmware flashing implementation.
//
// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
module;

#include <filesystem>
#include <optional>
#include <string>

module mm.flash;

namespace mm::flash {

bool supports(const mm::build::Platform& platform) {
    if (!platform.sdk || !platform.board) return false;
    const auto& board = *platform.board;
    if (*platform.sdk == "pico-arm")
        return board == "pico" || board == "pico-w" || board == "pico2-arm" ||
               board == "pico2-w-arm";
    if (*platform.sdk == "pico-riscv")
        return board == "pico2-riscv" || board == "pico2-w-riscv";
    return false;
}

std::filesystem::path image_for(const std::filesystem::path& executable) {
    auto image = executable;
    image += ".uf2";
    return image;
}

std::optional<std::string> command(const std::filesystem::path& picotool,
                                   const std::filesystem::path& image) {
    if (picotool.empty() || image.empty()) return std::nullopt;
    return mm::build::shell_quote(picotool) + " 'load' '-v' '-x' " +
           mm::build::shell_quote(image);
}

int execute(const mm::build::Toolchain& toolchain,
            const std::filesystem::path& picotool,
            const std::filesystem::path& image) {
    const auto invocation = command(picotool, image);
    return invocation ? mm::build::run(toolchain, *invocation) : -1;
}

}
