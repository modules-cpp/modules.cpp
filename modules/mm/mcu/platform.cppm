// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
module;

#include <cstddef>
#include <cstdint>
#include <span>

export module mm.mcu:platform;

import :status;
import :board;
import :spi_types;
import :i2c_types;
import :adc_types;
import :pwm_types;

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

    [[nodiscard]] virtual Board board() const { return {}; }

    [[nodiscard]] virtual Status gpio_configure(unsigned int, Direction, Pull) {
        return Status::Unsupported;
    }
    [[nodiscard]] virtual Status gpio_write(unsigned int, bool) { return Status::Unsupported; }
    [[nodiscard]] virtual Status gpio_read(unsigned int, bool&) { return Status::Unsupported; }
    [[nodiscard]] virtual Status gpio_watch(unsigned int, Pull, Edge) {
        return Status::Unsupported;
    }
    [[nodiscard]] virtual Status gpio_take(unsigned int, bool&) { return Status::Unsupported; }
    [[nodiscard]] virtual Status gpio_unwatch(unsigned int) { return Status::Unsupported; }
    [[nodiscard]] virtual Status gpio_wait(unsigned int, unsigned long, bool&) {
        return Status::Unsupported;
    }

    [[nodiscard]] virtual Status spi_configure(const SpiConfiguration&) {
        return Status::Unsupported;
    }
    [[nodiscard]] virtual Status spi_write(unsigned int, std::span<const std::byte>) {
        return Status::Unsupported;
    }
    [[nodiscard]] virtual Status spi_transfer(unsigned int, std::span<const std::byte>,
                                              std::span<std::byte>) {
        return Status::Unsupported;
    }

    [[nodiscard]] virtual Status i2c_configure(const I2cConfiguration&) {
        return Status::Unsupported;
    }
    [[nodiscard]] virtual Status i2c_write(unsigned int, unsigned int,
                                           std::span<const std::byte>) {
        return Status::Unsupported;
    }
    [[nodiscard]] virtual Status i2c_read(unsigned int, unsigned int, std::span<std::byte>) {
        return Status::Unsupported;
    }
    [[nodiscard]] virtual Status i2c_write_read(unsigned int, unsigned int,
                                                std::span<const std::byte>,
                                                std::span<std::byte>) {
        return Status::Unsupported;
    }

    [[nodiscard]] virtual Status uart_write(unsigned int, const char*) {
        return Status::Unsupported;
    }

    [[nodiscard]] virtual Status delay_ms(unsigned long) { return Status::Unsupported; }
    [[nodiscard]] virtual Status ticks_ms(unsigned long&) { return Status::Unsupported; }
    [[nodiscard]] virtual Status delay_us(unsigned long) { return Status::Unsupported; }
    [[nodiscard]] virtual Status ticks_us(unsigned long&) { return Status::Unsupported; }

    // The analog inventories are answered here rather than carried on Board
    // because their facts are per channel and per output -- a reference, a
    // period range -- where Board carries what is one fact for the whole
    // board. An unserved platform has empty inventories.
    [[nodiscard]] virtual AdcDescription adc_description() const { return {}; }
    [[nodiscard]] virtual Status adc_configure(unsigned int) { return Status::Unsupported; }
    [[nodiscard]] virtual Status adc_read(unsigned int, unsigned int&) {
        return Status::Unsupported;
    }
    [[nodiscard]] virtual Status adc_release(unsigned int) { return Status::Unsupported; }

    [[nodiscard]] virtual PwmDescription pwm_description() const { return {}; }
    [[nodiscard]] virtual Status pwm_configure(unsigned int, std::uint64_t) {
        return Status::Unsupported;
    }
    [[nodiscard]] virtual Status pwm_period(unsigned int, std::uint64_t&) {
        return Status::Unsupported;
    }
    [[nodiscard]] virtual Status pwm_write(unsigned int, std::uint64_t) {
        return Status::Unsupported;
    }
    [[nodiscard]] virtual Status pwm_release(unsigned int) { return Status::Unsupported; }
};

// Registered by the platform's module from a static initialiser, which runs
// because objects are linked directly and in order rather than through an
// archive. A lane whose closure reaches no platform module gets the default
// below, and every call answers Unsupported instead of dereferencing nothing.
void set_platform(Platform& platform);

[[nodiscard]] Platform& platform();

}
