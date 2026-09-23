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

const mm::test::case_ cases[]{
    {"variable transactions", &variable_transactions},
    {"positional transactions and shift",
     &positional_transactions_and_shift},
};

const mm::test::registrar reg{"mm.shell state", cases};

}  // namespace
