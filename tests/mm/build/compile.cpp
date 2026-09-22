// Tests for mm.build's compile lane: artifact contexts, direct compile()
// failures, and the compile, link, install and run steps.

#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

import mm.build;
import mm.test;

namespace {

using mm::test::expect;

class current_directory_guard {
public:
    current_directory_guard() : original_(std::filesystem::current_path()) {}
    ~current_directory_guard() {
        std::error_code ec;
        std::filesystem::current_path(original_, ec);
    }

    current_directory_guard(const current_directory_guard&) = delete;
    current_directory_guard& operator=(const current_directory_guard&) = delete;

private:
    std::filesystem::path original_;
};

void context_paths_and_prefixes() {
    const std::filesystem::path proj_root = "/project";
    const std::filesystem::path ext_root = "/external";
    const std::filesystem::path build_dir = "out-host";

    // 1. External invocation
    mm::build::ArtifactContext ext_context(
        ext_root, ext_root / build_dir, proj_root / "out/bin", true);
    expect(ext_context.valid(), "external context is valid");
    expect(ext_context.is_external(), "context reports external");

    mm::build::BuildableNode ext_app;
    ext_app.name = "blink";
    ext_app.kind = "app";
    ext_app.external = true;
    ext_app.logical_dir = "sketches/blink";

    mm::build::BuildableNode proj_mod;
    proj_mod.name = "sketch";
    proj_mod.kind = "module";
    proj_mod.external = false;
    proj_mod.logical_dir = "modules/mm/sketch";

    expect(ext_context.prefix(ext_app) == "", "external node prefix is empty");
    expect(ext_context.prefix(proj_mod) == "graft/project/",
           "project node prefix in external context is graft/project/");

    mm::build::TranslationUnit ext_unit{"sketches/blink/main.cpp", "", {}};
    const auto ext_obj = ext_context.object_path(ext_app, ext_unit);
    expect(ext_obj == ext_root / build_dir / "sketches/blink/main.cpp.o",
           "external object lands under output_root / unit.path.o");

    mm::build::TranslationUnit proj_unit{
        "modules/mm/sketch/sketch.cppm", "", {}};
    const auto proj_obj = ext_context.object_path(proj_mod, proj_unit);
    expect(proj_obj ==
           ext_root / build_dir /
           "graft/project/modules/mm/sketch/sketch.cppm.o",
           "project module object lands under graft/project/");

    const auto board_obj = ext_context.board_object_path(
        "platforms/rp2040/board.cpp", "rp2040");
    expect(board_obj ==
           ext_root / build_dir / "graft/project/platforms/rp2040/board.cpp.o",
           "board unit object lands under graft/project/");

    expect(ext_context.executable_path(ext_app) ==
           ext_root / build_dir / "sketches/blink/blink",
           "executable path matches output_root / logical_dir / name");
    expect(ext_context.bmi_dir() == ext_root / build_dir / "bmi",
           "bmi dir is under external output_root");

    // 2. Project invocation
    mm::build::ArtifactContext proj_context(
        proj_root, proj_root / build_dir, proj_root / "out/bin", false);
    expect(proj_context.valid(), "project context is valid");
    expect(!proj_context.is_external(), "context reports non-external");
    expect(proj_context.prefix(proj_mod) == "", "project prefix is empty");
    expect(proj_context.object_path(proj_mod, proj_unit) ==
           proj_root / build_dir / "modules/mm/sketch/sketch.cppm.o",
           "project unit lands directly under lane directory");
    expect(proj_context.board_object_path("platforms/rp2040/board.cpp",
                                          "rp2040") ==
           proj_root / build_dir / "platforms/rp2040/board.cpp.o",
           "project board unit lands directly under lane directory");
}

void context_write_guards() {
    const mm::test::scoped_tree tree{"context_guards"};
    const auto out_dir = tree.root() / "out-host";
    std::filesystem::create_directories(out_dir);

    // 1. Output root as symlink out of tree is refused
    const mm::test::scoped_tree outside_tree{"context_outside"};
    const auto sym_out = tree.root() / "out-symlink";
    std::error_code ec;
    std::filesystem::create_directory_symlink(outside_tree.root(), sym_out, ec);
    if (!ec) {
        mm::build::ArtifactContext bad_context(tree.root(), sym_out);
        expect(!bad_context.valid(),
               "symlinked output root outside tree is refused");
    }

    // 2. Normal context validates paths inside tree & output root
    mm::build::ArtifactContext context(tree.root(), out_dir,
                                       tree.root() / "out/bin", false);
    expect(context.valid(), "context is valid");

    const auto valid_artifact = out_dir / "foo.o";
    expect(context.check_artifact_path(valid_artifact),
           "artifact inside output_root is accepted");

    const auto outside_artifact = outside_tree.root() / "foo.o";
    expect(!context.check_artifact_path(outside_artifact),
           "artifact outside tree is refused");

    // 3. Symlink planted beneath output root pointing outside tree
    const auto planted_sym = out_dir / "leak_dir";
    std::filesystem::create_directory_symlink(outside_tree.root(), planted_sym,
                                              ec);
    if (!ec) {
        expect(!context.check_artifact_path(planted_sym / "stolen.o"),
               "link planted beneath output root resolving outside is refused");
    }

    // 4. Install destination guard
    const auto bin_dir = tree.root() / "out/bin";
    std::filesystem::create_directories(bin_dir);
    expect(context.check_install_path(bin_dir, bin_dir / "app"),
           "installation destination inside tree is admitted");
    expect(!context.check_install_path(outside_tree.root(),
                                       outside_tree.root() / "app"),
           "installation destination outside tree is refused");

    // 5. External context has no installation destination
    mm::build::ArtifactContext ext_context(tree.root(), out_dir,
                                           tree.root() / "out/bin", true);
    expect(!ext_context.check_install_path(bin_dir, bin_dir / "app"),
           "external context refuses all installation paths");
}

void rejects_compilation_when_declared_sketch_missing() {
    const mm::test::scoped_tree tree{"sketchmissing"};
    tree.manifest_raw("", "mm: 1.3\nkind: project\nname: p\nfolder: a\n");
    tree.manifest_raw("a", "mm: 1.3\nkind: app\nname: a\nfile: main.cpp\nsketch: missing.ino\n");

    // Create existing main.cpp so absence of main.cpp does not trigger generation
    std::filesystem::create_directories(tree.root() / "a");
    {
        std::ofstream out(tree.root() / "a" / "main.cpp");
        out << "int main() { return 0; }\n";
    }

    auto loaded = mm::build::load_tree(tree.root());
    mm::test::expect(loaded.ok, "manifest load succeeds");
    mm::test::expect(loaded.targets.size() == 1, "expected 1 target");

    // Missing declared sketch must cause compile() to fail with exit_manifest
    const int status = mm::build::compile(mm::build::default_toolchain(), loaded.targets[0],
                                          tree.root() / "build", {});
    mm::test::expect(status == mm::build::exit_manifest, "expected compile to fail on missing sketch");
}


// --- direct compile lane tests

void a_translation_unit_compiles_and_appends_objects() {
    const mm::test::scoped_tree tree{"compile_tu"};
    tree.manifest("", "kind: project\nname: p\nfolder: a\n");
    tree.manifest("a", "kind: app\nname: a\nfile: main.cpp\n");
    std::filesystem::create_directories(tree.root() / "a");
    std::ofstream(tree.root() / "a" / "main.cpp") << "int main() { return 0; }\n";

    // The compile lane guards writes against the project root and builds
    // object paths relative to it, so the case runs from inside the tree,
    // exactly as the tools do.
    const current_directory_guard cwd;
    std::error_code ec;
    std::filesystem::current_path(tree.root(), ec);
    expect(!ec, "the case enters its project root");

    auto loaded = mm::build::load_tree(".");
    expect(loaded.ok, "the app tree loads");
    expect(loaded.targets.size() == 1, "the app is the single target");
    expect(mm::build::prepare_module_cache(mm::build::default_toolchain(),
                                           loaded, "build"),
           "the module cache is prepared");

    const auto status = mm::build::compile(mm::build::default_toolchain(),
                                           loaded.targets[0], "build");
    expect(status == mm::build::exit_ok, "the translation unit compiles");
    expect(loaded.targets[0].objects.size() == 1, "the object is appended");
    expect(std::filesystem::exists(tree.root() / "build" / "a" / "main.cpp.o"),
           "the object file exists");
}

void link_produces_an_executable() {
    const mm::test::scoped_tree tree{"compile_link"};
    tree.manifest("", "kind: project\nname: p\nfolder: a\n");
    tree.manifest("a", "kind: app\nname: a\nfile: main.cpp\n");
    std::filesystem::create_directories(tree.root() / "a");
    std::ofstream(tree.root() / "a" / "main.cpp") << "int main() { return 0; }\n";

    const current_directory_guard cwd;
    std::error_code ec;
    std::filesystem::current_path(tree.root(), ec);
    expect(!ec, "the case enters its project root");

    auto loaded = mm::build::load_tree(".");
    expect(loaded.ok, "the app tree loads");
    const auto toolchain = mm::build::default_toolchain();
    expect(mm::build::prepare_module_cache(toolchain, loaded, "build"),
           "the module cache is prepared");
    expect(mm::build::compile(toolchain, loaded.targets[0], "build")
               == mm::build::exit_ok,
           "the translation unit compiles");
    expect(mm::build::link(toolchain, loaded.targets[0].objects, "build/app")
               == mm::build::exit_ok,
           "the objects link");
    expect(std::filesystem::exists(tree.root() / "build" / "app"),
           "the executable exists");
}

void install_copies_the_binary_into_the_bin_directory() {
    const mm::test::scoped_tree tree{"compile_install"};
    std::filesystem::create_directories(tree.root() / "build");
    const auto binary = tree.root() / "build" / "app";
    std::ofstream(binary) << "payload\n";
    const auto bin_dir = tree.root() / "out" / "bin";

    expect(mm::build::install(binary, bin_dir, "app", tree.root()) == mm::build::exit_ok,
           "a binary installs into the bin directory");
    expect(std::filesystem::exists(bin_dir / "app"), "the installed binary exists");
    std::ofstream(binary) << "payload two\n";
    expect(mm::build::install(binary, bin_dir, "app", tree.root()) == mm::build::exit_ok,
           "a second install replaces the running binary");
    std::ifstream installed(bin_dir / "app");
    expect((std::string((std::istreambuf_iterator<char>(installed)),
                        std::istreambuf_iterator<char>())) == "payload two\n",
           "the replacement reached the bin directory");
}

void run_returns_the_exit_code_of_the_command() {
    const auto toolchain = mm::build::default_toolchain();
    expect(mm::build::run(toolchain, "true") == 0, "a passing command reports zero");
    expect(mm::build::run(toolchain, "false") == 1, "a failing command reports one");
}
const mm::test::case_ cases[] = {
    {"context paths and prefixes", &context_paths_and_prefixes},
    {"context write guards", &context_write_guards},
    {"rejects compilation when declared sketch missing", &rejects_compilation_when_declared_sketch_missing},
    {"a translation unit compiles and appends objects", &a_translation_unit_compiles_and_appends_objects},
    {"link produces an executable", &link_produces_an_executable},
    {"install copies the binary into the bin directory", &install_copies_the_binary_into_the_bin_directory},
    {"run returns the exit code of the command", &run_returns_the_exit_code_of_the_command},
};

const mm::test::registrar reg{"mm.build compile", cases};

}  // namespace
