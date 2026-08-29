// Black box tests for mm.shell's get/set/unset.
//
// All tests in a run share one real process environment, so every case
// unsets its own variable before and after, rather than relying on
// registration order to leave it in a known state for the next case.
// Setup and cleanup calls are voided rather than asserted: their success is
// not what the case is testing.

#include <optional>
#include <string>
#include <string_view>

import mm.shell;
import mm.test;

namespace {

constexpr std::string_view name = "MM_SHELL_TEST_VAR";

void get_of_an_unset_variable_is_absent() {
    (void)mm::shell::unset(name);

    mm::test::expect(!mm::shell::get(name).has_value(),
                     "expected an unset variable to have no value");

    (void)mm::shell::unset(name);
}

void set_makes_get_return_the_value() {
    (void)mm::shell::unset(name);

    mm::test::expect(mm::shell::set(name, "first"), "expected set to succeed");
    mm::test::expect(mm::shell::get(name) == "first",
                     "expected get to return the value just set");

    (void)mm::shell::unset(name);
}

void set_with_overwrite_replaces_an_existing_value() {
    (void)mm::shell::unset(name);
    (void)mm::shell::set(name, "first");

    mm::test::expect(mm::shell::set(name, "second", true), "expected an overwriting set to succeed");
    mm::test::expect(mm::shell::get(name) == "second",
                     "expected get to return the replaced value");

    (void)mm::shell::unset(name);
}

void set_without_overwrite_keeps_an_existing_value() {
    (void)mm::shell::unset(name);
    (void)mm::shell::set(name, "first");

    mm::test::expect(mm::shell::set(name, "second", false),
                     "expected a non-overwriting set to still report success");
    mm::test::expect(mm::shell::get(name) == "first",
                     "expected get to still return the original value");

    (void)mm::shell::unset(name);
}

void unset_makes_get_return_absent_again() {
    (void)mm::shell::unset(name);
    (void)mm::shell::set(name, "value");

    mm::test::expect(mm::shell::unset(name), "expected unset to succeed");
    mm::test::expect(!mm::shell::get(name).has_value(),
                     "expected get to have no value after unset");
}

void unset_of_an_already_absent_variable_still_succeeds() {
    (void)mm::shell::unset(name);

    mm::test::expect(mm::shell::unset(name),
                     "expected unset to succeed even when the variable was already absent");
}

const mm::test::case_ cases[] = {
    { "get of an unset variable is absent",              &get_of_an_unset_variable_is_absent },
    { "set makes get return the value",                  &set_makes_get_return_the_value },
    { "set with overwrite replaces an existing value",   &set_with_overwrite_replaces_an_existing_value },
    { "set without overwrite keeps an existing value",   &set_without_overwrite_keeps_an_existing_value },
    { "unset makes get return absent again",             &unset_makes_get_return_absent_again },
    { "unset of an already absent variable still succeeds", &unset_of_an_already_absent_variable_still_succeeds },
};

const mm::test::registrar reg{"mm.shell environment", cases};

}  // namespace
