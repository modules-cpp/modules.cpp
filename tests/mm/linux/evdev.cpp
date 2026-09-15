// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
#include <array>
#include <linux/input.h>
#include <span>

import mm.test;
import mm.touch;
import platform.linux.touch;

namespace {

using mm::test::expect;
using platform::linux::evdev_detail::Contact;
using platform::linux::evdev_detail::Kind;

void type_b_decoder() {
    std::array<Contact, 2> contacts{};
    int slot = 0;
    const input_event events[]{
        {{}, EV_ABS, ABS_MT_SLOT, 0},
        {{}, EV_ABS, ABS_MT_TRACKING_ID, 100},
        {{}, EV_ABS, ABS_MT_POSITION_X, 100},
        {{}, EV_ABS, ABS_MT_POSITION_Y, 200},
        {{}, EV_ABS, ABS_MT_SLOT, 1},
        {{}, EV_ABS, ABS_MT_TRACKING_ID, 101},
        {{}, EV_ABS, ABS_MT_POSITION_X, 300},
        {{}, EV_ABS, ABS_MT_POSITION_Y, 400},
    };
    for (const auto& event : events)
        platform::linux::evdev_detail::consume(
            Kind::TypeB, slot, contacts, event, 800, 600);
    expect(contacts[0].active && contacts[0].x == 100 &&
               contacts[0].y == 200 && contacts[1].active &&
               contacts[1].x == 300 && contacts[1].y == 400,
           "production Type-B decoder tracks slots and positions");

    const input_event release{{}, EV_ABS, ABS_MT_TRACKING_ID, -1};
    platform::linux::evdev_detail::consume(
        Kind::TypeB, slot, contacts, release, 800, 600);
    expect(!contacts[1].active, "production Type-B decoder releases a slot");
}

void single_and_relative_decoders() {
    std::array<Contact, 1> contact{};
    int slot = 0;
    const input_event single[]{
        {{}, EV_ABS, ABS_X, 500},
        {{}, EV_ABS, ABS_Y, 250},
        {{}, EV_KEY, BTN_TOUCH, 1},
    };
    for (const auto& event : single)
        platform::linux::evdev_detail::consume(
            Kind::Single, slot, contact, event, 800, 600);
    expect(contact[0].active && contact[0].x == 500 &&
               contact[0].y == 250,
           "production single-touch decoder publishes its contact");

    contact[0] = {100, 100, false};
    const input_event relative[]{
        {{}, EV_REL, REL_X, 25},
        {{}, EV_REL, REL_Y, -10},
        {{}, EV_KEY, BTN_LEFT, 1},
    };
    for (const auto& event : relative)
        platform::linux::evdev_detail::consume(
            Kind::Relative, slot, contact, event, 800, 600);
    expect(contact[0].active && contact[0].x == 125 &&
               contact[0].y == 90,
           "production relative decoder accumulates bounded deltas");
}

void coordinate_normalization() {
    expect(platform::linux::evdev_detail::coordinate(500, 0, 1000, 101) == 50,
           "production coordinate conversion normalizes the midpoint");
    expect(platform::linux::evdev_detail::coordinate(-1, 0, 1000, 101) == 0 &&
               platform::linux::evdev_detail::coordinate(
                   1001, 0, 1000, 101) == 100,
           "production coordinate conversion clips both ends");
}

void type_b_resynchronization() {
    std::array<Contact, 2> contacts{{{1, 2, true}, {3, 4, false}}};
    const std::array tracking{41, -1};
    const std::array x{120, 340};
    const std::array y{220, 440};
    int slot = 0;
    expect(platform::linux::evdev_detail::apply_type_b_snapshot(
               contacts, tracking, x, y, 1, slot),
           "complete Type-B kernel state resynchronizes");
    expect(contacts[0].active && contacts[0].x == 120 &&
               contacts[0].y == 220 && !contacts[1].active &&
               contacts[1].x == 340 && contacts[1].y == 440 && slot == 1,
           "resynchronization replaces every slot and current slot");
    expect(!platform::linux::evdev_detail::apply_type_b_snapshot(
               contacts, std::span{tracking}.first(1), x, y, 0, slot),
           "incomplete Type-B kernel state is rejected");
}

void frames_resynchronization_and_clipping() {
    using platform::linux::evdev_detail::SyncAction;
    bool dropped = false;
    const input_event incomplete{{}, EV_ABS, ABS_X, 12};
    const input_event lost{{}, EV_SYN, SYN_DROPPED, 0};
    const input_event ignored{{}, EV_ABS, ABS_Y, 34};
    const input_event report{{}, EV_SYN, SYN_REPORT, 0};
    expect(platform::linux::evdev_detail::synchronization(
               dropped, incomplete) == SyncAction::Consume,
           "a truncated frame is not published");
    expect(platform::linux::evdev_detail::synchronization(
               dropped, report) == SyncAction::Publish,
           "SYN_REPORT completes an intact frame");
    expect(platform::linux::evdev_detail::synchronization(
               dropped, lost) == SyncAction::Discard && dropped &&
               platform::linux::evdev_detail::synchronization(
                   dropped, ignored) == SyncAction::Discard &&
               platform::linux::evdev_detail::synchronization(
                   dropped, report) == SyncAction::Resync && !dropped,
           "SYN_DROPPED discards through the next report");

    std::array<Contact, 2> contacts{{{10, 20, true}, {30, 40, true}}};
    std::array<mm::touch::Point, 1> output{};
    expect(platform::linux::evdev_detail::publish_contacts(
               contacts, output, 0, 100, 0, 100, 101, 101,
               false, false, false) == 1 && output[0].x == 10 &&
               output[0].y == 20,
           "publication clips active contacts to the caller span");

    std::array<Contact, 1> contact{{{55, 66, true}}};
    expect(platform::linux::evdev_detail::apply_relative_snapshot(
               contact, false) && contact[0].x == 55 &&
               contact[0].y == 66 && !contact[0].active,
           "relative resynchronization retains unrecoverable coordinates");
    expect(platform::linux::evdev_detail::apply_single_snapshot(
               contact, 70, 80, true) && contact[0].active &&
               contact[0].x == 70 && contact[0].y == 80,
           "single-touch resynchronization replaces kernel state");
}

const mm::test::case_ cases[]{
    {"Type B decoder", &type_b_decoder},
    {"single and relative decoders", &single_and_relative_decoders},
    {"coordinate normalization", &coordinate_normalization},
    {"Type B resynchronization", &type_b_resynchronization},
    {"frames, resynchronization and clipping",
     &frames_resynchronization_and_clipping},
};
const mm::test::registrar reg{"platform.linux.evdev", cases};

}
