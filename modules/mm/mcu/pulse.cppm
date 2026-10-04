// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
module;

#include <cstddef>
#include <span>

export module mm.mcu:pulse;

import :status;
import :pulse_types;
import :platform;

export namespace mm::mcu {

// Claims an instance on its pad and drives the line low. Times must satisfy
// 0 < zero_high_ns < one_high_ns < bit_period_ns, or the configuration is
// BadArgument. A pad another facility holds is Busy. The same configuration
// of a claimed instance is idempotent; another is Busy until release.
[[nodiscard]] inline Status pulse_configure(const PulseConfiguration& configuration) {
    return platform().pulse_configure(configuration);
}

// Sends every byte as one unbroken frame and returns once the reset time has
// passed, so the next write starts a new frame. A frame is unbroken only while
// the caller's core keeps up with the line; a platform that buffers says so
// and one that cannot keep up reports TransportError. An unconfigured instance
// is BadArgument.
[[nodiscard]] inline Status pulse_write(unsigned int instance, std::span<const std::byte> data) {
    return platform().pulse_write(instance, data);
}

// Returns the pad. Releasing an instance that is not claimed is Ok.
[[nodiscard]] inline Status pulse_release(unsigned int instance) {
    return platform().pulse_release(instance);
}

}
