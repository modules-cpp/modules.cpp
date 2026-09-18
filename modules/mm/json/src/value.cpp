// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
module;

#include <charconv>
#include <cmath>
#include <cstddef>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

module mm.json;

import :status;
import :scan;
import :value;

namespace mm::json {

Value::Value() = default;
Value::Value(bool boolean) : type_(Type::Boolean), boolean_(boolean) {}
Value::Value(long long integer) : type_(Type::Integer), integer_(integer) {}
Value::Value(double number) : type_(Type::Number), number_(number) {}
Value::Value(std::string_view text) : type_(Type::String), string_(text) {}

Value Value::array() {
    Value value;
    value.type_ = Type::Array;
    return value;
}

Value Value::object() {
    Value value;
    value.type_ = Type::Object;
    return value;
}

Value::Value(const Value&) = default;
Value& Value::operator=(const Value&) = default;
Value::Value(Value&&) noexcept = default;
Value& Value::operator=(Value&&) noexcept = default;
Value::~Value() = default;

bool Value::boolean(bool& out) const {
    if (type_ != Type::Boolean) return false;
    out = boolean_;
    return true;
}

bool Value::integer(long long& out) const {
    if (type_ != Type::Integer) return false;
    out = integer_;
    return true;
}

bool Value::number(double& out) const {
    if (type_ == Type::Number) { out = number_; return true; }
    if (type_ == Type::Integer) { out = static_cast<double>(integer_); return true; }
    return false;
}

std::string_view Value::string() const {
    return type_ == Type::String ? std::string_view{string_} : std::string_view{};
}

std::span<const Value> Value::items() const {
    return type_ == Type::Array ? std::span<const Value>{items_} : std::span<const Value>{};
}

std::span<const Member> Value::members() const {
    return type_ == Type::Object ? std::span<const Member>{members_}
                                 : std::span<const Member>{};
}

const Value* Value::find(std::string_view key) const {
    if (type_ != Type::Object) return nullptr;
    for (const auto& member : members_)
        if (member.key == key) return &member.value;
    return nullptr;
}

bool Value::push(Value item) {
    if (type_ != Type::Array) return false;
    items_.push_back(std::move(item));
    return true;
}

bool Value::set(std::string_view key, Value value) {
    if (type_ != Type::Object) return false;
    for (auto& member : members_)
        if (member.key == key) {
            member.value = std::move(value);
            return true;
        }
    members_.push_back(Member{std::string(key), std::move(value)});
    return true;
}

bool Value::equals(const Value& other) const {
    if (type_ != other.type_) return false;
    switch (type_) {
        case Type::Null: return true;
        case Type::Boolean: return boolean_ == other.boolean_;
        case Type::Integer: return integer_ == other.integer_;
        case Type::Number: return number_ == other.number_ && std::signbit(number_) == std::signbit(other.number_);
        case Type::String: return string_ == other.string_;
        case Type::Array:
            if (items_.size() != other.items_.size()) return false;
            for (std::size_t i = 0; i < items_.size(); ++i)
                if (!items_[i].equals(other.items_[i])) return false;
            return true;
        case Type::Object:
            if (members_.size() != other.members_.size()) return false;
            for (std::size_t i = 0; i < members_.size(); ++i)
                if (members_[i].key != other.members_[i].key ||
                    !members_[i].value.equals(other.members_[i].value))
                    return false;
            return true;
    }
    return false;
}

namespace {

// The scanner's fault as an Outcome.
[[nodiscard]] Outcome failed(const Scanner& scanner) {
    return {scanner.issue().status, scanner.issue()};
}

[[nodiscard]] Outcome failed(Status status, std::size_t offset, std::string_view text) {
    unsigned int line = 1;
    unsigned int column = 1;
    for (std::size_t i = 0; i < offset && i < text.size(); ++i) {
        if (text[i] == '\n') { ++line; column = 1; }
        else ++column;
    }
    return {status, Issue{offset, line, column, status, describe(status)}};
}

// A frame of the parse: the container being filled, and the key its next
// value belongs to when it is an object. discard is KeepFirst's doing: the
// key repeats one already held, so the value under it is parsed and dropped.
struct Frame {
    Value* container = nullptr;
    std::string key;
    bool discard = false;
};

}  // namespace

Outcome parse(std::string_view text, Value& out, ParseOptions options) {
    Scanner scanner(text);
    Value root;
    bool root_set = false;
    std::vector<Frame> stack;
    stack.reserve(8);

    // Places a finished scalar or a new container where the grammar put it:
    // as the root, as an array item, or as the pending key's value. Answers
    // where it landed, or null when KeepFirst dropped it. Only the innermost
    // container is ever appended to, so a pointer into a parent's storage
    // stays valid for as long as a frame holds it.
    const auto place = [&](Value&& value) -> Value* {
        if (stack.empty()) {
            root = std::move(value);
            root_set = true;
            return &root;
        }
        Frame& frame = stack.back();
        if (frame.container->type() == Type::Array) {
            (void)frame.container->push(std::move(value));
            return const_cast<Value*>(&frame.container->items().back());
        }
        if (frame.discard) return nullptr;
        (void)frame.container->set(frame.key, std::move(value));
        return const_cast<Value*>(frame.container->find(frame.key));
    };

    while (true) {
        Event event{};
        const auto status = scanner.next(event);
        if (status != Status::Ok) return failed(scanner);
        Value value;
        switch (event.token) {
            case Token::End:
                if (!root_set) return failed(Status::Truncated, text.size(), text);
                out = std::move(root);
                return {Status::Ok, {}};
            case Token::ObjectEnd:
            case Token::ArrayEnd:
                stack.pop_back();
                continue;
            case Token::Key: {
                std::string key;
                const auto decoded = unescape_into(
                    text.substr(event.offset, event.length), key);
                if (decoded != Status::Ok) return failed(decoded, event.offset, text);
                Frame& frame = stack.back();
                frame.discard = false;
                if (frame.container->find(key) != nullptr) {
                    // The offset of the key's opening quote, which is where
                    // a person looking for the repeat will look.
                    if (options.duplicates == Duplicates::Reject)
                        return failed(Status::DuplicateKey, event.offset - 1, text);
                    if (options.duplicates == Duplicates::KeepFirst) frame.discard = true;
                }
                frame.key = std::move(key);
                continue;
            }
            case Token::ObjectBegin: value = Value::object(); break;
            case Token::ArrayBegin: value = Value::array(); break;
            case Token::String: {
                std::string decoded;
                const auto status_decoded = unescape_into(
                    text.substr(event.offset, event.length), decoded);
                if (status_decoded != Status::Ok)
                    return failed(status_decoded, event.offset, text);
                value = Value(std::string_view{decoded});
                break;
            }
            case Token::Integer: {
                long long parsed = 0;
                const auto digits = text.substr(event.offset, event.length);
                if (integer(digits, parsed) == Status::Ok) {
                    value = Value(parsed);
                } else {
                    if (options.wide_integers == WideIntegers::Reject)
                        return failed(Status::BadNumber, event.offset, text);
                    double approximate = 0;
                    if (number(digits, approximate) != Status::Ok)
                        return failed(Status::BadNumber, event.offset, text);
                    value = Value(approximate);
                }
                break;
            }
            case Token::Number: {
                double parsed = 0;
                if (number(text.substr(event.offset, event.length), parsed) != Status::Ok)
                    return failed(Status::BadNumber, event.offset, text);
                value = Value(parsed);
                break;
            }
            case Token::True: value = Value(true); break;
            case Token::False: value = Value(false); break;
            case Token::Null: value = Value(); break;
        }
        const bool container = event.token == Token::ObjectBegin ||
                               event.token == Token::ArrayBegin;
        Value* placed = place(std::move(value));
        if (!container) continue;
        if (placed == nullptr) {
            // Dropped under KeepFirst: the scanner walks it so the grammar is
            // still checked, and nothing is built.
            if (scanner.skip() != Status::Ok) return failed(scanner);
            continue;
        }
        stack.push_back(Frame{placed, std::string{}, false});
    }
}

namespace {

// UTF-8 validity of a string about to be written, by the same table the
// scanner uses; a string a Value holds may have come from anywhere.
[[nodiscard]] bool well_formed(std::string_view text) {
    std::size_t i = 0;
    while (i < text.size()) {
        const int lead = static_cast<unsigned char>(text[i]);
        std::size_t length = 0;
        if (lead < 0x80) length = 1;
        else if (lead < 0xC2) return false;
        else if (lead < 0xE0) length = 2;
        else if (lead < 0xF0) length = 3;
        else if (lead <= 0xF4) length = 4;
        else return false;
        if (i + length > text.size()) return false;
        if (length >= 2) {
            const int second = static_cast<unsigned char>(text[i + 1]);
            int low = 0x80;
            int high = 0xBF;
            if (lead == 0xE0) low = 0xA0;
            if (lead == 0xED) high = 0x9F;
            if (lead == 0xF0) low = 0x90;
            if (lead == 0xF4) high = 0x8F;
            if (second < low || second > high) return false;
            for (std::size_t k = 2; k < length; ++k)
                if ((static_cast<unsigned char>(text[i + k]) & 0xC0) != 0x80) return false;
        }
        i += length;
    }
    return true;
}

void write_string(std::string_view text, std::string& out) {
    static constexpr char hex[] = "0123456789abcdef";
    out.push_back('"');
    for (const char c : text) {
        const unsigned char byte = static_cast<unsigned char>(c);
        switch (c) {
            case '"': out += "\\\""; continue;
            case '\\': out += "\\\\"; continue;
            case '\b': out += "\\b"; continue;
            case '\f': out += "\\f"; continue;
            case '\n': out += "\\n"; continue;
            case '\r': out += "\\r"; continue;
            case '\t': out += "\\t"; continue;
            default: break;
        }
        if (byte < 0x20) {
            out += "\\u00";
            out.push_back(hex[byte >> 4]);
            out.push_back(hex[byte & 0xF]);
            continue;
        }
        out.push_back(c);
    }
    out.push_back('"');
}

[[nodiscard]] bool write_number(double value, std::string& out) {
    if (!std::isfinite(value)) return false;
    char buffer[64];
    const auto result = std::to_chars(buffer, buffer + sizeof buffer, value);
    if (result.ec != std::errc{}) return false;
    std::string_view text{buffer, static_cast<std::size_t>(result.ptr - buffer)};
    out += text;
    // A double whose shortest form has no point and no exponent would read
    // back as an Integer; the type survives the round trip with a .0.
    if (text.find_first_of(".eE") == std::string_view::npos) out += ".0";
    return true;
}

void write_integer(long long value, std::string& out) {
    char buffer[32];
    const auto result = std::to_chars(buffer, buffer + sizeof buffer, value);
    out.append(buffer, static_cast<std::size_t>(result.ptr - buffer));
}

void newline(std::string& out, Layout layout, unsigned int depth) {
    if (layout != Layout::Indented) return;
    out.push_back('\n');
    for (unsigned int i = 0; i < depth; ++i) out += "  ";
}

struct WriteFrame {
    const Value* container;
    std::size_t index;
};

}  // namespace

Outcome write(const Value& value, std::string& out, Layout layout) {
    std::string text;
    std::vector<WriteFrame> stack;
    stack.reserve(8);

    // Writes one scalar, or opens a container and pushes its frame.
    // Answers false for a value the format cannot hold.
    const auto emit = [&](const Value& item) -> Status {
        switch (item.type()) {
            case Type::Null: text += "null"; return Status::Ok;
            case Type::Boolean: {
                bool flag = false;
                (void)item.boolean(flag);
                text += flag ? "true" : "false";
                return Status::Ok;
            }
            case Type::Integer: {
                long long integral = 0;
                (void)item.integer(integral);
                write_integer(integral, text);
                return Status::Ok;
            }
            case Type::Number: {
                double real = 0;
                (void)item.number(real);
                return write_number(real, text) ? Status::Ok : Status::BadNumber;
            }
            case Type::String:
                if (!well_formed(item.string())) return Status::BadUnicode;
                write_string(item.string(), text);
                return Status::Ok;
            case Type::Array:
            case Type::Object:
                if (stack.size() >= maximum_depth) return Status::TooDeep;
                text.push_back(item.type() == Type::Array ? '[' : '{');
                stack.push_back({&item, 0});
                return Status::Ok;
        }
        return Status::BadNumber;
    };

    const auto first = emit(value);
    if (first != Status::Ok) return {first, Issue{0, 1, 1, first, describe(first)}};

    while (!stack.empty()) {
        WriteFrame& frame = stack.back();
        const bool array = frame.container->type() == Type::Array;
        const std::size_t count = array ? frame.container->items().size()
                                        : frame.container->members().size();
        if (frame.index == count) {
            if (count != 0) newline(text, layout, static_cast<unsigned int>(stack.size() - 1));
            text.push_back(array ? ']' : '}');
            stack.pop_back();
            continue;
        }
        if (frame.index != 0) text.push_back(',');
        newline(text, layout, static_cast<unsigned int>(stack.size()));
        const std::size_t index = frame.index++;
        const Value* next = nullptr;
        if (array) {
            next = &frame.container->items()[index];
        } else {
            const Member& member = frame.container->members()[index];
            if (!well_formed(member.key))
                return {Status::BadUnicode,
                        Issue{0, 1, 1, Status::BadUnicode, describe(Status::BadUnicode)}};
            write_string(member.key, text);
            text += layout == Layout::Indented ? ": " : ":";
            next = &member.value;
        }
        const auto status = emit(*next);
        if (status != Status::Ok) return {status, Issue{0, 1, 1, status, describe(status)}};
    }
    out = std::move(text);
    return {Status::Ok, {}};
}

}  // namespace mm::json
