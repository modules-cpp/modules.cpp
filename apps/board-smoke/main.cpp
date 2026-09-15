// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
//
// Bring up every interface the selected platform binds, then report. Each step
// returns its own exit code so a failure names the interface that produced it.
#include <array>
#include <cstddef>

import mm.display;
import mm.touch;
import mm.imu;
import mm.rtc;

int main() {
    auto& display = mm::display::selected_display();
    if (display.initialize() != mm::display::Status::Ok) return 2;
    if (display.clear(mm::display::Color::Black) != mm::display::Status::Ok) return 3;

    auto& touch = mm::touch::selected_touch();
    if (touch.initialize() != mm::touch::Status::Ok) return 4;

    auto& sensor = mm::imu::selected_imu();
    if (sensor.initialize() != mm::imu::Status::Ok) return 5;

    auto& clock = mm::rtc::selected_clock();
    if (clock.initialize() != mm::rtc::Status::Ok) return 6;

    // One reading from each, so the whole chain from interface to bus is
    // exercised rather than only the initialisation half.
    std::array<mm::touch::Point, 5> points{};
    std::size_t contacts = 0;
    if (touch.read(points, contacts) != mm::touch::Status::Ok) return 7;

    mm::imu::Axes acceleration;
    mm::imu::Axes rotation;
    if (sensor.read(acceleration, rotation) != mm::imu::Status::Ok) return 8;

    mm::rtc::DateTime now;
    bool trusted = false;
    if (clock.read(now, trusted) != mm::rtc::Status::Ok) return 9;

    // A white frame is the visible sign that every step above succeeded.
    if (display.clear(mm::display::Color::White) != mm::display::Status::Ok) return 10;
    if (display.refresh(mm::display::Refresh::Full) != mm::display::Status::Ok) return 11;
    return 0;
}
