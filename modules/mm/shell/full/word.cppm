// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
module;

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

export module mm.shell.full:word;

import :service;
import :syntax;
import mm.shell;

export namespace mm::shell::full {

// How a nested $(list) is run. Word expansion cannot execute a list itself, so
// the interpreter supplies this and the expander only asks. out receives the
// captured ordinary output with trailing newlines already removed.
struct Substituter {
    void* context = nullptr;
    ServiceStatus (*run)(void* context, std::string_view source,
                         std::string& out, int& status) = nullptr;
};

struct WordFields {
    Status status = Status::Ok;
    ServiceStatus service = ServiceStatus::Ok;
    std::vector<std::string> fields;
    // Byte offset in the spelling where a malformed or unsupported form
    // starts, for a diagnostic.
    std::size_t issue = 0;
    // The status of the last command substitution the word ran, which is what
    // a command consisting only of a substitution reports.
    int substitution_status = 0;

    [[nodiscard]] bool ok() const {
        return status == Status::Ok && service == ServiceStatus::Ok;
    }
};

struct ExpandRequest {
    // Split the result on IFS. False for an assignment value, a here-document
    // operand, a case selector, and a redirection target.
    bool split = true;
    // Match the result against the directory tree. False wherever POSIX says
    // pathname expansion does not apply.
    bool pathname = true;
    // The working directory a relative pattern resolves against.
    std::string_view directory;
};

// Expands one word's raw shell spelling into fields: quote handling, parameter
// expansion with every full-profile operator, arithmetic, command
// substitution, field splitting, pathname expansion, and quote removal.
//
// A field is globbed only when it holds an unquoted pattern byte and no quoted
// one. A word mixing both, such as "*"x*, is left literal rather than risking
// a quoted metacharacter acting as a wildcard.
[[nodiscard]] WordFields expand_word_full(std::string_view spelling,
                                          ShellState& state,
                                          ExpandRequest request,
                                          FileService files,
                                          Substituter substituter);

}  // namespace mm::shell::full
