// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
module;

#include <cstdint>

export module mm.mcu:interrupt_types;

export namespace mm::mcu {

// What interrupts_disable saved and interrupts_enable restores. saved is the
// platform's own word -- a mask register's previous value on a
// microcontroller -- and means nothing above the seam. held says the state is
// between a disable and its enable, so one state cannot be disabled twice or
// enabled without a disable. Nesting takes one state per level.
struct InterruptState {
    std::uint32_t saved = 0;
    bool held = false;
};

}
