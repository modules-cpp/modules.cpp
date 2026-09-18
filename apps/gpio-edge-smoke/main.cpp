// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
#include <cstddef>
#include <span>

import mm.mcu;
import mm.stdio;

namespace {

// Join these two user GPIOs with a jumper. On Linux the map must name both,
// and the sensor also needs a 10 kOhm pull-up to the board I/O voltage.
constexpr unsigned int driver_pin = 14;
constexpr unsigned int sensor_pin = 15;

using mm::mcu::Edge;
using mm::mcu::Pull;
using mm::mcu::Status;

void say(const char* message) {
    auto& console = mm::stdio::selected_console();
    const char* end = message;
    while (*end) ++end;
    auto bytes = std::as_bytes(std::span{message, static_cast<std::size_t>(end - message)});
    while (!bytes.empty()) {
        std::size_t written = 0;
        if (console.write(bytes, written) != mm::stdio::Status::Ok || written == 0) break;
        bytes = bytes.subspan(written);
    }
    (void)console.flush();
}

bool prepare(bool baseline, Edge edge) {
    return mm::mcu::gpio_write(driver_pin, baseline) == Status::Ok &&
           mm::mcu::gpio_watch(sensor_pin, Pull::Up, edge) == Status::Ok;
}

bool check_edge(bool baseline, Edge edge, bool expected) {
    if (!prepare(baseline, edge)) return false;
    if (mm::mcu::gpio_write(driver_pin, !baseline) != Status::Ok) return false;
    if (mm::mcu::delay_ms(20) != Status::Ok) return false;
    bool pending = false;
    const bool passed = mm::mcu::gpio_take(sensor_pin, pending) == Status::Ok &&
                        pending == expected &&
                        mm::mcu::gpio_unwatch(sensor_pin) == Status::Ok;
    return passed;
}

bool operator_says_yes() {
    auto& console = mm::stdio::selected_console();
    unsigned long start = 0;
    if (mm::mcu::ticks_ms(start) != Status::Ok) return false;
    char answer[8]{};
    std::size_t used = 0;
    while (used < sizeof(answer) - 1) {
        unsigned long now = 0;
        if (mm::mcu::ticks_ms(now) != Status::Ok || now - start >= 30'000)
            return false;
        std::byte byte{};
        std::size_t count = 0;
        if (console.read(std::span{&byte, 1}, count) != mm::stdio::Status::Ok)
            return false;
        if (count) {
            const char c = static_cast<char>(byte);
            if (c == '\n' || c == '\r') break;
            answer[used++] = c;
        } else if (mm::mcu::delay_ms(20) != Status::Ok) {
            return false;
        }
    }
    return (used == 3 && answer[0] == 'y' && answer[1] == 'e' &&
            answer[2] == 's') || (used == 1 && answer[0] == 'y');
}

} // namespace

int main() {
    (void)mm::stdio::selected_console().initialize();
    // Availability is decided before the driver is touched. This first watch
    // makes no assertion about delivery and is released before every baseline.
    const auto availability = mm::mcu::gpio_watch(sensor_pin, Pull::Up,
                                                  Edge::Rising);
    if (availability == Status::Unsupported) {
        say("GPIO edge facility unavailable; skip\n");
        return 0;
    }
    if (availability != Status::Ok) return 1;
    if (mm::mcu::gpio_configure(driver_pin, mm::mcu::Direction::Out,
                                Pull::None) != Status::Ok ||
        mm::mcu::gpio_unwatch(sensor_pin) != Status::Ok) return 2;

    if (!check_edge(false, Edge::Rising, true) ||
        !check_edge(true, Edge::Falling, true) ||
        !check_edge(false, Edge::Falling, false) ||
        !check_edge(true, Edge::Rising, false)) {
        say("Edge or fixture failure on GPIO14/15\n");
        return 3;
    }

    if (!prepare(false, Edge::Both)) return 4;
    if (mm::mcu::gpio_watch(sensor_pin, Pull::Up, Edge::Both) != Status::Busy ||
        mm::mcu::gpio_configure(sensor_pin, mm::mcu::Direction::In,
                                Pull::Up) != Status::Busy) return 5;
    for (unsigned int i = 0; i < 64; ++i) {
        if (mm::mcu::gpio_write(driver_pin, (i & 1u) == 0) != Status::Ok)
            return 6;
        if (mm::mcu::delay_ms(1) != Status::Ok) return 6;
    }
    bool pending = false;
    if (mm::mcu::gpio_take(sensor_pin, pending) != Status::Ok || !pending)
        return 7;
    bool cleared = false;
    for (unsigned int i = 0; i < 128; ++i) {
        if (mm::mcu::gpio_take(sensor_pin, pending) != Status::Ok) return 8;
        if (!pending) { cleared = true; break; }
    }
    if (!cleared || mm::mcu::gpio_unwatch(sensor_pin) != Status::Ok) return 8;

    if (!prepare(false, Edge::Rising)) return 9;
    if (mm::mcu::gpio_write(driver_pin, true) != Status::Ok ||
        mm::mcu::delay_ms(20) != Status::Ok ||
        mm::mcu::gpio_wait(sensor_pin, 1000, pending) != Status::Ok ||
        !pending || mm::mcu::gpio_unwatch(sensor_pin) != Status::Ok) return 10;
    if (!prepare(true, Edge::Rising)) return 11;
    if (mm::mcu::gpio_wait(sensor_pin, 1000, pending) != Status::Ok ||
        pending || mm::mcu::gpio_unwatch(sensor_pin) != Status::Ok) return 12;
    if (mm::mcu::gpio_take(sensor_pin, pending) != Status::BadArgument) return 13;

    if (!prepare(false, Edge::Rising)) return 14;
    say("Remove the GPIO14/15 jumper within ten seconds, then replace it.\n");
    if (mm::mcu::gpio_wait(sensor_pin, 10'000, pending) != Status::Ok) return 15;
    bool high = false;
    if (mm::mcu::gpio_read(sensor_pin, high) != Status::Ok) return 16;
    if (mm::mcu::gpio_unwatch(sensor_pin) != Status::Ok) return 17;
    if (!high) {
        say("Fixture failure: GPIO15 stayed low; check jumper and pull-up. "
            "Replace the jumper before retrying.\n");
        return 18;
    }
    if (pending) {
        say("GPIO edge smoke passed. Replace the jumper.\n");
        return 0;
    }
    say("Did the jumper come out before the wait ended? (yes/no)\n");
    if (operator_says_yes()) {
        say("Missed GPIO15 rising edge. Replace the jumper before retrying.\n");
        return 19;
    }
    say("Inconclusive: replace the jumper, then repeat the manual step.\n");
    return 20;
}
