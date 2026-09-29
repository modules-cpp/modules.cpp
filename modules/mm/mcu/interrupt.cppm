// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
module;

#include <cstdint>

export module mm.mcu:interrupt;

import :status;
import :interrupt_types;
import :platform;

export namespace mm::mcu {

// A critical section against the platform's own handlers. Between
// interrupts_disable and its interrupts_enable no handler of the selected
// platform runs on the calling core, so a short sequence of calls -- two
// transports' progress read at one instant, say -- sees one state of the
// world. A platform with no handler above its seam answers Ok and masks
// nothing, which is the same promise kept trivially.
//
// enable restores what its disable saved rather than unmasking outright, so
// sections nest when each level has its own state and they end in reverse
// order. A section is short and bounded: it makes no call that waits -- no
// delay, no gpio_wait, no bus transfer -- and it ends on the core that began
// it. docs/modules-execution.mdy states these rules.
//
// A state already held is BadArgument for disable, and a state not held is
// BadArgument for enable; neither reaches the platform. state changes only on
// Ok.
[[nodiscard]] inline Status interrupts_disable(InterruptState& state) {
    if (state.held) return Status::BadArgument;
    std::uint32_t saved = 0;
    const auto status = platform().interrupts_disable(saved);
    if (status != Status::Ok) return status;
    state.saved = saved;
    state.held = true;
    return Status::Ok;
}

[[nodiscard]] inline Status interrupts_enable(InterruptState& state) {
    if (!state.held) return Status::BadArgument;
    const auto status = platform().interrupts_enable(state.saved);
    if (status != Status::Ok) return status;
    state.held = false;
    return Status::Ok;
}

// The section as a scope: disabled on construction, enabled on destruction
// when the disable succeeded. status says whether the section is in force, and
// a caller that cannot proceed without one checks it.
class InterruptGuard {
public:
    InterruptGuard() : status_(interrupts_disable(state_)) {}
    ~InterruptGuard() {
        if (state_.held) static_cast<void>(interrupts_enable(state_));
    }
    InterruptGuard(const InterruptGuard&) = delete;
    InterruptGuard& operator=(const InterruptGuard&) = delete;
    InterruptGuard(InterruptGuard&&) = delete;
    InterruptGuard& operator=(InterruptGuard&&) = delete;

    [[nodiscard]] Status status() const { return status_; }

private:
    InterruptState state_;
    Status status_;
};

}
