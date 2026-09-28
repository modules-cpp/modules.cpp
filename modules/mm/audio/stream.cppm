// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
module;

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <span>

export module mm.audio:stream;

import :status;
import :types;

export namespace mm::audio {

// The connection between a producer and a consumer of samples: an In and the
// application, the application and an Out, or an In and an Out directly. It
// has exactly one producer and exactly one consumer. The execution model gives
// a program one thread and no interrupt touches a Stream, but an
// implementation must still be correct with its two ends in two contexts -- a
// provider that moves one end inside itself, a hosted test's two threads. A
// device needs only this contract, never a Stream's storage, so a buffer, a
// generator, a file, or a mixer above this module can each stand at either
// end.
//
// Every method has an honest default, as every platform interface here does.
class Stream {
public:
    virtual ~Stream() = default;

    [[nodiscard]] virtual Format format() const { return {}; }
    [[nodiscard]] virtual std::size_t readable() const { return 0; }
    [[nodiscard]] virtual std::size_t writable() const { return 0; }

    // count is written on Ok and may be less than the span holds, including
    // zero, which is an answer, not a failure.
    [[nodiscard]] virtual Status write(std::span<const std::int16_t>, std::size_t&) {
        return Status::Unsupported;
    }
    [[nodiscard]] virtual Status read(std::span<std::int16_t>, std::size_t&) {
        return Status::Unsupported;
    }

    // A device takes one end when it starts and gives it back when it stops,
    // so that two devices cannot share an end. A second claim on a held end is
    // Busy. A Stream with one meaningful end -- a generator -- refuses the
    // other. Claims are made from the thread that starts and stops devices.
    [[nodiscard]] virtual Status claim(End) { return Status::Unsupported; }
    virtual void release(End) {}

    // Written by the device at the end that met the loss -- underran by the
    // consumer, overran by the producer -- and read by anyone.
    virtual void underran(std::size_t) {}
    virtual void overran(std::size_t) {}
    [[nodiscard]] virtual Counts counts() const { return {}; }
};

// The Stream this module ships: a single-producer, single-consumer ring over
// storage the caller owns. It never allocates, so a bare-metal application
// gives it a static array.
//
// Its two indices run from zero to twice the capacity and wrap there, which
// tells full from empty without a wasted slot and without a power-of-two
// capacity. Each is written by one side only, with plain atomic loads and
// stores that are lock-free on every target here, Cortex-M0+ included: no
// read-modify-write, so no lock and no runtime support.
class Ring final : public Stream {
public:
    Ring() = default;
    Ring(const Ring&) = delete;
    Ring& operator=(const Ring&) = delete;
    Ring(Ring&&) = delete;
    Ring& operator=(Ring&&) = delete;
    ~Ring() override = default;

    // storage is the caller's and must outlive every device started on this
    // Ring. BadArgument for empty storage, storage too large for the indices,
    // or a zero rate; Busy while an end is claimed. The Ring starts empty with
    // zero counts.
    [[nodiscard]] Status configure(std::span<std::int16_t> storage, Format format);
    [[nodiscard]] std::size_t capacity() const { return capacity_; }

    // Zero-copy access for an application that synthesises or consumes in
    // place: the contiguous run available now, up to the wrap. Committing more
    // than the run now available is BadArgument and commits nothing. An
    // unconfigured Ring lends an empty span and answers NotInitialized.
    [[nodiscard]] std::span<std::int16_t> write_region();
    [[nodiscard]] Status commit_write(std::size_t count);
    [[nodiscard]] std::span<const std::int16_t> read_region() const;
    [[nodiscard]] Status commit_read(std::size_t count);

    // Empties the ring and zeroes the counts. Busy while an end is held.
    [[nodiscard]] Status reset();

    [[nodiscard]] Format format() const override { return format_; }
    [[nodiscard]] std::size_t readable() const override;
    [[nodiscard]] std::size_t writable() const override;
    [[nodiscard]] Status write(std::span<const std::int16_t> samples,
                               std::size_t& count) override;
    [[nodiscard]] Status read(std::span<std::int16_t> samples, std::size_t& count) override;
    [[nodiscard]] Status claim(End end) override;
    void release(End end) override;
    void underran(std::size_t count) override;
    void overran(std::size_t count) override;
    [[nodiscard]] Counts counts() const override;

private:
    [[nodiscard]] std::size_t fill(std::uint32_t write, std::uint32_t read) const;
    [[nodiscard]] std::size_t slot(std::uint32_t index) const;
    [[nodiscard]] std::uint32_t advance(std::uint32_t index, std::size_t count) const;

    std::span<std::int16_t> storage_;
    std::size_t capacity_ = 0;
    Format format_;
    std::atomic<std::uint32_t> write_{0};
    std::atomic<std::uint32_t> read_{0};
    std::atomic<std::uint32_t> underrun_{0};
    std::atomic<std::uint32_t> overrun_{0};
    std::atomic<bool> producer_{false};
    std::atomic<bool> consumer_{false};
};

}
