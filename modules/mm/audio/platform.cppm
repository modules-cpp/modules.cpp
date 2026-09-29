// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
module;

#include <cstddef>
#include <cstdint>

export module mm.audio:platform;

import :status;
import :types;
import :stream;

export namespace mm::audio {

// The two device seams. A board's platform provider derives a Microphone from
// In and a Speaker from Out, owns their pins and transports, and registers one
// of each; a board without one registers an instance whose every call answers
// Unsupported, so the answer always comes from the board. mm.audio itself
// knows no pin, transport, or board.
//
// A device's life is initialize, configure, start, service, stop, and start
// again. service is non-blocking and is called at least once per
// Description::depth_samples; a device that feeds itself answers Ok and does
// nothing. Silence on underrun and dropping on overrun are the transport's
// own, and a device reports their growth to its Stream.

class Out {
public:
    virtual ~Out() = default;

    [[nodiscard]] virtual Description description() const { return {}; }
    [[nodiscard]] virtual Status initialize() { return Status::Unsupported; }

    // Claims the transport at the nearest rate it holds, which then idles at
    // silence. actual is written only on Ok. Busy while started.
    [[nodiscard]] virtual Status configure(const Format&, Format&) {
        return Status::Unsupported;
    }

    // The exact rate configured. NotInitialized before configure.
    [[nodiscard]] virtual Status rate(Rate&) const { return Status::Unsupported; }

    // Claims stream's consumer end, whose format must equal the configured
    // one, BadArgument otherwise, and starts playing. Position restarts at
    // zero. The first sample plays at the first period after the first
    // accepted offer; the silence before it is not an underrun.
    [[nodiscard]] virtual Status start(Stream&) { return Status::Unsupported; }

    // Offers the pending run, then reads more from the Stream only once
    // nothing is pending, so a sample read from the Stream is played or
    // discarded by stop and never lost. Never waits. An error leaves the
    // pending run for the next call.
    [[nodiscard]] virtual Status service() { return Status::Unsupported; }

    // Samples from the Stream the transport has finished outputting since
    // start: what a listener has heard. Inserted silence is not counted.
    [[nodiscard]] virtual Status position(std::uint64_t&) const {
        return Status::Unsupported;
    }

    // Samples taken from the Stream and not yet output: the pending run plus
    // the transport's queue. Drained when this is zero and the Stream is empty.
    [[nodiscard]] virtual Status pending(std::size_t&) const { return Status::Unsupported; }

    // Discards everything below the Stream, returns the transport to idle
    // silence, and releases the consumer end. The Stream keeps what it holds,
    // so a later start resumes from it; stop is immediate, not a sample-exact
    // pause.
    [[nodiscard]] virtual Status stop() { return Status::Unsupported; }

    [[nodiscard]] virtual Status sleep() { return Status::Unsupported; }
};

class In {
public:
    virtual ~In() = default;

    [[nodiscard]] virtual Description description() const { return {}; }
    [[nodiscard]] virtual Status initialize() { return Status::Unsupported; }

    // Claims the transport at the nearest rate it holds, idle. actual is
    // written only on Ok. Busy while started.
    [[nodiscard]] virtual Status configure(const Format&, Format&) {
        return Status::Unsupported;
    }

    [[nodiscard]] virtual Status rate(Rate&) const { return Status::Unsupported; }

    // Claims stream's producer end, whose format must equal the configured
    // one, discards whatever the transport held, and starts capture. Nothing
    // converted before start ever reaches the Stream.
    [[nodiscard]] virtual Status start(Stream&) { return Status::Unsupported; }

    // Takes from the transport no more than the Stream has room for, so a
    // sample taken is never dropped here; the loss, when the application falls
    // behind, is the transport's and is reported as overrun. Never waits.
    [[nodiscard]] virtual Status service() { return Status::Unsupported; }

    // Samples converted since start, including those the transport dropped.
    [[nodiscard]] virtual Status position(std::uint64_t&) const {
        return Status::Unsupported;
    }

    // Ends capture, discards what the transport held, and releases the
    // producer end. The Stream keeps what was captured.
    [[nodiscard]] virtual Status stop() { return Status::Unsupported; }

    [[nodiscard]] virtual Status sleep() { return Status::Unsupported; }
};

// Registered by the board's provider from a static initialiser. A lane whose
// closure reaches no provider -- a host test -- gets the fallbacks, which
// answer Unsupported.
void set_in(In& in);
[[nodiscard]] In& selected_in();
void set_out(Out& out);
[[nodiscard]] Out& selected_out();

}
