// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
#include <cstddef>
#include <string_view>

import mm.shell;
import mm.test;

namespace {

void capability_set_operations() {
    mm::shell::CapabilitySet set;
    mm::test::expect(set.empty(), "expected initial set to be empty");

    set.set(mm::shell::Capability::Variables);
    mm::test::expect(!set.empty(), "expected set to be non-empty");
    mm::test::expect(set.has(mm::shell::Capability::Variables),
                     "expected Variables to be present");
    mm::test::expect(!set.has(mm::shell::Capability::Functions),
                     "expected Functions to be absent");

    set.set(mm::shell::Capability::Functions);
    mm::test::expect(set.has(mm::shell::Capability::Functions),
                     "expected Functions to be present");

    set.clear(mm::shell::Capability::Variables);
    mm::test::expect(!set.has(mm::shell::Capability::Variables),
                     "expected Variables to be cleared");
    mm::test::expect(set.has(mm::shell::Capability::Functions),
                     "expected Functions to remain");
}

void capability_contains_and_merge() {
    mm::shell::CapabilitySet a;
    a.set(mm::shell::Capability::Scripts);
    a.set(mm::shell::Capability::CustomCommands);

    mm::shell::CapabilitySet b;
    b.set(mm::shell::Capability::Scripts);

    mm::test::expect(a.contains(b), "expected a to contain b");
    mm::test::expect(!b.contains(a), "expected b to not contain a");

    b.merge(a);
    mm::test::expect(b.contains(a), "expected b to contain a after merge");
    mm::test::expect(b == a, "expected b to equal a");
}

void capability_standard_levels() {
    mm::test::expect(
        static_cast<int>(mm::shell::Level::BareMetal) == 1,
        "bare-metal level value");
    mm::test::expect(static_cast<int>(mm::shell::Level::Mcu) == 2,
                     "mcu level value");
    mm::test::expect(static_cast<int>(mm::shell::Level::Posix) == 3,
                     "posix level value");

    const auto l1 = mm::shell::CapabilitySet::level1();
    const auto l2 = mm::shell::CapabilitySet::level2();
    const auto l3 = mm::shell::CapabilitySet::level3();

    mm::test::expect(l2.contains(l1), "expected level 2 to contain level 1");
    mm::test::expect(l3.contains(l2), "expected level 3 to contain level 2");
    mm::test::expect(!l1.contains(l3),
                     "expected level 1 not to contain level 3");

    mm::test::expect(l1.has(mm::shell::Capability::Scripts), "l1 scripts");
    mm::test::expect(l1.has(mm::shell::Capability::CustomCommands),
                     "l1 custom");
    mm::test::expect(!l1.has(mm::shell::Capability::Gpio), "l1 no gpio");

    // Level 2 contains mandatory software capabilities; hardware is optional
    mm::test::expect(!l2.has(mm::shell::Capability::Gpio),
                     "l2 does not claim unprovided gpio");
    mm::test::expect(!l2.has(mm::shell::Capability::Adc),
                     "l2 does not claim unprovided adc");
    mm::test::expect(!l2.has(mm::shell::Capability::Files), "l2 no files");

    // Level 3 contains host features and does not falsely claim MCU hardware
    mm::test::expect(!l3.has(mm::shell::Capability::Gpio),
                     "l3 does not claim unprovided gpio");
    mm::test::expect(l3.has(mm::shell::Capability::Files), "l3 files");
    mm::test::expect(l3.has(mm::shell::Capability::Processes), "l3 processes");

    // Optional hardware capabilities are merged explicitly
    auto board_caps = l2;
    board_caps.set(mm::shell::Capability::Gpio);
    board_caps.set(mm::shell::Capability::Adc);
    mm::test::expect(board_caps.has(mm::shell::Capability::Gpio),
                     "board caps has gpio");
    mm::test::expect(board_caps.has(mm::shell::Capability::Adc),
                     "board caps has adc");
    mm::test::expect(!board_caps.has(mm::shell::Capability::Pwm),
                     "board caps lacks unprovided pwm");
}

void capability_names_and_lookup() {
    for (std::size_t i = 0; i < mm::shell::capability_count; ++i) {
        const auto capability = static_cast<mm::shell::Capability>(i);
        const auto name = mm::shell::name_of(capability);
        const auto looked = mm::shell::lookup_capability(name);
        mm::test::expect(name != "unknown", "capability has canonical name");
        mm::test::expect(looked.has_value(), "canonical name looks up");
        if (looked.has_value()) {
            mm::test::expect(*looked == capability,
                             "capability name round trips");
        }
    }

    mm::test::expect(!mm::shell::lookup_capability("nonexistent").has_value(),
                     "lookup invalid absent");
    mm::test::expect(
        mm::shell::name_of(static_cast<mm::shell::Capability>(99)) == "unknown",
        "invalid capability has unknown name");
}

const mm::test::case_ cases[] = {
    { "capability set operations", &capability_set_operations },
    { "capability contains and merge", &capability_contains_and_merge },
    { "capability standard levels", &capability_standard_levels },
    { "capability names and lookup", &capability_names_and_lookup },
};

const mm::test::registrar reg{"mm.shell capability", cases};

}  // namespace
