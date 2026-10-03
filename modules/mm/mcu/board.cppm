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
    // The board's default chip select: a plain GPIO a driver lowers to talk
    // to one device on the bus, what an Arduino core calls SS. Absent where
    // the board names none.
    std::optional<unsigned int> chip_select_gpio;
};

// Default I2C wiring on the board.
struct I2cWiring {
    unsigned int instance = 0;
    unsigned int data_gpio = 0;
    unsigned int clock_gpio = 0;
};

// A UART on the board: its instance and the pins it is on by default.
struct UartWiring {
    unsigned int instance = 0;
    unsigned int transmit_gpio = 0;
    unsigned int receive_gpio = 0;
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
    // A second default I2C wiring, on another instance, where the board has
    // pins free for one. Absent otherwise: it is never the first wiring
    // again, and a board whose candidate pins do something else has none.
    std::optional<I2cWiring> second_i2c;
    // The board's default UART and a second one, where it has them: what a
    // sketch calls Serial1 and Serial2. Absent where the pins do something
    // else, as second_i2c is.
    std::optional<UartWiring> uart;
    std::optional<UartWiring> second_uart;
};

// The selected platform's board description, or an empty description from the
// unserved fallback when no platform has registered one.
[[nodiscard]] Board board();

}
