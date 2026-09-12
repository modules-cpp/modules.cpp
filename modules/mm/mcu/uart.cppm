// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
module;

#include "mcu-cxx.h"

export module mm.mcu:uart;

import :status;

export namespace mm::mcu {

// text is a C string because the ABI beneath is C and a platform implementation
// may be written in C. A null pointer is the platform's BadArgument to report,
// not this layer's to guess at.
[[nodiscard]] inline Status uart_write(unsigned int instance, const char* text) {
    return detail::status_from(mm_mcu_uart_write(instance, text));
}

}
