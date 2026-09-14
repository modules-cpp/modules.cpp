// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
module;

#include "mcu-cxx.h"
#include <cstddef>
#include <optional>
#include <span>
#include <string_view>

export module platform.pico.mcu;

import mm.mcu;

namespace {

constexpr mm::mcu::Gpio gpios[] = {
    {0, "GP0"},   {1, "GP1"},   {2, "GP2"},   {3, "GP3"},   {4, "GP4"},
    {5, "GP5"},   {6, "GP6"},   {7, "GP7"},   {8, "GP8"},   {9, "GP9"},
    {10, "GP10"}, {11, "GP11"}, {12, "GP12"}, {13, "GP13"}, {14, "GP14"},
    {15, "GP15"}, {16, "GP16"}, {17, "GP17"}, {18, "GP18"}, {19, "GP19"},
    {20, "GP20"}, {21, "GP21"}, {22, "GP22"}, {23, "GP23"}, {24, "GP24"},
    {25, "GP25"}, {26, "GP26"}, {27, "GP27"}, {28, "GP28"}, {29, "GP29"},
    {30, "GP30"}, {31, "GP31"}, {32, "GP32"}, {33, "GP33"}, {34, "GP34"},
    {35, "GP35"}, {36, "GP36"}, {37, "GP37"}, {38, "GP38"}, {39, "GP39"},
    {40, "GP40"}, {41, "GP41"}, {42, "GP42"}, {43, "GP43"}, {44, "GP44"},
    {45, "GP45"}, {46, "GP46"}, {47, "GP47"},
};

// Status codes cross the C boundary as integers because the boundary is C. This
// is the only place they are integers: above it the interface is typed, and below
// it the adapter uses the same constants.
mm::mcu::Status from(int code) {
    switch (code) {
        case MM_PICO_MCU_OK: return mm::mcu::Status::Ok;
        case MM_PICO_MCU_BAD_ARGUMENT: return mm::mcu::Status::BadArgument;
        case MM_PICO_MCU_UNSUPPORTED: return mm::mcu::Status::Unsupported;
        case MM_PICO_MCU_BUSY: return mm::mcu::Status::Busy;
        case MM_PICO_MCU_TIMEOUT: return mm::mcu::Status::Timeout;
        default: return mm::mcu::Status::BadArgument;
    }
}

class PicoPlatform : public mm::mcu::Platform {
public:
    [[nodiscard]] mm::mcu::Board board() const override {
        const auto count = mm_pico_mcu_gpio_count();
        if (count > sizeof(gpios) / sizeof(gpios[0])) return {};
        std::optional<mm::mcu::Led> led;
        if (mm_pico_mcu_has_led()) {
            const auto gpio = mm_pico_mcu_led_gpio();
            if (gpio < count)
                led = mm::mcu::Led{"LED", gpio, mm_pico_mcu_led_active_high() != 0};
        }
        return {mm_pico_mcu_board_name(), std::span<const mm::mcu::Gpio>{gpios, count}, led};
    }

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

    [[nodiscard]] mm::mcu::Status spi_configure(
        const mm::mcu::SpiConfiguration& configuration) override {
        return from(mm_pico_mcu_spi_configure(
            configuration.instance, configuration.clock_gpio, configuration.transmit_gpio,
            configuration.receive_gpio.value_or(0), configuration.receive_gpio ? 1 : 0,
            configuration.baud, static_cast<int>(configuration.mode),
            configuration.bit_order == mm::mcu::BitOrder::LeastSignificantFirst ? 1 : 0));
    }

    [[nodiscard]] mm::mcu::Status spi_write(
        unsigned int instance, std::span<const std::byte> data) override {
        return from(mm_pico_mcu_spi_write(
            instance, reinterpret_cast<const unsigned char*>(data.data()), data.size()));
    }

    [[nodiscard]] mm::mcu::Status spi_transfer(
        unsigned int instance, std::span<const std::byte> transmit,
        std::span<std::byte> receive) override {
        if (transmit.size() != receive.size()) return mm::mcu::Status::BadArgument;
        return from(mm_pico_mcu_spi_transfer(
            instance, reinterpret_cast<const unsigned char*>(transmit.data()),
            reinterpret_cast<unsigned char*>(receive.data()), transmit.size()));
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
// because objects are linked directly rather than through an archive, and the
// external CMake hand-off carries them as EXTERNAL_OBJECT for the same reason.
struct Register {
    Register() { mm::mcu::set_platform(pico_platform); }
};

const Register registered;

}  // namespace
