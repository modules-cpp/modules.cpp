// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
export module platform.rp2350_touch_lcd_28.rtc;

import mm.mcu;
import mm.rtc;
import mm.rtc.pcf85063;

namespace {

constexpr mm::rtc::pcf85063::Wiring wiring{
    .i2c = {.instance = 1, .data_gpio = 6, .clock_gpio = 7, .baud = 400'000},
    .address = 0x51,
};

mm::rtc::pcf85063::Clock clock{wiring};

struct Register {
    Register() { mm::rtc::set_clock(clock); }
};

const Register registered;

}  // namespace
