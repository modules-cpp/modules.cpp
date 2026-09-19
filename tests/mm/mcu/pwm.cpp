// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>

import mm.mcu;
import mm.test;

void mm_test_reset();
void mm_test_force(mm::mcu::Status status);
int mm_test_pin_owner(unsigned int pin);
bool mm_test_gpio_configured(unsigned int pin);
std::uint64_t mm_test_pwm_duty(unsigned int number);
unsigned int mm_test_pwm_group_members(unsigned int group);

namespace {

using mm::mcu::Direction;
using mm::mcu::Edge;
using mm::mcu::Pull;
using mm::mcu::PwmCounter;
using mm::mcu::PwmPlan;
using mm::mcu::Status;
using mm::test::expect;

constexpr int owner_none = 0;
constexpr int owner_gpio = 1;
constexpr int owner_watched = 2;
constexpr int owner_pwm = 4;

// The legacy 490 Hz default, as a period.
constexpr std::uint64_t default_period = 2'040'816;

void description_reaches_the_caller() {
    mm_test_reset();
    const auto description = mm::mcu::pwm_description();
    expect(description.outputs.size() == 5, "the stand-in describes five outputs");
    const auto& first = description.outputs[0];
    expect(first.number == 0 && first.name == "PWM0" && first.gpio == 0 && first.group == 0 &&
               first.comparator == 0 && first.minimum_period_ns == 16 &&
               first.maximum_period_ns == 134'000'000,
           "an output carries its number, name, pad, group, comparator, and limits");
    expect(description.outputs[2].group == 0 && description.outputs[2].comparator == 0,
           "output 16 is the alias of output 0's comparator");
    expect(description.outputs[4].minimum_period_ns == 0 &&
               description.outputs[4].maximum_period_ns == 0,
           "an output with unknown limits says zero for both");
}

void lookup_by_gpio() {
    mm_test_reset();
    unsigned int output = 99;
    expect(mm::mcu::pwm_output_for_gpio(2, output) == Status::Ok && output == 2,
           "an attached pad finds its output");
    output = 99;
    expect(mm::mcu::pwm_output_for_gpio(9, output) == Status::BadArgument && output == 99,
           "a pad with no output is refused and the output is untouched");
}

void configure_period_write_release_round_trip() {
    mm_test_reset();
    std::uint64_t actual = 1;
    expect(mm::mcu::pwm_period(0, actual) == Status::BadArgument && actual == 1,
           "period before configure is BadArgument and leaves the output alone");
    expect(mm::mcu::pwm_write(0, 0) == Status::BadArgument,
           "write before configure is BadArgument");
    expect(mm::mcu::pwm_configure(0, default_period) == Status::Ok,
           "an output configures");
    expect(mm_test_pin_owner(0) == owner_pwm && mm_test_pwm_duty(0) == 0,
           "the modulator holds the pad and starts at zero duty");
    expect(mm::mcu::pwm_period(0, actual) == Status::Ok && actual == 2'040'816,
           "the actual period is what the hardware holds, here the nearest eight nanoseconds");
    expect(mm::mcu::pwm_configure(0, default_period) == Status::Ok,
           "the same output at the same period is idempotent");
    expect(mm::mcu::pwm_configure(0, 1'000'000) == Status::Busy,
           "the same output at another period is Busy until released");
    expect(mm::mcu::pwm_write(0, actual / 2) == Status::Ok && mm_test_pwm_duty(0) == actual / 2,
           "a duty within the period is written");
    expect(mm::mcu::pwm_write(0, 0) == Status::Ok && mm::mcu::pwm_write(0, actual) == Status::Ok,
           "zero and the whole period are both duties");
    expect(mm::mcu::pwm_write(0, actual + 1) == Status::BadArgument &&
               mm_test_pwm_duty(0) == actual,
           "a duty past the period is BadArgument and the duty stands");
    expect(mm::mcu::pwm_release(0) == Status::Ok && mm_test_pin_owner(0) == owner_none &&
               mm_test_pwm_group_members(0) == 0,
           "release frees the pad and stops the counter");
    expect(mm::mcu::pwm_release(0) == Status::Ok, "releasing again is idempotent");
    expect(mm::mcu::pwm_configure(0, 1'000'000) == Status::Ok, "and the output can be reclaimed");
}

void period_validation() {
    mm_test_reset();
    expect(mm::mcu::pwm_configure(0, 0) == Status::BadArgument, "a zero period is BadArgument");
    expect(mm::mcu::pwm_configure(0, 8) == Status::BadArgument &&
               mm::mcu::pwm_configure(0, 134'000'001) == Status::BadArgument,
           "a period outside nonzero limits is BadArgument");
    expect(mm_test_pin_owner(0) == owner_none, "a refused configure claims nothing");
    expect(mm::mcu::pwm_configure(3, 1) == Status::Ok,
           "an output with unknown limits leaves the period to the provider");
    expect(mm::mcu::pwm_configure(7, 1000) == Status::BadArgument &&
               mm::mcu::pwm_release(7) == Status::BadArgument,
           "an output outside the inventory is BadArgument");
}

void groups_and_aliases() {
    mm_test_reset();
    expect(mm::mcu::pwm_configure(0, default_period) == Status::Ok,
           "the first output claims");
    expect(mm::mcu::pwm_configure(1, 1'000'000) == Status::Busy,
           "a sibling at another period is Busy");
    expect(mm_test_pin_owner(1) == owner_none && mm_test_pwm_group_members(0) == 1,
           "the refused sibling claimed nothing");
    expect(mm::mcu::pwm_configure(1, default_period) == Status::Ok,
           "a sibling at the group's period joins");
    expect(mm::mcu::pwm_configure(16, default_period) == Status::Busy,
           "an alias of a claimed comparator is Busy even at the same period");
    expect(mm::mcu::pwm_write(1, 100) == Status::Ok && mm::mcu::pwm_release(0) == Status::Ok,
           "the first output releases");
    std::uint64_t actual = 0;
    expect(mm_test_pwm_duty(1) == 100 && mm::mcu::pwm_period(1, actual) == Status::Ok &&
               mm_test_pwm_group_members(0) == 1,
           "the sibling's duty and period are undisturbed");
    expect(mm::mcu::pwm_configure(16, default_period) == Status::Ok,
           "once the comparator is free its alias can claim it");
    expect(mm::mcu::pwm_configure(2, 1'000'000) == Status::Ok,
           "an output in another group has its own period");
}

void plain_gpio_yields_and_watched_gpio_does_not() {
    mm_test_reset();
    expect(mm::mcu::gpio_configure(2, Direction::Out, Pull::None) == Status::Ok,
           "the pad starts as a GPIO");
    expect(mm::mcu::pwm_configure(2, 1'000'000) == Status::Ok,
           "the modulator takes a plain GPIO over");
    expect(!mm_test_gpio_configured(2) && mm_test_pin_owner(2) == owner_pwm,
           "the digital mode is gone");
    bool high = false;
    expect(mm::mcu::gpio_write(2, true) == Status::Busy &&
               mm::mcu::gpio_read(2, high) == Status::Busy &&
               mm::mcu::gpio_configure(2, Direction::In, Pull::None) == Status::Busy &&
               mm::mcu::gpio_watch(2, Pull::None, Edge::Rising) == Status::Busy,
           "the GPIO facility is refused on a pad the modulator holds");
    expect(mm::mcu::pwm_release(2) == Status::Ok &&
               mm::mcu::gpio_configure(2, Direction::Out, Pull::None) == Status::Ok,
           "after release the pad is a GPIO again");

    mm_test_reset();
    expect(mm::mcu::gpio_watch(3, Pull::Up, Edge::Falling) == Status::Ok, "a pad is watched");
    expect(mm::mcu::pwm_configure(3, 1000) == Status::Busy && mm_test_pin_owner(3) == owner_watched,
           "a watched pad refuses the modulator and the watch stands");
}

void a_failed_takeover_leaves_the_record_true() {
    mm_test_reset();
    expect(mm::mcu::gpio_configure(2, Direction::Out, Pull::None) == Status::Ok, "a GPIO");
    mm_test_force(Status::TransportError);
    expect(mm::mcu::pwm_configure(2, 1'000'000) == Status::TransportError, "hardware refuses");
    mm_test_force(Status::Ok);
    expect(mm_test_pin_owner(2) == owner_gpio && mm_test_pwm_group_members(1) == 0,
           "the GPIO claim survives and no counter runs");
}

void every_status_reaches_the_caller() {
    mm_test_reset();
    expect(mm::mcu::pwm_configure(0, default_period) == Status::Ok,
           "configured");
    std::uint64_t actual = 5;
    for (const auto status : {Status::BadArgument, Status::Unsupported, Status::Busy,
                              Status::Timeout, Status::TransportError}) {
        mm_test_force(status);
        expect(mm::mcu::pwm_period(0, actual) == status && actual == 5,
               "a refused period carries the platform's status and changes nothing");
        expect(mm::mcu::pwm_write(0, 1) == status && mm::mcu::pwm_release(0) == status &&
                   mm::mcu::pwm_configure(1, default_period) == status,
               "write, release, and configure carry the status");
    }
    mm_test_force(Status::Ok);
}

// The RP2040 and RP2350 counter: sixteen bits, an eight-and-four divider.
constexpr PwmCounter rp2040{125'000'000, 16, 8, 4};
constexpr PwmCounter rp2350{150'000'000, 16, 8, 4};

struct Expected {
    const PwmCounter& counter;
    std::uint64_t period_ns;
    unsigned int top;
    unsigned int divider;
    std::uint64_t actual_ns;
};

// Computed independently with exact rational arithmetic: counts at a divider
// are the period in ticks times sixteen over the divider, rounded half up;
// the smallest divider whose counts are at most 65535 wins.
constexpr Expected table[] = {
    {rp2040, 2'040'816, 64787, 63, 2'040'822},      // the legacy 490 Hz default
    {rp2040, 1'020'408, 63775, 32, 1'020'416},      // 980 Hz
    {rp2040, 32'258'065, 65498, 985, 32'258'258},   // tone's 31 Hz
    {rp2040, 15'259, 1906, 16, 15'256},             // tone's 65,535 Hz
    {rp2040, 1'000'000, 64515, 31, 999'998},        // 1 kHz
    {rp2040, 16, 1, 16, 16},                        // two ticks, the shortest
    {rp2040, 134'000'000, 65525, 4090, 134'000'670},  // near the longest
    // The reservation interval at divider one: 65,535 counts fit, 65,536 do not.
    {rp2040, 524'280, 65534, 16, 524'280},          // 65,535.000 counts
    {rp2040, 524'281, 65534, 16, 524'280},          // 65,535.125 rounds down
    {rp2040, 524'284, 61679, 17, 524'280},          // 65,535.5 rounds up: next step
    {rp2040, 524'286, 61680, 17, 524'289},          // 65,535.75
    {rp2040, 524'288, 61680, 17, 524'289},          // 65,536 exactly
    {rp2350, 2'040'816, 65305, 75, 2'040'813},
    {rp2350, 1'020'408, 64446, 38, 1'020'411},
    {rp2350, 32'258'065, 65498, 1182, 32'258'258},
    {rp2350, 15'259, 2288, 16, 15'260},
    {rp2350, 1'000'000, 64864, 37, 1'000'002},
    {rp2350, 524'288, 62914, 20, 524'292},
    {rp2350, 16, 1, 16, 13},
};

void plan_table() {
    for (const auto& row : table) {
        PwmPlan plan;
        const auto status = mm::mcu::pwm_plan(row.counter, row.period_ns, plan);
        expect(status == Status::Ok, "a period in range plans");
        expect(plan.top == row.top && plan.divider == row.divider &&
                   plan.actual_period_ns == row.actual_ns,
               "the plan is the one the rational model gives");
        expect(plan.top <= 65534, "top leaves room for top + 1 in the compare register");
    }
}

void plan_refusals() {
    PwmPlan plan{7, 7, 7};
    const PwmPlan untouched{7, 7, 7};
    const auto unchanged = [&] {
        return plan.top == untouched.top && plan.divider == untouched.divider &&
               plan.actual_period_ns == untouched.actual_period_ns;
    };
    expect(mm::mcu::pwm_plan(rp2040, 0, plan) == Status::BadArgument && unchanged(),
           "a zero period is refused");
    expect(mm::mcu::pwm_plan(rp2040, 8, plan) == Status::BadArgument && unchanged(),
           "a period under two ticks is refused");
    expect(mm::mcu::pwm_plan(rp2040, 134'217'728, plan) == Status::BadArgument && unchanged(),
           "a period beyond the counter's reach is refused");
    expect(mm::mcu::pwm_plan(rp2350, 134'000'000, plan) == Status::BadArgument && unchanged(),
           "the faster clock reaches less far");
    expect(mm::mcu::pwm_plan({0, 16, 8, 4}, 1000, plan) == Status::BadArgument && unchanged(),
           "a zero clock is refused");
    expect(mm::mcu::pwm_plan({125'000'000, 1, 8, 4}, 1000, plan) == Status::BadArgument &&
               mm::mcu::pwm_plan({125'000'000, 33, 8, 4}, 1000, plan) == Status::BadArgument &&
               mm::mcu::pwm_plan({125'000'000, 16, 0, 4}, 1000, plan) == Status::BadArgument &&
               mm::mcu::pwm_plan({125'000'000, 16, 16, 9}, 1000, plan) == Status::BadArgument &&
               unchanged(),
           "bit widths outside the arithmetic's bounds are refused");
    expect(mm::mcu::pwm_plan({5'000'000'000, 16, 8, 4}, 1000, plan) == Status::BadArgument &&
               unchanged(),
           "a clock beyond 2^32 is refused");
    expect(mm::mcu::pwm_plan(rp2040, 0xffff'ffff'ffff'ffffull, plan) == Status::BadArgument &&
               unchanged(),
           "a period whose scaling overflows is refused");
}

void unserved_platform_defaults() {
    mm::mcu::Platform bare;
    expect(bare.pwm_description().outputs.empty(), "an unserved inventory is empty");
    std::uint64_t actual = 3;
    expect(bare.pwm_configure(0, 1000) == Status::Unsupported &&
               bare.pwm_period(0, actual) == Status::Unsupported && actual == 3 &&
               bare.pwm_write(0, 1) == Status::Unsupported &&
               bare.pwm_release(0) == Status::Unsupported,
           "every PWM call defaults to Unsupported without changing output");
}

const mm::test::case_ cases[] = {
    {"description reaches the caller", &description_reaches_the_caller},
    {"lookup by gpio", &lookup_by_gpio},
    {"configure period write release round trip", &configure_period_write_release_round_trip},
    {"period validation", &period_validation},
    {"groups and aliases", &groups_and_aliases},
    {"plain gpio yields and watched does not", &plain_gpio_yields_and_watched_gpio_does_not},
    {"a failed takeover leaves the record true", &a_failed_takeover_leaves_the_record_true},
    {"every status reaches the caller", &every_status_reaches_the_caller},
    {"plan table", &plan_table},
    {"plan refusals", &plan_refusals},
    {"unserved platform defaults", &unserved_platform_defaults},
};

const mm::test::registrar reg{"mm.mcu pwm", cases};

}  // namespace
