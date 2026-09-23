// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
#include <cstddef>
#include <span>
#include <string_view>

import mm.shell;
import mm.test;

namespace {

struct FakeDevice {
    bool side_effect_performed = false;
    std::size_t required_bytes = 4;
};

void transaction_handler(
    void* ctx,
    std::span<const std::string_view> args,
    mm::shell::CommandContext& context,
    mm::shell::CommandResult& result) {
    (void)args;
    auto* dev = static_cast<FakeDevice*>(ctx);

    // Preflight transaction scratch capacity before any side effect or output!
    if (context.scratch.size() < dev->required_bytes) {
        result.flow = mm::shell::Flow::Normal;
        result.status = static_cast<int>(mm::shell::CommandStatus::Failure);
        result.error = mm::shell::Status::Overflow;
        result.overflow = mm::shell::OverflowInfo{
            .storage_class = mm::shell::StorageClass::TransactionScratch,
            .required = dev->required_bytes,
        };
        return;
    }

    // Capacity is sufficient: perform physical side effect and write output
    dev->side_effect_performed = true;
    (void)context.io.out.write("ok\n");
    result.flow = mm::shell::Flow::Normal;
    result.status = 0;
    result.error = mm::shell::Status::Ok;
}

void scratch_capacity_and_overflow_protection() {
    FakeDevice device;
    char out_buf[32];
    mm::shell::MemorySink out_sink{out_buf};
    mm::shell::IoServices io{
        .out = out_sink.sink(),
        .err = out_sink.sink(),
    };
    const auto all_caps = mm::shell::CapabilitySet::all();

    mm::shell::CommandDescriptor desc{
        .name = "transact",
        .required_capabilities = all_caps,
        .handler = &transaction_handler,
        .context = &device,
    };

    const std::string_view argv[] = { "transact" };
    mm::shell::ShellState state;

    // 1. Empty scratch span
    {
        device.side_effect_performed = false;
        out_sink.reset();
        mm::shell::CommandContext ctx{
            .io = io,
            .state = state,
            .capabilities = all_caps,
            .scratch = {},
        };
        const auto res = mm::shell::dispatch(desc, argv, ctx);
        mm::test::expect(
            res.status == static_cast<int>(mm::shell::CommandStatus::Failure),
            "empty scratch reports exit failure");
        mm::test::expect(res.error == mm::shell::Status::Overflow,
                         "empty scratch reports Status::Overflow error");
        mm::test::expect(
            res.overflow.storage_class ==
                mm::shell::StorageClass::TransactionScratch,
            "overflow storage class is TransactionScratch");
        mm::test::expect(res.overflow.required == device.required_bytes,
                         "overflow required matches device");
        mm::test::expect(!device.side_effect_performed,
                         "no side effect with empty scratch");
        mm::test::expect(out_sink.view().empty(),
                         "no output with empty scratch");
    }

    // 2. One-byte-short scratch span (required 4, provide 3)
    {
        std::byte scratch3[3];
        device.side_effect_performed = false;
        out_sink.reset();
        mm::shell::CommandContext ctx{
            .io = io,
            .state = state,
            .capabilities = all_caps,
            .scratch = scratch3,
        };
        const auto res = mm::shell::dispatch(desc, argv, ctx);
        mm::test::expect(
            res.status == static_cast<int>(mm::shell::CommandStatus::Failure),
            "one-byte-short scratch reports exit failure");
        mm::test::expect(res.error == mm::shell::Status::Overflow,
                         "one-byte-short scratch reports overflow error");
        mm::test::expect(
            res.overflow.storage_class ==
                mm::shell::StorageClass::TransactionScratch,
            "overflow storage class is TransactionScratch");
        mm::test::expect(res.overflow.required == device.required_bytes,
                         "overflow required matches device");
        mm::test::expect(!device.side_effect_performed,
                         "no side effect with one-byte-short scratch");
        mm::test::expect(out_sink.view().empty(),
                         "no output with one-byte-short scratch");
    }

    // 3. Exact scratch span (required 4, provide 4)
    {
        std::byte scratch4[4];
        device.side_effect_performed = false;
        out_sink.reset();
        mm::shell::CommandContext ctx{
            .io = io,
            .state = state,
            .capabilities = all_caps,
            .scratch = scratch4,
        };
        const auto res = mm::shell::dispatch(desc, argv, ctx);
        mm::test::expect(res.status == 0, "exact scratch succeeds");
        mm::test::expect(res.error == mm::shell::Status::Ok, "error is ok");
        mm::test::expect(device.side_effect_performed,
                         "side effect performed with exact scratch");
        mm::test::expect(out_sink.view() == "ok\n",
                         "output written with exact scratch");
    }

    // 4. Larger scratch span (required 4, provide 16)
    {
        std::byte scratch16[16];
        device.side_effect_performed = false;
        out_sink.reset();
        mm::shell::CommandContext ctx{
            .io = io,
            .state = state,
            .capabilities = all_caps,
            .scratch = scratch16,
        };
        const auto res = mm::shell::dispatch(desc, argv, ctx);
        mm::test::expect(res.status == 0, "larger scratch succeeds");
        mm::test::expect(res.error == mm::shell::Status::Ok, "error is ok");
        mm::test::expect(device.side_effect_performed,
                         "side effect performed with larger scratch");
        mm::test::expect(out_sink.view() == "ok\n",
                         "output written with larger scratch");
    }
}

void capability_refusal_before_handler() {
    FakeDevice device;
    char out_buf[32];
    mm::shell::MemorySink out_sink{out_buf};
    mm::shell::IoServices io{
        .out = out_sink.sink(),
        .err = out_sink.sink(),
    };

    mm::shell::CapabilitySet required_caps;
    required_caps.set(mm::shell::Capability::Gpio);

    mm::shell::CommandDescriptor desc{
        .name = "gpio_cmd",
        .required_capabilities = required_caps,
        .handler = &transaction_handler,
        .context = &device,
    };

    const std::string_view argv[] = { "gpio_cmd" };
    std::byte scratch[16];
    mm::shell::ShellState state;

    // Capabilities provided do NOT include Gpio
    const mm::shell::CapabilitySet active_caps =
        mm::shell::CapabilitySet::level1();
    mm::shell::CommandContext ctx{
        .io = io,
        .state = state,
        .capabilities = active_caps,
        .scratch = scratch,
    };

    const auto res = mm::shell::dispatch(desc, argv, ctx);
    mm::test::expect(
        res.status == static_cast<int>(mm::shell::CommandStatus::Unavailable),
        "dispatch refuses missing capability with Unavailable");
    mm::test::expect(res.error == mm::shell::Status::Unavailable,
                     "error is Status::Unavailable");
    mm::test::expect(!device.side_effect_performed,
                     "handler not entered when capability missing");
    mm::test::expect(out_sink.view().empty(),
                     "no output when capability missing");
}

void dispatch_argv_validation() {
    FakeDevice device;
    char out_buf[32];
    mm::shell::MemorySink out_sink{out_buf};
    mm::shell::IoServices io{
        .out = out_sink.sink(),
        .err = out_sink.sink(),
    };
    std::byte scratch[16];
    mm::shell::ShellState state;

    mm::shell::CommandDescriptor desc{
        .name = "mycmd",
        .required_capabilities = mm::shell::CapabilitySet::all(),
        .handler = &transaction_handler,
        .context = &device,
    };

    mm::shell::CommandContext ctx{
        .io = io,
        .state = state,
        .capabilities = mm::shell::CapabilitySet::all(),
        .scratch = scratch,
    };

    // 1. Empty args span rejected
    {
        device.side_effect_performed = false;
        const auto res = mm::shell::dispatch(desc, {}, ctx);
        mm::test::expect(
            res.status == static_cast<int>(mm::shell::CommandStatus::Usage),
            "empty args rejected with Usage");
        mm::test::expect(res.error == mm::shell::Status::BadArgument,
                         "empty args error is BadArgument");
        mm::test::expect(!device.side_effect_performed,
                         "handler not called on empty args");
    }

    // 2. Argv[0] mismatch rejected
    {
        device.side_effect_performed = false;
        const std::string_view bad_argv[] = { "other_cmd" };
        const auto res = mm::shell::dispatch(desc, bad_argv, ctx);
        mm::test::expect(
            res.status == static_cast<int>(mm::shell::CommandStatus::Usage),
            "argv[0] mismatch rejected with Usage");
        mm::test::expect(res.error == mm::shell::Status::BadArgument,
                         "argv[0] mismatch error is BadArgument");
        mm::test::expect(!device.side_effect_performed,
                         "handler not called on argv[0] mismatch");
    }
}

const mm::test::case_ cases[] = {
    { "scratch capacity and overflow protection",
      &scratch_capacity_and_overflow_protection },
    { "capability refusal before handler",
      &capability_refusal_before_handler },
    { "dispatch argv validation",
      &dispatch_argv_validation },
};

const mm::test::registrar reg{"mm.shell command context", cases};

}  // namespace
