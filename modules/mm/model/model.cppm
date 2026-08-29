// Adapts mm::build's and mm::mdy's real data onto the models.* abstract data
// model (models/), rather than duplicating either. Loaded owns every
// concrete node and document built while loading a project, so pointers
// returned through the models:: interfaces stay valid for Loaded's
// lifetime.
//
// This adapter builds modules(), apps(), tests(), and docs() from
// mm::build::load_tree's Target lists, and gives every node a real
// document() by re-parsing its own mm.mdy through mm::mdy::Parser. It does
// not yet resolve ManifestNode::parent()/children(): every node built here
// returns nullptr/empty for those, since load_tree's Target does not carry
// the structural nesting mm::build::Node does. A future revision that also
// walks mm::build::load_nodes could fill them in; tools/consistency, the
// first consumer of this adapter, does not need them.
//
// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
module;

#include <filesystem>
#include <memory>

export module mm.model;

import models.repository;

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

private:
    Loaded();

    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace mm::model
