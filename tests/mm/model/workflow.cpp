// Black box tests for mm.model::Loaded::operations() and
// mm::model::recommended_sequence(), run against this project's own real
// tree.

#include <cstddef>
#include <fstream>
#include <iterator>
#include <string>
#include <string_view>
#include <vector>

import mm.model;
import mm.test;
import models.tool;
import models.workflow;

namespace {

const models::Operation* find_operation(const std::vector<const models::Operation*>& operations,
                                        std::string_view name) {
    for (const auto* operation : operations)
        if (operation->name() == name) return operation;
    return nullptr;
}

bool invokes_name(const models::Operation& operation, std::size_t branch, std::string_view name) {
    for (const auto* tool : operation.invokes(branch))
        if (tool != nullptr && tool->name() == name) return true;
    return false;
}

void seven_operations_are_present() {
    bool ok = false;
    auto loaded = mm::model::Loaded::load(".", ok);
    const auto operations = loaded.operations();
    mm::test::expect(operations.size() == 7, "expected exactly the seven documented operations");
}

void bootstrap_has_two_branches_with_the_same_products() {
    bool ok = false;
    auto loaded = mm::model::Loaded::load(".", ok);
    const auto* bootstrap = find_operation(loaded.operations(), "bootstrap");
    mm::test::expect(bootstrap != nullptr, "expected a bootstrap operation");
    if (bootstrap == nullptr) return;

    mm::test::expect(bootstrap->branch_count() == 2, "expected bootstrap to have two branches");
    mm::test::expect(bootstrap->role() == models::Role::Required, "expected bootstrap to be Required");

    mm::test::expect(invokes_name(*bootstrap, 0, "build0"),
                     "expected bootstrap's first branch to invoke build0");
    mm::test::expect(invokes_name(*bootstrap, 1, "c++"),
                     "expected bootstrap's fallback branch to invoke c++ directly");

    const auto produces = bootstrap->produces();
    bool has_staged = false;
    for (const auto kind : produces)
        if (kind == models::ArtifactKind::Staged) has_staged = true;
    mm::test::expect(has_staged, "expected bootstrap to produce Staged regardless of branch");
}

// A prior version of this test asserted the model's own claim that
// build.sh invokes build1 and requires Staged, and it kept passing after
// build.sh actually changed to invoke out/bin/build instead — because it
// only ever checked the hand authored model against itself, never against
// build.sh's real content. This version reads the actual script, so a
// future edit to build.sh that the model is not updated to match fails
// here instead of silently passing.
void build_matches_the_real_build_sh() {
    bool ok = false;
    auto loaded = mm::model::Loaded::load(".", ok);
    const auto* build = find_operation(loaded.operations(), "build");
    mm::test::expect(build != nullptr, "expected a build operation");
    if (build == nullptr) return;

    // Optional, not Required: bootstrap.sh's final step already produces
    // every artifact build.sh would, so nothing after it in the sequence
    // depends on build.sh having run.
    mm::test::expect(build->role() == models::Role::Optional, "expected build to be Optional");

    std::ifstream script("build.sh");
    const std::string content((std::istreambuf_iterator<char>(script)),
                              std::istreambuf_iterator<char>());
    mm::test::expect(content.find("bin/build") != std::string::npos,
                     "expected build.sh to actually invoke out/bin/build; "
                     "update the model in build_operations() if this changed");

    bool invokes_build = false;
    for (const auto* tool : build->invokes(0))
        if (tool != nullptr && tool->name() == "build") invokes_build = true;
    mm::test::expect(invokes_build, "expected the model's build operation to invoke the build tool");

    bool requires_installed_binary = false;
    for (const auto kind : build->requires_artifacts())
        if (kind == models::ArtifactKind::InstalledBinary) requires_installed_binary = true;
    mm::test::expect(requires_installed_binary,
                     "expected build to require InstalledBinary (out/bin/build)");
}

void clean_is_user_initiated_and_invokes_nothing() {
    bool ok = false;
    auto loaded = mm::model::Loaded::load(".", ok);
    const auto* clean = find_operation(loaded.operations(), "clean");
    mm::test::expect(clean != nullptr, "expected a clean operation");
    if (clean == nullptr) return;

    mm::test::expect(clean->role() == models::Role::UserInitiated,
                     "expected clean to be UserInitiated, not Optional");
    mm::test::expect(clean->invokes(0).empty(), "expected clean to invoke no tool");
}

void check_and_model_are_optional() {
    bool ok = false;
    auto loaded = mm::model::Loaded::load(".", ok);
    const auto operations = loaded.operations();

    const auto* check = find_operation(operations, "check");
    const auto* model = find_operation(operations, "model");
    mm::test::expect(check != nullptr && check->role() == models::Role::Optional,
                     "expected check to be Optional");
    mm::test::expect(model != nullptr && model->role() == models::Role::Optional,
                     "expected model to be Optional");
}

void test_invokes_the_test_runner_four_times() {
    bool ok = false;
    auto loaded = mm::model::Loaded::load(".", ok);
    const auto* test_operation = find_operation(loaded.operations(), "test");
    mm::test::expect(test_operation != nullptr, "expected a test operation");
    if (test_operation == nullptr) return;

    int count = 0;
    for (const auto* tool : test_operation->invokes(0))
        if (tool != nullptr && tool->name() == "test") ++count;
    mm::test::expect(count == 4, "expected test.sh to invoke the test runner exactly four times");
}

void recommended_sequence_matches_the_documented_order() {
    bool ok = false;
    auto loaded = mm::model::Loaded::load(".", ok);
    const auto ordered = mm::model::recommended_sequence(loaded.operations());

    mm::test::expect(ordered.size() == 7, "expected the recommended sequence to cover all seven");
    if (ordered.size() != 7) return;

    const std::string_view expected[7] = {
        "clean", "bootstrap", "build", "test", "document", "check", "model",
    };
    for (std::size_t i = 0; i < 7; ++i)
        mm::test::expect(ordered[i]->name() == expected[i],
                         "expected the recommended sequence to match the documented order");
}

const mm::test::case_ cases[] = {
    { "seven operations are present",                 &seven_operations_are_present },
    { "bootstrap has two branches, same products",     &bootstrap_has_two_branches_with_the_same_products },
    { "build matches the real build.sh",               &build_matches_the_real_build_sh },
    { "clean is UserInitiated and invokes nothing",    &clean_is_user_initiated_and_invokes_nothing },
    { "check and model are Optional",                  &check_and_model_are_optional },
    { "test invokes the test runner four times",       &test_invokes_the_test_runner_four_times },
    { "recommended sequence matches documented order", &recommended_sequence_matches_the_documented_order },
};

const mm::test::registrar reg{"mm.model workflow", cases};

}  // namespace
