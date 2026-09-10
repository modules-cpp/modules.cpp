// Tests for reusable host and target execution.
//
// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
#include <string>

import mm.build;
import mm.run;
import mm.test;

namespace {

void constructs_a_host_command() {
    const auto toolchain = mm::build::default_toolchain();
    const auto result = mm::run::command(toolchain, false, "host app", {"a; false"});
    mm::test::expect(result && *result == "'host app' 'a; false'",
                     "expected one safely quoted host command");
    mm::test::expect(!mm::run::command(toolchain, false, "host app", {}, {"runner-only"}),
                     "expected runner arguments to be rejected for a host command");
}

void constructs_a_target_runner_command() {
    auto toolchain = mm::build::default_toolchain();
    toolchain.runner = mm::build::ToolchainRunner{
        .invocation = "target runner",
        .prefix_arguments = {"--sysroot", "/target root"},
        .suffix_arguments = {"--suffix"},
    };
    const auto result = mm::run::command(toolchain, true, "guest image", {"a; false"},
                                          {"-g", "1234"});
    mm::test::expect(result &&
                         *result == "'target runner' '--sysroot' '/target root' '-g' '1234' "
                                    "'guest image' '--suffix' 'a; false'",
                     "expected configured and extension arguments in runner order");
}

void constructs_an_option_image_command_without_forwarding() {
    auto toolchain = mm::build::default_toolchain();
    toolchain.runner = mm::build::ToolchainRunner{
        .invocation = "runner",
        .image = mm::build::RunnerImage::Option,
        .image_option = "--image",
        .forwards_arguments = false,
    };
    const auto result = mm::run::command(toolchain, true, "guest", {"not-forwarded"});
    mm::test::expect(result && *result == "'runner' '--image' 'guest'",
                     "expected option image placement and forwarding policy");
}

void constructs_an_option_image_command_with_template() {
    auto toolchain = mm::build::default_toolchain();
    toolchain.runner = mm::build::ToolchainRunner{
        .invocation = "openocd",
        .prefix_arguments = {"-f", "interface/cmsis-dap.cfg", "-f", "target/rp2040.cfg",
                             "-c", "adapter speed 5000", "-c", "init", "-c", "arm semihosting enable"},
        .image = mm::build::RunnerImage::Option,
        .image_option = "-c \"program ... verify reset\"",
        .forwards_arguments = false,
    };
    const auto result = mm::run::command(toolchain, true, "out/bin/smoke", {"not-forwarded"});
    mm::test::expect(result &&
                         *result == "'openocd' '-f' 'interface/cmsis-dap.cfg' '-f' 'target/rp2040.cfg' "
                                    "'-c' 'adapter speed 5000' '-c' 'init' '-c' 'arm semihosting enable' "
                                    "'-c' 'program out/bin/smoke verify reset'",
                     "expected openocd template image expansion");
}

void executes_directly_or_through_a_runner() {
    auto toolchain = mm::build::default_toolchain();
    mm::test::expect(mm::run::execute(toolchain, false, "/bin/true") == 0,
                     "expected a host executable to run directly");
    mm::test::expect(mm::run::execute(toolchain, true, "/bin/true") == -1,
                     "expected a target without a runner to be unavailable");
    toolchain.runner = mm::build::ToolchainRunner{.invocation = "/bin/true"};
    mm::test::expect(mm::run::execute(toolchain, true, "guest image", {"a; false"}) == 0,
                     "expected a configured target runner to execute safely");
}

const mm::test::case_ cases[] = {
    {"constructs host command", &constructs_a_host_command},
    {"constructs target runner command", &constructs_a_target_runner_command},
    {"constructs option image command", &constructs_an_option_image_command_without_forwarding},
    {"constructs option image template command", &constructs_an_option_image_command_with_template},
    {"executes directly or through runner", &executes_directly_or_through_a_runner},
};

const mm::test::registrar reg{"mm.run", cases};

}
