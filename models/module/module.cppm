// Abstract data model of a resolved C++20 module: the actual import graph
// node, as opposed to the manifest declaration that produces it. See
// docs/modules-model.mdy for the full models/ picture.
//
// Distinct from models::ModuleNode (models.manifest) the same way
// models::Tool is distinct from models::AppNode: ModuleNode is a manifest's
// declaration that a module should exist, with uses() holding the
// dependency module names as plain text; Module is that module resolved
// against the rest of a Repository, with imports() holding the actual
// Module objects those names name, one edge per use: entry.
//
// imports() is direct dependencies only, not a topological order:
// mm::build::order_from (modules/mm/build/build.cppm) returns the full
// transitive closure of a target's dependencies plus the target itself,
// dependencies first; imports() returns none of that recursion or
// self-inclusion; it is the direct edge list order_from's own recursion
// walks one level at a time. A caller that wants order_from's result can
// get it by walking imports() transitively.
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

    // The modules named by declared_by().uses(), resolved against the
    // modules known to the same Repository, in use: declaration order.
    // Direct dependencies only; see the note above the class for how this
    // differs from mm::build::order_from.
    [[nodiscard]] virtual std::vector<const Module*> imports() const = 0;
};

}  // namespace models
