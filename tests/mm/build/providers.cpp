// Black box tests for platform-interface and platform-provider.
//
// Grammar and reference resolution go through a manifest tree, because that is
// where they are decided. Selection, availability, and closure augmentation are
// pure functions over a loaded Project or a Tree, so those cases build the
// input directly and stay about the mechanism rather than the filesystem.
//
// None of this needs a cross toolchain, Pico SDK, or hardware: a provider is a
// module name and an edge, and every case here is about names and edges.
//
// Negative cases drive the loader into its error paths, so this suite prints
// "configure: ..." diagnostics to stderr while passing.

#include <cstddef>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <vector>

import mm.build;
import mm.test;

namespace {

using mm::test::expect;

constexpr std::string_view sdk_header =
    "mm: 1.2\nkind: sdk\nname: demo-sdk\ntarget: arm-none-eabi\ncompiler-family: gcc\n"
    "runtime: newlib\nspecs-profile: rdimon\n";

constexpr std::string_view board_header =
    "mm: 1.2\nkind: board\nname: demo-board\nsdk: demo-sdk\ncpu: cortex-m0plus\n"
    "instruction-set: thumb\nfloat-abi: soft\nlinker-script: link.ld\nfile: vectors.cpp\n";

// A project whose interface, two providers, SDK, and board are all present.
// Each caller appends the declarations the case is about.
void write_tree(const mm::test::scoped_tree& tree, std::string_view sdk_extra,
                std::string_view board_extra, std::string_view interface_extra = "") {
    tree.manifest("", "kind: project\nname: p\nfolder: iface\nfolder: first\nfolder: second\n"
                      "folder: app\nfolder: sdk\nfolder: board\n");
    tree.manifest_raw("iface", std::string("mm: 1.2\nkind: module\nname: iface\n"
                                           "module: mm.iface\nfile: iface.cppm\n") +
                                   std::string(interface_extra));
    tree.manifest_raw("first", "mm: 1.2\nkind: module\nname: first\nmodule: platform.first.iface\n"
                               "use: mm.iface\nfile: first.cppm\n");
    tree.manifest_raw("second", "mm: 1.2\nkind: module\nname: second\n"
                                "module: platform.second.iface\nuse: mm.iface\n"
                                "file: second.cppm\n");
    tree.manifest_raw("app", "mm: 1.2\nkind: app\nname: demo-app\nuse: mm.iface\nfile: main.cpp\n");
    tree.manifest_raw("sdk", std::string(sdk_header) + std::string(sdk_extra));
    tree.manifest_raw("board", std::string(board_header) + std::string(board_extra));

    // A board's linker-script and file: entries are resolved against the working
    // tree, so they have to exist for the definition to parse at all.
    std::ofstream(tree.root() / "board/link.ld") << "/* fixture */\n";
    std::ofstream(tree.root() / "board/vectors.cpp") << "// fixture\n";
}

mm::build::Project load(const mm::test::scoped_tree& tree) {
    return mm::build::load_project(tree.root(), {.tool = "configure", .strict_tree = true});
}

std::size_t node_named(const mm::build::Project& project, std::string_view name) {
    for (std::size_t i = 0; i < project.nodes.size(); ++i)
        if (project.nodes[i].name == name) return i;
    return mm::build::no_target;
}

// --- the marker ---------------------------------------------------------

void marker_grammar() {
    {
        const mm::test::scoped_tree tree{"provider_marker_ok"};
        write_tree(tree, "", "", "platform-interface:\n");
        const auto project = load(tree);
        expect(project.ok, "a valueless platform-interface marker loads");
        const auto node = node_named(project, "iface");
        expect(node != mm::build::no_target &&
                   project.targets[project.target[node]].platform_interface,
               "the marker is carried on the interface module's target");
    }
    {
        const mm::test::scoped_tree tree{"provider_marker_value"};
        write_tree(tree, "", "", "platform-interface: mm.iface\n");
        expect(!load(tree).ok, "platform-interface with a value is rejected");
    }
    {
        const mm::test::scoped_tree tree{"provider_marker_twice"};
        write_tree(tree, "", "", "platform-interface:\nplatform-interface:\n");
        expect(!load(tree).ok, "a repeated platform-interface marker is rejected");
    }
    {
        const mm::test::scoped_tree tree{"provider_marker_kind"};
        tree.manifest("", "kind: project\nname: p\nfolder: app\n");
        tree.manifest_raw("app", "mm: 1.2\nkind: app\nname: demo-app\nfile: main.cpp\n"
                                 "platform-interface:\n");
        expect(!load(tree).ok, "platform-interface is rejected on a kind other than module");
    }
    {
        const mm::test::scoped_tree tree{"provider_marker_version"};
        tree.manifest("", "kind: project\nname: p\nfolder: iface\n");
        tree.manifest_raw("iface", "mm: 1.1\nkind: module\nname: iface\nmodule: mm.iface\n"
                                   "file: iface.cppm\nplatform-interface:\n");
        expect(!load(tree).ok, "an older manifest version rejects the marker rather than "
                               "assigning it an older meaning");
    }
}

// --- reference resolution -----------------------------------------------

void provider_grammar_and_references() {
    {
        const mm::test::scoped_tree tree{"provider_binding_ok"};
        write_tree(tree, "platform-provider: mm.iface platform.first.iface\n", "",
                   "platform-interface:\n");
        const auto project = load(tree);
        expect(project.ok, "a resolved binding loads");
        expect(project.sdks.size() == 1 && project.sdks.front().providers.size() == 1 &&
                   project.sdks.front().providers.front().interface_module == "mm.iface" &&
                   project.sdks.front().providers.front().provider_module ==
                       "platform.first.iface",
               "the SDK carries the declared binding");
    }
    {
        const mm::test::scoped_tree tree{"provider_binding_arity"};
        write_tree(tree, "platform-provider: mm.iface\n", "", "platform-interface:\n");
        expect(!load(tree).ok, "a binding naming only an interface is rejected");
    }
    {
        const mm::test::scoped_tree tree{"provider_binding_extra"};
        write_tree(tree, "platform-provider: mm.iface platform.first.iface extra\n", "",
                   "platform-interface:\n");
        expect(!load(tree).ok, "a binding with a third field is rejected");
    }
    {
        const mm::test::scoped_tree tree{"provider_binding_unmarked"};
        write_tree(tree, "platform-provider: mm.iface platform.first.iface\n", "");
        expect(!load(tree).ok,
               "binding a module that is not marked as a platform interface is rejected");
    }
    {
        const mm::test::scoped_tree tree{"provider_binding_unknown_iface"};
        write_tree(tree, "platform-provider: mm.absent platform.first.iface\n", "",
                   "platform-interface:\n");
        expect(!load(tree).ok, "binding an unknown interface is rejected");
    }
    {
        const mm::test::scoped_tree tree{"provider_binding_unknown_provider"};
        write_tree(tree, "platform-provider: mm.iface platform.absent.iface\n", "",
                   "platform-interface:\n");
        expect(!load(tree).ok, "binding an unknown provider module is rejected");
    }
    {
        const mm::test::scoped_tree tree{"provider_binding_self"};
        write_tree(tree, "platform-provider: mm.iface mm.iface\n", "", "platform-interface:\n");
        expect(!load(tree).ok, "binding an interface to itself is rejected");
    }
    {
        const mm::test::scoped_tree tree{"provider_binding_unrelated"};
        tree.manifest("", "kind: project\nname: p\nfolder: iface\nfolder: other\nfolder: sdk\n");
        tree.manifest_raw("iface", "mm: 1.2\nkind: module\nname: iface\nmodule: mm.iface\n"
                                   "file: iface.cppm\nplatform-interface:\n");
        tree.manifest_raw("other", "mm: 1.2\nkind: module\nname: other\nmodule: mm.other\n"
                                   "file: other.cppm\n");
        tree.manifest_raw("sdk", std::string(sdk_header) +
                                     "platform-provider: mm.iface mm.other\n");
        expect(!load(tree).ok,
               "a provider whose use closure does not reach its interface is rejected");
    }
    {
        const mm::test::scoped_tree tree{"provider_binding_duplicate"};
        write_tree(tree,
                   "platform-provider: mm.iface platform.first.iface\n"
                   "platform-provider: mm.iface platform.second.iface\n",
                   "", "platform-interface:\n");
        expect(!load(tree).ok, "two bindings for one interface in one SDK are rejected");
    }
    {
        const mm::test::scoped_tree tree{"provider_binding_duplicate_board"};
        write_tree(tree, "",
                   "platform-provider: mm.iface platform.first.iface\n"
                   "platform-provider: mm.iface platform.second.iface\n",
                   "platform-interface:\n");
        expect(!load(tree).ok, "two bindings for one interface on one board are rejected");
    }
}

// --- selection ----------------------------------------------------------

void precedence_and_selection() {
    const mm::test::scoped_tree tree{"provider_precedence"};
    write_tree(tree, "platform-provider: mm.iface platform.first.iface\n",
               "platform-provider: mm.iface platform.second.iface\n", "platform-interface:\n");
    const auto project = load(tree);
    expect(project.ok, "a project binding one interface from both owners loads");

    // An SDK alone binds its default.
    mm::build::Platform sdk_only;
    sdk_only.sdk = "demo-sdk";
    const auto by_sdk = mm::build::platform_providers(project, true, &sdk_only, "test");
    const auto* sdk_binding = by_sdk.binding("mm.iface");
    expect(sdk_binding != nullptr && sdk_binding->provider_module == "platform.first.iface" &&
               sdk_binding->owner == "demo-sdk" && !sdk_binding->from_board,
           "a selected SDK supplies its default provider");

    // The board overrides it, and does so structurally: the board's binding is
    // declared after the SDK's here, but the rule is most specific wins rather
    // than last declaration wins.
    mm::build::Platform with_board;
    with_board.sdk = "demo-sdk";
    with_board.board = "demo-board";
    const auto by_board = mm::build::platform_providers(project, true, &with_board, "test");
    const auto* board_binding = by_board.binding("mm.iface");
    expect(board_binding != nullptr && board_binding->provider_module == "platform.second.iface" &&
               board_binding->owner == "demo-board" && board_binding->from_board,
           "a selected board overrides its SDK's default for the same interface");
    expect(by_board.effective.size() == 1,
           "an overridden interface resolves to exactly one effective provider");

    // Both modules are declared providers in every lane; only one is selected.
    expect(by_board.declares_provider("platform.first.iface") &&
               by_board.declares_provider("platform.second.iface"),
           "every module named by a declaration is a provider target");
    expect(!by_board.selects_provider("platform.first.iface") &&
               by_board.selects_provider("platform.second.iface"),
           "only the effective provider is selected");

    // The host lane selects no platform at all.
    const auto host = mm::build::platform_providers(project, false, nullptr, "test");
    expect(host.binding("mm.iface") == nullptr, "a host lane binds no provider");
    expect(host.interfaces.size() == 1 && host.interfaces.front() == "mm.iface",
           "the interface is still known in a lane that binds nothing");
}

void requirements_follow_the_authored_closure() {
    const mm::test::scoped_tree tree{"provider_requirements"};
    write_tree(tree, "platform-provider: mm.iface platform.first.iface\n", "",
               "platform-interface:\n");
    const auto project = load(tree);
    expect(project.ok, "the project loads");

    mm::build::Platform platform;
    platform.sdk = "demo-sdk";
    const auto providers = mm::build::platform_providers(project, true, &platform, "test");

    const auto app = node_named(project, "demo-app");
    expect(app != mm::build::no_target, "the application node is found");
    expect(providers.requirements[app].size() == 1 &&
               providers.requirements[app].front() == "mm.iface",
           "an application reaching the interface requires it");
    expect(providers.unmet_requirement(app).empty(),
           "a requirement a selected platform binds is met");

    const auto sdk_node = node_named(project, "demo-sdk");
    expect(providers.requirements[sdk_node].empty(),
           "a node that is not a target requires nothing");
}

void requirements_follow_selected_provider_closures() {
    const auto write_nested = [](const mm::test::scoped_tree& tree, bool bind_second) {
        tree.manifest("", "kind: project\nname: p\nfolder: iface-a\nfolder: iface-b\n"
                          "folder: provider-a\nfolder: provider-b\nfolder: app\nfolder: sdk\n");
        tree.manifest_raw("iface-a", "mm: 1.2\nkind: module\nname: iface-a\n"
                                     "module: mm.iface_a\nfile: iface.cppm\n"
                                     "platform-interface:\n");
        tree.manifest_raw("iface-b", "mm: 1.2\nkind: module\nname: iface-b\n"
                                     "module: mm.iface_b\nfile: iface.cppm\n"
                                     "platform-interface:\n");
        tree.manifest_raw("provider-a", "mm: 1.2\nkind: module\nname: provider-a\n"
                                        "module: platform.iface_a\nuse: mm.iface_a\n"
                                        "use: mm.iface_b\nfile: provider.cppm\n");
        tree.manifest_raw("provider-b", "mm: 1.2\nkind: module\nname: provider-b\n"
                                        "module: platform.iface_b\nuse: mm.iface_b\n"
                                        "file: provider.cppm\n");
        tree.manifest_raw("app", "mm: 1.2\nkind: app\nname: nested-app\n"
                                  "use: mm.iface_a\nfile: main.cpp\n");
        tree.manifest_raw(
            "sdk", std::string(sdk_header) +
                       "platform-provider: mm.iface_a platform.iface_a\n" +
                       (bind_second
                            ? "platform-provider: mm.iface_b platform.iface_b\n"
                            : ""));
    };

    {
        const mm::test::scoped_tree tree{"provider_nested_unmet"};
        write_nested(tree, false);
        const auto project = load(tree);
        expect(project.ok, "a provider may use a second platform interface");
        mm::build::Platform platform;
        platform.sdk = "demo-sdk";
        const auto providers = mm::build::platform_providers(project, true, &platform, "test");
        const auto app = node_named(project, "nested-app");
        expect(providers.requirements[app].size() == 2,
               "provider requirements expand transitively");
        expect(providers.unmet_requirement(app).find("mm.iface_b") != std::string::npos,
               "a missing nested provider is diagnosed before compilation");
    }
    {
        const mm::test::scoped_tree tree{"provider_nested_met"};
        write_nested(tree, true);
        const auto project = load(tree);
        expect(project.ok, "both nested bindings load");
        mm::build::Platform platform;
        platform.sdk = "demo-sdk";
        const auto providers = mm::build::platform_providers(project, true, &platform, "test");
        const auto app = node_named(project, "nested-app");
        expect(providers.requirements[app].size() == 2 &&
                   providers.unmet_requirement(app).empty(),
               "a complete nested provider set is available");
    }
}

void an_unmet_requirement_names_the_platform() {
    const mm::test::scoped_tree tree{"provider_unmet"};
    write_tree(tree, "", "", "platform-interface:\n");
    const auto project = load(tree);
    expect(project.ok, "a project with no binding at all still loads");

    const auto app = node_named(project, "demo-app");

    mm::build::Platform platform;
    platform.sdk = "demo-sdk";
    platform.board = "demo-board";
    const auto providers = mm::build::platform_providers(project, true, &platform, "test");

    const auto reason = providers.unmet_requirement(app);
    expect(reason.find("mm.iface") != std::string::npos, "the reason names the interface");
    expect(reason.find("demo-board") != std::string::npos, "the reason names the board");
    expect(reason.find("demo-sdk") != std::string::npos, "the reason names the SDK");

    const auto unavailable =
        mm::build::availability(project, app, true, true, &platform, &providers);
    expect(!unavailable.available, "an executable with an unmet requirement is unavailable");
    expect(unavailable.reason.find("demo-app") != std::string::npos,
           "the availability reason names the executable");

    // Compiling and modelling the interface never needs a platform: only an
    // executable that reaches it does.
    const auto iface = node_named(project, "iface");
    expect(mm::build::availability(project, iface, true, true, &platform, &providers).available,
           "the interface module itself stays available with no provider bound");
}

void a_provider_is_never_an_independent_root() {
    // Both modules are named by a declaration; only the SDK's is selected,
    // because no board is.
    const mm::test::scoped_tree tree{"provider_roots"};
    write_tree(tree, "platform-provider: mm.iface platform.first.iface\n",
               "platform-provider: mm.iface platform.second.iface\n", "platform-interface:\n");
    const auto project = load(tree);
    expect(project.ok, "the project loads");

    mm::build::Platform platform;
    platform.sdk = "demo-sdk";
    const auto providers = mm::build::platform_providers(project, true, &platform, "test");

    const auto selected = node_named(project, "first");
    const auto unselected = node_named(project, "second");
    expect(mm::build::availability(project, selected, true, true, &platform, &providers).available,
           "the selected provider is available");
    const auto skipped =
        mm::build::availability(project, unselected, true, true, &platform, &providers);
    expect(!skipped.available, "an unselected provider is an unavailable skipped target");
    expect(skipped.reason.find("platform.second.iface") != std::string::npos,
           "the reason names the provider module");

    // Without the analysis, nothing classifies a provider, and the old answer
    // stands. This is what keeps every other caller of availability unchanged.
    expect(mm::build::availability(project, unselected, true, true, &platform).available,
           "an unanalysed lane treats a provider as an ordinary module");
}

// --- closure augmentation -----------------------------------------------

mm::build::BuildableNode target_of(std::string_view kind, std::string_view name,
                                   std::string_view module_name, std::vector<std::string> uses,
                                   bool interface_marker = false) {
    mm::build::BuildableNode target;
    target.kind = std::string(kind);
    target.name = std::string(name);
    target.module_name = std::string(module_name);
    target.dir = std::string(name);
    target.uses = std::move(uses);
    target.platform_interface = interface_marker;
    target.objects.push_back(std::string(name) + ".o");
    return target;
}

bool has_object(const std::vector<std::filesystem::path>& objects, std::string_view name) {
    for (const auto& object : objects)
        if (object.string() == name) return true;
    return false;
}

std::size_t count_object(const std::vector<std::filesystem::path>& objects,
                         std::string_view name) {
    std::size_t total = 0;
    for (const auto& object : objects)
        if (object.string() == name) ++total;
    return total;
}

void augmentation_adds_only_what_is_required() {
    mm::build::Tree tree;
    tree.targets.push_back(target_of("app", "user", "", {"mm.iface"}));       // 0
    tree.targets.push_back(target_of("app", "bystander", "", {"mm.other"}));  // 1
    // iface-extra deliberately precedes iface: it is reached only after iface's
    // provider is merged, so a one-pass forward scan would miss its provider.
    tree.targets.push_back(
        target_of("module", "iface-extra", "mm.iface_extra", {}, true));
    tree.targets.push_back(target_of("module", "iface", "mm.iface", {}, true));
    tree.targets.push_back(target_of("module", "other", "mm.other", {}));
    tree.targets.push_back(target_of("module", "provider", "platform.demo.iface",
                                     {"mm.iface", "mm.iface_extra", "mm.support"}));
    tree.targets.push_back(target_of("module", "provider-extra",
                                     "platform.demo.iface_extra", {"mm.iface_extra"}));
    tree.targets.push_back(target_of("module", "support", "mm.support", {}));

    mm::build::PlatformProviders providers;
    providers.interfaces.push_back("mm.iface");
    providers.interfaces.push_back("mm.iface_extra");
    providers.declared.push_back("platform.demo.iface");
    providers.declared.push_back("platform.demo.iface_extra");
    providers.effective.push_back({"mm.iface", "platform.demo.iface", "demo-sdk", false});
    providers.effective.push_back(
        {"mm.iface_extra", "platform.demo.iface_extra", "demo-sdk", false});

    std::vector<std::string> merged;
    const auto objects = mm::build::augmented_closure(tree, 0, providers, &merged);
    expect(merged.size() == 2 && merged.front() == "platform.demo.iface" &&
               merged.back() == "platform.demo.iface_extra",
           "direct and nested providers are reported");
    expect(has_object(objects, "user.o") && has_object(objects, "iface.o"),
           "the authored closure is still linked");
    expect(has_object(objects, "provider.o"), "the selected provider is linked");
    expect(has_object(objects, "provider-extra.o"),
           "an interface reached from a provider receives its own provider");
    expect(has_object(objects, "support.o"), "the provider's own closure is linked");
    expect(count_object(objects, "iface.o") == 1,
           "an object shared by the executable and its provider is linked once");

    // An executable that does not reach the interface receives nothing.
    std::vector<std::string> none;
    const auto bystander = mm::build::augmented_closure(tree, 1, providers, &none);
    expect(none.empty(), "an executable that does not reach the interface merges no provider");
    expect(!has_object(bystander, "provider.o") && !has_object(bystander, "iface.o"),
           "an unrelated executable receives no provider object");
    expect(bystander.size() == 2, "an unrelated executable links only its authored closure");

    // With no binding, the interface is linked and answers for itself.
    const mm::build::PlatformProviders unbound;
    const auto unserved = mm::build::augmented_closure(tree, 0, unbound, nullptr);
    expect(has_object(unserved, "iface.o") && !has_object(unserved, "provider.o"),
           "an unbound interface links without a provider");
}

const mm::test::case_ cases[] = {
    {"platform-interface grammar", &marker_grammar},
    {"platform-provider grammar and references", &provider_grammar_and_references},
    {"board over SDK precedence", &precedence_and_selection},
    {"interface requirements", &requirements_follow_the_authored_closure},
    {"nested interface requirements", &requirements_follow_selected_provider_closures},
    {"unmet requirement diagnostics", &an_unmet_requirement_names_the_platform},
    {"providers are not roots", &a_provider_is_never_an_independent_root},
    {"closure augmentation", &augmentation_adds_only_what_is_required},
};

const mm::test::registrar reg{"mm.build platform providers", cases};

}  // namespace
