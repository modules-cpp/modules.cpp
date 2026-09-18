// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
module;

#include <cstddef>
#include <span>
#include <string>
#include <string_view>
#include <vector>

export module mm.json:value;

import :status;
import :scan;

export namespace mm::json {

enum class Type { Null, Boolean, Integer, Number, String, Array, Object };

struct Member;

// A document value that owns its strings and children. One field per type
// rather than a variant, because the language document forbids project
// templates and permits standard ones; the unused fields cost a few bytes
// per value, and a document here is a compile-command list or a
// configuration, not a dataset.
class Value {
public:
    Value();                              // Null
    explicit Value(bool boolean);
    explicit Value(long long integer);
    explicit Value(double number);
    explicit Value(std::string_view text);
    static Value array();
    static Value object();

    // Declared here and defined after Member is complete, as the language
    // document requires of a type that owns storage and as the vector of an
    // incomplete type requires of anything that touches it.
    Value(const Value&);
    Value& operator=(const Value&);
    Value(Value&&) noexcept;
    Value& operator=(Value&&) noexcept;
    ~Value();

    [[nodiscard]] Type type() const { return type_; }
    [[nodiscard]] bool is_null() const { return type_ == Type::Null; }
    [[nodiscard]] bool boolean(bool& out) const;
    [[nodiscard]] bool integer(long long& out) const;
    [[nodiscard]] bool number(double& out) const;   // true for Integer too
    [[nodiscard]] std::string_view string() const;  // empty unless String
    [[nodiscard]] std::span<const Value> items() const;    // empty unless Array
    [[nodiscard]] std::span<const Member> members() const; // empty unless Object
    [[nodiscard]] const Value* find(std::string_view key) const;

    // Array only, and Object only: on any other type they answer false and
    // change nothing. set replaces an existing key's value in place and
    // appends a new key.
    bool push(Value item);
    bool set(std::string_view key, Value value);

    [[nodiscard]] bool equals(const Value& other) const;

private:
    Type type_ = Type::Null;
    bool boolean_ = false;
    long long integer_ = 0;
    double number_ = 0;
    std::string string_;
    std::vector<Value> items_;
    std::vector<Member> members_;   // incomplete here; permitted since C++17
};

struct Member {
    std::string key;
    Value value;
};

enum class Duplicates { Reject, KeepFirst, KeepLast };
enum class WideIntegers { Reject, Approximate };

struct ParseOptions {
    Duplicates duplicates = Duplicates::Reject;
    WideIntegers wide_integers = WideIntegers::Reject;
};

// Status and issue together, so that a failure is one answer and the
// caller's own variables are as they were.
struct Outcome {
    Status status = Status::Ok;
    Issue issue;
};

// out changes only when status is Ok.
[[nodiscard]] Outcome parse(std::string_view text, Value& out,
                            ParseOptions options = {});

enum class Layout { Compact, Indented };

// out is replaced by the document when status is Ok, and untouched
// otherwise.
[[nodiscard]] Outcome write(const Value& value, std::string& out,
                            Layout layout = Layout::Compact);

}
