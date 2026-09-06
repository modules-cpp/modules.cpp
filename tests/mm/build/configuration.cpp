// Black box tests for loading the compiler lane selected by out/config.mdy.

#include <filesystem>
#include <fstream>
#include <string_view>

import mm.build;
import mm.test;

namespace {

void write(const std::filesystem::path& path, std::string_view front_matter) {
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);
    std::ofstream out(path);
    out << "---\n" << front_matter << "---\n";
}

constexpr std::string_view native_configuration =
    "mm: 1.0\n"
    "kind: configuration\n"
    "name: native\n"
    "build: release\n"
    "target-compiler: host\n"
    "host-compiler-family: clang\n"
    "host-compiler: clang++\n"
    "host-target: host\n"
    "host-platform: POSIX\n"
    "host-compile-flags: host compile flags\n"
    "host-link-flags: host link flags\n"
    "host-build-directory: out/host\n"
    "target-build-directory: out/host\n";

void loads_the_host_selection() {
    const mm::test::scoped_tree tree{"build_native_configuration"};
    const auto path = tree.root() / "out" / "config.mdy";
    write(path, native_configuration);

    mm::build::BuildConfiguration configuration;
    mm::test::expect(mm::build::load_configuration(path, true, configuration),
                     "expected native configuration to load");
    mm::test::expect(configuration.toolchain.cxx == "clang++",
                     "expected host compiler selection");
    mm::test::expect(configuration.toolchain.family == mm::build::CompilerFamily::Clang,
                     "expected Clang family selection");
    mm::test::expect(configuration.toolchain.cxxflags == "host compile flags" &&
                         configuration.toolchain.ldflags == "host link flags",
                     "expected host flags");
    mm::test::expect(configuration.toolchain.verbose,
                     "expected the caller's verbose setting to be retained");
    mm::test::expect(configuration.build == mm::build::Build::Release,
                     "expected release build selection");
    mm::test::expect(configuration.build_directory == "out/host",
                     "expected target build directory");
}

void loads_the_cross_selection() {
    const mm::test::scoped_tree tree{"build_cross_configuration"};
    const auto path = tree.root() / "out" / "config.mdy";
    write(path,
          "mm: 1.0\n"
          "kind: configuration\n"
          "name: cross\n"
          "target-compiler: cross\n"
          "host-compiler-family: clang\n"
          "host-compiler: clang++\n"
          "host-target: host\n"
          "host-platform: POSIX\n"
          "host-compile-flags: host compile flags\n"
          "host-link-flags: host link flags\n"
          "cross-compiler-family: gcc\n"
          "cross-compiler: aarch64-linux-gnu-g++\n"
          "cross-target: aarch64-linux-gnu\n"
          "cross-platform: POSIX\n"
          "cross-compile-flags: cross compile flags\n"
          "cross-link-flags: cross link flags\n"
          "host-build-directory: out/host\n"
          "target-build-directory: out/target/aarch64-linux-gnu\n");

    mm::build::BuildConfiguration configuration;
    mm::test::expect(mm::build::load_configuration(path, false, configuration),
                     "expected cross configuration to load");
    mm::test::expect(configuration.toolchain.cxx == "aarch64-linux-gnu-g++",
                     "expected cross compiler selection");
    mm::test::expect(configuration.toolchain.family == mm::build::CompilerFamily::Gcc,
                     "expected cross compiler family selection");
    mm::test::expect(configuration.toolchain.cxxflags == "cross compile flags" &&
                         configuration.toolchain.ldflags == "cross link flags",
                     "expected cross flags");
    mm::test::expect(configuration.build_directory == "out/target/aarch64-linux-gnu",
                     "expected cross target build directory");
}

void rejects_a_selected_but_missing_cross_compiler() {
    const mm::test::scoped_tree tree{"build_missing_cross_configuration"};
    const auto path = tree.root() / "out" / "config.mdy";
    std::string text(native_configuration);
    const auto selection = text.find("target-compiler: host");
    text.replace(selection, std::string_view("target-compiler: host").size(),
                 "target-compiler: cross");
    write(path, text);

    mm::build::BuildConfiguration configuration;
    mm::test::expect(!mm::build::load_configuration(path, false, configuration),
                     "expected a missing selected cross group to fail");
}

void rejects_an_escaping_build_directory() {
    const mm::test::scoped_tree tree{"build_escaping_configuration"};
    const auto path = tree.root() / "out" / "config.mdy";
    std::string text(native_configuration);
    const auto directory = text.find("target-build-directory: out/host");
    text.replace(directory, std::string_view("target-build-directory: out/host").size(),
                 "target-build-directory: ../outside");
    write(path, text);

    mm::build::BuildConfiguration configuration;
    mm::test::expect(!mm::build::load_configuration(path, false, configuration),
                     "expected an escaping target build directory to fail");
}

