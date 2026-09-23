// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
module;

#include <cstddef>
#include <span>
#include <string>
#include <string_view>
#include <vector>

module mm.shell.full;

import :service;
import :state;
import :trap;

namespace mm::shell::full {
namespace {

[[nodiscard]] int condition_number(std::string_view name) {
    if (name == "0" || name == "EXIT") return exit_condition;
    if (name == "2" || name == "INT") return interrupt_condition;
    if (name == "15" || name == "TERM") return terminate_condition;
    return -1;
}

}  // namespace

TrapResult configure_trap(
    FullState& state, std::span<const std::string_view> arguments,
    SignalService signals) {
    if (arguments.size() < 3 || arguments[0] != "trap") {
        return {.service = ServiceStatus::Invalid};
    }
    std::vector<int> conditions;
    conditions.reserve(arguments.size() - 2);
    for (std::size_t i = 2; i < arguments.size(); ++i) {
        const int number = condition_number(arguments[i]);
        if (number < 0) {
            return {.service = ServiceStatus::Invalid,
                    .bad_condition = std::string(arguments[i])};
        }
        for (const auto existing : conditions) {
            if (existing == number) {
                return {.service = ServiceStatus::Invalid,
                        .bad_condition = std::string(arguments[i])};
            }
        }
        conditions.push_back(number);
    }
    const bool reset = arguments[1] == "-";
    for (const auto number : conditions) {
        if (number != exit_condition &&
            (reset ? signals.restore : signals.install) == nullptr) {
            return {.service = ServiceStatus::Invalid};
        }
    }
    std::size_t changed = 0;
    for (const auto number : conditions) {
        if (number != exit_condition) {
            auto* callback = reset ? signals.restore : signals.install;
            const auto status = callback(signals.context, number);
            if (status != ServiceStatus::Ok) {
                for (std::size_t i = 0; i < changed; ++i) {
                    const auto prior = conditions[i];
                    if (prior == exit_condition) continue;
                    auto* rollback = state.trap(prior).empty()
                        ? signals.restore : signals.install;
                    if (rollback != nullptr) {
                        (void)rollback(signals.context, prior);
                    }
                }
                return {.service = status};
            }
        }
        ++changed;
    }
    for (const auto number : conditions) {
        if (reset) {
            state.clear_trap(number);
        } else {
            state.set_trap(number, arguments[1]);
        }
    }
    return {};
}

PendingTrap poll_trap(const FullState& state,
                      SignalService signals) {
    if (signals.poll == nullptr) {
        return {.service = ServiceStatus::Invalid};
    }
    int condition = 0;
    const auto result = signals.poll(signals.context, condition);
    if (result != ServiceStatus::Ok) {
        return {.service = result};
    }
    if (condition != interrupt_condition &&
        condition != terminate_condition) {
        return {.service = ServiceStatus::Invalid};
    }
    return {.condition = condition,
            .action = std::string(state.trap(condition))};
}

}  // namespace mm::shell::full
