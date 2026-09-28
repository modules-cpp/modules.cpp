// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
export module platform.rp2350_touch_lcd_154.touch;

import mm.mcu;
import mm.touch;
import mm.touch.cst816;

namespace {

constexpr mm::touch::cst816::Wiring wiring{
    .i2c = {.instance = 1, .data_gpio = 6, .clock_gpio = 7, .baud = 400'000},
    .address = 0x15,
    .reset_gpio = 16,
    .interrupt_gpio = 15,
    .reset_hold_ms = 100,
    .reset_release_ms = 100,
};

// The panel the controller is laminated to, in its own coordinates. It matches
// the display's geometry here, which is a fact about this board rather than a
// rule: mm.touch and mm.display describe different devices. The reference
// driver reports one point, which is all this driver decodes.
constexpr mm::touch::cst816::Panel panel{.width = 240, .height = 240, .points = 1};

mm::touch::cst816::Controller touch{wiring, panel};

struct Register {
    Register() { mm::touch::set_touch(touch); }
};

const Register registered;

}  // namespace
