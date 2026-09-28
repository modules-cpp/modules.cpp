// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
module;

#include <cstdint>
#include <string_view>

export module mm.audio:types;

export namespace mm::audio {

// One audio sample, sixteen-bit signed PCM. It is the width every device in
// this interface speaks, so a caller that captures from a microphone and plays
// to a codec does not convert between the two.
using Sample = std::int16_t;

// Which end of the link a device occupies. A microphone is an input; a codec's
// DAC is an output. A device that does both is still described per direction.
enum class Direction { Input, Output };

// What a device is, in the terms a caller needs to program it: its name, the
// rate it samples at, how many channels it carries, and which direction it
// faces. These are the audio facts that stand in for a touch panel's geometry.
struct Description {
    std::string_view name = "";
    unsigned int rate = 0;         // samples per second
    unsigned int channels = 0;     // the channels one capture or playback frame carries
    Direction direction = Direction::Input;
};

}
