#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <map>
#include <string>
#include <string_view>
#include <vector>

import mm.build;
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
               toolchain_file, c_compiler, cxx_compiler),
           "valid inputs succeed");

    std::ifstream in(inputs_file);
    std::string content((std::istreambuf_iterator<char>(in)),
                        std::istreambuf_iterator<char>());
    expect(content.find("set(MM_OUTPUT_NAME [==[smoke]==])") != std::string::npos,
           "output name uses bracket syntax");
    expect(content.find("set(MM_BOARD [==[pico]==])") != std::string::npos,
           "board name uses bracket syntax");
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

    // Semicolon in output_name
    expect(!mm::build::write_inputs_cmake(
               inputs_file, objects, "bad;name", tree.root() / "lib", "pico",
               toolchain_file, c_compiler, cxx_compiler),
           "semicolon in output name is rejected");

    // Semicolon in board_name
    expect(!mm::build::write_inputs_cmake(
               inputs_file, objects, "smoke", tree.root() / "lib", "bad;board",
               toolchain_file, c_compiler, cxx_compiler),
           "semicolon in board name is rejected");

    // Semicolon in object path
    std::vector<std::filesystem::path> bad_objects = {
        tree.root() / "bad;obj.o"
    };
    expect(!mm::build::write_inputs_cmake(
               inputs_file, bad_objects, "smoke", tree.root() / "lib", "pico",
               toolchain_file, c_compiler, cxx_compiler),
           "semicolon in object path is rejected");

    // Semicolon in library source
    expect(!mm::build::write_inputs_cmake(
               inputs_file, objects, "smoke", tree.root() / "bad;lib", "pico",
               toolchain_file, c_compiler, cxx_compiler),
           "semicolon in library source path is rejected");

    // Newline in output_name
    expect(!mm::build::write_inputs_cmake(
               inputs_file, objects, "bad\nname", tree.root() / "lib", "pico",
               toolchain_file, c_compiler, cxx_compiler),
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

const mm::test::case_ cases[] = {
    {"inputs cmake escaping and validation", &inputs_cmake_escaping_and_validation},
    {"cmake package requirements", &cmake_package_requirements},
    {"projection schemas", &projection_schemas},
    {"query driver projection live", &query_driver_projection_live},
    {"projection accepts present empty value", &projection_accepts_present_empty_value},
    {"publish results validation", &publish_results_validation},
};

const mm::test::registrar reg{"mm.build external", cases};

}
