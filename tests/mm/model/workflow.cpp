// Black box tests for mm.model::Loaded::operations() and
// mm::model::recommended_sequence(), run against this project's own real
// tree.

#include <cstddef>
#include <fstream>
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

int count_tool(const models::Operation& operation, std::size_t branch, std::string_view name) {
    int count = 0;
    for (const auto* tool : operation.invokes(branch))
        if (tool != nullptr && tool->name() == name) ++count;
    return count;
}

std::size_t count_occurrences(const std::string& content, std::string_view needle) {
    std::size_t count = 0;
    for (std::size_t pos = 0; (pos = content.find(needle, pos)) != std::string::npos; ++pos)
        ++count;
    return count;
}

void twelve_operations_are_present() {
    bool ok = false;
    auto loaded = mm::model::Loaded::load(".", ok);
    const auto operations = loaded.operations();
    mm::test::expect(operations.size() == 12,
                     "expected exactly the twelve documented operations");

    const auto* json = find_operation(operations, "json");
    mm::test::expect(json != nullptr, "expected a json operation");
    if (json == nullptr) return;

    // Same discipline as build_matches_the_real_build_sh: check the script,
    // not the model.
    std::ifstream script("json.sh");
    const std::string content((std::istreambuf_iterator<char>(script)),
                              std::istreambuf_iterator<char>());
    mm::test::expect(content.find("bin/json") != std::string::npos,
                     "expected json.sh to actually invoke out/bin/json; "
                     "update the model in build_operations() if this changed");

    mm::test::expect(json->role() == models::Role::Optional,
                     "expected json to be Optional, a utility outside the self-hosting chain");
    mm::test::expect(json->branch_count() == 1, "expected json to have one branch");
    mm::test::expect(invokes_name(*json, 0, "json"),
                     "expected the json operation to invoke the json tool");
    bool requires_installed_binary = false;
    for (const auto kind : json->requires_artifacts())
        if (kind == models::ArtifactKind::InstalledBinary) requires_installed_binary = true;
    mm::test::expect(requires_installed_binary,
                     "expected json to require InstalledBinary (out/bin/json)");
    mm::test::expect(json->produces().empty(), "expected json to produce no artifact");
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

    // Bootstrap stages only build and configure. This required step produces
    // the complete repository needed by the later workflow operations.
    mm::test::expect(build->role() == models::Role::Required, "expected build to be Required");

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

// A prior version of this test asserted the model's test runner count
// against the model itself, so it would keep passing while test.sh's suite
// roster grew. This version reads the actual script, so a suite added to
// test.sh that the model is not updated to match fails here.
void test_invocations_match_the_real_test_sh() {
    bool ok = false;
    auto loaded = mm::model::Loaded::load(".", ok);
    const auto* test_operation = find_operation(loaded.operations(), "test");
    mm::test::expect(test_operation != nullptr, "expected a test operation");
    if (test_operation == nullptr) return;

    mm::test::expect(test_operation->branch_count() == 2,
                     "expected test to have a non-Darwin and a Darwin branch");

    std::ifstream script("test.sh");
    const std::string content((std::istreambuf_iterator<char>(script)),
                              std::istreambuf_iterator<char>());
    // The function definition line is run_test_target( with no following
    // space, so the spaced form counts exactly the suite invocations.
    const std::size_t suites = count_occurrences(content, "run_test_target ");
    mm::test::expect(suites == 27,
                     "expected test.sh to invoke the test runner 27 times; "
                     "update the model's branch suite counts if this changed");
    mm::test::expect(content.find("tests/mm/linux/") != std::string::npos,
                     "expected test.sh to skip tests/mm/linux/ on macOS");

    // Branch 0 runs every suite; branch 1 (Darwin) skips exactly the linux
    // suite.
    mm::test::expect(count_tool(*test_operation, 0, "test") == static_cast<int>(suites),
                     "expected test's non-Darwin branch to invoke the test "
                     "runner once per suite");
    mm::test::expect(count_tool(*test_operation, 1, "test") == static_cast<int>(suites) - 1,
                     "expected test's Darwin branch to skip exactly the linux suite");

    // The staged and installed tools checked before the suites run.
    mm::test::expect(count_tool(*test_operation, 0, "build0") == 2,
                     "expected test.sh to run build0's usage and help checks");
    mm::test::expect(count_tool(*test_operation, 0, "build1") == 1,
                     "expected test.sh to run build1's help check");
    mm::test::expect(count_tool(*test_operation, 0, "build") == 1,
                     "expected test.sh to run build's help check");
    mm::test::expect(count_tool(*test_operation, 0, "configure") == 1,
                     "expected test.sh to check configure's unknown-option error");
    mm::test::expect(count_tool(*test_operation, 0, "main") == 3,
                     "expected test.sh to run main's three output modes");
    mm::test::expect(count_tool(*test_operation, 0, "mdy") == 1,
                     "expected test.sh to run mdy's smoke check");
}

void bootstrap_fallback_matches_the_real_bootstrap_sh() {
    bool ok = false;
    auto loaded = mm::model::Loaded::load(".", ok);
    const auto* bootstrap = find_operation(loaded.operations(), "bootstrap");
    mm::test::expect(bootstrap != nullptr, "expected a bootstrap operation");
    if (bootstrap == nullptr) return;

    // Branch 0, the common path: c++ compiles build0, build0 builds build1,
    // build1 then stages build and configure.
    mm::test::expect(bootstrap->invokes(0).size() == 4,
                     "expected bootstrap's main path to invoke exactly four tools");
    mm::test::expect(count_tool(*bootstrap, 0, "c++") == 1,
                     "expected bootstrap's main path to compile build0 once");
    mm::test::expect(count_tool(*bootstrap, 0, "build0") == 1,
                     "expected bootstrap's main path to build build1 with build0");
    mm::test::expect(count_tool(*bootstrap, 0, "build1") == 2,
                     "expected bootstrap's main path to stage build and configure");

    std::ifstream script("bootstrap.sh");
    const std::string content((std::istreambuf_iterator<char>(script)),
                              std::istreambuf_iterator<char>());
    // The fallback drives the host compiler once per -c compile plus one
    // link of build1.tmp, on top of the initial build0 compile.
    const std::size_t compiles = count_occurrences(content, " -c ");
    mm::test::expect(compiles == 25,
                     "expected bootstrap.sh's fallback to compile 25 units; "
                     "update the model's fallback branch if this changed");
    mm::test::expect(content.find("build1.tmp") != std::string::npos,
                     "expected bootstrap.sh's fallback to link build1.tmp");
    mm::test::expect(count_tool(*bootstrap, 1, "c++") == static_cast<int>(compiles) + 2,
                     "expected bootstrap's fallback branch to invoke c++ once "
                     "for build0, once per fallback compile, and once for the link");
    mm::test::expect(count_tool(*bootstrap, 1, "build0") == 1,
                     "expected bootstrap's fallback branch to try build0 first");
    mm::test::expect(count_tool(*bootstrap, 1, "build1") == 2,
                     "expected bootstrap's fallback branch to stage build and configure");
}

void recommended_sequence_matches_the_documented_order() {
    bool ok = false;
    auto loaded = mm::model::Loaded::load(".", ok);
    const auto ordered = mm::model::recommended_sequence(loaded.operations());

    mm::test::expect(ordered.size() == 12, "expected the recommended sequence to cover all twelve");
    if (ordered.size() != 12) return;

    const std::string_view expected[12] = {
        "clean", "bootstrap", "configure", "build", "test", "document", "check", "model", "json",
        "run", "flash", "debug",
    };
    for (std::size_t i = 0; i < 12; ++i)
        mm::test::expect(ordered[i]->name() == expected[i],
                         "expected the recommended sequence to match the documented order");
}

const mm::test::case_ cases[] = {
    { "twelve operations are present",              &twelve_operations_are_present },
    { "bootstrap branches match the real bootstrap.sh", &bootstrap_fallback_matches_the_real_bootstrap_sh },
    { "bootstrap has two branches, same products",   &bootstrap_has_two_branches_with_the_same_products },
    { "build matches the real build.sh",             &build_matches_the_real_build_sh },
    { "clean is UserInitiated and invokes nothing",  &clean_is_user_initiated_and_invokes_nothing },
    { "check and model are Optional",                &check_and_model_are_optional },
    { "test invocations match the real test.sh",     &test_invocations_match_the_real_test_sh },
    { "recommended sequence matches documented order", &recommended_sequence_matches_the_documented_order },
};

const mm::test::registrar reg{"mm.model workflow", cases};

}  // namespace
