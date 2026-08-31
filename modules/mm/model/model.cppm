// Adapts mm::build's and mm::mdy's real data onto the models.* abstract data
// model (models/), rather than duplicating either. Loaded owns every
// concrete node and document built while loading a project, so pointers
// returned through the models:: interfaces stay valid for Loaded's
// lifetime.
//
// This adapter builds modules(), apps(), tests(), and docs() from
// mm::build::load_tree's Target lists, gives every node a real document() by
// re-parsing its own mm.mdy through mm::mdy::Parser, and resolves
// parent()/children() from mm::build::load_nodes's structural walk, cross
// referenced against the Target lists by directory. root() is the manifest
// load_nodes actually visited first: if it is not kind:project (a subtree
// load rooted at a kind:dir manifest, for instance), load() fails rather
// than fabricate a project identity for it, since Repository::root() is
// typed ProjectNode&.
//
// tools() gives one models::Tool per kind:app manifest, each
// Provenance::BuiltIn with declared_by() pointing at that app's AppNode,
// plus two fixed entries neither manifest walk can produce: build0
// (declared_by() == nullptr, tools/build/main.cpp has no manifest at all)
// and build1 (declared_by() pointing at the same AppNode as out/bin/build:
// bootstrap.sh compiles build1 from tools/build/build.cpp, the exact file
// tools/build/mm.mdy declares, so build1 and out/bin/build are two Tools
// for one declared app). Third party tools (cppcheck, semgrep) still have
// no representation: they are not fixed, project-known paths the way
// build0/build1 are, so populating them would mean inventing data instead
// of stating a fact this adapter already knows.
//
// default_configuration() is unrelated to a Loaded tree: it needs no
// manifest, so it is a free function rather than a Loaded member. It stays
// downstream of mm.build the same way the rest of this adapter does:
// nothing in tools/build or the bootstrap chain imports mm.model, so this
// is read by tools/model, never by the build path itself.
//
// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
module;

#include <filesystem>
#include <memory>
#include <vector>

export module mm.model;

import models.configuration;
import models.repository;
import models.tool;

export namespace mm::model {

class Loaded {
public:
    Loaded(Loaded&&) noexcept;
    Loaded& operator=(Loaded&&) noexcept;
    ~Loaded();

    Loaded(const Loaded&) = delete;
    Loaded& operator=(const Loaded&) = delete;

    // Loads the project rooted at root_dir, one manifest tree reaching
    // everything including tests/ through the root manifest's ordinary
    // folder: entries. ok is false on a malformed manifest tree, matching
    // mm::build::Tree's own ok flag.
    [[nodiscard]] static Loaded load(const std::filesystem::path& root_dir, bool& ok);

    [[nodiscard]] const models::Repository& repository() const;

    // One Tool per kind:app manifest, plus build0 and build1; see the note
    // above the class for what is still not covered.
    [[nodiscard]] std::vector<const models::Tool*> tools() const;

private:
    Loaded();

    struct Impl;
    std::unique_ptr<Impl> impl_;
};

// Reports the project's build configuration: the live mm::build::Toolchain
// ($CXX honored, verbose as given) plus the fixed platform/locale/shell
// policy models.configuration documents. A fresh value every call: no
// caching, so a changed $CXX or a different verbose argument is always
// reflected.
[[nodiscard]] std::unique_ptr<models::Configuration> default_configuration(bool verbose = false);

}  // namespace mm::model
