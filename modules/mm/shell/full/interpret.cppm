// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
module;

#include <cstddef>
#include <span>
#include <string>
#include <string_view>
#include <vector>

export module mm.shell.full:interpret;

import :function;
import :service;
import :state;
import :syntax;
import mm.shell;

export namespace mm::shell::full {

// What the shell handed to the process service, in order. The differential
// harness compares this trace rather than guessing at a child's behaviour.
struct ExternalRecord {
    std::vector<std::string> arguments;
    std::vector<std::string> environment;
    std::string directory;
};

struct RunOutcome {
    int status = 0;
    ServiceStatus service = ServiceStatus::Ok;
    Diagnostic diagnostic;
    // True when exit, an errexit failure, or an unset parameter under set -u
    // ended the script rather than the list simply running out.
    bool exited = false;

    [[nodiscard]] bool ok() const {
        return service == ServiceStatus::Ok &&
               diagnostic.status == ParseStatus::Complete;
    }
};

// The descriptors the shell itself reads and writes. A handle left invalid
// means that stream is unavailable, which a builtin reports rather than
// silently discarding.
struct Streams {
    Handle input = invalid_handle;
    Handle output = invalid_handle;
    Handle error = invalid_handle;
};

// Walks a FullScript and executes it against abstract services. Recursion is
// ordinary C++ recursion: the cooperative step budget belongs to the embedded
// profile, where an application pumps between commands, not to a host shell.
class Interpreter {
public:
    Interpreter(FullState& state, Services services);
    Interpreter(const Interpreter&) = delete;
    Interpreter& operator=(const Interpreter&) = delete;
    Interpreter(Interpreter&&) = delete;
    Interpreter& operator=(Interpreter&&) = delete;

    void set_streams(Streams streams);
    // $0 and the positional parameters for this script.
    [[nodiscard]] Status set_arguments(
        std::string_view name, std::span<const std::string_view> arguments);

    [[nodiscard]] RunOutcome run(const FullScript& script);
    // Parses and runs text, which is how a trap action and a command
    // substitution body reach the same evaluator.
    [[nodiscard]] RunOutcome run_text(std::string_view text);

    [[nodiscard]] FullFunctionLibrary& functions() { return functions_; }
    [[nodiscard]] const std::vector<ExternalRecord>& externals() const {
        return externals_;
    }
    // How many times a program was actually handed to the process service,
    // which native qualification asserts is zero for /bin/sh.
    [[nodiscard]] std::size_t spawn_count() const { return spawns_; }

private:
    enum class Flow { Normal, Break, Continue, Return, Exit };

    struct Step {
        Flow flow = Flow::Normal;
        int status = 0;
        ServiceStatus service = ServiceStatus::Ok;
        Diagnostic diagnostic;
        std::size_t levels = 0;

        [[nodiscard]] bool ok() const {
            return service == ServiceStatus::Ok &&
                   diagnostic.status == ParseStatus::Complete;
        }
    };

    // Everything one simple command needs after expansion and planning.
    struct Command {
        std::vector<std::string> arguments;
        std::vector<std::string> assignments;
        Streams streams;
        std::vector<Handle> opened;
    };

    [[nodiscard]] Step run_node(const FullScript& script, std::size_t node);
    [[nodiscard]] Step run_list(const FullScript& script, std::size_t node);
    [[nodiscard]] Step run_and_or(const FullScript& script,
                                  std::size_t node, bool tested);
    [[nodiscard]] Step run_pipeline_node(const FullScript& script,
                                         std::size_t node, bool tested);
    [[nodiscard]] Step run_simple(const FullScript& script,
                                  std::size_t node, bool tested);
    [[nodiscard]] Step run_if(const FullScript& script, std::size_t node);
    [[nodiscard]] Step run_while(const FullScript& script, std::size_t node);
    [[nodiscard]] Step run_for(const FullScript& script, std::size_t node);
    [[nodiscard]] Step run_case(const FullScript& script, std::size_t node);
    [[nodiscard]] Step run_subshell(const FullScript& script,
                                    std::size_t node);
    [[nodiscard]] Step define(const FullScript& script, std::size_t node);

    [[nodiscard]] Step builtin(const Command& command, bool& handled);
    [[nodiscard]] Step external(const Command& command);
    [[nodiscard]] Step call(const FullScript& body, const Command& command);

    [[nodiscard]] bool expand_fields(const FullScript& script,
                                     std::size_t token, bool split,
                                     bool pathname,
                                     std::vector<std::string>& out,
                                     Step& failure);
    [[nodiscard]] bool apply_redirections(const FullScript& script,
                                          std::size_t node, Command& command,
                                          Step& failure);
    void release(Command& command);
    [[nodiscard]] bool emit(Handle stream, std::string_view text);
    [[nodiscard]] Step fire_traps(int status);
    [[nodiscard]] Step poll_signals();

    static ServiceStatus substitute(void* context, std::string_view source,
                                    std::string& out, int& status);

    FullState& state_;
    Services services_;
    Streams streams_;
    FullFunctionLibrary functions_;
    std::vector<ExternalRecord> externals_;
    std::size_t spawns_ = 0;
    int last_substitution_ = 0;
    unsigned int depth_ = 0;
    // Nonzero while a condition, an inverted command, or a non-final && or ||
    // operand is running, which is exactly where set -e does not act.
    unsigned int tested_ = 0;
    bool in_function_ = false;
};

}  // namespace mm::shell::full
