// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
module;

#include <span>
#include <string>
#include <string_view>

export module mm.shell.full:trap;

import :service;
import :state;

export namespace mm::shell::full {

constexpr int exit_condition = 0;
constexpr int interrupt_condition = 2;
constexpr int terminate_condition = 15;

struct TrapResult {
    ServiceStatus service = ServiceStatus::Ok;
    std::string bad_condition;

    [[nodiscard]] bool ok() const {
        return service == ServiceStatus::Ok;
    }
};

// Arguments include "trap" and its action. All conditions are validated
// before a provider or the owning state is changed. EXIT is a shell event,
// not an operating-system signal.
[[nodiscard]] TrapResult configure_trap(
    FullState& state, std::span<const std::string_view> arguments,
    SignalService signals);

struct PendingTrap {
    ServiceStatus service = ServiceStatus::Ok;
    int condition = 0;
    std::string action;
};

[[nodiscard]] PendingTrap poll_trap(
    const FullState& state, SignalService signals);

}  // namespace mm::shell::full
