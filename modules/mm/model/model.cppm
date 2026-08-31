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
// Provenance::BuiltIn with declared_by() pointing at that app's AppNode.
// build0, build1, and third party tools (cppcheck, semgrep) have no
// declaring manifest and are not represented here yet: populating those
// would mean inventing data this adapter cannot derive from a manifest
// walk, rather than adapting data that is already there.
//
// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
module;

#include <filesystem>
#include <memory>
#include <vector>

export module mm.model;

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

    // Loads the project rooted at root_dir plus its tests/ subtree, the same
    // two-tree combination tools/check uses (docs/modules.mdy: the root
    // manifest has no folder: tests entry). ok is false on a malformed
    // manifest tree, matching mm::build::Tree's own ok flag.
    [[nodiscard]] static Loaded load(const std::filesystem::path& root_dir, bool& ok);

    [[nodiscard]] const models::Repository& repository() const;

    // One Tool per kind:app manifest; see the note above the class for what
    // is not covered.
    [[nodiscard]] std::vector<const models::Tool*> tools() const;

private:
    Loaded();

    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace mm::model
