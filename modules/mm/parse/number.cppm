// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
module;

#include <cstddef>
#include <cstdint>
#include <string_view>

export module mm.parse:number;

export namespace mm::parse {

// The kind of number a token holds, after shape validation.
enum class NumberKind {
    Invalid,       // no digits, wrong shape
    Integer,       // optional sign, digits only (decimal, hex, octal, binary)
    Float,         // decimal point present
    Scientific,    // exponent (e/E) present
};

// The result of parsing one number token.
struct NumberValue {
    NumberKind kind = NumberKind::Invalid;
    std::size_t offset = 0;      // start of the number in the source
    std::size_t length = 0;      // length of the number token
    std::size_t consumed = 0;    // bytes consumed (may include leading ws)
    std::int64_t integer = 0;    // valid when kind is Integer
    double real = 0.0;           // valid when kind is Float or Scientific,
                                 // and the nearest double of a decimal Integer
    bool overflow = false;
};

// Parse a number starting at `at` in `text`.
[[nodiscard]] NumberValue parse_number_at(std::string_view text,
                                        std::size_t at);

// Parse the entire text as a single number.
[[nodiscard]] NumberValue parse_number(std::string_view text);

}  // namespace mm::parse
