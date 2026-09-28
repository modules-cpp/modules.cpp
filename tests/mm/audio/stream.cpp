// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
#include <cstddef>
#include <cstdint>
#include <span>

import mm.audio;
import mm.test;

namespace {

using mm::audio::End;
using mm::audio::Format;
using mm::audio::Status;
using mm::audio::Stream;
using mm::test::expect;

// A Stream that is not a buffer: a generator of a rising ramp, with a
// consumer end only. It is what a tone or a test signal looks like to an Out,
// which calls only the interface.
class Ramp final : public Stream {
public:
    [[nodiscard]] Format format() const override { return {.rate_hz = 8'000}; }
    [[nodiscard]] std::size_t readable() const override { return 64; }

    [[nodiscard]] Status read(std::span<std::int16_t> samples, std::size_t& count) override {
        const auto moved = samples.size() < 64 ? samples.size() : std::size_t{64};
        for (std::size_t i = 0; i < moved; ++i) samples[i] = next++;
        count = moved;
        return Status::Ok;
    }

    [[nodiscard]] Status claim(End end) override {
        if (end != End::Consumer) return Status::BadArgument;
        if (held) return Status::Busy;
        held = true;
        return Status::Ok;
    }

    void release(End) override { held = false; }

    std::int16_t next = 0;
    bool held = false;
};

void an_unimplemented_stream_answers_unsupported() {
    Stream bare;
    std::int16_t samples[2] = {};
    std::size_t count = 5;
    expect(bare.format() == Format{} && bare.readable() == 0 && bare.writable() == 0,
           "a bare Stream has no format, nothing to read, and no room");
    expect(bare.write(samples, count) == Status::Unsupported &&
               bare.read(samples, count) == Status::Unsupported && count == 5,
           "and moves nothing");
    expect(bare.claim(End::Producer) == Status::Unsupported, "no device can start on it");
    bare.underran(3);
    expect(bare.counts().underrun_samples == 0 && bare.counts().overrun_samples == 0,
           "and it counts nothing");
}

void a_generator_stands_in_for_a_ring() {
    Ramp ramp;
    Stream& stream = ramp;
    expect(stream.claim(End::Producer) == Status::BadArgument,
           "a generator refuses the end it has no use for");
    expect(stream.claim(End::Consumer) == Status::Ok, "and admits a consumer");
    std::int16_t samples[3] = {};
    std::size_t count = 0;
    expect(stream.read(samples, count) == Status::Ok && count == 3 && samples[0] == 0 &&
               samples[2] == 2,
           "a consumer reads through the interface alone");
    expect(stream.write(samples, count) == Status::Unsupported,
           "and writing to a generator is honestly unsupported");
    stream.release(End::Consumer);
    expect(!ramp.held, "release reaches the implementation");
}

const mm::test::case_ cases[] = {
    {"an unimplemented stream answers Unsupported", &an_unimplemented_stream_answers_unsupported},
    {"a generator stands in for a ring", &a_generator_stands_in_for_a_ring},
};

const mm::test::registrar reg{"mm.audio stream", cases};

}  // namespace
