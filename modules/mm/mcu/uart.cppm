// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
export module mm.mcu:uart;

import :status;
import :platform;

export namespace mm::mcu {

// text is a C string because the platform beneath may be written in C. A null
// pointer is the platform's BadArgument to report, not this layer's to guess at.
[[nodiscard]] inline Status uart_write(unsigned int instance, const char* text) {
    return platform().uart_write(instance, text);
}

}
