// Tests for debugger orchestration.
//
// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
#include <string>

import mm.build;
import mm.debug;
import mm.test;

namespace {

void rejects_an_unconfigured_debugger() {
    const auto toolchain = mm::build::default_toolchain();
    mm::test::expect(mm::debug::execute(toolchain, false, "/bin/true") == -1,
                     "expected an unconfigured debugger to be unavailable");
}

void starts_a_direct_host_debugger() {
    auto toolchain = mm::build::default_toolchain();
    toolchain.debugger = mm::build::ToolchainDebugger{.invocation = "/bin/true"};
    mm::test::expect(mm::debug::execute(toolchain, false, "application", {"a; false"}) == 0,
                     "expected direct debugger arguments to be safely quoted");
    mm::test::expect(mm::debug::execute(toolchain, true, "application") == -1,
                     "expected a direct debugger not to debug a target lane");
}

void starts_a_runner_remote_debugger() {
    auto toolchain = mm::build::default_toolchain();
    toolchain.debugger = mm::build::ToolchainDebugger{
        .invocation = "/bin/true",
        .connection = mm::build::DebuggerConnection::RunnerRemote,
        .remote_endpoint = "localhost:1234",
        .runner_arguments = {"-g", "1234"},
    };
    toolchain.runner = mm::build::ToolchainRunner{.invocation = "/bin/true"};
    mm::test::expect(mm::debug::execute(toolchain, true, "guest image", {"a; false"}) == 0,
                     "expected a remote debugger to start through the target runner");
    mm::test::expect(mm::debug::execute(toolchain, false, "guest image") == -1,
                     "expected runner-remote debugging not to apply to the host lane");
}

const mm::test::case_ cases[] = {
    {"rejects unconfigured debugger", &rejects_an_unconfigured_debugger},
    {"starts direct host debugger", &starts_a_direct_host_debugger},
    {"starts runner remote debugger", &starts_a_runner_remote_debugger},
};

const mm::test::registrar reg{"mm.debug", cases};

}
