// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
export module mm.mcu:platform;

import :status;

export namespace mm::mcu {

// The seam. A name declared in a module's purview is attached to that module and
// cannot be defined by a translation unit outside it, so a platform cannot supply
// this module's functions directly. It supplies an object instead: one level of
// virtual dispatch, resolved when the platform's own module is linked in.
//
// Every method answers Unsupported by default, so a platform implements the
// facilities it has and inherits an honest answer for the rest.
class Platform {
public:
    virtual ~Platform() = default;

    [[nodiscard]] virtual Status gpio_configure(unsigned int, Direction, Pull) {
        return Status::Unsupported;
    }
    [[nodiscard]] virtual Status gpio_write(unsigned int, bool) { return Status::Unsupported; }
    [[nodiscard]] virtual Status gpio_read(unsigned int, bool&) { return Status::Unsupported; }

    [[nodiscard]] virtual Status uart_write(unsigned int, const char*) {
        return Status::Unsupported;
    }

    [[nodiscard]] virtual Status delay_ms(unsigned long) { return Status::Unsupported; }
    [[nodiscard]] virtual Status ticks_ms(unsigned long&) { return Status::Unsupported; }
};

// Registered by the platform's module from a static initialiser, which runs
// because objects are linked directly and in order rather than through an
// archive. A lane whose closure reaches no platform module gets the default
// below, and every call answers Unsupported instead of dereferencing nothing.
void set_platform(Platform& platform);

[[nodiscard]] Platform& platform();

}
