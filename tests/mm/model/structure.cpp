// Black box tests for mm.model, run against this project's own real tree
// (the test binary's working directory is the project root; see
// tools/test's front end). Prior to fixing a review finding, every node
// here returned nullptr/empty for parent()/children() and the root was a
// hard-coded "modules.cpp" ProjectNode regardless of what was actually
// loaded; these cases pin the real values instead.

#include <cstddef>
#include <string_view>

import mm.model;
import mm.test;
import models.manifest;
import models.repository;

namespace {

const models::ManifestNode* find_child(const models::ManifestNode& node, std::string_view name) {
    for (const auto* child : node.children())
        if (child->name() == name) return child;
    return nullptr;
}

void load_of_the_real_root_succeeds() {
    bool ok = false;
    auto loaded = mm::model::Loaded::load(".", ok);
    mm::test::expect(ok, "expected loading this project's own root to succeed");
}

void root_reflects_the_real_project_manifest() {
    bool ok = false;
    auto loaded = mm::model::Loaded::load(".", ok);
    const auto& root = loaded.repository().root();

    mm::test::expect(root.name() == "modules.cpp",
                     "expected the root's name to come from the real mm.mdy, not a hard-coded value");
    mm::test::expect(root.kind() == models::Kind::Project, "expected the root to be Kind::Project");
    mm::test::expect(root.parent() == nullptr, "expected the root to have no parent");
}

void root_children_include_a_real_directory_node() {
    bool ok = false;
    auto loaded = mm::model::Loaded::load(".", ok);
    const auto& root = loaded.repository().root();

    const auto* apps = find_child(root, "apps");
    mm::test::expect(apps != nullptr, "expected the root's children to include apps/");
    if (apps != nullptr)
        mm::test::expect(apps->kind() == models::Kind::Directory, "expected apps/ to be Kind::Directory");
}

void child_and_parent_agree_with_each_other() {
    bool ok = false;
    auto loaded = mm::model::Loaded::load(".", ok);
    const auto& root = loaded.repository().root();

    const auto* apps = find_child(root, "apps");
    mm::test::expect(apps != nullptr, "expected apps/ to be a child of the root");
    if (apps == nullptr) return;

    const auto* main_app = find_child(*apps, "main");
    mm::test::expect(main_app != nullptr, "expected apps/main to be a child of apps/");
    if (main_app == nullptr) return;

    mm::test::expect(main_app->parent() == apps,
                     "expected apps/main's parent() to be the same object as apps/'s child entry for it");
    mm::test::expect(main_app->kind() == models::Kind::App, "expected apps/main to be Kind::App");
}

void every_app_reaches_the_root_by_walking_parent() {
    bool ok = false;
    auto loaded = mm::model::Loaded::load(".", ok);
    const auto& root = loaded.repository().root();

    for (const auto* app : loaded.repository().apps()) {
        const models::ManifestNode* walk = app;
        std::size_t hops = 0;
        while (walk->parent() != nullptr && hops < 20) {
            walk = walk->parent();
            ++hops;
        }
        mm::test::expect(walk == &root,
                         "expected walking parent() from an app to terminate at the root");
        mm::test::expect(hops < 20, "expected the parent chain to end, not cycle");
    }
}

void load_of_a_nonexistent_directory_fails() {
    bool ok = false;
    auto loaded = mm::model::Loaded::load("this/directory/does/not/exist", ok);
    mm::test::expect(!ok, "expected loading a nonexistent directory to fail");
}

void every_app_has_exactly_one_tool() {
    bool ok = false;
    auto loaded = mm::model::Loaded::load(".", ok);
    mm::test::expect(loaded.tools().size() == loaded.repository().apps().size(),
                     "expected one Tool per AppNode");
}

const mm::test::case_ cases[] = {
    { "load of the real root succeeds",                &load_of_the_real_root_succeeds },
    { "root reflects the real project manifest",       &root_reflects_the_real_project_manifest },
    { "root children include a real directory node",   &root_children_include_a_real_directory_node },
    { "child and parent agree with each other",        &child_and_parent_agree_with_each_other },
    { "every app reaches the root by walking parent",  &every_app_reaches_the_root_by_walking_parent },
    { "load of a nonexistent directory fails",         &load_of_a_nonexistent_directory_fails },
    { "every app has exactly one tool",                &every_app_has_exactly_one_tool },
};

const mm::test::registrar reg{"mm.model structure", cases};

}  // namespace
