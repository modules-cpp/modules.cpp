// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
export module mm.mcu:timer;

import :status;
import :platform;

export namespace mm::mcu {

[[nodiscard]] inline Status delay_ms(unsigned long milliseconds) {
    return platform().delay_ms(milliseconds);
}

// A monotonic millisecond counter whose zero point is the platform's business.
[[nodiscard]] inline Status ticks_ms(unsigned long& ticks) {
    return platform().ticks_ms(ticks);
}

}
