// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
#include <cstddef>
#include <span>
#include <string_view>

import mm.shell;
import mm.test;

namespace {

void memory_sink_write_and_overflow() {
    char buffer[10];
    mm::shell::MemorySink sink{buffer};
    const auto bs = sink.sink();

    const auto res1 = bs.write("hello");
    mm::test::expect(res1 == mm::shell::SinkResult::Accepted,
                     "write hello accepted");
    mm::test::expect(sink.view() == "hello", "view matches hello");

    const auto res2 = bs.write("!");
    mm::test::expect(res2 == mm::shell::SinkResult::Accepted,
                     "write ! accepted");
    mm::test::expect(sink.view() == "hello!", "view matches hello!");

    // Attempt to write beyond remaining capacity (size 10, used 6, need 5)
    const auto res3 = bs.write("12345");
    mm::test::expect(res3 == mm::shell::SinkResult::Failed,
                     "overflow write fails without partial write");
    mm::test::expect(sink.view() == "hello!",
                     "unsuccessful write leaves sink unmodified");

    sink.reset();
    mm::test::expect(sink.view().empty(), "reset clears view");
}

void io_services_independent_channels() {
    char out_buf[32];
    char err_buf[32];
    mm::shell::MemorySink out_sink{out_buf};
    mm::shell::MemorySink err_sink{err_buf};

    mm::shell::IoServices io{
        .out = out_sink.sink(),
        .err = err_sink.sink(),
    };

    mm::test::expect(io.out.write("output") == mm::shell::SinkResult::Accepted,
                     "out write accepted");
    mm::test::expect(io.err.write("error") == mm::shell::SinkResult::Accepted,
                     "err write accepted");

    mm::test::expect(out_sink.view() == "output", "out has output");
    mm::test::expect(err_sink.view() == "error", "err has error");
}

void two_independent_io_services() {
    char b1[16];
    char b2[16];
    mm::shell::MemorySink m1{b1};
    mm::shell::MemorySink m2{b2};

    mm::shell::IoServices io1{ .out = m1.sink(), .err = m1.sink() };
    mm::shell::IoServices io2{ .out = m2.sink(), .err = m2.sink() };

    (void)io1.out.write("A");
    (void)io2.out.write("B");

    mm::test::expect(m1.view() == "A", "m1 is A");
    mm::test::expect(m2.view() == "B", "m2 is B");
}

void byte_sink_results_and_null_rejection() {
    // Null callback fails rather than silently succeeding
    mm::shell::ByteSink null_sink;
    mm::test::expect(null_sink.write("data") == mm::shell::SinkResult::Failed,
                     "null callback write fails");
    mm::test::expect(null_sink.flush() == mm::shell::SinkResult::Failed,
                     "null callback flush fails");

    // DiscardSink explicitly accepts and discards
    const auto discard = mm::shell::DiscardSink::sink();
    mm::test::expect(
        discard.write("any data") == mm::shell::SinkResult::Accepted,
        "discard sink accepts write");
    mm::test::expect(
        discard.flush() == mm::shell::SinkResult::Accepted,
        "discard sink accepts flush");

    // Cooperative backpressure (WouldBlock) is distinct from Failed
    const mm::shell::ByteSink retry_sink{
        .context = nullptr,
        .write_fn = [](void*, std::span<const char>) {
            return mm::shell::SinkResult::WouldBlock;
        },
        .flush_fn = nullptr,
    };
    mm::test::expect(
        retry_sink.write("chunk") == mm::shell::SinkResult::WouldBlock,
        "would-block result carried to caller");
}

const mm::test::case_ cases[] = {
    { "memory sink write and overflow", &memory_sink_write_and_overflow },
    { "io services independent channels", &io_services_independent_channels },
    { "two independent io services", &two_independent_io_services },
    { "byte sink results and null rejection",
      &byte_sink_results_and_null_rejection },
};

const mm::test::registrar reg{"mm.shell io", cases};

}  // namespace
