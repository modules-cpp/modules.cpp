// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
//
// The board shell with file commands over the board's own storage at /data.
#include <string_view>

import mm.fs;
import mm.fs.local;
import mm.shell;
import mm.shell.board;
import mm.shell.fs;

namespace {

constexpr std::string_view prefixes[]{"/data"};
mm::shell::fs::FsBinding binding{prefixes};

mm::shell::InstallResult install(void*, mm::shell::Registry& registry,
                                 mm::shell::CapabilitySet& capabilities,
                                 mm::shell::IoServices& io) {
    if (mm::fs::local::mount("/data", {.read_only = false, .format_if_blank = true}) !=
        mm::fs::Status::Ok)
        (void)io.err.write("mcu-shell: /data did not mount\n");
    mm::shell::fs::enable(capabilities);
    return mm::shell::fs::install_fs(registry, binding);
}

}  // namespace

int main() { return mm::shell::board::run({.context = nullptr, .install = &install}); }
