// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
module;

#include <cstddef>
#include <span>
#include <string_view>

export module mm.shell:state;

import :source;
import :status;

export namespace mm::shell {

struct VariableSlot {
    SourceSpan name;
    SourceSpan value;
};

struct PositionalSlot {
    SourceSpan value;
};

struct StateResult {
    Status status = Status::Ok;
    OverflowInfo overflow;

    [[nodiscard]] constexpr bool ok() const { return status == Status::Ok; }
};

struct ValueLookup {
    bool found = false;
    std::string_view value;
};

struct ShellState {
    int last_status = 0;
    bool errexit = false;
    bool nounset = false;

    ShellState() = default;
    ShellState(std::span<VariableSlot> variables,
               std::span<char> variable_text,
               std::span<PositionalSlot> positionals,
               std::span<char> positional_text);

    [[nodiscard]] StateResult assign(std::string_view name,
                                     std::string_view value);
    // Returned views become invalid on assignment or reset. Assignment
    // input must not alias the variable text pool.
    [[nodiscard]] ValueLookup lookup(std::string_view name) const;
    [[nodiscard]] std::string_view ifs() const;

    // Replacements append to the caller's text pool, so arguments may refer
    // to the previous positional values. Reset reclaims the entire pool.
    [[nodiscard]] StateResult set_positionals(
        std::string_view command_name,
        std::span<const std::string_view> arguments);
    [[nodiscard]] ValueLookup positional(std::size_t index) const;
    [[nodiscard]] std::size_t argument_count() const;
    [[nodiscard]] StateResult shift(std::size_t count = 1);
    void reset();

private:
    std::span<VariableSlot> variables_;
    std::span<char> variable_text_;
    std::span<PositionalSlot> positionals_;
    std::span<char> positional_text_;
    std::size_t variable_count_ = 0;
    std::size_t variable_text_used_ = 0;
    std::size_t positional_count_ = 0;
    std::size_t positional_text_used_ = 0;
    std::size_t shifted_ = 0;
};

}  // namespace mm::shell
