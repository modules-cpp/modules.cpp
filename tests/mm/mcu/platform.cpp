// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
//
// A stand-in platform for mm.mcu, and a demonstration of the supply mechanism
// rather than a mock of it: it registers a Platform subclass exactly as a real
// platform's module does, and nothing here is scaffolding a real platform would
// not also have to provide.
import mm.mcu;

namespace {

constexpr unsigned int pin_count = 32;

class Stand : public mm::mcu::Platform {
public:
    [[nodiscard]] mm::mcu::Status gpio_configure(unsigned int pin, mm::mcu::Direction direction,
                                                 mm::mcu::Pull pull) override {
        if (forced != mm::mcu::Status::Ok) return forced;
        if (pin >= pin_count) return mm::mcu::Status::BadArgument;
        if (pull != mm::mcu::Pull::None && pull != mm::mcu::Pull::Up &&
            pull != mm::mcu::Pull::Down)
            return mm::mcu::Status::BadArgument;
        configured[pin] = true;
        output[pin] = direction == mm::mcu::Direction::Out;
        return mm::mcu::Status::Ok;
    }

    [[nodiscard]] mm::mcu::Status gpio_write(unsigned int pin, bool high) override {
        if (forced != mm::mcu::Status::Ok) return forced;
        if (pin >= pin_count) return mm::mcu::Status::BadArgument;
        if (!configured[pin]) return mm::mcu::Status::BadArgument;
        if (!output[pin]) return mm::mcu::Status::Unsupported;
        level[pin] = high;
        return mm::mcu::Status::Ok;
    }

    [[nodiscard]] mm::mcu::Status gpio_read(unsigned int pin, bool& high) override {
        if (forced != mm::mcu::Status::Ok) return forced;
        if (pin >= pin_count || !configured[pin]) return mm::mcu::Status::BadArgument;
        high = level[pin];
        return mm::mcu::Status::Ok;
    }

    [[nodiscard]] mm::mcu::Status uart_write(unsigned int instance, const char* text) override {
        if (forced != mm::mcu::Status::Ok) return forced;
        if (text == nullptr) return mm::mcu::Status::BadArgument;
        if (instance != 0) return mm::mcu::Status::Unsupported;
        uart_instance = instance;
        uart_text = text;
        return mm::mcu::Status::Ok;
    }

    [[nodiscard]] mm::mcu::Status delay_ms(unsigned long milliseconds) override {
        if (forced != mm::mcu::Status::Ok) return forced;
        ticks += milliseconds;
        return mm::mcu::Status::Ok;
    }

    [[nodiscard]] mm::mcu::Status ticks_ms(unsigned long& out) override {
        if (forced != mm::mcu::Status::Ok) return forced;
        out = ticks;
        return mm::mcu::Status::Ok;
    }

    void reset() {
        for (unsigned int pin = 0; pin < pin_count; ++pin) {
            configured[pin] = false;
            output[pin] = false;
            level[pin] = false;
        }
        forced = mm::mcu::Status::Ok;
        ticks = 0;
        uart_instance = 0;
        uart_text = nullptr;
    }

    bool configured[pin_count] = {};
    bool output[pin_count] = {};
    bool level[pin_count] = {};
    mm::mcu::Status forced = mm::mcu::Status::Ok;
    unsigned long ticks = 0;
    unsigned int uart_instance = 0;
    const char* uart_text = nullptr;
};

Stand stand;

// Registration at static initialisation, the way a platform module does it. The
// object is linked because objects are linked directly rather than through an
// archive, so nothing has to reference it for it to arrive.
struct Register {
    Register() { mm::mcu::set_platform(stand); }
};

const Register registered;

}  // namespace

// The control surface the test uses, as free functions so the Stand type itself
// stays internal. A real platform has no such surface, and mm.mcu cannot see it:
// none of it is part of the Platform interface.
void mm_test_reset() { stand.reset(); }
void mm_test_force(mm::mcu::Status status) { stand.forced = status; }
void mm_test_set_level(unsigned int pin, bool high) {
    if (pin < pin_count) stand.level[pin] = high;
}
unsigned long mm_test_ticks() { return stand.ticks; }
unsigned int mm_test_uart_instance() { return stand.uart_instance; }
bool mm_test_uart_written() { return stand.uart_text != nullptr; }
