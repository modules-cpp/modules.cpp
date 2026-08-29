// Abstract data model of a resolved C++20 module: the actual import graph
// node, as opposed to the manifest declaration that produces it. See
// models.manifest (models/manifest/manifest.cppm) for the note on why this
// is a design artifact rather than a build target.
//
// Distinct from models::ModuleNode (models.manifest) the same way
// models::Tool is distinct from models::AppNode: ModuleNode is a manifest's
// declaration that a module should exist, with uses() holding the
// dependency module names as plain text; Module is that module resolved
// against the rest of a Repository, with imports() holding the actual
// Module objects those names name. mm::build::order_from
// (modules/mm/build/build.cppm) performs the resolution this type's
// imports() is the result of.
//
// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
module;

#include <string_view>
#include <vector>

export module models.module;

import models.manifest;

export namespace models {

class Module {
public:
    virtual ~Module() = default;

    // The module: name, e.g. "mm.build".
    [[nodiscard]] virtual std::string_view name() const = 0;

    // The kind:module manifest that declares this module. Never null: every
    // Module in this model comes from a manifest walk, unlike a Tool, which
    // may have no declaring manifest at all.
    [[nodiscard]] virtual const ModuleNode& declared_by() const = 0;

    // Other modules this one imports, resolved from declared_by().uses()
    // against the modules known to the same Repository, dependencies first,
    // matching mm::build::order_from's order.
    [[nodiscard]] virtual std::vector<const Module*> imports() const = 0;
};

}  // namespace models
