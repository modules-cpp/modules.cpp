// Tests for Pico firmware flashing orchestration.
//
// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
#include <string>
#include <utility>

import mm.build;
import mm.flash;
import mm.test;

namespace {

mm::build::Platform pico(std::string sdk, std::string board) {
    mm::build::Platform platform;
    platform.sdk = std::move(sdk);
    platform.board = std::move(board);
    return platform;
}

void recognizes_only_pico_sdk_platforms() {
    mm::test::expect(mm::flash::supports(pico("pico-arm", "pico")),
                     "expected the Pico Arm board to be supported");
    mm::test::expect(mm::flash::supports(pico("pico-arm", "pico-w")),
                     "expected the Pico W board to be supported");
    mm::test::expect(mm::flash::supports(pico("pico-riscv", "pico2-w-riscv")),
                     "expected the Pico 2 W RISC-V board to be supported");
    mm::test::expect(!mm::flash::supports(pico("arm-none-eabi-newlib", "rp2040-ram")),
                     "expected a project-linked RP2040 board not to use picotool");
    mm::test::expect(!mm::flash::supports(pico("pico-arm", "unknown")),
                     "expected an unknown Pico board to be rejected");
    mm::test::expect(!mm::flash::supports(pico("pico-riscv", "pico")),
                     "expected an incompatible Pico SDK/board pair to be rejected");
}

void derives_the_uf2_supplement() {
    mm::test::expect(mm::flash::image_for("out/apps/blink/blink") ==
                         "out/apps/blink/blink.uf2",
                     "expected the UF2 image beside the extensionless executable");
}

void constructs_a_safely_quoted_picotool_command() {
    const auto result = mm::flash::command("tool path/picotool", "image; false.uf2");
    mm::test::expect(result &&
                         *result == "'tool path/picotool' 'load' '-v' '-x' "
                                    "'image; false.uf2'",
                     "expected paths to remain single shell arguments");
    mm::test::expect(!mm::flash::command({}, "image.uf2"),
                     "expected an empty picotool path to be rejected");
}

void executes_without_touching_hardware_in_the_test() {
    const auto toolchain = mm::build::default_toolchain();
    mm::test::expect(mm::flash::execute(toolchain, "/bin/true", "image.uf2") == 0,
                     "expected the common process wrapper to return the tool status");
}

const mm::test::case_ cases[] = {
    {"recognizes only Pico SDK platforms", &recognizes_only_pico_sdk_platforms},
    {"derives UF2 supplement", &derives_the_uf2_supplement},
    {"constructs picotool command", &constructs_a_safely_quoted_picotool_command},
    {"executes without hardware", &executes_without_touching_hardware_in_the_test},
};

const mm::test::registrar reg{"mm.flash", cases};

}
