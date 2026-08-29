// Abstract data model of an mm.mdy manifest node, following every rule in
// docs/modules-c++20.mdy: modules instead of headers, no exceptions, no new
// templates, enum class, [[nodiscard]] on accessors, and explicit virtual
// destructors on every base.
//
// See docs/modules-model.mdy for the full models/ picture: what each
// module covers, the dependency order between them, and how mm.model and
// tools/model consume this one.
//
// ManifestNode models the six kinds docs/modules.mdy defines: project, dir,
// module, app, test, and doc. It mirrors mm::build::Node
// (modules/mm/build/build.cppm) conceptually, as an abstract interface
// instead of that module's flat struct. See models.repository for the
// aggregate view over a whole tree of these nodes.
//
// This module imports models.document because every manifest is itself an
// MDY document (docs/modules.mdy's "MDY manifests"): ManifestNode::document()
// is the parsed front matter and body a node's own manifest_path() names,
// the same way mm::build imports mm::mdy to read manifests.
//
// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
module;

#include <filesystem>
#include <string_view>
#include <vector>

export module models.manifest;

import models.document;

export namespace models {

enum class Kind { Project, Directory, Module, App, Test, Doc };

// A single file: or unit: entry. Source order is declaration order, per
// docs/modules.mdy's "Repeated manifest values retain declaration order."
class SourceUnit {
public:
    virtual ~SourceUnit() = default;

    // Root relative.
    [[nodiscard]] virtual std::filesystem::path path() const = 0;

    // An optional per-unit module: hint, distinct from a target's own
    // module: field (ModuleNode::exported_module_name()): a file: or unit:
    // value may name a module after the path, separated by whitespace, for
    // a compiler that needs to be told which name a specific interface unit
    // defines. No manifest in this project uses that second word today, so
    // this is empty for every current SourceUnit, including interface units
    // such as mdy.cppm that do produce a BMI; emptiness here is silence, not
    // evidence that a unit produces no BMI.
    [[nodiscard]] virtual std::string_view module_name() const = 0;
};

// One manifest in the tree: the kind:project and kind:dir manifests that
// only organize the tree, and the kind:module, kind:app, kind:test, and
// kind:doc manifests that name something.
class ManifestNode {
public:
    virtual ~ManifestNode() = default;

    [[nodiscard]] virtual Kind kind() const = 0;
    [[nodiscard]] virtual std::string_view name() const = 0;

    // Root relative path to this node's mm.mdy.
    [[nodiscard]] virtual std::filesystem::path manifest_path() const = 0;

    // Root relative path to the directory holding manifest_path().
    [[nodiscard]] virtual std::filesystem::path directory() const = 0;

    // nullptr at the root.
    [[nodiscard]] virtual const ManifestNode* parent() const = 0;

    // folder: entries for project/dir; empty for every other kind.
    // mm::build::load_nodes (modules/mm/build/build.cppm) only recurses
    // through folder: on a project or dir manifest, so a doc manifest's
    // file: entries are never walked into ManifestNodes of their own: see
    // DocNode::files() for those.
    [[nodiscard]] virtual std::vector<const ManifestNode*> children() const = 0;

    // The MDY document at manifest_path(): front matter and, for a doc
    // manifest, the prose body.
    [[nodiscard]] virtual const Document& document() const = 0;
};

// A module, app, or test target: the three kinds that declare file: or
// unit: sources and use: dependencies. Distinct from ManifestNode's
// children(), which is about manifest nesting, not build inputs.
class BuildableNode : public ManifestNode {
public:
    // file:/unit: entries, in declaration order.
    [[nodiscard]] virtual std::vector<const SourceUnit*> sources() const = 0;

    // use: entries, the module names this target depends on.
    [[nodiscard]] virtual std::vector<std::string_view> uses() const = 0;
};

// The root of a manifest tree: exactly one kind:project manifest, found by
// walking up from any node (mm::build::find_project_root). Adds nothing to
// ManifestNode; it exists so a Repository can hand back a type that is
// known, at compile time, to be the root.
class ProjectNode : public ManifestNode {
public:
    [[nodiscard]] Kind kind() const override { return Kind::Project; }
};

// A purely organizational manifest: folder: entries only, no sources or
// dependencies of its own. modules/mm.mdy and tools/mm.mdy are examples.
class DirectoryNode : public ManifestNode {
public:
    [[nodiscard]] Kind kind() const override { return Kind::Directory; }
};

// A module target, such as modules/mm/build/mm.mdy. Compiled by the build
// tool; consumed by other targets through use:.
class ModuleNode : public BuildableNode {
public:
    [[nodiscard]] Kind kind() const override { return Kind::Module; }

    // The module: field: the importable name this target exports, e.g.
    // "mm.build". Distinct from a SourceUnit's own module_name(), which is
    // per interface unit rather than per target.
    [[nodiscard]] virtual std::string_view exported_module_name() const = 0;
};

// An application target, such as apps/main/mm.mdy. Compiled, linked, and
// installed under out/bin by the build tool.
class AppNode : public BuildableNode {
public:
    [[nodiscard]] Kind kind() const override { return Kind::App; }
};

// A test target, such as tests/mm/build/mm.mdy. sources() holds unit:
// entries rather than file: entries, but the shape is otherwise the same as
// a module or app: its own translation units plus use: dependencies, which
// pull in the test framework and the module under test transitively.
// Built and run by the test tool, never by the build tool.
class TestNode : public BuildableNode {
public:
    [[nodiscard]] Kind kind() const override { return Kind::Test; }
};

// A prose document, such as docs/modules.mdy. Reached by a manifest walk so
// it is not invisible to the tools, but nothing compiles or links it. Its
// own prose is document().body().
class DocNode : public ManifestNode {
public:
    [[nodiscard]] Kind kind() const override { return Kind::Doc; }

    // Root relative paths from this doc's file: entries, in declaration
    // order, e.g. docs/mm.mdy listing docs/modules.mdy and docs/mdy.mdy.
    // These are plain paths, not ManifestNodes: unlike folder:, mm::build
    // never walks a doc's file: entries into nodes of their own, so there
    // is nothing here for children() to return (see ManifestNode).
    [[nodiscard]] virtual std::vector<std::filesystem::path> files() const = 0;
};

}  // namespace models
