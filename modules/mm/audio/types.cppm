// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
module;

#include <cstddef>
#include <cstdint>
#include <string_view>

export module mm.audio:types;

export namespace mm::audio {

// What a device runs at, nominally. This version is mono: a frame is one
// sample, and every count is in samples. A channel count joins this struct
// when stereo does, and nothing that compares Formats has to change.
struct Format {
    unsigned int rate_hz = 0;

    [[nodiscard]] bool operator==(const Format&) const = default;
};

// A rate exactly, hertz = numerator / denominator. Dividers rarely produce the
// rate asked for, and two devices at one nominal rate may run at two exact
// ones, so the exact one is reported without rounding and without floating
// point. Correcting the difference is policy above this module.
struct Rate {
    std::uint64_t numerator = 0;
    std::uint64_t denominator = 1;
};

// What a device can be asked for, and how often it must be serviced.
// depth_samples is what the device buffers below its Stream, its own scratch
// included: service must come within that many samples' time or the
// transport falls silent. name views provider-owned static storage.
struct Description {
    std::string_view name;
    unsigned int rate_min_hz = 0;
    unsigned int rate_max_hz = 0;
    std::size_t depth_samples = 0;
};

// The two ends of a Stream. A device claims one when it starts.
enum class End { Producer, Consumer };

// What a Stream's devices lost: samples of silence a consumer's transport
// played for want of one, and samples a producer's transport dropped for want
// of room. Each count is written only by the device at its end, and wraps.
struct Counts {
    std::uint32_t underrun_samples = 0;
    std::uint32_t overrun_samples = 0;
};

}
