// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
module;

#include <cstddef>
#include <cstdint>
#include <span>

export module mm.mcu:sdio;

import :status;
import :sdio_types;
import :platform;

export namespace mm::mcu {

// Claims an instance on its six pads, the clock idle and the command and data
// lines released, at 400 kHz, the identification clock. A pad another facility
// holds is Busy; a width or pin arrangement the platform cannot drive is
// BadArgument. The same configuration of a claimed instance is idempotent;
// another is Busy until release.
[[nodiscard]] inline Status sdio_configure(const SdioConfiguration& configuration) {
    return platform().sdio_configure(configuration);
}

// Sets the bus clock as near hz as the platform can without exceeding it,
// reporting what it got.
[[nodiscard]] inline Status sdio_clock(unsigned int instance, unsigned long hz,
                                       unsigned long& actual_hz) {
    return platform().sdio_clock(instance, hz, actual_hz);
}

// count clocks with the command line high: the 74 a card needs after power
// before its first command.
[[nodiscard]] inline Status sdio_idle_clocks(unsigned int instance, unsigned int count) {
    return platform().sdio_idle_clocks(instance, count);
}

// One command and its response. The platform frames the command with its
// CRC7, checks a Short or ShortBusy response's index and CRC7 and a Long
// response's CRC7, and waits out ShortBusy's busy. words receives the
// response's content: one word for a short response, its bits 39 to 8 -- the
// card status, OCR, or RCA and status; four for a long one, its bits 127 to
// 0, the CID or CSD with its own CRC7, most significant word first. Timeout
// when no response came, or busy did not end; TransportError for a bad CRC or
// index.
[[nodiscard]] inline Status sdio_command(unsigned int instance, unsigned int index,
                                         std::uint32_t argument, SdioResponse response,
                                         std::span<std::uint32_t> words) {
    return platform().sdio_command(instance, index, argument, response, words);
}

// A read command -- CMD17, CMD18 -- with its Short response in response, and
// the whole blocks it returns in data, each checked against its CRC16 on every
// data line. The platform is ready for the data before it sends the command,
// since a card may start sending before its response has ended. A multiple
// read is the caller's to stop with CMD12. data must be whole blocks of
// block_size, at most sdio_read_blocks_at_least of them unless the platform
// says otherwise, and start on a four-byte boundary, so a platform can move
// it by DMA in words; BadArgument otherwise.
[[nodiscard]] inline Status sdio_read(unsigned int instance, unsigned int index,
                                      std::uint32_t argument, std::uint32_t& response,
                                      std::span<std::byte> data, unsigned int block_size) {
    return platform().sdio_read(instance, index, argument, response, data, block_size);
}

// A write command -- CMD24, CMD25 -- with its Short response in response, then
// the whole blocks of data, each sent with its CRC16 on every line, each
// answered by the card's CRC status, and each followed by its busy, waited
// out. TransportError when the card answers a block with a CRC or write error.
// A multiple write is the caller's to stop with CMD12.
[[nodiscard]] inline Status sdio_write(unsigned int instance, unsigned int index,
                                       std::uint32_t argument, std::uint32_t& response,
                                       std::span<const std::byte> data,
                                       unsigned int block_size) {
    return platform().sdio_write(instance, index, argument, response, data, block_size);
}

// Returns the pads. Releasing an instance that is not claimed is Ok.
[[nodiscard]] inline Status sdio_release(unsigned int instance) {
    return platform().sdio_release(instance);
}

}
