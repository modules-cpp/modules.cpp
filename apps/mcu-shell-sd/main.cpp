// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
//
// The board shell with file commands over the board's own storage at /data
// and the card in its SD socket at /sd.
#include <cstddef>
#include <span>
#include <string_view>

import mm.fs;
import mm.fs.fat;
import mm.fs.local;
import mm.sdcard.socket;
import mm.shell;
import mm.shell.board;
import mm.shell.fs;

namespace {

constexpr std::string_view prefixes[]{"/data", "/sd"};
mm::shell::fs::FsBinding binding{prefixes};

[[nodiscard]] std::string_view card_problem(mm::fs::Status status) {
    switch (status) {
        case mm::fs::Status::Ok: return {};
        case mm::fs::Status::TransportError:
        case mm::fs::Status::Timeout: return "no card in the socket\n";
        case mm::fs::Status::Corrupt: return "the card holds no FAT volume\n";
        default: return "the card did not mount\n";
    }
}

// "sd": mounts the card at /sd if it is not mounted, for a card inserted
// after the shell started.
void sd_handler(void*, std::span<const std::string_view> args,
                mm::shell::CommandContext& context, mm::shell::CommandResult& result) {
    if (args.size() != 1) {
        result.status = 2;
        result.error = mm::shell::Status::BadArgument;
        (void)context.io.err.write("sd: bad argument\n");
        return;
    }
    mm::fs::Volume* volume = nullptr;
    if (mm::fs::mounted("/sd", volume) == mm::fs::Status::Ok) return;
    const auto problem = card_problem(mm::fs::fat::mount("/sd", mm::sdcard::socket::card()));
    if (problem.empty()) return;
    result.status = 1;
    result.error = mm::shell::Status::Unavailable;
    (void)context.io.err.write("sd: ");
    (void)context.io.err.write(problem);
}

mm::shell::InstallResult install(void*, mm::shell::Registry& registry,
                                 mm::shell::CapabilitySet& capabilities,
                                 mm::shell::IoServices& io) {
    if (mm::fs::local::mount("/data", {.read_only = false, .format_if_blank = true}) !=
        mm::fs::Status::Ok)
        (void)io.err.write("mcu-shell-sd: /data did not mount\n");
    const auto problem = card_problem(mm::fs::fat::mount("/sd", mm::sdcard::socket::card()));
    if (!problem.empty()) {
        (void)io.err.write("mcu-shell-sd: /sd: ");
        (void)io.err.write(problem);
    }
    mm::shell::fs::enable(capabilities);
    const auto installed = mm::shell::fs::install_fs(registry, binding);
    if (!installed.ok()) return installed;
    mm::shell::CapabilitySet required;
    required.set(mm::shell::Capability::Files);
    return registry.install({.name = "sd",
                             .summary = "mount the card at /sd after inserting it",
                             .command_class = mm::shell::CommandClass::Builtin,
                             .required_capabilities = required,
                             .handler = &sd_handler,
                             .context = nullptr});
}

}  // namespace

int main() { return mm::shell::board::run({.context = nullptr, .install = &install}); }
