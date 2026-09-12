#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

import mm.build;
import mm.configure;
import mm.mdy;
import mm.test;

namespace {

using mm::test::expect;
using mm::configure::Build;

struct Resolution {
    std::vector<mm::configure::OptionNode> nodes;
    std::vector<mm::configure::OptionValues> values;
};

bool resolve(const mm::test::scoped_tree& tree, Build build, Resolution& result) {
    const auto project = mm::build::load_project(tree.root(), {.tool = "configure", .strict_tree = true});
    if (!project.ok) return false;
    result.nodes = mm::build::configuration_nodes(project);
    return mm::configure::resolve_options(tree.root(), build, result.nodes, result.values);
}

std::string read(const std::filesystem::path& path) {
    std::ifstream in(path);
    std::ostringstream text;
    text << in.rdbuf();
    return text.str();
}

void shared_defaults() {
    const mm::test::scoped_tree tree{"options_defaults"};
    tree.manifest("", "kind: project\nname: p\n");
    for (const auto build : {Build::Debug, Build::Release}) {
        Resolution result;
        expect(resolve(tree, build, result), "defaults resolve");
        const auto defaults = mm::configure::build_defaults(build);
        const auto& values = result.values[0];
        expect(values.size() == 9, "every registry name is represented");
        expect(values.at("core").boolean, "nodes are core by default in both builds");
        expect(values.at("buildable-host").boolean && values.at("buildable-target").boolean,
               "both lanes default to buildable in both builds");
        expect(values.at("optimize").number == defaults.optimize, "optimization shares build policy");
        expect(values.at("debug-info").boolean == defaults.debug_info, "debug info shares policy");
        expect(values.at("assertions").boolean == defaults.assertions, "assertions share policy");
        expect(values.at("include-dir").unset, "optional directory starts unset");
        expect(!values.at("warnings").boolean && !values.at("warnings-error").boolean, "warnings default off");
        expect(!values.at("optimize").read_only, "defaults are mutable");
    }
    expect(mm::configure::build_compile_flags(Build::Debug) == "-std=c++20 -O0 -g", "debug compile unchanged");
    expect(mm::configure::build_compile_flags(Build::Release) == "-std=c++20 -O2 -DNDEBUG", "release compile unchanged");
    expect(mm::configure::build_link_flags(Build::Debug) == "-std=c++20 -g", "debug link unchanged");
    expect(mm::configure::build_link_flags(Build::Release) == "-std=c++20 -O2", "release link unchanged");
    expect(mm::build::default_toolchain().compiler.arguments ==
               mm::configure::build_compile_flags(Build::Debug),
           "fallback shares policy");
}

void inheritance_reset_and_records() {
    const mm::test::scoped_tree tree{"options_inheritance"};
    tree.manifest_raw("", "mm: 1.1\nkind: project\nname: p\nfolder: a\nfolder: b\noption: optimize 1\noption: warnings yes\n");
    tree.manifest_raw("a", "mm: 1.1\nkind: dir\nname: a\nfolder: leaf\noption: optimize 3\nread-only: warnings\n");
    tree.manifest_raw("a/leaf", "mm: 1.1\nkind: module\nname: leaf\nmodule: example\nfile: leaf.cppm\nreset: optimize\nread-only: optimize\n");
    tree.manifest("b", "kind: doc\nname: b\n");
    for (const auto build : {Build::Release, Build::Debug}) {
        Resolution result;
        expect(resolve(tree, build, result), "mixed-version tree resolves");
        expect(result.values[0].at("optimize").number == 1, "project overrides default");
        expect(result.values[1].at("optimize").number == 3, "child overrides parent");
        const auto& leaf = result.values[2];
        expect(leaf.at("optimize").number == mm::configure::build_defaults(build).optimize, "reset restores build default");
        expect(leaf.at("optimize").origin == mm::configure::OptionOrigin::Reset, "reset provenance retained");
        expect(leaf.at("warnings").value_source == tree.root() / "mm.mdy", "value origin differs from lock origin");
        expect(leaf.at("warnings").lock_source == tree.root() / "a/mm.mdy", "original locking manifest retained");
        expect(result.values[3].at("optimize").number == 1 && !result.values[3].at("warnings").read_only, "sibling unaffected");
        expect(mm::configure::write_option_records(tree.root(), "out-host", build, result.nodes, result.values, false), "records publish");
        const auto record = read(tree.root() / "out-host/a/leaf/resolved-options.mdy");
        expect(record.find("option: optimize " + std::to_string(mm::configure::build_defaults(build).optimize)) != std::string::npos, "same record replaced for selected build");
        expect(record.find("read-only: optimize") != std::string::npos && record.find("unset-option: include-dir") != std::string::npos, "record contains locks and unset values");
        expect(record.find("applied-by-build: structural-properties") != std::string::npos && record.find("compile-arg:") == std::string::npos, "record identifies structural-only consumption");
        expect(record.find("output: out-host") != std::string::npos,
               "record names the lane that produced it");
    }
    expect(!std::filesystem::exists(tree.root() / "out"),
           "records live in the lane directory and never write into out/");
}

void locks_and_leaf_intent() {
    const mm::test::scoped_tree tree{"options_locks"};
    tree.manifest_raw("", "mm: 1.1\nkind: project\nname: p\nfolder: child\nread-only: optimize\nread-only: include-dir\nreset: include-dir\n");
    for (const auto& declaration : {std::string("option: optimize 2\n"), std::string("reset: optimize\n"), std::string("option: include-dir .\n")}) {
        tree.manifest_raw("child", "mm: 1.1\nkind: dir\nname: child\n" + declaration);
        for (const auto build : {Build::Debug, Build::Release}) {
            Resolution result;
            expect(!resolve(tree, build, result), "root lock rejects even equal assignments or resets in both builds");
            expect(result.values.empty(), "failed resolution returns no partial map");
        }
    }
    for (const auto& kind : {std::string("module"), std::string("app"), std::string("test")}) {
        const auto sources = kind == "test" ? "unit: child.cpp\n" : "file: child.cpp\n";
        const auto module = kind == "module" ? "module: child\n" : "";
        tree.manifest_raw("child", "mm: 1.1\nkind: " + kind + "\nname: child\n" + module + sources + "read-only: optimize\nread-only: warnings\noption: warnings yes\n");
        Resolution result;
        expect(resolve(tree, Build::Debug, result), "leaf lock and reordered assignment are permitted");
        expect(result.values[1].at("optimize").lock_source == tree.root() / "mm.mdy", "idempotent inherited marker retains first lock");
        expect(result.values[1].at("include-dir").unset && result.values[1].at("include-dir").read_only, "unset lock is inherited");
        expect(result.values[1].at("warnings").read_only && result.values[1].at("warnings").boolean, "local value set before local lock");
    }
}

void invalid_declarations() {
    const mm::test::scoped_tree tree{"options_invalid"};
    const std::vector<std::string> declarations = {
        "option: warnings true\n", "option: warnings\n", "option: \n", "option: unknown yes\n",
        "option: optimize +2\n", "option: optimize 2x\n", "option: optimize -1\n",
        "option: optimize 4\n", "option: optimize 9223372036854775808\n",
        "option: warnings yes\noption: warnings no\n", "option: warnings yes\nreset: warnings\n",
        "reset: warnings\nreset: warnings\n", "reset: warnings extra\n", "reset: missing\n",
        "read-only: warnings\nread-only: warnings\n", "read-only: missing\n", "read-only: warnings extra\n",
        "option: include-dir\n", "options: warnings yes\n",
    };
    for (const auto& declaration : declarations) {
        tree.manifest_raw("", "mm: 1.1\nkind: project\nname: p\n" + declaration);
        Resolution result;
        expect(!resolve(tree, Build::Debug, result), "invalid declaration rejected");
    }
    for (const auto& declaration : {"option: warnings yes\n", "reset: warnings\n", "read-only: warnings\n"}) {
        tree.manifest("", "kind: project\nname: p\nfolder: d\n");
        tree.manifest_raw("d", std::string("mm: 1.1\nkind: doc\nname: d\n") + declaration);
        Resolution result;
        expect(!resolve(tree, Build::Debug, result), "doc cannot declare any option operation");
    }
}

void directory_domain() {
    const mm::test::scoped_tree tree{"options_paths"};
    tree.manifest("", "kind: project\nname: p\nfolder: a\n");
    tree.manifest_raw("a", "mm: 1.1\nkind: dir\nname: a\nfolder: leaf\noption: include-dir include ; $(touch sentinel)\n");
    tree.manifest("a/leaf", "kind: dir\nname: leaf\n");
    std::error_code ec;
    std::filesystem::create_directory(tree.root() / "a/include ; $(touch sentinel)", ec);
    expect(!ec, "literal directory created");
    Resolution result;
    expect(resolve(tree, Build::Debug, result), "spaces and punctuation are literal path data");
    expect(result.values[2].at("include-dir").directory == "a/include ; $(touch sentinel)", "inherited path not rebased");
    expect(mm::configure::write_option_records(tree.root(), "out-host", Build::Debug, result.nodes, result.values, false), "literal path serialized");
    expect(!std::filesystem::exists(tree.root() / "sentinel"), "configuration executes nothing");
    std::ofstream(tree.root() / "a/file") << "not a directory";
    std::filesystem::create_directory_symlink(tree.root().parent_path(), tree.root() / "a/escape", ec);
    expect(!ec, "escape symlink created");
    for (const auto& path : {"missing", "file", "../../", "escape", "/tmp"}) {
        tree.manifest_raw("a", std::string("mm: 1.1\nkind: dir\nname: a\noption: include-dir ") + path + "\n");
        expect(!resolve(tree, Build::Debug, result), "unsafe or nonexistent directory rejected");
    }
}

void shared_schema_and_strict_tree() {
    const mm::test::scoped_tree tree{"options_schema"};
    tree.manifest_raw("", "mm: 1.1\nkind: project\nname: p\nfolder: shared\nfolder: shared\noption: unknown yes\n");
    tree.manifest("shared", "kind: dir\nname: shared\n");
    for (const auto& tool : {"build", "test", "check", "model", "document"}) {
        const auto project = mm::build::load_project(tree.root(), {.tool = tool, .warn_options = true});
        expect(project.ok && project.nodes.size() == 2, "ordinary consumers accept 1.1 and retain repeat-visit behavior");
        expect(project.documents[0].metadata.contains("option"), "metadata preserved");
        expect(!mm::build::load_project(tree.root(), {.tool = tool, .strict_tree = true}).ok, "strict mode rejects repeated directory");
        for (const auto& key : {"option: warnings yes\n", "reset: warnings\n", "read-only: warnings\n"}) {
            tree.manifest("shared", std::string("kind: test\nname: shared\nunit: test.cpp\n") + key);
            bool ok = true;
            mm::build::load_test(tree.root() / "shared/mm.mdy", ok, {.tool = tool});
            expect(!ok, "standalone path shares key-version gate");
            expect(!mm::build::load_project(tree.root(), {.tool = tool}).ok, "walk shares key-version gate");
        }
        tree.manifest("shared", "kind: dir\nname: shared\n");
    }
    for (const auto& output : {"out", "out-host", "out-target-aarch64-linux-gnu"}) {
        tree.manifest("", std::string("kind: project\nname: p\nfolder: ") + output + "\n");
        tree.manifest(output, "kind: dir\nname: output\n");
        expect(!mm::build::load_project(tree.root(), {.tool = "configure", .strict_tree = true}).ok,
               "configure never traverses a generated output lane");
    }
    tree.manifest("", "kind: project\nname: p\nfolder: outside\n");
    tree.manifest("outside", "kind: dir\nname: outside\n");
    expect(mm::build::load_project(tree.root(), {.tool = "configure", .strict_tree = true}).ok,
           "an unrelated source directory beginning with out remains valid");
}

void output_safety() {
    const mm::test::scoped_tree tree{"options_output"};
    const mm::test::scoped_tree outside{"options_output_outside"};
    tree.manifest("", "kind: project\nname: p\n");
    Resolution result;
    expect(resolve(tree, Build::Debug, result), "valid input resolves");
    std::error_code ec;
    std::filesystem::create_directory_symlink(outside.root(), tree.root() / "out-host", ec);
    expect(!ec, "external output symlink created");
    expect(!mm::configure::write_option_records(tree.root(), "out-host", Build::Debug, result.nodes, result.values, false), "external output rejected");
    expect(!std::filesystem::exists(outside.root() / "resolved-options.mdy"), "no writes outside project");
    std::filesystem::remove(tree.root() / "out-host", ec);
    std::filesystem::create_directory_symlink(tree.root(), tree.root() / "out-host", ec);
    expect(!ec, "output alias to source root created");
    expect(!mm::configure::write_option_records(tree.root(), "out-host", Build::Debug, result.nodes, result.values, false), "output cannot redirect records to source root");
    expect(!std::filesystem::exists(tree.root() / "resolved-options.mdy"), "source tree not modified by output alias");
    std::filesystem::remove(tree.root() / "out-host", ec);
    std::filesystem::create_directories(tree.root() / "out-host/resolved-options.mdy", ec);
    std::ofstream(tree.root() / "out-host/resolved-options.mdy/keep") << "preserve";
    expect(!mm::configure::write_option_records(tree.root(), "out-host", Build::Debug, result.nodes, result.values, false), "rename failure reported");
    expect(read(tree.root() / "out-host/resolved-options.mdy/keep") == "preserve", "failure preserves unrelated files");
}

// Structural properties are declared per node but constrained across use: edges, which the
// folder-tree resolver never follows. Both halves are checked here: what the
// resolver produces, and what edge validation rejects on top of it.
void structural_properties() {
    mm::build::StructuralProperties projection;
    projection.nodes.resize(3);
    projection.nodes[0].buildable_host.value = true;
    projection.nodes[0].buildable_target.value = false;
    projection.nodes[1].buildable_host.value = false;
    projection.nodes[1].buildable_target.value = true;
    projection.nodes[2].buildable_host.value = true;
    projection.nodes[2].buildable_target.value = true;
    expect(projection.lane(true, false) == std::vector<bool>({false, true, true}),
           "an ordinary target uses target capability only");
    expect(projection.lane(true, true) == std::vector<bool>({true, true, true}),
           "a hosted target uses the union of host and target capabilities");

    const mm::test::scoped_tree tree{"options_capabilities"};
    tree.manifest("", "kind: project\nname: p\nfolder: modules\nfolder: apps\nfolder: tools\n");
    tree.manifest("modules", "kind: dir\nname: modules\nfolder: lib\n");
    tree.manifest("modules/lib", "kind: module\nname: lib\nmodule: p.lib\nfile: lib.cppm\n");
    tree.manifest("apps", "kind: dir\nname: apps\nfolder: app\n");
    tree.manifest("apps/app", "kind: app\nname: app\nuse: p.lib\nfile: a.cpp\n");
    tree.manifest_raw("tools", "mm: 1.1\nkind: dir\nname: tools\noption: buildable-target no\nread-only: buildable-target\nfolder: t\n");
    tree.manifest("tools/t", "kind: app\nname: t\nuse: p.lib\nfile: t.cpp\n");

    Resolution result;
    expect(resolve(tree, Build::Debug, result), "capability tree resolves");

    const auto lane = [&](std::string_view node, std::string_view name) {
        for (std::size_t i = 0; i < result.nodes.size(); ++i)
            if (result.nodes[i].name == node) return result.values[i].find(name)->second;
        return mm::configure::OptionValue{};
    };
    expect(lane("p", "buildable-host").boolean && lane("p", "buildable-target").boolean,
           "both lanes default to yes, so restriction is opt-in");
    expect(!lane("t", "buildable-target").boolean && lane("t", "buildable-host").boolean,
           "a directory declaration makes every tool beneath it host only");
    expect(lane("t", "buildable-target").read_only &&
               lane("t", "buildable-target").lock_source == tree.root() / "tools/mm.mdy",
           "the tool inherits the lock and its origin, not its own manifest");
    expect(lane("app", "buildable-target").boolean,
           "a sibling outside the locked branch keeps the default");

    // {host} is a subset of {host, target}: a host-only consumer of a portable
    // module is exactly the arrangement this project relies on.
    expect(mm::build::validate_structural_properties(
               result.nodes, mm::build::structural_properties(result.values), "configure"),
           "host-only tools may use portable modules");

    // The reverse fails: the app claims a lane its dependency cannot supply.
    tree.manifest_raw("modules/lib",
                      "mm: 1.1\nkind: module\nname: lib\nmodule: p.lib\nfile: lib.cppm\noption: buildable-target no\n");
    Resolution restricted;
    expect(resolve(tree, Build::Debug, restricted), "restricted module still resolves");
    expect(!mm::build::validate_structural_properties(
               restricted.nodes, mm::build::structural_properties(restricted.values), "configure"),
           "a portable app cannot use a host-only module");

    // Declaring the same restriction on the consumer satisfies the rule.
    tree.manifest_raw("apps/app",
                      "mm: 1.1\nkind: app\nname: app\nuse: p.lib\nfile: a.cpp\noption: buildable-target no\n");
    Resolution agreed;
    expect(resolve(tree, Build::Debug, agreed), "agreeing tree resolves");
    expect(mm::build::validate_structural_properties(
               agreed.nodes, mm::build::structural_properties(agreed.values), "configure"),
           "matching capabilities satisfy the use: rule");

    // A core consumer cannot cross into a non-core dependency. The dependency's
    // assignment origin is retained for the diagnostic.
    tree.manifest_raw("modules/lib",
                      "mm: 1.1\nkind: module\nname: lib\nmodule: p.lib\nfile: lib.cppm\noption: core no\n");
    tree.manifest("apps/app", "kind: app\nname: app\nuse: p.lib\nfile: a.cpp\n");
    Resolution non_core;
    expect(resolve(tree, Build::Debug, non_core), "non-core dependency resolves as a tree property");
    const auto structural = mm::build::structural_properties(non_core.values);
    expect(!mm::build::validate_structural_properties(non_core.nodes, structural, "configure"),
           "a core node cannot use a non-core module");
    std::size_t library_node = 0;
    for (std::size_t i = 0; i < non_core.nodes.size(); ++i)
        if (non_core.nodes[i].name == "lib") library_node = i;
    expect(!structural.nodes[library_node].core.value &&
               structural.nodes[library_node].core.value_source == tree.root() / "modules/lib/mm.mdy",
           "core value retains its assigning manifest");

    // Restore modules/lib to core so apps/app and tools/t do not violate the core rule.
    tree.manifest("", "kind: project\nname: p\nfolder: modules\nfolder: apps\nfolder: tools\nfolder: libdef\n");
    tree.manifest_raw("libdef", "mm: 1.2\nkind: library\nname: demo\nsource: src\nlicence: LICENSE\n");
    std::ofstream(tree.root() / "libdef/LICENSE") << "mit\n";
    std::filesystem::create_directories(tree.root() / "libdef/src");
    tree.manifest("modules/lib", "kind: module\nname: lib\nmodule: p.lib\nfile: lib.cppm\n");

    // A core module declaring library: is rejected by structural property validation.
    tree.manifest_raw("modules/lib",
                      "mm: 1.2\nkind: module\nname: lib\nmodule: lib.demo\nfile: lib.cppm\nlibrary: demo\n");
    Resolution core_with_library;
    expect(resolve(tree, Build::Debug, core_with_library), "module with library resolves options");
    expect(!mm::build::validate_structural_properties(
               core_with_library.nodes, mm::build::structural_properties(core_with_library.values), "configure"),
           "a core module declaring library: is rejected");

    // A non-core module declaring library: is permitted.
    tree.manifest_raw("modules/lib",
                      "mm: 1.2\nkind: module\nname: lib\nmodule: lib.demo\nfile: lib.cppm\nlibrary: demo\noption: core no\n");
    tree.manifest_raw("apps/app",
                      "mm: 1.1\nkind: app\nname: app\nuse: lib.demo\nfile: a.cpp\noption: core no\n");
    tree.manifest_raw("tools/t",
                      "mm: 1.1\nkind: app\nname: t\nuse: lib.demo\nfile: t.cpp\noption: core no\n");
    Resolution non_core_with_library;
    expect(resolve(tree, Build::Debug, non_core_with_library), "non-core module with library resolves");
    expect(mm::build::validate_structural_properties(
               non_core_with_library.nodes, mm::build::structural_properties(non_core_with_library.values), "configure"),
           "a non-core module declaring library: is permitted");

    tree.manifest("tools/t", "kind: app\nname: t\nuse: p.lib\nfile: t.cpp\n");
    tree.manifest("modules/lib", "kind: module\nname: lib\nmodule: p.lib\nfile: lib.cppm\n");
    tree.manifest("", "kind: project\nname: p\nfolder: modules\nfolder: apps\nfolder: tools\n");

    // A node buildable for nothing is rejected by the resolver itself.
    tree.manifest_raw("apps/app",
                      "mm: 1.1\nkind: app\nname: app\nuse: p.lib\nfile: a.cpp\noption: buildable-host no\noption: buildable-target no\n");
    Resolution empty;
    expect(!resolve(tree, Build::Debug, empty), "a node buildable for no lane is rejected");
}

const mm::test::case_ cases[] = {
    {"shared build defaults", &shared_defaults},
    {"tree inheritance reset and records", &inheritance_reset_and_records},
    {"locks and leaf intent", &locks_and_leaf_intent},
    {"invalid declarations", &invalid_declarations},
    {"directory domain", &directory_domain},
    {"shared schema and strict tree", &shared_schema_and_strict_tree},
    {"output safety", &output_safety},
    {"structural properties", &structural_properties},
};
const mm::test::registrar reg{"mm.configure options", cases};

}
