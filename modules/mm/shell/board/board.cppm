// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
module;

#include <cstddef>
#include <span>
#include <string_view>

export module mm.shell.board;

import mm.shell;

export namespace mm::shell::board {

// What an application adds before the session begins: further command packs
// and the capabilities they need, and whatever it mounts. io is the console,
// for reporting what did not work; install runs once, after the level-2 pack
// and the built-in scripts, and a failure ends run with 9.
struct Extras {
    void* context = nullptr;
    InstallResult (*install)(void* context, Registry& registry, CapabilitySet& capabilities,
                             IoServices& io) = nullptr;
};

// Command descriptors the board shell owns: the level-2 pack's 28, the two
// built-in scripts, and room for an application's packs.
inline constexpr std::size_t command_slots = 48;

// The interactive board shell over the selected mm.stdio console, as
// apps/mcu-shell runs it. Returns only on a failure, with its code.
[[nodiscard]] int run(const Extras& extras = {});

}  // namespace mm::shell::board
