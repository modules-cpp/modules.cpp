// Black box tests for mm.shell's current_shell.
//
// Each case saves and restores the real $SHELL around itself, since it is
// this process's actual environment, shared with every other test in the
// same run.

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

import mm.shell;
import mm.test;

namespace {

constexpr std::string_view name = "SHELL";

class scoped_shell_variable {
public:
    scoped_shell_variable() : previous_(mm::shell::get(name)) {}

    ~scoped_shell_variable() {
        if (previous_.has_value())
            (void)mm::shell::set(name, *previous_);
        else
            (void)mm::shell::unset(name);
    }

    scoped_shell_variable(const scoped_shell_variable&) = delete;
    scoped_shell_variable& operator=(const scoped_shell_variable&) = delete;

private:
    std::optional<std::string> previous_;
};

void reflects_a_set_shell_variable() {
    const scoped_shell_variable guard;

    (void)mm::shell::set(name, "/bin/dash");

    mm::test::expect(mm::shell::current_shell() == std::filesystem::path("/bin/dash"),
                     "expected current_shell to return the path named by $SHELL");
}

void is_empty_when_shell_is_unset() {
    const scoped_shell_variable guard;

    (void)mm::shell::unset(name);

    mm::test::expect(mm::shell::current_shell().empty(),
                     "expected current_shell to be empty when $SHELL is unset");
}

const mm::test::case_ cases[] = {
    { "reflects a set shell variable",       &reflects_a_set_shell_variable },
    { "is empty when shell is unset",        &is_empty_when_shell_is_unset },
};

const mm::test::registrar reg{"mm.shell current_shell", cases};

}  // namespace
