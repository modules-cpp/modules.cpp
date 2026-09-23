// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
module;

#include <cerrno>
#include <csignal>
#include <cstddef>
#include <vector>

module mm.shell.posix;

import :service;
import mm.shell.full;

namespace mm::shell::posix {
namespace {

// A handler may touch nothing but a volatile sig_atomic_t, so the pending set
// is file scope. Dispositions are process-wide, which is why at most one
// HostServices may install them at a time.
constexpr int condition_limit = 64;
volatile std::sig_atomic_t pending[condition_limit];

extern "C" void record_condition(int number) {
    if (number > 0 && number < condition_limit) pending[number] = 1;
}

[[nodiscard]] bool valid(int number) {
    return number > 0 && number < condition_limit;
}

}  // namespace

full::ServiceStatus HostServices::install_callback(void* context,
                                                  int number) {
    auto& self = *static_cast<HostServices*>(context);
    if (!valid(number)) return full::ServiceStatus::Invalid;
    struct sigaction action{};
    action.sa_handler = &record_condition;
    (void)sigemptyset(&action.sa_mask);
    // No SA_RESTART: a pending condition should interrupt a blocking read so
    // the shell can act on it between commands.
    action.sa_flags = 0;
    if (::sigaction(number, &action, nullptr) != 0) {
        return errno == EINVAL ? full::ServiceStatus::Invalid
                               : full::ServiceStatus::Failed;
    }
    pending[number] = 0;
    for (const auto installed : self.signals_) {
        if (installed == number) return full::ServiceStatus::Ok;
    }
    self.signals_.push_back(number);
    return full::ServiceStatus::Ok;
}

full::ServiceStatus HostServices::restore_callback(void* context,
                                                  int number) {
    auto& self = *static_cast<HostServices*>(context);
    if (!valid(number)) return full::ServiceStatus::Invalid;
    struct sigaction action{};
    action.sa_handler = SIG_DFL;
    (void)sigemptyset(&action.sa_mask);
    action.sa_flags = 0;
    if (::sigaction(number, &action, nullptr) != 0) {
        return errno == EINVAL ? full::ServiceStatus::Invalid
                               : full::ServiceStatus::Failed;
    }
    pending[number] = 0;
    for (std::size_t i = 0; i < self.signals_.size(); ++i) {
        if (self.signals_[i] != number) continue;
        self.signals_.erase(self.signals_.begin() +
                            static_cast<std::ptrdiff_t>(i));
        break;
    }
    return full::ServiceStatus::Ok;
}

// Reports one pending condition and consumes it. Conditions are reported in
// ascending order so a run is deterministic.
full::ServiceStatus HostServices::poll_callback(void* context, int& number) {
    auto& self = *static_cast<HostServices*>(context);
    number = 0;
    for (const auto installed : self.signals_) {
        if (!valid(installed) || pending[installed] == 0) continue;
        if (number == 0 || installed < number) number = installed;
    }
    if (number == 0) return full::ServiceStatus::Ok;
    pending[number] = 0;
    return full::ServiceStatus::Ok;
}

void HostServices::restore_all_signals() {
    while (!signals_.empty()) {
        const auto number = signals_.back();
        struct sigaction action{};
        action.sa_handler = SIG_DFL;
        (void)sigemptyset(&action.sa_mask);
        action.sa_flags = 0;
        (void)::sigaction(number, &action, nullptr);
        if (valid(number)) pending[number] = 0;
        signals_.pop_back();
    }
}

}  // namespace mm::shell::posix
