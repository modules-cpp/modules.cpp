// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
//
// Critical sections against the platform's handlers, against the stand-in,
// which keeps a mask depth the way a mask register nests.
#include <cstdint>

import mm.mcu;
import mm.test;

void mm_test_reset();
void mm_test_force(mm::mcu::Status status);
unsigned int mm_test_interrupt_depth();

namespace {

using mm::mcu::InterruptGuard;
using mm::mcu::InterruptState;
using mm::mcu::Status;
using mm::test::expect;

void a_section_disables_and_enables() {
    mm_test_reset();
    InterruptState state;
    expect(mm::mcu::interrupts_disable(state) == Status::Ok && state.held,
           "a disable holds the state it saved");
    expect(mm_test_interrupt_depth() == 1, "and the platform masks");
    expect(mm::mcu::interrupts_enable(state) == Status::Ok && !state.held,
           "the enable restores it and lets the state go");
    expect(mm_test_interrupt_depth() == 0, "and the platform unmasks");
}

void sections_nest_in_reverse_order() {
    mm_test_reset();
    InterruptState outer;
    InterruptState inner;
    expect(mm::mcu::interrupts_disable(outer) == Status::Ok &&
               mm::mcu::interrupts_disable(inner) == Status::Ok,
           "a section inside a section takes its own state");
    expect(mm::mcu::interrupts_enable(inner) == Status::Ok && mm_test_interrupt_depth() == 1,
           "ending the inner section leaves the outer one in force");
    expect(mm::mcu::interrupts_enable(outer) == Status::Ok && mm_test_interrupt_depth() == 0,
           "ending the outer one ends both");
}

void out_of_order_ends_are_refused() {
    mm_test_reset();
    InterruptState outer;
    InterruptState inner;
    expect(mm::mcu::interrupts_disable(outer) == Status::Ok &&
               mm::mcu::interrupts_disable(inner) == Status::Ok,
           "two sections begin");
    expect(mm::mcu::interrupts_enable(outer) == Status::BadArgument && outer.held,
           "ending the outer first is refused and the state stays held");
    expect(mm::mcu::interrupts_enable(inner) == Status::Ok &&
               mm::mcu::interrupts_enable(outer) == Status::Ok,
           "in order they end");
}

void a_state_is_used_once_per_section() {
    mm_test_reset();
    InterruptState state;
    expect(mm::mcu::interrupts_enable(state) == Status::BadArgument,
           "an enable without a disable is refused");
    expect(mm_test_interrupt_depth() == 0, "and does not reach the platform");
    expect(mm::mcu::interrupts_disable(state) == Status::Ok, "a section begins");
    expect(mm::mcu::interrupts_disable(state) == Status::BadArgument,
           "a held state cannot begin a second section");
    expect(mm_test_interrupt_depth() == 1, "which does not reach the platform either");
    expect(mm::mcu::interrupts_enable(state) == Status::Ok, "the section ends");
}

void a_guard_is_a_scope() {
    mm_test_reset();
    {
        const InterruptGuard guard;
        expect(guard.status() == Status::Ok && mm_test_interrupt_depth() == 1,
               "a guard disables on construction");
        {
            const InterruptGuard nested;
            expect(nested.status() == Status::Ok && mm_test_interrupt_depth() == 2,
                   "guards nest");
        }
        expect(mm_test_interrupt_depth() == 1, "the inner guard ends first");
    }
    expect(mm_test_interrupt_depth() == 0, "and the outer one on leaving its scope");
}

void a_refused_section_is_not_ended() {
    mm_test_reset();
    mm_test_force(Status::Unsupported);
    InterruptState state;
    state.saved = 77;
    expect(mm::mcu::interrupts_disable(state) == Status::Unsupported && !state.held &&
               state.saved == 77,
           "a platform's refusal reaches the caller and leaves the state alone");
    {
        const InterruptGuard guard;
        expect(guard.status() == Status::Unsupported, "a guard reports the refusal");
    }
    mm_test_force(Status::Ok);
    expect(mm_test_interrupt_depth() == 0, "and ends nothing it did not begin");
}

void an_unserved_platform_answers_unsupported() {
    mm::mcu::Platform bare;
    std::uint32_t saved = 5;
    expect(bare.interrupts_disable(saved) == Status::Unsupported && saved == 5 &&
               bare.interrupts_enable(saved) == Status::Unsupported,
           "an unserved platform neither masks nor claims to");
}

const mm::test::case_ cases[] = {
    {"a section disables and enables", &a_section_disables_and_enables},
    {"sections nest in reverse order", &sections_nest_in_reverse_order},
    {"out of order ends are refused", &out_of_order_ends_are_refused},
    {"a state is used once per section", &a_state_is_used_once_per_section},
    {"a guard is a scope", &a_guard_is_a_scope},
    {"a refused section is not ended", &a_refused_section_is_not_ended},
    {"an unserved platform answers Unsupported", &an_unserved_platform_answers_unsupported},
};

const mm::test::registrar reg{"mm.mcu interrupt", cases};

}  // namespace
