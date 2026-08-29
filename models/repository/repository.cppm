// Abstract data model of a loaded manifest tree. See models.manifest
// (models/manifest/manifest.cppm) for the node hierarchy this aggregates,
// and for the note on why this is a design artifact rather than a build
// target.
//
// Mirrors mm::build::Tree (modules/mm/build/build.cppm): a project's root
// plus every module, app, test, and doc reachable from it. Unlike Tree,
// which mixes module and app targets in one vector because the build tool
// walks them together, this model keeps modules() and apps() separate: they
// are different kinds, and a reader of this model has no reason to already
// know that build order, not kind, is why mm::build groups them.
//
// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
module;

#include <vector>

export module models.repository;

import models.manifest;

export namespace models {

class Repository {
public:
    virtual ~Repository() = default;

    [[nodiscard]] virtual const ProjectNode& root() const = 0;

    [[nodiscard]] virtual std::vector<const ModuleNode*> modules() const = 0;
    [[nodiscard]] virtual std::vector<const AppNode*> apps() const = 0;
    [[nodiscard]] virtual std::vector<const TestNode*> tests() const = 0;
    [[nodiscard]] virtual std::vector<const DocNode*> docs() const = 0;
};

}  // namespace models
