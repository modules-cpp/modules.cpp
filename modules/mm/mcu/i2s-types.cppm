// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
module;

#include <optional>

export module mm.mcu:i2s_types;

export namespace mm::mcu {

// I2S is a serial audio link, so unlike I2C it carries no address: the link is
// point-to-point and there is nothing to select on it. Unlike SPI it carries no
// mode or bit order and no chip select: the protocol fixes the framing, and a
// device's chip select, like its reset, belongs to the device transaction.
//
// The link has a bit clock, a word clock that selects the slot -- left while
// low -- and a data line in each direction it carries: a transmitter has
// transmit_gpio, a receiver receive_gpio, a full-duplex interface both. Every
// frame carries two slots whatever the device uses. rate_hz is the word-clock
// rate, one frame per period. slot_bits is the width of a slot on the wire and
// decides the word in memory: sixteen-bit slots are std::int16_t, wider ones
// std::int32_t, signed and right-aligned.
struct I2sConfiguration {
    unsigned int instance = 0;
    unsigned int bit_clock_gpio = 0;
    unsigned int word_clock_gpio = 0;
    std::optional<unsigned int> transmit_gpio;
    std::optional<unsigned int> receive_gpio;
    unsigned long rate_hz = 48'000;
    unsigned int slot_bits = 16;
};

enum class I2sDirection { Transmit, Receive };

}
