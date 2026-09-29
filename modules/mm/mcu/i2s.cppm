// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
module;

#include <cstddef>
#include <cstdint>
#include <span>

export module mm.mcu:i2s;

import :status;
import :transport_types;
import :i2s_types;
import :platform;

export namespace mm::mcu {

// Claims the pads and starts the clocks at the nearest word-clock rate the
// platform holds, transmitting zeros. The clocks then run until release: a
// DAC that derives its own clocks from the bit clock relocks, audibly, every
// time they stop. BadArgument for a zero rate, a slot width the platform does
// not carry, no data line, a pin reused across the link's signals, or a wiring
// the platform cannot drive; Busy for a pad someone else holds or an instance
// configured differently. Idempotent for the same configuration.
[[nodiscard]] inline Status i2s_configure(const I2sConfiguration& configuration) {
    return platform().i2s_configure(configuration);
}

[[nodiscard]] inline Status i2s_rate(unsigned int instance, Frequency& actual) {
    return platform().i2s_rate(instance, actual);
}

// Starts moving frames in one direction. Progress restarts at zero; a
// receiver discards what it held, so nothing received before start is read.
[[nodiscard]] inline Status i2s_start(unsigned int instance, I2sDirection direction) {
    return platform().i2s_start(instance, direction);
}

// Queues frames, left word then right word, oldest first. accepted counts
// frames, is written on Ok, and may be less than offered, including zero. The
// overload must match the configured slot width, BadArgument otherwise, and an
// odd word count is BadArgument because a frame is two words. With nothing
// queued at a frame the transmitter sends zeros and the frame counts as missed.
[[nodiscard]] inline Status i2s_write(unsigned int instance,
                                      std::span<const std::int16_t> words,
                                      std::size_t& accepted) {
    if (words.size() % 2 != 0) return Status::BadArgument;
    return platform().i2s_write(instance, words, accepted);
}

[[nodiscard]] inline Status i2s_write(unsigned int instance,
                                      std::span<const std::int32_t> words,
                                      std::size_t& accepted) {
    if (words.size() % 2 != 0) return Status::BadArgument;
    return platform().i2s_write(instance, words, accepted);
}

// Frames received since the last read, left word then right word. count is
// written on Ok and may be zero. The same width and pairing rules.
[[nodiscard]] inline Status i2s_read(unsigned int instance, std::span<std::int16_t> words,
                                     std::size_t& count) {
    if (words.size() % 2 != 0) return Status::BadArgument;
    return platform().i2s_read(instance, words, count);
}

[[nodiscard]] inline Status i2s_read(unsigned int instance, std::span<std::int32_t> words,
                                     std::size_t& count) {
    if (words.size() % 2 != 0) return Status::BadArgument;
    return platform().i2s_read(instance, words, count);
}

[[nodiscard]] inline Status i2s_progress(unsigned int instance, I2sDirection direction,
                                         Progress& progress) {
    return platform().i2s_progress(instance, direction, progress);
}

// Empties the direction's buffer and, for a transmitter, returns it to zeros.
// The clocks keep running.
[[nodiscard]] inline Status i2s_stop(unsigned int instance, I2sDirection direction) {
    return platform().i2s_stop(instance, direction);
}

// Stops the clocks and returns the pads.
[[nodiscard]] inline Status i2s_release(unsigned int instance) {
    return platform().i2s_release(instance);
}

}
