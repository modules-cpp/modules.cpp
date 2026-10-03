// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
module;

#include <cstddef>
#include <span>

export module mm.mcu:uart;

import :status;
import :uart_types;
import :platform;

export namespace mm::mcu {

// text is a C string because the platform beneath may be written in C. A null
// pointer is the platform's BadArgument to report, not this layer's to guess at.
// It needs no configure: a platform that serves it uses its default UART.
[[nodiscard]] inline Status uart_write(unsigned int instance, const char* text) {
    return platform().uart_write(instance, text);
}

// Claims an instance on its pins at a rate. A second configure of a claimed
// instance reconfigures it.
[[nodiscard]] inline Status uart_configure(const UartConfiguration& configuration) {
    return platform().uart_configure(configuration);
}

// Neither call waits. uart_write queues what the transmitter has room for and
// reports it in accepted, which may be less than data holds, including zero;
// uart_read takes what has arrived, up to data's size, and reports it in count,
// including zero. Both need a configured instance and answer BadArgument
// otherwise. On failure accepted and count are unchanged.
[[nodiscard]] inline Status uart_write(unsigned int instance, std::span<const std::byte> data,
                                       std::size_t& accepted) {
    return platform().uart_write(instance, data, accepted);
}

[[nodiscard]] inline Status uart_read(unsigned int instance, std::span<std::byte> data,
                                      std::size_t& count) {
    return platform().uart_read(instance, data, count);
}

// Returns the instance's pads and discards what it had not sent or read.
[[nodiscard]] inline Status uart_release(unsigned int instance) {
    return platform().uart_release(instance);
}

}
