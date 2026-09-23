// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
module;

#include <cstddef>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

export module mm.shell.full:function;

import :syntax;
import mm.shell;

export namespace mm::shell::full {

struct FullFunction {
    std::string name;
    // Owns its own source and syntax, so a definition outlives the script that
    // created it and a redefinition cannot dangle an executing body.
    std::shared_ptr<const FullScript> body;
};

struct DefineResult {
    Status status = Status::Ok;
    Diagnostic diagnostic;

    [[nodiscard]] bool ok() const { return status == Status::Ok; }
};

// The full-profile function table. It keeps mm.shell's lookup, atomic
// replacement, and lifetime contract, and differs only in owning its storage
// instead of borrowing caller spans.
//
// A body is parsed before the visible handle changes, so a failed definition
// leaves the previous one in place. A handle is shared, so a body that is
// running when it is redefined keeps executing the text it started with.
class FullFunctionLibrary {
public:
    [[nodiscard]] DefineResult define(std::string_view name,
                                      std::string_view body);
    [[nodiscard]] const FullFunction* find(std::string_view name) const;
    // Returns the shared handle, which is what a call frame should retain.
    [[nodiscard]] std::shared_ptr<const FullScript> body(
        std::string_view name) const;
    [[nodiscard]] bool remove(std::string_view name);
    [[nodiscard]] std::size_t size() const { return functions_.size(); }
    [[nodiscard]] const std::vector<FullFunction>& functions() const {
        return functions_;
    }
    void reset() { functions_.clear(); }

private:
    std::vector<FullFunction> functions_;
};

}  // namespace mm::shell::full
