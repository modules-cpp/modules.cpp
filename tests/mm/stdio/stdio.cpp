// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
#include <array>
#include <cstddef>
#include <span>

import mm.stdio;
import mm.test;

void mm_test_stdio_reset();
void mm_test_stdio_force(mm::stdio::Status status);
void mm_test_stdio_connect(bool connected);
void mm_test_stdio_capacity(std::size_t capacity);
void mm_test_stdio_offer(const unsigned char* bytes, std::size_t size);
std::size_t mm_test_stdio_accepted();
unsigned int mm_test_stdio_byte(std::size_t index);
std::size_t mm_test_stdio_flushes();
std::size_t mm_test_stdio_connection_queries();

namespace {

using mm::test::expect;
using mm::stdio::Status;

constexpr std::array message{std::byte{'m'}, std::byte{'m'}, std::byte{'.'},
                             std::byte{'c'}, std::byte{'p'}, std::byte{'p'}};

void a_whole_message_reports_what_it_sent() {
    mm_test_stdio_reset();
    auto& console = mm::stdio::selected_console();
    expect(console.initialize() == Status::Ok, "the console initializes");

    std::size_t written = 0;
    expect(console.write(message, written) == Status::Ok, "a write succeeds");
    expect(written == message.size(), "a console with room takes everything");
    expect(mm_test_stdio_accepted() == message.size() &&
               mm_test_stdio_byte(0) == 'm' && mm_test_stdio_byte(5) == 'p',
           "the bytes arrive in order");
}

// The behaviour the interface exists to express: a far end that will not take
// everything is reported, not hidden behind a status or a block.
void a_short_write_reports_what_it_accepted() {
    mm_test_stdio_reset();
    mm_test_stdio_capacity(2);
    auto& console = mm::stdio::selected_console();
    expect(console.initialize() == Status::Ok, "the console initializes");

    std::size_t written = 7;
    expect(console.write(message, written) == Status::Ok,
           "a console with too little room still succeeds");
    expect(written == 2, "it reports the two bytes it took");
    expect(mm_test_stdio_accepted() == 2, "and took exactly those");

    // A caller resends the tail rather than the whole message.
    std::size_t again = 0;
    expect(console.write(std::span<const std::byte>{message}.subspan(written), again) ==
               Status::Ok,
           "the remainder can be offered again");
    expect(again == 0, "a full console takes nothing more");
}

// A caller that writes into a disconnected console must make progress. This is
// the case that would hang if the interface blocked instead of counting.
void a_disconnected_console_accepts_nothing_and_does_not_hang() {
    mm_test_stdio_reset();
    mm_test_stdio_connect(false);
    auto& console = mm::stdio::selected_console();
    expect(console.initialize() == Status::Ok, "the console initializes");

    bool connected = true;
    expect(console.connected(connected) == Status::Ok && !connected,
           "the console reports that nothing is listening");

    std::size_t written = 9;
    expect(console.write(message, written) == Status::Ok,
           "writing with nobody listening is not an error");
    expect(written == 0, "and nothing was sent");
    expect(mm_test_stdio_accepted() == 0, "so nothing arrived");
}

// The pattern the Pico provider will need: poll until something is listening,
// then write, so the first output is not discarded during enumeration.
void a_caller_may_poll_before_writing() {
    mm_test_stdio_reset();
    mm_test_stdio_connect(false);
    auto& console = mm::stdio::selected_console();
    expect(console.initialize() == Status::Ok, "the console initializes");

    bool connected = false;
    unsigned int attempts = 0;
    constexpr unsigned int limit = 8;
    while (attempts < limit) {
        ++attempts;
        expect(console.connected(connected) == Status::Ok, "the query succeeds");
        if (connected) break;
        // Whatever a real caller does to pass the time; here, the host arrives.
        if (attempts == 3) mm_test_stdio_connect(true);
    }
    expect(connected && attempts == 4, "the caller notices when the far end appears");
    expect(mm_test_stdio_connection_queries() == attempts,
           "every poll reached the console");

    std::size_t written = 0;
    expect(console.write(message, written) == Status::Ok && written == message.size(),
           "and the first message is not lost");
}

void a_read_with_nothing_waiting_is_ok_with_no_bytes() {
    mm_test_stdio_reset();
    auto& console = mm::stdio::selected_console();
    expect(console.initialize() == Status::Ok, "the console initializes");

    std::array<std::byte, 4> buffer{};
    std::size_t count = 5;
    expect(console.read(buffer, count) == Status::Ok && count == 0,
           "nothing waiting is an answer rather than an error");

    const unsigned char offered[] = {'h', 'i'};
    mm_test_stdio_offer(offered, sizeof offered);
    expect(console.read(buffer, count) == Status::Ok && count == 2,
           "what is waiting is delivered");
    expect(buffer[0] == std::byte{'h'} && buffer[1] == std::byte{'i'},
           "and delivered in order");

    expect(console.read(buffer, count) == Status::Ok && count == 0,
           "a drained console reads empty again");
}

void a_read_never_writes_past_the_caller_span() {
    mm_test_stdio_reset();
    auto& console = mm::stdio::selected_console();
    expect(console.initialize() == Status::Ok, "the console initializes");

    const unsigned char offered[] = {'a', 'b', 'c', 'd', 'e'};
    mm_test_stdio_offer(offered, sizeof offered);

    std::array<std::byte, 2> small{};
    std::size_t count = 0;
    expect(console.read(small, count) == Status::Ok && count == 2,
           "a short span takes what it has room for");
    expect(small[0] == std::byte{'a'} && small[1] == std::byte{'b'},
           "starting from the front");
    expect(console.read(small, count) == Status::Ok && count == 2 &&
               small[0] == std::byte{'c'},
           "and continues where it left off");
}

void the_lifecycle_is_explicit() {
    mm_test_stdio_reset();
    auto& console = mm::stdio::selected_console();

    std::array<std::byte, 1> buffer{};
    std::size_t count = 0;
    bool connected = false;
    expect(console.write(message, count) == Status::NotInitialized,
           "writing before initialization reports so");
    expect(console.read(buffer, count) == Status::NotInitialized, "so does reading");
    expect(console.flush() == Status::NotInitialized, "so does flushing");
    expect(console.connected(connected) == Status::NotInitialized,
           "so does asking about the connection");

    expect(console.initialize() == Status::Ok, "the console initializes");
    expect(console.flush() == Status::Ok && mm_test_stdio_flushes() == 1,
           "flush reaches the console once");
}

void transport_failure_reaches_the_caller() {
    mm_test_stdio_reset();
    auto& console = mm::stdio::selected_console();
    expect(console.initialize() == Status::Ok, "the console initializes");
    mm_test_stdio_force(Status::Busy);

    std::size_t written = 0;
    expect(console.write(message, written) == Status::Busy,
           "a busy transport is reported rather than counted as a short write");
    expect(mm_test_stdio_accepted() == 0, "and nothing was sent");
    mm_test_stdio_force(Status::Ok);
}

// A lane whose closure reaches mm.stdio but whose platform binds no provider
// links against the fallback, which must answer rather than dereference nothing.
void the_unserved_fallback_is_safe() {
    mm::stdio::Console bare;
    std::array<std::byte, 1> buffer{};
    std::size_t count = 7;
    bool connected = true;

    expect(bare.initialize() == Status::Unsupported,
           "an unserved console answers rather than failing to link");
    expect(bare.write(message, count) == Status::Unsupported, "so does a write");
    expect(bare.read(buffer, count) == Status::Unsupported, "so does a read");
    expect(bare.flush() == Status::Unsupported, "so does a flush");
    expect(bare.connected(connected) == Status::Unsupported,
           "and it does not guess that something is listening");
}

const mm::test::case_ cases[] = {
    {"a whole message reports what it sent", &a_whole_message_reports_what_it_sent},
    {"a short write reports what it took", &a_short_write_reports_what_it_accepted},
    {"a disconnected console does not hang",
     &a_disconnected_console_accepts_nothing_and_does_not_hang},
    {"a caller may poll before writing", &a_caller_may_poll_before_writing},
    {"a read with nothing waiting", &a_read_with_nothing_waiting_is_ok_with_no_bytes},
    {"a read respects the caller span", &a_read_never_writes_past_the_caller_span},
    {"the lifecycle is explicit", &the_lifecycle_is_explicit},
    {"transport failure reaches the caller", &transport_failure_reaches_the_caller},
    {"the unserved fallback is safe", &the_unserved_fallback_is_safe},
};

const mm::test::registrar reg{"mm.stdio", cases};

}  // namespace
