#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

import mm.build;
import mm.configure;
import mm.test;

namespace {

using mm::test::expect;

void inputs_cmake_escaping_and_validation() {
    const mm::test::scoped_tree tree{"inputs_cmake_test"};
    const auto inputs_file = tree.root() / "mm-inputs.cmake";

    std::vector<std::filesystem::path> objects = {
        tree.root() / "obj1.o",
        tree.root() / "obj2.o"
    };
    const auto toolchain_file = tree.root() / "mm-toolchain.cmake";
    const auto c_compiler = tree.root() / "bin/fixture-gcc";
    const auto cxx_compiler = tree.root() / "bin/fixture-g++";

    // Valid inputs
    expect(mm::build::write_inputs_cmake(
               inputs_file, objects, "smoke", tree.root() / "lib", "pico",
               {"pico"}, toolchain_file, c_compiler, cxx_compiler),
           "valid inputs succeed");

    std::ifstream in(inputs_file);
    std::string content((std::istreambuf_iterator<char>(in)),
                        std::istreambuf_iterator<char>());
    expect(content.find("set(MM_OUTPUT_NAME [==[smoke]==])") != std::string::npos,
           "output name uses bracket syntax");
    expect(content.find("set(MM_BOARD [==[pico]==])\nset(MM_BOARD_CHAIN\n  [==[pico]==]\n)\n") != std::string::npos,
           "board name and chain emitted byte-for-byte");
    expect(content.find("set(MM_TOOLCHAIN_FILE [==[") != std::string::npos &&
               content.find("mm-toolchain.cmake]==])") != std::string::npos,
           "generated toolchain path is a generic bridge input");
    expect(content.find("set(MM_C_COMPILER [==[") != std::string::npos &&
               content.find("fixture-gcc]==])") != std::string::npos &&
               content.find("set(MM_CXX_COMPILER [==[") != std::string::npos &&
               content.find("fixture-g++]==])") != std::string::npos,
           "canonical compiler paths are generic bridge inputs");
    expect(content.find("obj1.o]==]") != std::string::npos &&
           content.find("obj2.o]==]") != std::string::npos,
           "objects use bracket syntax");

    // Multi-element board chain asserted byte-for-byte
    expect(mm::build::write_inputs_cmake(
               inputs_file, objects, "smoke", tree.root() / "lib", "widget-pico",
               {"widget-pico", "pico"}, toolchain_file, c_compiler, cxx_compiler),
           "multi-element board chain succeeds");
    std::ifstream in_chain(inputs_file);
    std::string content_chain((std::istreambuf_iterator<char>(in_chain)),
                              std::istreambuf_iterator<char>());
    expect(content_chain.find("set(MM_BOARD [==[widget-pico]==])\nset(MM_BOARD_CHAIN\n  [==[widget-pico]==]\n  [==[pico]==]\n)\n") != std::string::npos,
           "multi-element board chain emitted byte-for-byte");

    // Boardless lane asserted byte-for-byte
    expect(mm::build::write_inputs_cmake(
               inputs_file, objects, "smoke", tree.root() / "lib", "",
               {}, toolchain_file, c_compiler, cxx_compiler),
           "boardless lane succeeds");
    std::ifstream in_empty(inputs_file);
    std::string content_empty((std::istreambuf_iterator<char>(in_empty)),
                              std::istreambuf_iterator<char>());
    expect(content_empty.find("set(MM_BOARD [==[]==])\nset(MM_BOARD_CHAIN)\n") != std::string::npos,
           "boardless lane emits empty board and empty chain list byte-for-byte");

    // Semicolon in output_name
    expect(!mm::build::write_inputs_cmake(
               inputs_file, objects, "bad;name", tree.root() / "lib", "pico",
               {"pico"}, toolchain_file, c_compiler, cxx_compiler),
           "semicolon in output name is rejected");

    // Semicolon in board_name
    expect(!mm::build::write_inputs_cmake(
               inputs_file, objects, "smoke", tree.root() / "lib", "bad;board",
               {"bad;board"}, toolchain_file, c_compiler, cxx_compiler),
           "semicolon in board name is rejected");

    // Semicolon in board_chain entry
    expect(!mm::build::write_inputs_cmake(
               inputs_file, objects, "smoke", tree.root() / "lib", "widget-pico",
               {"widget-pico", "bad;entry"}, toolchain_file, c_compiler, cxx_compiler),
           "semicolon in board chain entry is rejected");

    // Newline in board_chain entry
    expect(!mm::build::write_inputs_cmake(
               inputs_file, objects, "smoke", tree.root() / "lib", "widget-pico",
               {"widget-pico", "bad\nentry"}, toolchain_file, c_compiler, cxx_compiler),
           "newline in board chain entry is rejected");

    // Semicolon in object path
    std::vector<std::filesystem::path> bad_objects = {
        tree.root() / "bad;obj.o"
    };
    expect(!mm::build::write_inputs_cmake(
               inputs_file, bad_objects, "smoke", tree.root() / "lib", "pico",
               {"pico"}, toolchain_file, c_compiler, cxx_compiler),
           "semicolon in object path is rejected");

    // Semicolon in library source
    expect(!mm::build::write_inputs_cmake(
               inputs_file, objects, "smoke", tree.root() / "bad;lib", "pico",
               {"pico"}, toolchain_file, c_compiler, cxx_compiler),
           "semicolon in library source path is rejected");

    // Newline in output_name
    expect(!mm::build::write_inputs_cmake(
               inputs_file, objects, "bad\nname", tree.root() / "lib", "pico",
               {"pico"}, toolchain_file, c_compiler, cxx_compiler),
           "newline in output name is rejected");
}

void cmake_package_requirements() {
    const mm::test::scoped_tree tree{"cmake_package_requirements"};
    const auto bridge = tree.root() / "renamed-library/cmake";
    const auto requirements_file = bridge / "mm-requires.txt";
    const auto package = tree.root() / "package";
    std::filesystem::create_directories(bridge);
    std::filesystem::create_directories(package);

    std::vector<mm::build::CMakePackageRequirement> requirements;
    expect(mm::build::read_cmake_package_requirements(bridge, requirements, "test") &&
               requirements.empty(),
           "a bridge with no requirements file has no package requirements");

    std::ofstream(requirements_file) << "MM_TEST_PACKAGE_DIR\n";
    ::unsetenv("MM_TEST_PACKAGE_DIR");
    expect(!mm::build::read_cmake_package_requirements(bridge, requirements, "test"),
           "an unset required package variable is rejected");

    ::setenv("MM_TEST_PACKAGE_DIR", (tree.root() / "missing").c_str(), 1);
    expect(!mm::build::read_cmake_package_requirements(bridge, requirements, "test"),
           "a required package variable naming a missing directory is rejected");

    ::setenv("MM_TEST_PACKAGE_DIR", package.c_str(), 1);
    expect(!mm::build::read_cmake_package_requirements(bridge, requirements, "test"),
           "a required package directory without its config file is rejected");

    const auto config = package / "MM_TEST_PACKAGEConfig.cmake";
    std::filesystem::create_directory(config);
    expect(!mm::build::read_cmake_package_requirements(bridge, requirements, "test"),
           "a required config-package spelling must name a regular file");
    std::filesystem::remove(config);
    std::ofstream(config) << "set(MM_TEST_PACKAGE_FOUND TRUE)\n";
    expect(mm::build::read_cmake_package_requirements(bridge, requirements, "test") &&
               requirements.size() == 1 &&
               requirements.front().variable == "MM_TEST_PACKAGE_DIR" &&
               requirements.front().directory == std::filesystem::canonical(package),
           "requirements work under a renamed library and canonicalise the package path");

    std::filesystem::remove(config);
    std::ofstream(package / "MM_TEST_PACKAGE-config.cmake")
        << "set(MM_TEST_PACKAGE_FOUND TRUE)\n";
    expect(mm::build::read_cmake_package_requirements(bridge, requirements, "test"),
           "lowercase config-package spelling is accepted generically");

    std::ofstream(requirements_file) << "bad-name_DIR\n";
    expect(!mm::build::read_cmake_package_requirements(bridge, requirements, "test"),
           "invalid CMake variable syntax is rejected");
    std::ofstream(requirements_file) << "MM_TEST_PACKAGE_DIR\nMM_TEST_PACKAGE_DIR\n";
    expect(!mm::build::read_cmake_package_requirements(bridge, requirements, "test"),
           "duplicate package requirements are rejected");
    ::unsetenv("MM_TEST_PACKAGE_DIR");
}

void projection_schemas() {
    const auto* arm_schema = mm::build::find_projection_schema("arm-none-eabi");
    expect(arm_schema != nullptr, "arm-none-eabi schema exists");
    expect(arm_schema->fields.size() == 5, "arm-none-eabi has 5 fields");
    expect(arm_schema->fields[0] == "-march=" &&
           arm_schema->fields[1] == "-mthumb" &&
           arm_schema->fields[2] == "-mfloat-abi=" &&
           arm_schema->fields[3] == "-mfpu=" &&
           arm_schema->fields[4] == "-mcmse",
           "arm-none-eabi schema fields match");

    const auto* m68k_schema = mm::build::find_projection_schema("m68k-linux-gnu");
    expect(m68k_schema != nullptr, "m68k-linux-gnu schema exists");
    expect(m68k_schema->fields.size() == 5, "m68k-linux-gnu has 5 fields");
    expect(m68k_schema->fields[0] == "-march=" &&
           m68k_schema->fields[1] == "-mcpu=" &&
           m68k_schema->fields[2] == "-m68881" &&
           m68k_schema->fields[3] == "-mhard-float" &&
           m68k_schema->fields[4] == "-msoft-float",
           "m68k-linux-gnu schema fields match");

    // -mcpu= is deliberately absent: naming an architecture leaves it empty,
    // and Pico SDK may name a CPU where the project names an architecture.
    const auto* riscv_schema = mm::build::find_projection_schema("riscv32-pico-elf");
    expect(riscv_schema != nullptr, "riscv32-pico-elf schema exists");
    expect(riscv_schema->fields.size() == 2, "riscv32-pico-elf has 2 fields");
    expect(riscv_schema->fields[0] == "-march=" && riscv_schema->fields[1] == "-mabi=",
           "riscv32-pico-elf schema fields match");

    const auto* unknown = mm::build::find_projection_schema("x86_64-linux-gnu");
    expect(unknown == nullptr, "unknown target triple returns nullptr");
}

void query_driver_projection_live() {
    const auto* m68k_schema = mm::build::find_projection_schema("m68k-linux-gnu");
    expect(m68k_schema != nullptr, "m68k schema exists");

    std::map<std::string, std::string> projection;
    const bool ok = mm::build::query_driver_projection(
        "m68k-linux-gnu-gcc", {}, *m68k_schema, projection, "test");
    if (ok) {
        expect(projection.size() == 5, "all 5 schema fields parsed");
        expect(projection.count("-march=") == 1, "-march= present");
        expect(projection.count("-mcpu=") == 1, "-mcpu= present");
        expect(projection.count("-m68881") == 1, "-m68881 present");
        expect(projection.count("-mhard-float") == 1, "-mhard-float present");
        expect(projection.count("-msoft-float") == 1, "-msoft-float present");
    }
}

void projection_accepts_present_empty_value() {
    const mm::build::ProjectionSchema schema{
        "fixture", {"-march=", "-mcpu=", "-mhard-float"}};
    std::map<std::string, std::string> projection;
    expect(mm::build::parse_driver_projection(
               "  -march= m68020\n  -mcpu=\t\n  -mhard-float [disabled]\n",
               schema, projection, "test"),
           "present empty value is a valid projection field");
    expect(projection.contains("-mcpu=") && projection["-mcpu="].empty(),
           "empty projection value remains distinguishable from a missing field");
    expect(!mm::build::parse_driver_projection(
               "  -march= m68020\n  -mhard-float [disabled]\n",
               schema, projection, "test"),
           "an absent value field remains an error");
}

void publish_results_validation() {
    const mm::test::scoped_tree tree{"publish_results_test"};
    const auto external_dir = tree.root() / "external";
    const auto target_output = tree.root() / "out-target" / "app";
    std::filesystem::create_directories(external_dir);
    std::filesystem::create_directories(target_output.parent_path());

    const auto results_file = external_dir / "mm-result.txt";

    // 1. Missing results file
    expect(!mm::build::publish_external_results(
               results_file, external_dir, "app", target_output, "test"),
           "missing results file is rejected");

    // 2. Empty results file
    {
        std::ofstream out(results_file);
        out << "   \n\n";
    }
    expect(!mm::build::publish_external_results(
               results_file, external_dir, "app", target_output, "test"),
           "empty results file is rejected");

    // 3. First line empty after stripping trailing blanks
    {
        std::ofstream out(results_file);
        out << "\n/some/path\n";
    }
    expect(!mm::build::publish_external_results(
               results_file, external_dir, "app", target_output, "test"),
           "results file with empty line 1 is rejected");

    // 4. Relative path
    {
        std::ofstream out(results_file);
        out << "relative/path/app\n";
    }
    expect(!mm::build::publish_external_results(
               results_file, external_dir, "app", target_output, "test"),
           "relative path is rejected");

    // 5. Path outside external_dir
    const auto outside_file = tree.root() / "outside.elf";
    {
        std::ofstream out(outside_file);
        out << "data";
        std::ofstream res(results_file);
        res << outside_file.string() << "\n";
    }
    expect(!mm::build::publish_external_results(
               results_file, external_dir, "app", target_output, "test"),
           "path outside external dir is rejected");

    // 6. Non-existent file
    const auto non_existent = external_dir / "non_existent";
    {
        std::ofstream res(results_file);
        res << non_existent.string() << "\n";
    }
    expect(!mm::build::publish_external_results(
               results_file, external_dir, "app", target_output, "test"),
           "non-existent file is rejected");

    // 7. Directory instead of regular file
    const auto sub_dir = external_dir / "sub_dir";
    std::filesystem::create_directories(sub_dir);
    {
        std::ofstream res(results_file);
        res << sub_dir.string() << "\n";
    }
    expect(!mm::build::publish_external_results(
               results_file, external_dir, "app", target_output, "test"),
           "directory path is rejected");

    // 8. Interior blank line
    const auto primary = external_dir / "app";
    const auto supplemental = external_dir / "app.uf2";
    {
        std::ofstream p(primary);
        p << "primary";
        std::ofstream s(supplemental);
        s << "supplemental";
        std::ofstream res(results_file);
        res << primary.string() << "\n\n" << supplemental.string() << "\n";
    }
    expect(!mm::build::publish_external_results(
               results_file, external_dir, "app", target_output, "test"),
           "interior blank line is rejected");

    // 9. Supplemental does not start with output_name
    const auto bad_supp = external_dir / "other.uf2";
    {
        std::ofstream bs(bad_supp);
        bs << "bad";
        std::ofstream res(results_file);
        res << primary.string() << "\n" << bad_supp.string() << "\n";
    }
    expect(!mm::build::publish_external_results(
               results_file, external_dir, "app", target_output, "test"),
           "supplemental not starting with output_name is rejected");

    // 10. Supplemental with empty suffix / duplicate source path
    {
        std::ofstream res(results_file);
        res << primary.string() << "\n" << primary.string() << "\n";
    }
    expect(!mm::build::publish_external_results(
               results_file, external_dir, "app", target_output, "test"),
           "duplicate source path is rejected");

    // 11. A component beginning with two dots remains inside the external tree.
    const auto dots_dir = external_dir / "..artifacts";
    const auto dots_primary = dots_dir / "app";
    std::filesystem::create_directories(dots_dir);
    {
        std::ofstream dp(dots_primary);
        dp << "primary";
        std::ofstream res(results_file);
        res << dots_primary.string() << "\n";
    }
    expect(mm::build::publish_external_results(
               results_file, external_dir, "app", target_output, "test"),
           "a contained path whose component begins with two dots is accepted");

    // 12. Valid primary + supplemental with trailing newlines
    {
        std::ofstream res(results_file);
        res << primary.string() << "\n" << supplemental.string() << "\n\n\n";
    }
    expect(mm::build::publish_external_results(
               results_file, external_dir, "app", target_output, "test"),
           "valid primary and supplemental publish cleanly");

    expect(std::filesystem::exists(target_output),
           "primary published extensionless at target_output");
    expect(std::filesystem::exists(target_output.parent_path() / "app.uf2"),
           "supplemental published at target_output parent / app.uf2");
}

void bridge_board_chain_resolution() {
    const mm::test::scoped_tree tree{"chain_resolution_test"};
    const auto script = tree.root() / "resolve.cmake";
    const auto resolve_cmake =
        std::filesystem::current_path() / "platforms/pico/sdk/pico-sdk/cmake/resolve-board.cmake";
    const auto run_resolve = [&](const std::string& cmake_inputs) -> std::pair<int, std::string> {
        std::ofstream out(script);
        out << cmake_inputs << "\n";
        out << "include(\"" << resolve_cmake.generic_string() << "\")\n";
        out << "message(STATUS \"RESOLVED: ${MM_VENDOR_BOARD}\")\n";
        out.close();

        const std::string command = "cmake -P " + script.string() + " 2>&1";
        FILE* pipe = ::popen(command.c_str(), "r");
        if (!pipe) return {-1, ""};
        std::string output;
        char buffer[256];
        while (std::fgets(buffer, sizeof(buffer), pipe) != nullptr) output += buffer;
        const int status = ::pclose(pipe);
        return {status, output};
    };

    // 1. Direct vendor board
    {
        const auto [status, output] = run_resolve("set(MM_BOARD_CHAIN [=[pico]=])");
        expect(status == 0, "direct vendor board succeeds");
        expect(output.find("RESOLVED: pico") != std::string::npos, "resolves to pico");
    }

    // 2. Custom board derived from pico
    {
        const auto [status, output] = run_resolve(
            "set(MM_BOARD_CHAIN\n  [=[widget-pico]=]\n  [=[pico]=]\n)\n");
        expect(status == 0, "derived board succeeds");
        expect(output.find("RESOLVED: pico") != std::string::npos, "resolves to pico");
    }

    // 3. Multi-level derivation: nearest vendor base wins
    {
        const auto [status, output] = run_resolve(
            "set(MM_BOARD_CHAIN\n  [=[leaf]=]\n  [=[custom]=]\n  [=[pico2-arm]=]\n  [=[pico]=]\n)\n");
        expect(status == 0, "multi-level derivation succeeds");
        expect(output.find("RESOLVED: pico2-arm") != std::string::npos,
               "nearest vendor base wins over distant ancestor");
    }

    // 4. Unrecognised board chain raises fatal error naming the chain
    {
        const auto [status, output] = run_resolve(
            "set(MM_BOARD_CHAIN\n  [=[custom-board]=]\n  [=[unknown-base]=]\n)\n");
        expect(status != 0, "unrecognised chain fails");
        expect(output.find("pico-sdk bridge recognises no board in chain: custom-board;unknown-base") != std::string::npos,
               "fatal error names the chain");
    }

    // 5. Boardless lane (empty chain) raises fatal error
    {
        const auto [status, output] = run_resolve("set(MM_BOARD_CHAIN)\n");
        expect(status != 0, "boardless empty chain fails");
        expect(output.find("pico-sdk bridge recognises no board in chain:") != std::string::npos,
               "empty chain raises fatal error");
    }
}

// The compile database is read through mm.json, so an escaped path
// resolves to the file it names and a malformed document is refused where
// it breaks rather than read past.
void compile_database_escaped_probe_path() {
    const mm::test::scoped_tree tree{"compile_database_escape"};
    const auto driver = tree.root() / "bin/fixture-gcc";
    std::filesystem::create_directories(driver.parent_path());
    std::ofstream(driver) << "";
    const auto probe = tree.root() / "probe.c";
    std::ofstream(probe) << "int main(void) { return 0; }\n";
    std::error_code ec;
    const auto canonical_probe = std::filesystem::canonical(probe, ec);
    expect(!ec, "probe fixture resolves");

    // "pr\u006fbe.c" spells probe.c; a reader that copied the escape
    // through as text would match no entry.
    const auto database = tree.root() / "compile_commands.json";
    std::ofstream(database)
        << "[\n"
        << "  {\n"
        << "    \"directory\": \"" << tree.root().generic_string() << "\",\n"
        << "    \"command\": \"" << driver.generic_string()
        << " -mcpu=cortex-m33 -O2 -mthumb -c probe.c\",\n"
        << "    \"file\": \"pr\\u006fbe.c\"\n"
        << "  }\n"
        << "]\n";

    std::vector<std::string> options;
    expect(mm::build::extract_probe_options(database, canonical_probe,
                                            driver.string(), options, "test"),
           "an escaped probe path matches its entry");
    expect(options.size() == 2 && options[0] == "-mcpu=cortex-m33" &&
               options[1] == "-mthumb",
           "only the -m options of the matching command are kept");
}

void compile_database_malformed_is_reported() {
    const mm::test::scoped_tree tree{"compile_database_malformed"};
    const auto probe = tree.root() / "probe.c";
    std::ofstream(probe) << "";
    std::error_code ec;
    const auto canonical_probe = std::filesystem::canonical(probe, ec);
    expect(!ec, "probe fixture resolves");

    const auto database = tree.root() / "compile_commands.json";
    std::ofstream(database)
        << "[\n"
        << "  {\n"
        << "    \"file\": \"probe.c\",\n"
        << "  }\n"
        << "]\n";

    std::vector<std::string> options{"-mkept"};
    std::ostringstream captured;
    auto* prev = std::cerr.rdbuf(captured.rdbuf());
    const bool accepted = mm::build::extract_probe_options(
        database, canonical_probe, "fixture-gcc", options, "test");
    std::cerr.rdbuf(prev);
    expect(!accepted, "a malformed compile database is refused");
    expect(captured.str().find("compile_commands.json:4:3: ") != std::string::npos,
           "the refusal names the line and column of the fault");
    expect(options.size() == 1 && options[0] == "-mkept",
           "the options are unchanged when the database is refused");
}

void writes_toolchain_cmake_bare_metal_and_linux() {
    const mm::test::scoped_tree tree{"build_toolchain_cmake"};

    mm::build::Toolchain toolchain;
    toolchain.compiler.invocation = "arm-none-eabi-g++";
    toolchain.c_compiler.invocation = "arm-none-eabi-gcc";

    mm::build::Platform bare_metal;
    bare_metal.system = mm::configure::PlatformSystem::BareMetal;

    const auto bm_path = tree.root() / "bm" / "mm-toolchain.cmake";
    mm::test::expect(mm::build::write_toolchain_cmake(bm_path, toolchain, bare_metal),
                     "expected bare metal toolchain cmake write to succeed");

    std::ifstream bm_file(bm_path);
    std::string bm_content((std::istreambuf_iterator<char>(bm_file)),
                            std::istreambuf_iterator<char>());
    mm::test::expect(bm_content.find("set(CMAKE_SYSTEM_NAME Generic)") != std::string::npos,
                     "expected CMAKE_SYSTEM_NAME Generic for bare metal");
    mm::test::expect(bm_content.find("set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)") != std::string::npos,
                     "expected CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY");
    mm::test::expect(bm_content.find("set(CMAKE_C_COMPILER [==[arm-none-eabi-gcc]==])") != std::string::npos,
                     "expected CMAKE_C_COMPILER");
    mm::test::expect(bm_content.find("set(CMAKE_CXX_COMPILER [==[arm-none-eabi-g++]==])") != std::string::npos,
                     "expected CMAKE_CXX_COMPILER");
    mm::test::expect(bm_content.find("CMAKE_SYSROOT") == std::string::npos,
                     "expected no sysroot when none declared");
    mm::test::expect(bm_content.find("-mcpu") == std::string::npos &&
                     bm_content.find("-mthumb") == std::string::npos,
                     "expected no processor flags in toolchain.cmake");

    mm::build::Platform linux_platform;
    linux_platform.system = mm::configure::PlatformSystem::Linux;
    linux_platform.sysroot = "/opt/sysroot";

    const auto linux_path = tree.root() / "linux" / "mm-toolchain.cmake";
    mm::test::expect(mm::build::write_toolchain_cmake(linux_path, toolchain, linux_platform),
                     "expected linux toolchain cmake write to succeed");

    std::ifstream linux_file(linux_path);
    std::string linux_content((std::istreambuf_iterator<char>(linux_file)),
                               std::istreambuf_iterator<char>());
    mm::test::expect(linux_content.find("set(CMAKE_SYSTEM_NAME Linux)") != std::string::npos,
                     "expected CMAKE_SYSTEM_NAME Linux for hosted lane");
    mm::test::expect(linux_content.find("set(CMAKE_SYSROOT [==[/opt/sysroot]==])") != std::string::npos,
                     "expected bracketed CMAKE_SYSROOT");

    toolchain.c_compiler.invocation.clear();
    const auto fail_path = tree.root() / "fail" / "mm-toolchain.cmake";
    mm::test::expect(!mm::build::write_toolchain_cmake(fail_path, toolchain, linux_platform),
                     "expected failure when C compiler is missing");
}

const mm::test::case_ cases[] = {
    {"inputs cmake escaping and validation", &inputs_cmake_escaping_and_validation},
    {"bridge board chain resolution", &bridge_board_chain_resolution},
    {"cmake package requirements", &cmake_package_requirements},
    {"projection schemas", &projection_schemas},
    {"query driver projection live", &query_driver_projection_live},
    {"projection accepts present empty value", &projection_accepts_present_empty_value},
    {"publish results validation", &publish_results_validation},
    {"compile database escaped probe path", &compile_database_escaped_probe_path},
    {"compile database malformed is reported", &compile_database_malformed_is_reported},
    {"writes toolchain cmake for bare metal and linux", &writes_toolchain_cmake_bare_metal_and_linux},
};

const mm::test::registrar reg{"mm.build external", cases};

}
