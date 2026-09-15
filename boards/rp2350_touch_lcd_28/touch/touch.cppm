// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
export module platform.rp2350_touch_lcd_28.touch;

import mm.mcu;
import mm.touch;
import mm.touch.cst328;

namespace {

constexpr mm::touch::cst328::Wiring wiring{
    .i2c = {.instance = 1, .data_gpio = 6, .clock_gpio = 7, .baud = 400'000},
    .address = 0x1a,
    .reset_gpio = 17,
    .interrupt_gpio = 18,
    .reset_hold_ms = 10,
    .reset_release_ms = 50,
};

// The panel the controller is laminated to, in its own coordinates. It matches
// the display's geometry here, which is a fact about this board rather than a
// rule: mm.touch and mm.display describe different devices.
constexpr mm::touch::cst328::Panel panel{.width = 240, .height = 320, .points = 5};

mm::touch::cst328::Controller touch{wiring, panel};

struct Register {
    Register() { mm::touch::set_touch(touch); }
};

const Register registered;

}  // namespace
