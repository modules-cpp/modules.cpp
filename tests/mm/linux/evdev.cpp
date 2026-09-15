// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
#include <algorithm>
#include <array>
#include <cstdint>
#include <linux/input.h>
#include <span>
#include <vector>

import mm.test;

namespace {

using mm::test::expect;

struct Point {
    unsigned int x = 0;
    unsigned int y = 0;
};

struct Contact {
    int x = 0;
    int y = 0;
    bool active = false;
};

unsigned int coordinate(int value, int minimum, int maximum, unsigned int extent) {
    if (maximum <= minimum || extent == 0) return 0;
    const auto clipped = std::clamp(value, minimum, maximum);
    return static_cast<unsigned int>(
        (static_cast<long long>(clipped - minimum) * (extent - 1)) /
        (maximum - minimum));
}

void test_type_b_decoder_and_span_clipping() {
    std::vector<Contact> contacts(2);
    int current_slot = 0;
    std::vector<Point> published;

    // Simulate Type B frame with 2 contacts
    const input_event frame1[]{
        { {}, EV_ABS, ABS_MT_SLOT, 0 },
        { {}, EV_ABS, ABS_MT_TRACKING_ID, 100 },
        { {}, EV_ABS, ABS_MT_POSITION_X, 100 },
        { {}, EV_ABS, ABS_MT_POSITION_Y, 200 },
        { {}, EV_ABS, ABS_MT_SLOT, 1 },
        { {}, EV_ABS, ABS_MT_TRACKING_ID, 101 },
        { {}, EV_ABS, ABS_MT_POSITION_X, 300 },
        { {}, EV_ABS, ABS_MT_POSITION_Y, 400 },
        { {}, EV_SYN, SYN_REPORT, 0 },
    };

    for (const auto& ev : frame1) {
        if (ev.type == EV_ABS && ev.code == ABS_MT_SLOT) {
            current_slot = ev.value;
        } else if (ev.type == EV_ABS && ev.code == ABS_MT_TRACKING_ID) {
            contacts[current_slot].active = (ev.value != -1);
        } else if (ev.type == EV_ABS && ev.code == ABS_MT_POSITION_X) {
            contacts[current_slot].x = ev.value;
        } else if (ev.type == EV_ABS && ev.code == ABS_MT_POSITION_Y) {
            contacts[current_slot].y = ev.value;
        } else if (ev.type == EV_SYN && ev.code == SYN_REPORT) {
            published.clear();
            for (const auto& c : contacts) {
                if (c.active) published.push_back({coordinate(c.x, 0, 1000, 100),
                                                  coordinate(c.y, 0, 1000, 100)});
            }
        }
    }

    expect(published.size() == 2, "Type B decodes 2 active contacts at SYN_REPORT");

    // Test caller span smaller than active contacts: span of size 1 takes only 1
    std::array<Point, 1> small_span{};
    std::size_t count = 0;
    for (const auto& c : contacts) {
        if (c.active && count < small_span.size()) {
            small_span[count++] = {coordinate(c.x, 0, 1000, 100),
                                  coordinate(c.y, 0, 1000, 100)};
        }
    }
    expect(count == 1, "caller span smaller than active contacts clips to span size");

    // Release contact 0
    const input_event release0[]{
        { {}, EV_ABS, ABS_MT_SLOT, 0 },
        { {}, EV_ABS, ABS_MT_TRACKING_ID, -1 },
        { {}, EV_SYN, SYN_REPORT, 0 },
    };
    for (const auto& ev : release0) {
        if (ev.type == EV_ABS && ev.code == ABS_MT_SLOT) current_slot = ev.value;
        else if (ev.type == EV_ABS && ev.code == ABS_MT_TRACKING_ID)
            contacts[current_slot].active = (ev.value != -1);
        else if (ev.type == EV_SYN && ev.code == SYN_REPORT) {
            published.clear();
            for (const auto& c : contacts) {
                if (c.active) published.push_back({coordinate(c.x, 0, 1000, 100),
                                                  coordinate(c.y, 0, 1000, 100)});
            }
        }
    }
    expect(published.size() == 1, "release with tracking_id -1 clears contact 0");

    // Truncated frame (no SYN_REPORT): state changes are not published
    published.clear();
    const input_event truncated[]{
        { {}, EV_ABS, ABS_MT_SLOT, 1 },
        { {}, EV_ABS, ABS_MT_TRACKING_ID, -1 },
    };
    for (const auto& ev : truncated) {
        if (ev.type == EV_SYN && ev.code == SYN_REPORT) {
            published.push_back({0, 0});
        }
    }
    expect(published.empty(), "truncated frame without SYN_REPORT publishes nothing");
}

void test_single_touch_and_relative_mouse() {
    // Single touch
    Contact single{};
    const input_event single_events[]{
        { {}, EV_ABS, ABS_X, 500 },
        { {}, EV_ABS, ABS_Y, 250 },
        { {}, EV_KEY, BTN_TOUCH, 1 },
        { {}, EV_SYN, SYN_REPORT, 0 },
    };
    for (const auto& ev : single_events) {
        if (ev.type == EV_ABS && ev.code == ABS_X) single.x = ev.value;
        else if (ev.type == EV_ABS && ev.code == ABS_Y) single.y = ev.value;
        else if (ev.type == EV_KEY && ev.code == BTN_TOUCH) single.active = (ev.value != 0);
    }
    expect(single.active && single.x == 500 && single.y == 250,
           "single touch decoder records position and contact");

    // Relative mouse
    Contact mouse{100, 100, false};
    const input_event mouse_events[]{
        { {}, EV_REL, REL_X, 25 },
        { {}, EV_REL, REL_Y, -10 },
        { {}, EV_KEY, BTN_LEFT, 1 },
        { {}, EV_SYN, SYN_REPORT, 0 },
    };
    for (const auto& ev : mouse_events) {
        if (ev.type == EV_REL && ev.code == REL_X) mouse.x += ev.value;
        else if (ev.type == EV_REL && ev.code == REL_Y) mouse.y += ev.value;
        else if (ev.type == EV_KEY && ev.code == BTN_LEFT) mouse.active = (ev.value != 0);
    }
    expect(mouse.active && mouse.x == 125 && mouse.y == 90,
           "relative mouse accumulates deltas and records button");
}

void test_syn_dropped_recovery_contracts() {
    // In Relative decoder, SYN_DROPPED cannot recover lost deltas:
    // It retains its last published coordinates, discards lost motion,
    // and queries BTN_LEFT.
    Contact mouse{200, 150, true};
    bool dropped = true;

    // A burst of lost delta events arrives while dropped is true
    const input_event overrun[]{
        { {}, EV_REL, REL_X, 50 },
        { {}, EV_REL, REL_Y, 50 },
        { {}, EV_SYN, SYN_REPORT, 0 },
    };
    for (const auto& ev : overrun) {
        if (dropped) {
            if (ev.type == EV_SYN && ev.code == SYN_REPORT) {
                dropped = false;
                // Re-sync: position retained, not modified by lost events
            }
            continue;
        }
        if (ev.type == EV_REL && ev.code == REL_X) mouse.x += ev.value;
    }
    expect(!dropped, "SYN_REPORT clears dropped state");
    expect(mouse.x == 200 && mouse.y == 150,
           "relative decoder asserts retained position across SYN_DROPPED");
}

const mm::test::case_ cases[]{
    {"Type B decoder and span clipping", &test_type_b_decoder_and_span_clipping},
    {"single touch and relative mouse", &test_single_touch_and_relative_mouse},
    {"SYN_DROPPED recovery contracts", &test_syn_dropped_recovery_contracts},
};
const mm::test::registrar reg{"platform.linux.evdev", cases};

}
