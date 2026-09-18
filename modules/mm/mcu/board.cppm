// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
module;

#include <optional>
#include <span>
#include <string_view>

export module mm.mcu:board;

export namespace mm::mcu {

// One GPIO exposed by a board. number is the value accepted by the GPIO
// facility functions; name is the board's stable, human-readable spelling.
struct Gpio {
    unsigned int number = 0;
    std::string_view name;
};

// A board LED is attached to one GPIO by number. active_high describes the
// electrical level that illuminates it, so portable code need not encode a
// board's polarity alongside its pin number.
struct Led {
    std::string_view name;
    unsigned int gpio = 0;
    bool active_high = true;
};

// Default SPI wiring on the board.
struct SpiWiring {
    unsigned int instance = 0;
    unsigned int clock_gpio = 0;
    unsigned int transmit_gpio = 0;
    std::optional<unsigned int> receive_gpio;
};

// Default I2C wiring on the board.
struct I2cWiring {
    unsigned int instance = 0;
    unsigned int data_gpio = 0;
    unsigned int clock_gpio = 0;
};

// A runtime description supplied by the selected platform provider. gpios is
// the authoritative inventory: every GPIO the board exposes occurs once. New
// board-specific device classes can be added as further inventories while
// retaining GPIO numbers as their attachment points.
struct Board {
    std::string_view name;
    std::span<const Gpio> gpios;
    std::optional<Led> led;
    std::optional<SpiWiring> spi;
    std::optional<I2cWiring> i2c;
};

// The selected platform's board description, or an empty description from the
// unserved fallback when no platform has registered one.
[[nodiscard]] Board board();

}
