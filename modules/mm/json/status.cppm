// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
module;

#include <cstddef>
#include <string_view>

export module mm.json:status;

export namespace mm::json {

// Every answer the module gives. Ok is the only success; the rest say what
// the grammar or a policy refused. Malformed is a character the grammar does
// not allow where it stands; Truncated is input that ended inside a value;
// the others name the thing that was wrong.
enum class Status {
    Ok,
    Malformed,
    Truncated,
    TooDeep,
    BadEscape,
    BadUnicode,
    BadNumber,
    DuplicateKey,
    Overflow,
};

// Where and what: the byte offset of the fault, the line and the column it
// falls on, both counted from one and the column in bytes, and the status.
// The description is fixed text for the status, never built at runtime.
struct Issue {
    std::size_t offset = 0;
    unsigned int line = 1;
    unsigned int column = 1;
    Status status = Status::Ok;
    std::string_view description;
};

// The fixed text an Issue carries for each status.
[[nodiscard]] constexpr std::string_view describe(Status status) {
    switch (status) {
        case Status::Ok: return "ok";
        case Status::Malformed: return "a character the grammar does not allow here";
        case Status::Truncated: return "input ended inside a value";
        case Status::TooDeep: return "nesting deeper than the module reads";
        case Status::BadEscape: return "an escape sequence the grammar does not allow";
        case Status::BadUnicode: return "an unpaired surrogate or malformed UTF-8";
        case Status::BadNumber: return "a number the type cannot hold";
        case Status::DuplicateKey: return "an object key repeated";
        case Status::Overflow: return "an output too small for the result";
    }
    return "unknown status";
}

}
