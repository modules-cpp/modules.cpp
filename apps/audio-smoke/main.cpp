// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
//
// Play a second of tone while recording, then report through the exit code.
#include <cstddef>
#include <cstdint>
#include <span>

import mm.audio;

namespace {

constexpr unsigned int requested_hz = 32'000;
constexpr unsigned int tone_hz = 500;
constexpr std::int16_t level = 8'000;

std::int16_t played[1024];
std::int16_t recorded[1024];
mm::audio::Ring speaker_ring;
mm::audio::Ring microphone_ring;
std::uint64_t phase = 0;

// Fill the speaker's Ring in place with the square wave, as far as it has room.
void fill(unsigned int rate_hz) {
    const auto half_period = rate_hz / (2 * tone_hz);
    auto region = speaker_ring.write_region();
    for (auto& sample : region) {
        sample = (phase / half_period) % 2 == 0 ? level : static_cast<std::int16_t>(-level);
        ++phase;
    }
    static_cast<void>(speaker_ring.commit_write(region.size()));
}

// Drop what was recorded; a smoke test only needs it to arrive.
void drain() {
    const auto region = microphone_ring.read_region();
    static_cast<void>(microphone_ring.commit_read(region.size()));
}

}  // namespace

int main() {
    auto& speaker = mm::audio::selected_out();
    auto& microphone = mm::audio::selected_in();
    if (speaker.initialize() != mm::audio::Status::Ok) return 2;
    // A board without a microphone answers Unsupported, and the tone plays
    // alone; any other answer is a failure.
    const auto heard = microphone.initialize();
    if (heard != mm::audio::Status::Ok && heard != mm::audio::Status::Unsupported) return 3;
    const bool recording = heard == mm::audio::Status::Ok;

    mm::audio::Format format;
    if (speaker.configure({.rate_hz = requested_hz}, format) != mm::audio::Status::Ok) return 4;
    mm::audio::Format recorded_format;
    if (recording && microphone.configure(format, recorded_format) != mm::audio::Status::Ok)
        return 5;
    if (speaker_ring.configure(played, format) != mm::audio::Status::Ok) return 6;
    if (recording && microphone_ring.configure(recorded, recorded_format) != mm::audio::Status::Ok)
        return 7;

    fill(format.rate_hz);
    if (recording && microphone.start(microphone_ring) != mm::audio::Status::Ok) return 8;
    if (speaker.start(speaker_ring) != mm::audio::Status::Ok) return 9;

    std::uint64_t position = 0;
    while (position < format.rate_hz) {
        fill(format.rate_hz);
        if (speaker.service() != mm::audio::Status::Ok) return 10;
        if (recording) {
            if (microphone.service() != mm::audio::Status::Ok) return 11;
            drain();
        }
        if (speaker.position(position) != mm::audio::Status::Ok) return 12;
    }

    if (speaker.stop() != mm::audio::Status::Ok) return 13;
    if (recording && microphone.stop() != mm::audio::Status::Ok) return 14;
    return 0;
}
