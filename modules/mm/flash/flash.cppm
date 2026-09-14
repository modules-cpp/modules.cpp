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

// True only when the selected board resolves through a supported Pico SDK
// board. BoardDefinition::chain lets a derived composite board use the same
// backend without turning its project name into Pico SDK vocabulary.
[[nodiscard]] bool supports(const mm::build::Platform& platform,
                            const mm::build::BoardDefinition& board);

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
