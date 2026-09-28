// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
module;

#include <cstddef>
#include <cstdint>

export module mm.mcu:transport_types;

export namespace mm::mcu {

// The continuous transports -- paced ADC capture, the DAC, and I2S -- run on a
// clock the hardware keeps, divided from some source by dividers that rarely
// produce the rate a caller asked for. A rate is therefore reported exactly,
// hertz = numerator / denominator, without rounding and without floating point.
struct Frequency {
    std::uint64_t numerator = 0;
    std::uint64_t denominator = 1;
};

// What a continuous transport has done since it was started. For a
// transmitter, completed counts queued samples output, queued those given and
// not yet output, and missed the sample periods it output silence for want of
// one. For a receiver, completed counts samples converted, queued those
// converted and not yet taken, and missed those dropped because the buffer the
// platform keeps was full. I2S counts in frames, the others in samples.
struct Progress {
    std::uint64_t completed = 0;
    std::size_t queued = 0;
    std::uint32_t missed = 0;
};

}
