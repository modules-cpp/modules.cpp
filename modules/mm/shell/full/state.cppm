// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
module;

#include <cstddef>
#include <span>
#include <string>
#include <string_view>
#include <vector>

export module mm.shell.full:state;

import mm.shell;

export namespace mm::shell::full {

struct StateCapacity {
    std::size_t variable_slots = 128;
    std::size_t variable_bytes = 8192;
    std::size_t positional_slots = 128;
    std::size_t positional_bytes = 8192;
};

struct TrapAction {
    int condition = 0;
    std::string command;
};

// Owns the storage backing the embedded ShellState view. A child forks the
// value state but owns its host metadata; it cannot mutate its parent.
class FullState {
public:
    explicit FullState(StateCapacity capacity = {});
    FullState(const FullState&) = delete;
    FullState& operator=(const FullState&) = delete;
    FullState(FullState&&) = delete;
    FullState& operator=(FullState&&) = delete;

    [[nodiscard]] ShellState& core() { return core_; }
    [[nodiscard]] const ShellState& core() const { return core_; }
    [[nodiscard]] std::string_view directory() const { return directory_; }
    void set_directory(std::string_view path);
    [[nodiscard]] Status seed_environment(
        std::span<const std::string_view> entries);
    void export_name(std::string_view name);
    [[nodiscard]] bool is_exported(std::string_view name) const;
    void set_trap(int condition, std::string_view command);
    void clear_trap(int condition);
    [[nodiscard]] std::string_view trap(int condition) const;
    [[nodiscard]] Status fork_into(FullState& child) const;

private:
    std::vector<VariableSlot> variables_;
    std::vector<char> variable_bytes_;
    std::vector<PositionalSlot> positionals_;
    std::vector<char> positional_bytes_;
    ShellState core_;
    std::string directory_;
    std::vector<std::string> exported_;
    std::vector<TrapAction> traps_;
};

}  // namespace mm::shell::full
