// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
module;

#include <cstddef>
#include <span>

export module mm.audio:platform;

import :status;
import :types;

export namespace mm::audio {

// A microphone is the input end of the audio link. It captures samples into
// the caller's span. Like mm.touch, it is polled: a caller calls capture, and a
// provider may use its own interrupt or DMA internally to make that cheap.
class Microphone {
public:
    virtual ~Microphone() = default;

    [[nodiscard]] virtual Description description() const { return {}; }
    [[nodiscard]] virtual Status initialize() { return Status::Unsupported; }

    // count is written only when the call answers Ok, so a caller that ignores
    // the status cannot mistake a stale count for a fresh reading.
    [[nodiscard]] virtual Status capture(std::span<Sample>, std::size_t&) {
        return Status::Unsupported;
    }
    [[nodiscard]] virtual Status shutdown() { return Status::Unsupported; }
};

// A codec is the output end of the audio link. It plays the caller's samples.
class Codec {
public:
    virtual ~Codec() = default;

    [[nodiscard]] virtual Description description() const { return {}; }
    [[nodiscard]] virtual Status initialize() { return Status::Unsupported; }
    [[nodiscard]] virtual Status play(std::span<const Sample>) { return Status::Unsupported; }
    [[nodiscard]] virtual Status shutdown() { return Status::Unsupported; }
};

void set_microphone(Microphone& microphone);
[[nodiscard]] Microphone& selected_microphone();
void set_codec(Codec& codec);
[[nodiscard]] Codec& selected_codec();

}
