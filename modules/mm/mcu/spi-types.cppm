// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
module;

#include <optional>

export module mm.mcu:spi_types;

export namespace mm::mcu {

enum class SpiMode { Mode0, Mode1, Mode2, Mode3 };
enum class BitOrder { MostSignificantFirst, LeastSignificantFirst };

// Chip select is deliberately absent. It belongs to the device transaction,
// along with any data/command, reset, and busy GPIOs.
struct SpiConfiguration {
    unsigned int instance = 0;
    unsigned int clock_gpio = 0;
    unsigned int transmit_gpio = 0;
    std::optional<unsigned int> receive_gpio;
    unsigned long baud = 0;
    SpiMode mode = SpiMode::Mode0;
    BitOrder bit_order = BitOrder::MostSignificantFirst;
};

}
