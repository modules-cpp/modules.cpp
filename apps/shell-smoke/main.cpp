// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
#include <cstddef>
#include <span>
#include <string_view>

import mm.shell;

namespace {

// Implemented foundation subset of the acceptance storage profile.
// Additional storage classes (parser nodes, word fragments, variables,
// execution frames, and function/script arenas) land in subsequent change sets.
alignas(void*) char source_storage[512];
alignas(void*) std::byte expansion_scratch[512];
alignas(void*) mm::shell::CommandDescriptor command_storage[8];
alignas(void*) char capture_storage[256];
alignas(void*) char staged_output_storage[256];
alignas(void*) char staged_error_storage[256];
alignas(void*) std::byte transaction_scratch[256];

void smoke_handler(
    void* context,
    std::span<const std::string_view> args,
    mm::shell::CommandContext& context_view,
    mm::shell::CommandResult& result) {
    (void)context;
    (void)args;
    const auto res = context_view.io.out.write("shell-smoke ok\n");
    if (res == mm::shell::SinkResult::Accepted) {
        result.flow = mm::shell::Flow::Normal;
        result.status = 0;
        result.error = mm::shell::Status::Ok;
    } else if (res == mm::shell::SinkResult::WouldBlock) {
        result.flow = mm::shell::Flow::Yield;
        result.status = 0;
        result.error = mm::shell::Status::Ok;
    } else {
        const auto failure = context_view.io.out.failure();
        result.flow = mm::shell::Flow::Normal;
        result.status =
            static_cast<int>(mm::shell::CommandStatus::Failure);
        result.error = failure.error;
        result.overflow = failure.overflow;
    }
}

}  // namespace

int main() {
    (void)source_storage;
    (void)expansion_scratch;
    (void)capture_storage;

    mm::shell::MemorySink out_sink{staged_output_storage};
    mm::shell::MemorySink err_sink{staged_error_storage};

    mm::shell::IoServices io{
        .out = out_sink.sink(),
        .err = err_sink.sink(),
    };

    mm::shell::Registry registry(command_storage);

    const mm::shell::CommandDescriptor smoke_desc{
        .name = "smoke",
        .summary = "Smoke test command",
        .command_class = mm::shell::CommandClass::Custom,
        .required_capabilities = mm::shell::CapabilitySet::level1(),
        .handler = &smoke_handler,
        .context = nullptr,
    };

    if (registry.install(smoke_desc) != mm::shell::Status::Ok) {
        return 1;
    }

    const auto* installed = registry.find("smoke");
    if (installed == nullptr) {
        return 2;
    }

    const auto active_caps = mm::shell::CapabilitySet::level1();
    mm::shell::ShellState state;
    mm::shell::CommandContext ctx{
        .io = io,
        .state = state,
        .capabilities = active_caps,
        .scratch = transaction_scratch,
    };

    const std::string_view argv[] = { "smoke" };
    const auto res = mm::shell::dispatch(*installed, argv, ctx);
    if (res.status != 0) {
        return 3;
    }

    if (out_sink.view() != "shell-smoke ok\n") {
        return 4;
    }

    return 0;
}