void resolves_one_project_configuration() {
    const mm::test::scoped_tree tree{"build_resolved_configuration"};
    const auto path = tree.root() / "out" / "config.mdy";
    write(path, native_configuration);

    mm::build::BuildConfiguration configuration;
    mm::test::expect(mm::build::resolve_configuration(tree.root(), false, configuration),
                     "expected project configuration to resolve");
    mm::test::expect(configuration.toolchain.family == mm::build::CompilerFamily::Clang &&
                         configuration.toolchain.cxx == "clang++",
                     "expected the resolved project compiler");
}

void resolves_the_shared_unconfigured_default() {
    const mm::test::scoped_tree tree{"build_default_configuration"};
    mm::build::BuildConfiguration configuration;
    mm::test::expect(mm::build::resolve_configuration(tree.root(), false, configuration),
                     "expected the default configuration to resolve");
    mm::test::expect(configuration.toolchain.family == mm::build::CompilerFamily::Gcc &&
                         configuration.toolchain.cxx == "g++" &&
                         configuration.build_directory == "out",
                     "expected the shared unconfigured GCC default");
}

void treats_an_older_configuration_as_gcc() {
    const mm::test::scoped_tree tree{"build_legacy_gcc_configuration"};
    const auto path = tree.root() / "out" / "config.mdy";
    std::string text(native_configuration);
    const auto family = text.find("host-compiler-family: clang\n");
    text.erase(family, std::string_view("host-compiler-family: clang\n").size());
    write(path, text);

    mm::build::BuildConfiguration configuration;
    mm::test::expect(mm::build::load_configuration(path, false, configuration),
                     "expected a configuration written before compiler families to load");
    mm::test::expect(configuration.toolchain.family == mm::build::CompilerFamily::Gcc,
                     "expected a missing family to retain the historical GCC behavior");
}

void treats_an_older_configuration_as_debug() {
    const mm::test::scoped_tree tree{"build_legacy_debug_configuration"};
    const auto path = tree.root() / "out" / "config.mdy";
    std::string text(native_configuration);
    const auto build = text.find("build: release\n");
    text.erase(build, std::string_view("build: release\n").size());
    write(path, text);

    mm::build::BuildConfiguration configuration;
    mm::test::expect(mm::build::load_configuration(path, false, configuration),
                     "expected a configuration written before build selection to load");
    mm::test::expect(configuration.build == mm::build::Build::Debug,
                     "expected a missing build to retain debug behavior");
}

void rejects_an_unknown_build() {
    const mm::test::scoped_tree tree{"build_unknown_build"};
    const auto path = tree.root() / "out" / "config.mdy";
    std::string text(native_configuration);
    const auto build = text.find("build: release");
    text.replace(build, std::string_view("build: release").size(), "build: optimized");
    write(path, text);

    mm::build::BuildConfiguration configuration;
    mm::test::expect(!mm::build::load_configuration(path, false, configuration),
                     "expected an unknown build to fail");
}

void rejects_an_unknown_compiler_family() {
    const mm::test::scoped_tree tree{"build_unknown_compiler_family"};
    const auto path = tree.root() / "out" / "config.mdy";
    std::string text(native_configuration);
    const auto family = text.find("host-compiler-family: clang");
    text.replace(family, std::string_view("host-compiler-family: clang").size(),
                 "host-compiler-family: msvc");
    write(path, text);

    mm::build::BuildConfiguration configuration;
    mm::test::expect(!mm::build::load_configuration(path, false, configuration),
                     "expected an unknown compiler family to fail");
}

const mm::test::case_ cases[] = {
    {"loads the host selection", &loads_the_host_selection},
    {"loads the cross selection", &loads_the_cross_selection},
    {"rejects selected missing cross compiler", &rejects_a_selected_but_missing_cross_compiler},
    {"rejects escaping build directory", &rejects_an_escaping_build_directory},
    {"resolves one project configuration", &resolves_one_project_configuration},
    {"resolves shared unconfigured default", &resolves_the_shared_unconfigured_default},
    {"older configuration defaults to GCC", &treats_an_older_configuration_as_gcc},
    {"older configuration defaults to debug", &treats_an_older_configuration_as_debug},
    {"rejects unknown build", &rejects_an_unknown_build},
    {"rejects unknown compiler family", &rejects_an_unknown_compiler_family},
};

const mm::test::registrar reg{"mm.build configuration", cases};

}  // namespace
