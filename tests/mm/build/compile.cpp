// Tests for mm.build's compile lane: shell quoting, the per-output-lane
// module cache, artifact contexts, and direct compile() failures.

#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <string>
#include <string_view>

import mm.build;
import mm.test;

namespace {

using mm::test::expect;
using mm::build::shell_quote;

using mm::build::shell_quote;

bool contains(std::string_view text, std::string_view needle) {
    return text.find(needle) != std::string_view::npos;
}

void wraps_a_plain_path_in_single_quotes() {
    mm::test::expect(shell_quote(std::filesystem::path("m/x.cppm")) == "'m/x.cppm'",
                     "expected a plain path to be wrapped in single quotes");
}

void quotes_an_empty_path() {
    mm::test::expect(shell_quote(std::filesystem::path("")) == "''",
                     "expected an empty path to become an empty quoted argument");
}

// The payload from the finding. Inside single quotes the shell sees the text,
// not a command.
void neutralises_command_substitution() {
    const auto quoted = shell_quote(std::filesystem::path("$(touch EXECUTED)x.cppm"));

    mm::test::expect(quoted == "'$(touch EXECUTED)x.cppm'",
                     "expected a command substitution to be quoted verbatim");
    mm::test::expect(!contains(quoted, "\""), "expected no double quotes in the result");
}

void neutralises_backticks() {
    mm::test::expect(shell_quote(std::filesystem::path("a`id`b.cppm")) == "'a`id`b.cppm'",
                     "expected backticks to be quoted verbatim");
}

void neutralises_variable_expansion() {
    mm::test::expect(shell_quote(std::filesystem::path("v$HOME.cppm")) == "'v$HOME.cppm'",
                     "expected a variable reference to be quoted verbatim");
    mm::test::expect(shell_quote(std::filesystem::path("v$1.cppm")) == "'v$1.cppm'",
                     "expected a positional parameter to be quoted verbatim");
}

// A double quote inside the path used to terminate the old quoting and let the
// rest of the name become further arguments.
void neutralises_a_double_quote() {
    mm::test::expect(shell_quote(std::filesystem::path("quote\".cppm")) == "'quote\".cppm'",
                     "expected a double quote to be harmless inside single quotes");
}

void neutralises_semicolons_and_pipes() {
    mm::test::expect(shell_quote(std::filesystem::path("a;rm -rf x|b.cppm")) ==
                         "'a;rm -rf x|b.cppm'",
                     "expected command separators to be quoted verbatim");
}

void keeps_backslashes_literal() {
    mm::test::expect(shell_quote(std::filesystem::path("back\\slash.cppm")) ==
                         "'back\\slash.cppm'",
                     "expected a backslash to stay literal inside single quotes");
}

void keeps_spaces_in_one_argument() {
    mm::test::expect(shell_quote(std::filesystem::path("plain space.cppm")) ==
                         "'plain space.cppm'",
                     "expected a path with a space to remain one argument");
}

// The one case single quoting cannot express directly: close the run, emit an
// escaped quote, reopen. 'it'\''s.cppm' is what the shell reassembles into
// it's.cppm.
void escapes_an_embedded_single_quote() {
    mm::test::expect(shell_quote(std::filesystem::path("it's.cppm")) == "'it'\\''s.cppm'",
                     "expected an embedded single quote to be closed, escaped and reopened");
}

void escapes_a_leading_single_quote() {
    mm::test::expect(shell_quote(std::filesystem::path("'x.cppm")) == "''\\''x.cppm'",
                     "expected a leading single quote to be escaped");
}

void escapes_repeated_single_quotes() {
    mm::test::expect(shell_quote(std::filesystem::path("a''b")) == "'a'\\'''\\''b'",
                     "expected each embedded single quote to be escaped");
}


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

void gcc_cache_is_private_and_maps_every_named_unit() {
    const mm::test::scoped_tree scratch{"build_module_cache"};
    current_directory_guard restore;
    std::filesystem::current_path(scratch.root());

    mm::build::Tree tree;
    mm::build::BuildableNode alpha;
    alpha.kind = "module";
    alpha.module_name = "mm.alpha";
    alpha.sources.push_back({"alpha.cppm", {}});
    tree.targets.push_back(alpha);

    mm::build::BuildableNode beta;
    beta.kind = "module";
    beta.module_name = "mm.beta";
    beta.sources.push_back({"part.cppm", "mm.beta:part"});
    beta.sources.push_back({"beta.cppm", {}});
    tree.targets.push_back(beta);

    mm::build::Toolchain toolchain;
    toolchain.family = mm::build::CompilerFamily::Gcc;

    mm::test::expect(mm::build::prepare_module_cache(toolchain, tree, "lane"),
                     "GCC cache preparation succeeds");

    const auto cache = scratch.root() / "lane/bmi";
    mm::test::expect(std::filesystem::is_directory(cache),
                     "cache is created beneath the output lane");
    mm::test::expect(!std::filesystem::exists(scratch.root() / "gcm.cache"),
                     "preparation does not create a shared root cache");

    std::ifstream input(cache / "gcc.mapper");
    const std::string mapper((std::istreambuf_iterator<char>(input)),
                             std::istreambuf_iterator<char>());
    mm::test::expect(
        mapper == "$root lane/bmi\n"
                  "mm.alpha mm.alpha.gcm\n"
                  "mm.beta mm.beta.gcm\n"
                  "mm.beta:part mm.beta-part.gcm\n",
        "mapper names primary modules and partitions deterministically");

    std::ofstream(cache / "stale.gcm") << "stale";
    mm::test::expect(mm::build::prepare_module_cache(toolchain, tree, "lane"),
                     "a second preparation succeeds");
    mm::test::expect(!std::filesystem::exists(cache / "stale.gcm"),
                     "a second preparation removes stale lane state");
}

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

const mm::test::case_ cases[] = {
    { "wraps a plain path in single quotes",  &wraps_a_plain_path_in_single_quotes },
    { "quotes an empty path",                 &quotes_an_empty_path },
    { "neutralises command substitution",     &neutralises_command_substitution },
    { "neutralises backticks",                &neutralises_backticks },
    { "neutralises variable expansion",       &neutralises_variable_expansion },
    { "neutralises a double quote",           &neutralises_a_double_quote },
    { "neutralises semicolons and pipes",     &neutralises_semicolons_and_pipes },
    { "keeps backslashes literal",            &keeps_backslashes_literal },
    { "keeps spaces in one argument",         &keeps_spaces_in_one_argument },
    { "escapes an embedded single quote",     &escapes_an_embedded_single_quote },
    { "escapes a leading single quote",       &escapes_a_leading_single_quote },
    { "escapes repeated single quotes",       &escapes_repeated_single_quotes },

    {"GCC cache is private and maps named units",
     &gcc_cache_is_private_and_maps_every_named_unit},
    {"context paths and prefixes", &context_paths_and_prefixes},
    {"context write guards", &context_write_guards},
    {"rejects compilation when declared sketch missing", &rejects_compilation_when_declared_sketch_missing},
};

const mm::test::registrar reg{"mm.build compile", cases};

}  // namespace
