// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
module;

#include "mcu-cxx.h"

export module lib.mcu;

import mm.mcu;

namespace {

// Status codes cross the C boundary as integers because the boundary is C. This
// is the only place they are integers: above it the interface is typed, and below
// it the adapter uses the same constants.
mm::mcu::Status from(int code) {
    switch (code) {
        case 0: return mm::mcu::Status::Ok;
        case 1: return mm::mcu::Status::BadArgument;
        case 2: return mm::mcu::Status::Unsupported;
        case 3: return mm::mcu::Status::Busy;
        case 4: return mm::mcu::Status::Timeout;
        default: return mm::mcu::Status::BadArgument;
    }
}

class PicoPlatform : public mm::mcu::Platform {
public:
    [[nodiscard]] mm::mcu::Status gpio_configure(unsigned int pin, mm::mcu::Direction direction,
                                                 mm::mcu::Pull pull) override {
        return from(mm_pico_mcu_gpio_configure(pin, static_cast<int>(direction),
                                               static_cast<int>(pull)));
    }

    [[nodiscard]] mm::mcu::Status gpio_write(unsigned int pin, bool high) override {
        return from(mm_pico_mcu_gpio_write(pin, high ? 1 : 0));
    }

    [[nodiscard]] mm::mcu::Status gpio_read(unsigned int pin, bool& high) override {
        int raw = 0;
        const auto status = from(mm_pico_mcu_gpio_read(pin, &raw));
        if (status == mm::mcu::Status::Ok) high = raw != 0;
        return status;
    }

    [[nodiscard]] mm::mcu::Status uart_write(unsigned int instance, const char* text) override {
        return from(mm_pico_mcu_uart_write(instance, text));
    }

    [[nodiscard]] mm::mcu::Status delay_ms(unsigned long milliseconds) override {
        return from(mm_pico_mcu_delay_ms(milliseconds));
    }

    [[nodiscard]] mm::mcu::Status ticks_ms(unsigned long& ticks) override {
        unsigned long raw = 0;
        const auto status = from(mm_pico_mcu_ticks_ms(&raw));
        if (status == mm::mcu::Status::Ok) ticks = raw;
        return status;
    }
};

PicoPlatform pico_platform;

// Registration at static initialisation. Linking this module's object is what
// makes mm.mcu answer for this platform; nothing has to reference the object,
// because objects are linked directly rather than through an archive.
struct Register {
    Register() { mm::mcu::set_platform(pico_platform); }
};

const Register registered;

}  // namespace
