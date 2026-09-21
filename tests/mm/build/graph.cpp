// Black box tests for mm.build's dependency ordering.
//
// order() and closure() are pure functions over a Tree, so these build the Tree
// directly rather than going through the filesystem. That keeps the cases about
// the graph and nothing else.
//
// Negative cases drive order() into its error paths, so this suite prints
// "build: ..." diagnostics to stderr while passing.

#include <cstddef>
#include <filesystem>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

import mm.build;
import mm.test;

namespace {

using mm::test::expect;

mm::build::BuildableNode module_target(std::string_view name, std::string_view module_name,
                                std::vector<std::string> uses = {}) {
    mm::build::BuildableNode target;
    target.kind = "module";
    target.name = std::string(name);
    target.module_name = std::string(module_name);
    target.dir = std::string("modules/") + std::string(name);
    target.sources.push_back({std::string(name) + ".cppm", std::string(module_name)});
    target.uses = std::move(uses);
    target.objects.push_back(std::string(name) + ".o");
    return target;
}

mm::build::BuildableNode app_target(std::string_view name, std::vector<std::string> uses = {}) {
    mm::build::BuildableNode target;
    target.kind = "app";
    target.name = std::string(name);
    target.dir = std::string("apps/") + std::string(name);
    target.sources.push_back({std::string(name) + ".cpp", ""});
    target.uses = std::move(uses);
    target.objects.push_back(std::string(name) + ".o");
    return target;
}

std::size_t position_of(const mm::build::Tree& tree, const std::vector<std::size_t>& order,
                        std::string_view name) {
    for (std::size_t i = 0; i < order.size(); ++i)
        if (tree.targets[order[i]].name == name) return i;
    return order.size();
}

bool contains_object(const std::vector<std::filesystem::path>& objects, std::string_view name) {
    for (const auto& object : objects)
        if (object.string() == name) return true;
    return false;
}

int count_object(const std::vector<std::filesystem::path>& objects, std::string_view name) {
    int found = 0;
    for (const auto& object : objects)
        if (object.string() == name) ++found;
    return found;
}

// --- ordering -----------------------------------------------------------

void puts_dependencies_before_dependents() {
    mm::build::Tree tree;
    tree.targets.push_back(app_target("tool", {"mm.build"}));
    tree.targets.push_back(module_target("build", "mm.build", {"mm.mdy"}));
    tree.targets.push_back(module_target("mdy", "mm.mdy"));

    std::vector<std::size_t> order;
    mm::test::expect(mm::build::order(tree, order), "expected an acyclic graph to order");
    mm::test::expect(order.size() == 3, "expected every target in the order");

    const auto mdy = position_of(tree, order, "mdy");
    const auto build = position_of(tree, order, "build");
    const auto tool = position_of(tree, order, "tool");

    mm::test::expect(mdy < build, "expected mm.mdy to be ordered before mm.build");
    mm::test::expect(build < tool, "expected mm.build to be ordered before the app that uses it");
}

void orders_a_diamond_dependency() {
    mm::build::Tree tree;
    tree.targets.push_back(app_target("app", {"mm.left", "mm.right"}));
    tree.targets.push_back(module_target("left", "mm.left", {"mm.base"}));
    tree.targets.push_back(module_target("right", "mm.right", {"mm.base"}));
    tree.targets.push_back(module_target("base", "mm.base"));

    std::vector<std::size_t> order;
    mm::test::expect(mm::build::order(tree, order), "expected a diamond to order");
    mm::test::expect(order.size() == 4, "expected each target exactly once");

    const auto base = position_of(tree, order, "base");
    mm::test::expect(base < position_of(tree, order, "left"), "expected the base before left");
    mm::test::expect(base < position_of(tree, order, "right"), "expected the base before right");
    mm::test::expect(position_of(tree, order, "app") == 3, "expected the app last");
}

void rejects_a_dependency_cycle() {
    mm::build::Tree tree;
    tree.targets.push_back(module_target("a", "mm.a", {"mm.b"}));
    tree.targets.push_back(module_target("b", "mm.b", {"mm.a"}));

    std::vector<std::size_t> order;
    mm::test::expect(!mm::build::order(tree, order), "expected a use: cycle to be rejected");
}

void rejects_self_dependency() {
    mm::build::Tree tree;
    tree.targets.push_back(module_target("a", "mm.a", {"mm.a"}));

    std::vector<std::size_t> order;
    mm::test::expect(!mm::build::order(tree, order), "expected a module using itself to be rejected");
}

void rejects_unknown_module() {
    mm::build::Tree tree;
    tree.targets.push_back(app_target("app", {"mm.absent"}));

    std::vector<std::size_t> order;
    mm::test::expect(!mm::build::order(tree, order), "expected an unknown use: target to be rejected");
}

// An app is not importable, so naming one in use: must not resolve.
void does_not_resolve_use_to_an_app() {
    mm::build::Tree tree;
    tree.targets.push_back(app_target("first", {"second"}));
    tree.targets.push_back(app_target("second"));

    std::vector<std::size_t> order;
    mm::test::expect(!mm::build::order(tree, order), "expected use: to resolve only to modules");
}

void orders_independent_targets() {
    mm::build::Tree tree;
    tree.targets.push_back(module_target("a", "mm.a"));
    tree.targets.push_back(module_target("b", "mm.b"));

    std::vector<std::size_t> order;
    mm::test::expect(mm::build::order(tree, order), "expected unrelated targets to order");
    mm::test::expect(order.size() == 2, "expected both targets in the order");
}

// --- ordering one target's dependencies ---------------------------------

// A test target must build the modules it uses and nothing else: order_from is
// what makes use: work for tests without dragging in every app in the project.
void order_from_visits_only_what_is_reachable() {
    mm::build::Tree tree;
    tree.targets.push_back(module_target("mdy", "mm.mdy"));
    tree.targets.push_back(module_target("harness", "mm.test"));
    tree.targets.push_back(app_target("unrelated", {"mm.mdy"}));

    mm::build::BuildableNode test;
    test.kind = "test";
    test.name = "mdy";
    test.uses = {"mm.test", "mm.mdy"};
    test.sources.push_back({"tests/mm/mdy/integration.cpp", ""});
    tree.targets.push_back(std::move(test));

    std::vector<std::size_t> order;
    mm::test::expect(mm::build::order_from(tree, 3, order), "expected the test target to order");

    mm::test::expect(order.size() == 3, "expected only the test and the two modules it uses");
    mm::test::expect(position_of(tree, order, "unrelated") == order.size(),
                     "expected an unrelated app to be left out");
    mm::test::expect(order.back() == 3, "expected the target itself to come last");
    mm::test::expect(position_of(tree, order, "harness") < position_of(tree, order, "mdy"),
                     "expected uses to be visited in declared order");
}

void order_from_resolves_transitively() {
    mm::build::Tree tree;
    tree.targets.push_back(module_target("mdy", "mm.mdy"));
    tree.targets.push_back(module_target("build", "mm.build", {"mm.mdy"}));

    mm::build::BuildableNode test;
    test.kind = "test";
    test.name = "build";
    test.uses = {"mm.build"};
    test.sources.push_back({"tests/mm/build/walk.cpp", ""});
    tree.targets.push_back(std::move(test));

    std::vector<std::size_t> order;
    mm::test::expect(mm::build::order_from(tree, 2, order), "expected the test target to order");

    mm::test::expect(order.size() == 3, "expected the transitive module to be pulled in");
    mm::test::expect(position_of(tree, order, "mdy") < position_of(tree, order, "build"),
                     "expected the transitive dependency to be built first");
}

void order_from_a_leaf_is_just_itself() {
    mm::build::Tree tree;
    tree.targets.push_back(module_target("mdy", "mm.mdy"));

    std::vector<std::size_t> order;
    mm::test::expect(mm::build::order_from(tree, 0, order), "expected a leaf to order");
    mm::test::expect(order.size() == 1, "expected a target with no uses to order alone");
}

void order_from_rejects_unknown_module() {
    mm::build::Tree tree;
    tree.targets.push_back(app_target("app", {"mm.absent"}));

    std::vector<std::size_t> order;
    mm::test::expect(!mm::build::order_from(tree, 0, order),
                     "expected an unknown use: target to be rejected");
}

void order_from_rejects_a_cycle() {
    mm::build::Tree tree;
    tree.targets.push_back(module_target("a", "mm.a", {"mm.b"}));
    tree.targets.push_back(module_target("b", "mm.b", {"mm.a"}));

    std::vector<std::size_t> order;
    mm::test::expect(!mm::build::order_from(tree, 0, order), "expected a use: cycle to be rejected");
}

// --- link closure -------------------------------------------------------

void closure_gathers_transitive_objects() {
    mm::build::Tree tree;
    tree.targets.push_back(app_target("tool", {"mm.build"}));
    tree.targets.push_back(module_target("build", "mm.build", {"mm.mdy"}));
    tree.targets.push_back(module_target("mdy", "mm.mdy"));

    const auto objects = mm::build::closure(tree, 0);

    mm::test::expect(objects.size() == 3, "expected the app and both modules on the link line");
    mm::test::expect(objects.front().string() == "tool.o", "expected the target's own object first");
    mm::test::expect(contains_object(objects, "build.o"), "expected the direct dependency");
    mm::test::expect(contains_object(objects, "mdy.o"), "expected the transitive dependency");
}

void closure_does_not_repeat_a_shared_dependency() {
    mm::build::Tree tree;
    tree.targets.push_back(app_target("app", {"mm.left", "mm.right"}));
    tree.targets.push_back(module_target("left", "mm.left", {"mm.base"}));
    tree.targets.push_back(module_target("right", "mm.right", {"mm.base"}));
    tree.targets.push_back(module_target("base", "mm.base"));

    const auto objects = mm::build::closure(tree, 0);

    mm::test::expect(count_object(objects, "base.o") == 1,
                     "expected a shared dependency to appear once on the link line");
    mm::test::expect(objects.size() == 4, "expected four distinct objects");
}

void closure_of_a_leaf_is_itself() {
    mm::build::Tree tree;
    tree.targets.push_back(module_target("mdy", "mm.mdy"));

    const auto objects = mm::build::closure(tree, 0);

    mm::test::expect(objects.size() == 1, "expected a module with no uses to link only itself");
}

// closure() is called after compile() has filled in objects, and a target may
// have several. Order within a target has to follow declaration order.
void closure_keeps_object_order_within_a_target() {
    mm::build::Tree tree;
    auto target = module_target("mdy", "mm.mdy");
    target.objects.clear();
    target.objects.push_back("types.o");
    target.objects.push_back("impl.o");
    target.objects.push_back("mdy.o");
    tree.targets.push_back(std::move(target));

    const auto objects = mm::build::closure(tree, 0);

    mm::test::expect(objects.size() == 3, "expected every object of the target");
    mm::test::expect(objects[0].string() == "types.o", "expected the types partition first");
    mm::test::expect(objects[1].string() == "impl.o", "expected the impl partition second");
    mm::test::expect(objects[2].string() == "mdy.o", "expected the interface last");
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

std::size_t count_object_times(const std::vector<std::filesystem::path>& objects,
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
    expect(count_object_times(objects, "iface.o") == 1,
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
    { "puts dependencies before dependents",     &puts_dependencies_before_dependents },
    { "orders a diamond dependency",             &orders_a_diamond_dependency },
    { "rejects a dependency cycle",              &rejects_a_dependency_cycle },
    { "rejects self dependency",                 &rejects_self_dependency },
    { "rejects unknown module",                  &rejects_unknown_module },
    { "does not resolve use: to an app",         &does_not_resolve_use_to_an_app },
    { "orders independent targets",              &orders_independent_targets },
    { "order_from visits only what is reachable", &order_from_visits_only_what_is_reachable },
    { "order_from resolves transitively",        &order_from_resolves_transitively },
    { "order_from of a leaf is just itself",     &order_from_a_leaf_is_just_itself },
    { "order_from rejects unknown module",       &order_from_rejects_unknown_module },
    { "order_from rejects a cycle",              &order_from_rejects_a_cycle },
    { "closure gathers transitive objects",      &closure_gathers_transitive_objects },
    { "closure does not repeat a shared dep",    &closure_does_not_repeat_a_shared_dependency },
    { "closure of a leaf is itself",             &closure_of_a_leaf_is_itself },
    { "closure keeps object order",              &closure_keeps_object_order_within_a_target },
    {"closure augmentation", &augmentation_adds_only_what_is_required},
};

const mm::test::registrar reg{"mm.build graph", cases};

}
