// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
module;

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>

export module mm.json:scan;

import :status;

export namespace mm::json {

enum class Token {
    ObjectBegin, ObjectEnd, ArrayBegin, ArrayEnd,
    Key, String, Integer, Number, True, False, Null,
    End,
};

// One token: its kind, the bytes of the input it spans -- for a string, the
// bytes between the quotes, still escaped -- and the number of containers
// enclosing it.
struct Event {
    Token token = Token::End;
    std::size_t offset = 0;
    std::size_t length = 0;
    unsigned int depth = 0;
};

// The most containers a document may nest. The scanner keeps one bit per
// level in three sixty-four bit sets, so this is their width.
inline constexpr unsigned int maximum_depth = 64;

// A pull reader over a string view: no allocation, no recursion, one token
// per call. It implements RFC 8259 strictly, validates UTF-8 and escapes as
// it passes over a string, and pairs surrogate escapes there, so a caller
// that never decodes a string still refuses the documents the value layer
// refuses.
class Scanner {
public:
    explicit Scanner(std::string_view text);
    Scanner(const Scanner&) = delete;
    Scanner& operator=(const Scanner&) = delete;
    Scanner(Scanner&&) = delete;
    Scanner& operator=(Scanner&&) = delete;
    ~Scanner() = default;

    // The next token, or End after a complete document. event changes only
    // on Ok; on any other answer issue() says where and why, the scanner
    // stays at the fault, and every later call answers the same.
    [[nodiscard]] Status next(Event& event);
    [[nodiscard]] const Issue& issue() const { return issue_; }

    // Skip the value the last token began: everything inside a container
    // whose beginning was just returned, or the value of a key just
    // returned. After a scalar it does nothing.
    [[nodiscard]] Status skip();

private:
    [[nodiscard]] Status fail(Status status, std::size_t at);
    void skip_whitespace();
    [[nodiscard]] Status scan_value(Event& event, bool as_key);
    [[nodiscard]] Status scan_string(std::size_t& length);
    [[nodiscard]] Status scan_number(std::size_t& length, bool& integral);
    [[nodiscard]] Status scan_literal(std::string_view word);
    void advance(std::size_t count);

    std::string_view text_;         // the document, owned by the caller
    std::size_t offset_ = 0;        // the cursor, in bytes
    unsigned int line_ = 1;         // kept as the cursor moves
    unsigned int column_ = 1;
    unsigned int depth_ = 0;        // containers currently open
    std::uint64_t objects_ = 0;     // bit n set: level n is an object
    std::uint64_t commas_ = 0;      // bit n set: level n holds a value, so
                                    // a comma or a close comes next
    std::uint64_t colons_ = 0;      // bit n set: level n has read a key and
                                    // awaits its colon and value
    bool value_seen_ = false;       // a top-level value has been read
    Event last_;                    // the token last returned, for skip
    Issue issue_;                   // the last fault, for issue()
};

// The answer of unescape: the status and the byte count of the decoded
// string. On Ok, count is what was written; on Overflow, what would have
// been, so a caller can size a buffer and try again; otherwise zero. The
// count is part of the answer, not an output, so nothing the caller owns
// changes on a failure.
struct Decoded {
    Status status = Status::Ok;
    std::size_t count = 0;
};

// The bytes of a String or Key event, unescaped into out. Decodes every
// escape RFC 8259 names, including \uXXXX and surrogate pairs, to UTF-8.
// Validates and measures first, writes second: out changes only on Ok.
[[nodiscard]] Decoded unescape(std::string_view escaped, std::span<char> out);

// A number token as an integer, or as a double. integer answers BadNumber
// for a fraction, an exponent, or a value outside long long; number answers
// BadNumber for a magnitude beyond double and zero, with the sign, for one
// beneath it. Each output changes only on Ok.
[[nodiscard]] Status integer(std::string_view digits, long long& value);
[[nodiscard]] Status number(std::string_view digits, double& value);

}

// Shared by the value partition: unescape into a string it owns, sizing it
// to the count. out changes only on Ok.
namespace mm::json {
[[nodiscard]] Status unescape_into(std::string_view escaped, std::string& out);
}
