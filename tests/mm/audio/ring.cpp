// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <span>
#include <thread>
#include <vector>

import mm.audio;
import mm.test;

namespace {

using mm::audio::End;
using mm::audio::Format;
using mm::audio::Ring;
using mm::audio::Status;
using mm::test::expect;

constexpr Format rate{.rate_hz = 32'000};

void an_unconfigured_ring_answers_honestly() {
    Ring ring;
    std::int16_t samples[2] = {};
    std::size_t count = 7;
    expect(ring.capacity() == 0 && ring.readable() == 0 && ring.writable() == 0,
           "an unconfigured ring holds nothing and has no room");
    expect(ring.write(samples, count) == Status::NotInitialized &&
               ring.read(samples, count) == Status::NotInitialized && count == 7,
           "and moves nothing");
    expect(ring.write_region().empty() && ring.read_region().empty(),
           "it lends no region");
    expect(ring.commit_write(0) == Status::NotInitialized &&
               ring.commit_read(0) == Status::NotInitialized,
           "and commits nothing");
    expect(ring.claim(End::Producer) == Status::NotInitialized,
           "no device can start on it");
}

void configure_refuses_what_it_cannot_hold() {
    Ring ring;
    std::int16_t storage[4] = {};
    expect(ring.configure({}, rate) == Status::BadArgument, "empty storage is refused");
    expect(ring.configure(storage, Format{}) == Status::BadArgument,
           "a zero rate is refused");
    expect(ring.configure(storage, rate) == Status::Ok && ring.capacity() == 4 &&
               ring.format() == rate,
           "a ring takes its capacity and format from configure");
    expect(ring.claim(End::Consumer) == Status::Ok, "a device claims an end");
    expect(ring.configure(storage, rate) == Status::Busy && ring.reset() == Status::Busy,
           "a ring cannot be reconfigured or reset under a device");
    ring.release(End::Consumer);
    expect(ring.reset() == Status::Ok, "once released it can");
}

void claims_admit_one_device_per_end() {
    Ring ring;
    std::int16_t storage[4] = {};
    expect(ring.configure(storage, rate) == Status::Ok, "configured");
    expect(ring.claim(End::Producer) == Status::Ok && ring.claim(End::Consumer) == Status::Ok,
           "one producer and one consumer claim their ends");
    expect(ring.claim(End::Producer) == Status::Busy && ring.claim(End::Consumer) == Status::Busy,
           "a second claim on either end is Busy");
    ring.release(End::Producer);
    expect(ring.claim(End::Producer) == Status::Ok, "a released end can be claimed again");
}

// Every capacity from one to nine, which covers powers of two and not, and at
// every starting offset a write followed by a read of every length returns
// what went in, in order.
void wrap_at_every_offset() {
    for (std::size_t capacity = 1; capacity <= 9; ++capacity) {
        std::vector<std::int16_t> storage(capacity);
        Ring ring;
        expect(ring.configure(storage, rate) == Status::Ok, "configured");
        std::int16_t next_in = 1;
        std::int16_t next_out = 1;
        for (std::size_t offset = 0; offset < 2 * capacity + 1; ++offset) {
            for (std::size_t length = 0; length <= capacity; ++length) {
                std::vector<std::int16_t> in(length);
                for (auto& sample : in) sample = next_in++;
                std::size_t count = 99;
                expect(ring.write(in, count) == Status::Ok && count == length,
                       "a write that fits is accepted whole");
                expect(ring.readable() == length && ring.writable() == capacity - length,
                       "readable and writable account for it");
                std::vector<std::int16_t> out(capacity + 1);
                expect(ring.read(out, count) == Status::Ok && count == length,
                       "a read returns what is there and no more");
                bool ordered = true;
                for (std::size_t i = 0; i < length; ++i)
                    if (out[i] != next_out++) ordered = false;
                expect(ordered, "samples come out in the order they went in");
            }
            // Step the offset by one sample.
            std::int16_t one[1] = {next_in++};
            std::size_t count = 0;
            expect(ring.write(one, count) == Status::Ok && ring.read(one, count) == Status::Ok &&
                       one[0] == next_out++,
                   "one sample steps the offset");
        }
    }
}

void full_and_empty_are_told_apart() {
    std::int16_t storage[3] = {};
    Ring ring;
    expect(ring.configure(storage, rate) == Status::Ok, "configured");
    const std::int16_t in[5] = {1, 2, 3, 4, 5};
    std::size_t count = 0;
    expect(ring.write(in, count) == Status::Ok && count == 3,
           "a write larger than the room is accepted in part");
    expect(ring.readable() == 3 && ring.writable() == 0, "a full ring uses every slot");
    expect(ring.write(in, count) == Status::Ok && count == 0,
           "a full ring accepts nothing, which is an answer");
    std::int16_t out[5] = {};
    expect(ring.read(out, count) == Status::Ok && count == 3 && out[2] == 3,
           "a full ring gives up everything");
    expect(ring.read(out, count) == Status::Ok && count == 0 && ring.readable() == 0,
           "an empty ring gives nothing, which is an answer");
}

void regions_lend_the_contiguous_run() {
    std::int16_t storage[5] = {};
    Ring ring;
    expect(ring.configure(storage, rate) == Status::Ok, "configured");
    auto region = ring.write_region();
    expect(region.size() == 5 && region.data() == storage,
           "an empty ring lends all of its storage");
    region[0] = 10;
    region[1] = 11;
    region[2] = 12;
    expect(ring.commit_write(6) == Status::BadArgument && ring.readable() == 0,
           "committing more than the run commits nothing");
    expect(ring.commit_write(3) == Status::Ok && ring.readable() == 3, "a commit publishes");
    auto lent = ring.read_region();
    expect(lent.size() == 3 && lent[0] == 10 && lent[2] == 12, "the consumer sees it in place");
    expect(ring.commit_read(2) == Status::Ok && ring.readable() == 1, "a partial read commits");
    region = ring.write_region();
    expect(region.size() == 2 && region.data() == storage + 3,
           "the write run stops at the wrap");
    region[0] = 13;
    region[1] = 14;
    expect(ring.commit_write(2) == Status::Ok, "the run to the wrap is committed");
    region = ring.write_region();
    expect(region.size() == 2 && region.data() == storage,
           "the next run starts at the beginning");
    expect(ring.commit_write(3) == Status::BadArgument, "and is no longer than the room");
    lent = ring.read_region();
    expect(lent.size() == 3 && lent[0] == 12 && lent[2] == 14,
           "the read run also stops at the wrap");
    expect(ring.commit_read(4) == Status::BadArgument, "a read commit past the run is refused");
}

// A commit is bounded by the wrap as well as by the room: with the whole ring
// free but the run ending two slots on, committing three would publish samples
// the caller never wrote into place.
void commits_stop_at_the_wrap() {
    std::int16_t storage[5] = {};
    Ring ring;
    expect(ring.configure(storage, rate) == Status::Ok, "configured");
    const std::int16_t three[3] = {1, 2, 3};
    std::int16_t out[3] = {};
    std::size_t count = 0;
    expect(ring.write(three, count) == Status::Ok && ring.read(out, count) == Status::Ok,
           "the indices move three slots on");
    expect(ring.writable() == 5 && ring.write_region().size() == 2,
           "all five are free, but only two before the wrap");
    expect(ring.commit_write(3) == Status::BadArgument && ring.readable() == 0,
           "a write commit across the wrap is refused");
    expect(ring.write(std::span<const std::int16_t>{three}, count) == Status::Ok && count == 3,
           "three samples across the wrap by copy");
    expect(ring.read_region().size() == 2 && ring.commit_read(3) == Status::BadArgument,
           "a read commit across the wrap is refused too");
}

void counts_belong_to_the_ends() {
    std::int16_t storage[2] = {};
    Ring ring;
    expect(ring.configure(storage, rate) == Status::Ok, "configured");
    ring.underran(3);
    ring.underran(4);
    ring.overran(5);
    const auto counts = ring.counts();
    expect(counts.underrun_samples == 7 && counts.overrun_samples == 5,
           "each end's losses accumulate separately");
    expect(ring.reset() == Status::Ok && ring.counts().underrun_samples == 0 &&
               ring.counts().overrun_samples == 0,
           "reset zeroes them");
}

constexpr std::uint32_t total = 3'000'000;

// A ring that stops moving must fail the case rather than hang the run: either
// side that goes this many turns without progress gives up, and says so.
constexpr std::uint64_t patience = 200'000'000;

// The producer: the copying end, offering seventeen samples at a time and
// offering the rest of a chunk again when only part of it was accepted.
void produce(Ring& ring, std::atomic<bool>& stalled) {
    std::int16_t chunk[17];
    std::uint32_t sent = 0;
    std::size_t offered = 0;
    std::size_t done = 0;
    std::uint64_t idle = 0;
    while (sent < total && !stalled.load(std::memory_order_relaxed)) {
        if (done == offered) {
            offered = 0;
            while (offered < 17 && sent + offered < total) {
                chunk[offered] = static_cast<std::int16_t>((sent + offered) & 0x7fff);
                ++offered;
            }
            done = 0;
        }
        std::size_t count = 0;
        if (ring.write(std::span<const std::int16_t>{chunk}.subspan(done, offered - done),
                       count) != Status::Ok)
            return;
        done += count;
        sent += static_cast<std::uint32_t>(count);
        idle = count == 0 ? idle + 1 : 0;
        if (idle > patience) stalled.store(true, std::memory_order_relaxed);
    }
}

// A producer thread and a consumer thread, a ring much smaller than what
// passes through it, and every sample arriving once, in order. The producer
// uses the copying end and the consumer the zero-copy one, so both paths cross
// the wrap under contention.
void two_threads_pass_every_sample_in_order() {
    std::int16_t storage[61] = {};
    Ring ring;
    expect(ring.configure(storage, rate) == Status::Ok, "configured");
    std::atomic<bool> stalled{false};
    std::thread producer(produce, std::ref(ring), std::ref(stalled));
    std::uint32_t received = 0;
    bool ordered = true;
    std::uint64_t idle = 0;
    while (received < total && !stalled.load(std::memory_order_relaxed)) {
        const auto lent = ring.read_region();
        idle = lent.empty() ? idle + 1 : 0;
        if (idle > patience) stalled.store(true, std::memory_order_relaxed);
        for (const auto sample : lent) {
            if (sample != static_cast<std::int16_t>(received & 0x7fff)) ordered = false;
            ++received;
        }
        if (ring.commit_read(lent.size()) != Status::Ok) {
            ordered = false;
            break;
        }
    }
    producer.join();
    expect(!stalled.load(), "neither side stalls");
    expect(received == total, "every sample arrives");
    expect(ordered, "and in the order it was written");
    expect(ring.readable() == 0, "nothing is left over");
}

const mm::test::case_ cases[] = {
    {"an unconfigured ring answers honestly", &an_unconfigured_ring_answers_honestly},
    {"configure refuses what it cannot hold", &configure_refuses_what_it_cannot_hold},
    {"claims admit one device per end", &claims_admit_one_device_per_end},
    {"wrap at every offset", &wrap_at_every_offset},
    {"full and empty are told apart", &full_and_empty_are_told_apart},
    {"regions lend the contiguous run", &regions_lend_the_contiguous_run},
    {"commits stop at the wrap", &commits_stop_at_the_wrap},
    {"counts belong to the ends", &counts_belong_to_the_ends},
    {"two threads pass every sample in order", &two_threads_pass_every_sample_in_order},
};

const mm::test::registrar reg{"mm.audio ring", cases};

}  // namespace
