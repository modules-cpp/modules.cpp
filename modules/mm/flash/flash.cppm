// Reusable firmware flashing orchestration. The first backend intentionally
// supports only the Raspberry Pi Pico SDK platforms.
//
// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
module;

#include <filesystem>
#include <optional>
#include <string>

export module mm.flash;

import mm.build;

export namespace mm::flash {

// True only for the Pico SDK/board combinations implemented by the current
// backend. A different target fails before any external program is started.
[[nodiscard]] bool supports(const mm::build::Platform& platform);

// External Pico builds publish a UF2 supplement beside the ordinary,
// extensionless application artifact.
[[nodiscard]] std::filesystem::path image_for(
    const std::filesystem::path& executable);

// Constructs: picotool load -v -x <image>. Every filesystem-derived argument
// is shell quoted through mm.build.
[[nodiscard]] std::optional<std::string> command(
    const std::filesystem::path& picotool,
    const std::filesystem::path& image);

// Executes command() through the common child-process wrapper. Returns -1 when
// either path is empty and otherwise returns picotool's exit status.
int execute(const mm::build::Toolchain& toolchain,
            const std::filesystem::path& picotool,
            const std::filesystem::path& image);

}
