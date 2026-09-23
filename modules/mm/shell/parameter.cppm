// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
module;

#include <cstdint>
#include <string_view>

export module mm.shell:parameter;

export namespace mm::shell {

enum class ParameterKind {
    Name,
    Positional,
    Count,
    LastStatus,
    ShellId,
    Star,
    At,
};

enum class ParameterOperator { None, Default, Alternate, Assign };
enum class ParameterStatus { Ok, Syntax, Range };

struct ParameterSpec {
    ParameterKind kind = ParameterKind::Name;
    ParameterOperator operation = ParameterOperator::None;
    std::string_view name;
    std::uint32_t index = 0;
    std::string_view operand;
};

struct ParameterParseResult {
    ParameterStatus status = ParameterStatus::Ok;
    ParameterSpec parameter;
};

[[nodiscard]] ParameterParseResult parse_parameter(
    std::string_view spelling);

}  // namespace mm::shell
