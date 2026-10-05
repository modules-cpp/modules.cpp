// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
module;

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <thread>

export module platform.linux.storage.bus;

import mm.mcu;
import platform.linux.storage.sdcard;
import platform.linux.storage.spiflash;

export namespace platform::linux::storage {

struct BusWiring {
    static constexpr unsigned int instance = 0;
    static constexpr unsigned int clock_gpio = 18;
    static constexpr unsigned int transmit_gpio = 19;
    static constexpr unsigned int receive_gpio = 20;
    static constexpr unsigned int sdcard_chip_select_gpio = 23;
    static constexpr unsigned int spiflash_chip_select_gpio = 17;
};

class Bus final : public mm::mcu::Platform {
public:
    Bus(SdCard* card, SpiFlash* flash) : card_(card), flash_(flash) {}

    [[nodiscard]] mm::mcu::Capabilities capabilities() const override {
        return {.board = true, .gpio = true, .spi = true, .timer = true};
    }

    [[nodiscard]] mm::mcu::Board board() const override {
        static const mm::mcu::Gpio gpios[]{{BusWiring::spiflash_chip_select_gpio, "FLASH_CS"},
                                           {BusWiring::clock_gpio, "SCK"},
                                           {BusWiring::transmit_gpio, "MOSI"},
                                           {BusWiring::receive_gpio, "MISO"},
                                           {BusWiring::sdcard_chip_select_gpio, "SD_CS"}};
        return {.name = "storage-linux",
                .gpios = gpios,
                .led = std::nullopt,
                .spi = mm::mcu::SpiWiring{.instance = BusWiring::instance,
                                          .clock_gpio = BusWiring::clock_gpio,
                                          .transmit_gpio = BusWiring::transmit_gpio,
                                          .receive_gpio = BusWiring::receive_gpio,
                                          .chip_select_gpio =
                                              BusWiring::sdcard_chip_select_gpio}};
    }

    [[nodiscard]] mm::mcu::Status gpio_configure(unsigned int pin, mm::mcu::Direction,
                                                 mm::mcu::Pull) override {
        return chip_select(pin) ? mm::mcu::Status::Ok : mm::mcu::Status::Unsupported;
    }

    [[nodiscard]] mm::mcu::Status gpio_write(unsigned int pin, bool high) override {
        if (pin == BusWiring::sdcard_chip_select_gpio) {
            card_high_ = high;
            if (card_ != nullptr) card_->chip_select(high);
            return mm::mcu::Status::Ok;
        }
        if (pin == BusWiring::spiflash_chip_select_gpio) {
            flash_high_ = high;
            if (flash_ != nullptr) flash_->chip_select(high);
            return mm::mcu::Status::Ok;
        }
        return mm::mcu::Status::Unsupported;
    }

    [[nodiscard]] mm::mcu::Status gpio_read(unsigned int pin, bool& high) override {
        if (pin == BusWiring::sdcard_chip_select_gpio) high = card_high_;
        else if (pin == BusWiring::spiflash_chip_select_gpio) high = flash_high_;
        else return mm::mcu::Status::Unsupported;
        return mm::mcu::Status::Ok;
    }

    [[nodiscard]] mm::mcu::Status spi_configure(const mm::mcu::SpiConfiguration& value) override {
        if (value.instance != BusWiring::instance || value.baud == 0 ||
            value.clock_gpio != BusWiring::clock_gpio ||
            value.transmit_gpio != BusWiring::transmit_gpio ||
            value.receive_gpio.value_or(BusWiring::receive_gpio) != BusWiring::receive_gpio)
            return mm::mcu::Status::BadArgument;
        configured_ = true;
        return mm::mcu::Status::Ok;
    }

    [[nodiscard]] mm::mcu::Status spi_write(unsigned int instance,
                                            std::span<const std::byte> data) override {
        if (instance != BusWiring::instance || !configured_) return mm::mcu::Status::BadArgument;
        for (const auto value : data) static_cast<void>(clock(value));
        return mm::mcu::Status::Ok;
    }

    [[nodiscard]] mm::mcu::Status spi_transfer(unsigned int instance,
                                               std::span<const std::byte> out,
                                               std::span<std::byte> in) override {
        if (instance != BusWiring::instance || !configured_ || out.size() != in.size())
            return mm::mcu::Status::BadArgument;
        for (std::size_t i = 0; i < out.size(); ++i) in[i] = clock(out[i]);
        return mm::mcu::Status::Ok;
    }

    [[nodiscard]] mm::mcu::Status delay_ms(unsigned long milliseconds) override {
        std::this_thread::sleep_for(std::chrono::milliseconds(milliseconds));
        return mm::mcu::Status::Ok;
    }

    [[nodiscard]] mm::mcu::Status delay_us(unsigned long microseconds) override {
        std::this_thread::sleep_for(std::chrono::microseconds(microseconds));
        return mm::mcu::Status::Ok;
    }

    [[nodiscard]] mm::mcu::Status ticks_ms(unsigned long& ticks) override {
        ticks = static_cast<unsigned long>(std::chrono::duration_cast<std::chrono::milliseconds>(
                                               std::chrono::steady_clock::now().time_since_epoch())
                                               .count());
        return mm::mcu::Status::Ok;
    }

    [[nodiscard]] mm::mcu::Status ticks_us(unsigned long& ticks) override {
        ticks = static_cast<unsigned long>(std::chrono::duration_cast<std::chrono::microseconds>(
                                               std::chrono::steady_clock::now().time_since_epoch())
                                               .count());
        return mm::mcu::Status::Ok;
    }

    // Bytes clocked while both chips were selected.
    [[nodiscard]] unsigned long wiring_errors() const { return wiring_errors_; }

private:
    [[nodiscard]] static bool chip_select(unsigned int pin) {
        return pin == BusWiring::sdcard_chip_select_gpio ||
               pin == BusWiring::spiflash_chip_select_gpio;
    }

    [[nodiscard]] std::byte clock(std::byte out) {
        if (!card_high_ && !flash_high_) {
            ++wiring_errors_;
            return std::byte{0xff};
        }
        const auto value = static_cast<std::uint8_t>(out);
        if (!card_high_ && card_ != nullptr) return static_cast<std::byte>(card_->exchange(value));
        if (!flash_high_ && flash_ != nullptr)
            return static_cast<std::byte>(flash_->exchange(value));
        return std::byte{0xff};
    }

    SdCard* card_;
    SpiFlash* flash_;
    bool card_high_ = true;
    bool flash_high_ = true;
    bool configured_ = false;
    unsigned long wiring_errors_ = 0;
};

}  // namespace platform::linux::storage
