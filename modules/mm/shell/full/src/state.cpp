// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
module;

#include <cstddef>
#include <span>
#include <string>
#include <string_view>
#include <vector>

module mm.shell.full;

import :state;
import mm.shell;

namespace mm::shell::full {

FullState::FullState(StateCapacity capacity)
    : variables_(capacity.variable_slots),
      variable_bytes_(capacity.variable_bytes),
      positionals_(capacity.positional_slots),
      positional_bytes_(capacity.positional_bytes),
      core_(variables_, variable_bytes_, positionals_,
            positional_bytes_) {}

void FullState::set_directory(std::string_view path) {
    directory_.assign(path);
}

Status FullState::seed_environment(
    std::span<const std::string_view> entries) {
    for (const auto entry : entries) {
        const auto equal = entry.find('=');
        if (equal == std::string_view::npos || equal == 0) {
            return Status::BadArgument;
        }
        const auto name = entry.substr(0, equal);
        const auto value = entry.substr(equal + 1);
        const auto assigned = core_.assign(name, value);
        if (!assigned.ok()) return assigned.status;
        export_name(name);
    }
    return Status::Ok;
}

void FullState::export_name(std::string_view name) {
    if (!is_exported(name)) exported_.emplace_back(name);
}

bool FullState::is_exported(std::string_view name) const {
    for (const auto& entry : exported_) {
        if (entry == name) return true;
    }
    return false;
}

void FullState::set_trap(int condition, std::string_view command) {
    for (auto& entry : traps_) {
        if (entry.condition == condition) {
            entry.command.assign(command);
            return;
        }
    }
    traps_.push_back({condition, std::string(command)});
}

void FullState::clear_trap(int condition) {
    for (std::size_t i = 0; i < traps_.size(); ++i) {
        if (traps_[i].condition == condition) {
            traps_.erase(traps_.begin() + i);
            return;
        }
    }
}

std::string_view FullState::trap(int condition) const {
    for (const auto& entry : traps_) {
        if (entry.condition == condition) return entry.command;
    }
    return {};
}

Status FullState::fork_into(FullState& child) const {
    ShellState snapshot;
    const auto copied = core_.fork_variables(
        child.variables_, child.variable_bytes_, snapshot);
    if (!copied.ok()) return copied.status;
    const auto variables = child.core_.commit_variables_from(snapshot);
    if (!variables.ok()) return variables.status;
    std::vector<std::string_view> arguments;
    arguments.reserve(core_.argument_count());
    for (std::size_t i = 1; i <= core_.argument_count(); ++i) {
        arguments.push_back(core_.positional(i).value);
    }
    const auto name = core_.positional(0).value;
    const auto positionals = child.core_.set_positionals(name, arguments);
    if (!positionals.ok()) return positionals.status;
    child.core_.last_status = core_.last_status;
    child.core_.errexit = core_.errexit;
    child.core_.nounset = core_.nounset;
    child.core_.shell_id = core_.shell_id;
    child.directory_ = directory_;
    child.exported_ = exported_;
    child.traps_ = traps_;
    return Status::Ok;
}

}  // namespace mm::shell::full
