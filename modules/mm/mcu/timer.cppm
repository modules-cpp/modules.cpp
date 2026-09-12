// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
module;

#include "mcu-cxx.h"

export module mm.mcu:timer;

import :status;

export namespace mm::mcu {

[[nodiscard]] inline Status delay_ms(unsigned long milliseconds) {
    return detail::status_from(mm_mcu_delay_ms(milliseconds));
}

// A monotonic millisecond counter whose zero point is the platform's business.
// ticks is written only on Ok, for the reason gpio_read gives.
[[nodiscard]] inline Status ticks_ms(unsigned long& ticks) {
    unsigned long raw = 0;
    const auto status = detail::status_from(mm_mcu_ticks_ms(&raw));
    if (status == Status::Ok) ticks = raw;
    return status;
}

}
