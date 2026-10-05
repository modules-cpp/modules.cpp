// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
module;

#include <cstdint>

export module mm.mcu:sdio_types;

export namespace mm::mcu {

// An SD card's native bus: a clock, a bidirectional command line, and four
// data lines, D0 to D3 on four consecutive GPIOs. The platform owns the bus's
// timing and its CRCs; mm.sdcard owns the card protocol above it. width is the
// number of data lines blocks move on; a platform supports what it says, and
// 4 is what SdioCard asks for.
struct SdioConfiguration {
    unsigned int instance = 0;
    unsigned int clock_gpio = 0;
    unsigned int command_gpio = 0;
    unsigned int data0_gpio = 0;    // D0; D1 to D3 are the next three GPIOs
    unsigned int width = 4;
};

// The response a command takes, by the SD Physical Layer Specification's
// names: Short is R1, R6, or R7, a 48-bit response with the command's index
// and a CRC7; ShortNoCrc is R3, whose index and CRC fields are all ones;
// ShortBusy is R1b, an R1 followed by busy on D0, which the platform waits
// out; Long is R2, 136 bits carrying a CID or CSD.
enum class SdioResponse { None, Short, ShortNoCrc, ShortBusy, Long };

// The most blocks one sdio_read must accept. A platform may accept more;
// SdioCard never asks for more.
inline constexpr unsigned int sdio_read_blocks_at_least = 8;

}
