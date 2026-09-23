// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
#include <array>
#include <string_view>

import mm.shell;
import mm.test;

namespace {

using mm::shell::ShellState;
using mm::shell::Status;
using mm::shell::StorageClass;
using mm::test::expect;

void variable_transactions() {
    mm::shell::VariableSlot slots[2]{};
    char text[12]{};
    ShellState state{slots, text, {}, {}};
    expect(state.assign("A", "one").ok(), "first assignment succeeds");
    expect(state.lookup("A").found &&
               state.lookup("A").value == "one",
           "assigned value is visible");
    const auto overflow = state.assign("A", "longer-than-pool");
    expect(overflow.status == Status::Overflow &&
               overflow.overflow.storage_class ==
                   StorageClass::VariableText,
           "replacement preflights value capacity");
    expect(state.lookup("A").value == "one",
           "failed replacement preserves old value");
    expect(state.assign("A", "two").ok(),
           "successful replacement becomes visible");
    expect(state.lookup("A").value == "two",
           "replacement points at new pool bytes");
    expect(state.assign("B", "x").ok(), "second slot installs");
    for (int i = 0; i < 20; ++i) {
        expect(state.assign("A", i % 2 == 0 ? "long" : "").ok(),
               "replacement reuses live variable capacity");
        expect(state.lookup("B").value == "x",
               "compaction preserves following variable");
    }
    const auto no_slot = state.assign("C", "x");
    expect(no_slot.status == Status::Overflow &&
               no_slot.overflow.storage_class == StorageClass::Variables &&
               no_slot.overflow.required == 3,
           "slot exhaustion is typed");
    expect(state.lookup("C").found == false,
           "failed new assignment remains invisible");
    expect(state.assign("1BAD", "x").status == Status::BadArgument,
           "invalid name is rejected");
    expect(state.ifs() == " \t\n", "IFS defaults to shell blanks");
    state.reset();
    expect(!state.lookup("A").found,
           "reset reclaims variable table without allocation");
}

void positional_transactions_and_shift() {
    mm::shell::PositionalSlot slots[4]{};
    char text[16]{};
    ShellState state{{}, {}, slots, text};
    constexpr std::array<std::string_view, 3> arguments{"one", "", "three"};
    expect(state.set_positionals("script", arguments).ok(),
           "positional setup succeeds");
    expect(state.argument_count() == 3 &&
               state.positional(0).value == "script" &&
               state.positional(1).value == "one" &&
               state.positional(2).found &&
               state.positional(2).value.empty() &&
               state.positional(3).value == "three",
           "zero and empty positional values are distinct");
    expect(state.shift().ok() && state.argument_count() == 2 &&
               state.positional(1).found &&
               state.positional(1).value.empty(),
           "shift changes the positional view");
    expect(state.shift(3).status == Status::BadArgument &&
               state.argument_count() == 2,
           "over-shift does not change state");
    constexpr std::array<std::string_view, 4> too_many{
        "a", "b", "c", "d"};
    const auto result = state.set_positionals("script", too_many);
    expect(result.status == Status::Overflow &&
               result.overflow.storage_class ==
                   StorageClass::PositionalParameters &&
               result.overflow.required == 5,
           "positional slot overflow is typed");
    expect(state.argument_count() == 2 &&
               state.positional(1).value.empty(),
           "failed positional replacement preserves old values");

    mm::shell::PositionalSlot alias_slots[3]{};
    char alias_text[32]{};
    ShellState alias_state{{}, {}, alias_slots, alias_text};
    constexpr std::array<std::string_view, 2> initial{"left", "right"};
    expect(alias_state.set_positionals("f", initial).ok(),
           "initial alias fixture installs");
    const std::array<std::string_view, 2> reversed{
        alias_state.positional(2).value,
        alias_state.positional(1).value};
    expect(alias_state.set_positionals(
               alias_state.positional(0).value, reversed).ok(),
           "replacement can read its previous positional pool");
    expect(alias_state.positional(1).value == "right" &&
               alias_state.positional(2).value == "left",
           "monotonic replacement preserves aliased input during copy");
}

void fork_and_commit_variables() {
    mm::shell::VariableSlot original_slots[2]{};
    char original_text[16]{};
    ShellState original{original_slots, original_text, {}, {}};
    expect(original.assign("A", "old").ok(),
           "original variable installs");
    mm::shell::VariableSlot shadow_slots[2]{};
    char shadow_text[16]{};
    ShellState shadow;
    expect(original.fork_variables(shadow_slots, shadow_text,
                                   shadow).ok(),
           "variable state forks into caller storage");
    expect(shadow.assign("A", "new").ok() &&
               shadow.assign("B", "x").ok(),
           "fork can stage multiple assignments");
    expect(original.lookup("A").value == "old" &&
               !original.lookup("B").found,
           "staging does not mutate original state");
    expect(original.commit_variables_from(shadow).ok(),
           "staged assignments commit together");
    expect(original.lookup("A").value == "new" &&
               original.lookup("B").value == "x",
           "commit publishes the forked values");

    mm::shell::VariableSlot short_slots[1]{};
    char short_text[1]{};
    ShellState untouched;
    untouched.last_status = 91;
    const auto failed = original.fork_variables(
        short_slots, short_text, untouched);
    expect(failed.status == Status::Overflow &&
               failed.overflow.storage_class == StorageClass::Variables &&
               untouched.last_status == 91,
           "short fork storage leaves destination unchanged");
    mm::shell::VariableSlot small_slots[1]{};
    char small_text[16]{};
    ShellState small{small_slots, small_text, {}, {}};
    expect(small.assign("A", "prior").ok(),
           "small destination starts with a value");
    const auto rejected = small.commit_variables_from(shadow);
    expect(rejected.status == Status::Overflow &&
               rejected.overflow.storage_class == StorageClass::Variables &&
               small.lookup("A").value == "prior" &&
               !small.lookup("B").found,
           "failed commit preserves destination state");
}

void positional_call_frames() {
    mm::shell::PositionalSlot slots[8]{};
    char text[64]{};
    ShellState state{{}, {}, slots, text};
    const std::string_view outer[]{"one", "two"};
    expect(state.set_positionals("script", outer).ok(),
           "caller arguments install");

    mm::shell::PositionalSlot saved[8]{};
    mm::shell::PositionalFrame frame;
    const std::string_view inner[]{"alpha"};
    expect(state.push_positionals("callee", inner, saved, frame).ok(),
           "a call installs its own arguments");
    expect(state.argument_count() == 1 &&
               state.positional(0).value == "callee" &&
               state.positional(1).value == "alpha" &&
               frame.count == 3,
           "the call sees only its own arguments");
    expect(state.shift().ok() && state.argument_count() == 0,
           "the call may shift its own arguments");

    mm::shell::PositionalSlot deeper[8]{};
    mm::shell::PositionalFrame nested;
    const std::string_view third[]{"x", "y"};
    expect(state.push_positionals("deeper", third, deeper, nested).ok(),
           "a nested call installs above the first");
    expect(state.argument_count() == 2 &&
               state.positional(2).value == "y",
           "the nested call sees its own arguments");
    expect(state.pop_positionals(deeper, nested).ok() &&
               state.argument_count() == 0 &&
               state.positional(0).value == "callee",
           "popping restores the first call, including its shift");
    expect(state.pop_positionals(saved, frame).ok() &&
               state.argument_count() == 2 &&
               state.positional(1).value == "one" &&
               state.positional(2).value == "two",
           "popping restores the caller's arguments and text");

    mm::shell::PositionalSlot narrow[1]{};
    mm::shell::PositionalFrame refused;
    const auto short_save = state.push_positionals("callee", inner, narrow,
                                                   refused);
    expect(short_save.status == Status::Overflow &&
               short_save.overflow.storage_class ==
                   StorageClass::PositionalParameters &&
               state.argument_count() == 2,
           "a short save span changes nothing");

    char tight[8]{};
    ShellState small{{}, {}, slots, tight};
    expect(small.set_positionals("s", outer).ok(),
           "tight-pool caller installs");
    mm::shell::PositionalFrame exhausted;
    const auto no_room = small.push_positionals("callee", inner, saved,
                                                exhausted);
    expect(no_room.status == Status::Overflow &&
               no_room.overflow.storage_class ==
                   StorageClass::PositionalParameterText &&
               small.argument_count() == 2 &&
               small.positional(1).value == "one",
           "an exhausted text pool leaves the caller intact");
}

const mm::test::case_ cases[]{
    {"variable transactions", &variable_transactions},
    {"positional transactions and shift",
     &positional_transactions_and_shift},
    {"fork and commit variables", &fork_and_commit_variables},
    {"positional call frames", &positional_call_frames},
};

const mm::test::registrar reg{"mm.shell state", cases};

}  // namespace
