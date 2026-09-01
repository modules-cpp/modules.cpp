// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
module;

#include <array>
#include <cstddef>
#include <filesystem>
#include <iostream>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

module mm.model;

import mm.build;
import mm.mdy;
import models.configuration;
import models.document;
import models.manifest;
import models.modules;
import models.tool;
import models.workflow;

namespace mm::model {

namespace {

// mm::mdy::Parser::parse_file never puts an Empty block into
// MDYDocument::body (modules/mm/mdy/src/mdy.cpp skips blank lines while
// parsing the body), so models::BlockType has no Empty variant to map that
// case to; this function is only ever called on the blocks that do appear.
models::BlockType to_models_block_type(mm::mdy::BlockType type) {
    switch (type) {
        case mm::mdy::BlockType::Heading1:      return models::BlockType::Heading1;
        case mm::mdy::BlockType::Heading2:      return models::BlockType::Heading2;
        case mm::mdy::BlockType::Heading3:      return models::BlockType::Heading3;
        case mm::mdy::BlockType::Paragraph:     return models::BlockType::Paragraph;
        case mm::mdy::BlockType::UnorderedList: return models::BlockType::UnorderedList;
        case mm::mdy::BlockType::Empty:         return models::BlockType::Paragraph;
    }
    return models::BlockType::Paragraph;
}

class RealBlock : public models::Block {
public:
    RealBlock(models::BlockType type, std::string text) : type_(type), text_(std::move(text)) {}

    [[nodiscard]] models::BlockType type() const override { return type_; }
    [[nodiscard]] std::string_view text() const override { return text_; }

private:
    models::BlockType type_;
    std::string text_;
};

class RealDocument : public models::Document {
public:
    RealDocument(std::filesystem::path path, mm::mdy::MDYDocument doc)
        : path_(std::move(path)) {
        for (const auto& block : doc.body)
            blocks_.push_back(
                std::make_unique<RealBlock>(to_models_block_type(block.type), block.content));
        metadata_ = std::move(doc.metadata);
    }

    [[nodiscard]] std::filesystem::path path() const override { return path_; }

    [[nodiscard]] std::vector<std::string_view> values(std::string_view key) const override {
        const auto it = metadata_.find(key);
        if (it == metadata_.end()) return {};
        std::vector<std::string_view> result;
        result.reserve(it->second.size());
        for (const auto& value : it->second) result.emplace_back(value);
        return result;
    }

    [[nodiscard]] std::vector<const models::Block*> body() const override {
        std::vector<const models::Block*> result;
        result.reserve(blocks_.size());
        for (const auto& block : blocks_) result.push_back(block.get());
        return result;
    }

private:
    std::filesystem::path path_;
    std::map<std::string, std::vector<std::string>, std::less<>> metadata_;
    std::vector<std::unique_ptr<RealBlock>> blocks_;
};

class RealSourceUnit : public models::SourceUnit {
public:
    explicit RealSourceUnit(const mm::build::Unit& unit)
        : path_(unit.path), module_name_(unit.module_name) {}

    [[nodiscard]] std::filesystem::path path() const override { return path_; }
    [[nodiscard]] std::string_view module_name() const override { return module_name_; }

private:
    std::filesystem::path path_;
    std::string module_name_;
};

// Shared storage and accessors for every leaf node kind, including
// parent()/children(): both are resolved against an index table built once
// all nodes exist (see Loaded::load), which every RealXNode holds a pointer
// to rather than a copy of, so it does not matter that the table is empty
// at the moment any individual node's own NodeData is constructed.
class NodeData {
public:
    NodeData(std::string name, std::filesystem::path directory, std::size_t parent_index,
             std::vector<std::size_t> children_indices,
             const std::vector<const models::ManifestNode*>* index,
             const mm::mdy::MDYDocument& doc)
        : name_(std::move(name)), directory_(std::move(directory)), parent_index_(parent_index),
          children_indices_(std::move(children_indices)), index_(index),
          document_(directory_ / "mm.mdy", doc) {}

    [[nodiscard]] std::string_view name() const { return name_; }
    [[nodiscard]] std::filesystem::path manifest_path() const { return directory_ / "mm.mdy"; }
    [[nodiscard]] std::filesystem::path directory() const { return directory_; }

    [[nodiscard]] const models::ManifestNode* parent() const {
        if (parent_index_ == mm::build::no_parent) return nullptr;
        return (*index_)[parent_index_];
    }

    [[nodiscard]] std::vector<const models::ManifestNode*> children() const {
        std::vector<const models::ManifestNode*> result;
        result.reserve(children_indices_.size());
        for (const auto child_index : children_indices_) result.push_back((*index_)[child_index]);
        return result;
    }

    [[nodiscard]] const models::Document& document() const { return document_; }

private:
    std::string name_;
    std::filesystem::path directory_;
    std::size_t parent_index_;
    std::vector<std::size_t> children_indices_;
    const std::vector<const models::ManifestNode*>* index_;
    RealDocument document_;
};

// Shared storage and accessors for the three kinds that build: module, app,
// and test.
class BuildableData {
public:
    explicit BuildableData(const mm::build::Target& target) {
        sources_.reserve(target.sources.size());
        for (const auto& unit : target.sources) sources_.emplace_back(unit);
        uses_ = target.uses;
    }

    [[nodiscard]] std::vector<const models::SourceUnit*> sources() const {
        std::vector<const models::SourceUnit*> result;
        result.reserve(sources_.size());
        for (const auto& unit : sources_) result.push_back(&unit);
        return result;
    }

    [[nodiscard]] std::vector<std::string_view> uses() const {
        std::vector<std::string_view> result;
        result.reserve(uses_.size());
        for (const auto& name : uses_) result.emplace_back(name);
        return result;
    }

private:
    std::vector<RealSourceUnit> sources_;
    std::vector<std::string> uses_;
};

class RealProjectNode : public models::ProjectNode {
public:
    explicit RealProjectNode(NodeData data) : data_(std::move(data)) {}

    [[nodiscard]] std::string_view name() const override { return data_.name(); }
    [[nodiscard]] std::filesystem::path manifest_path() const override { return data_.manifest_path(); }
    [[nodiscard]] std::filesystem::path directory() const override { return data_.directory(); }
    [[nodiscard]] const models::ManifestNode* parent() const override { return data_.parent(); }
    [[nodiscard]] std::vector<const models::ManifestNode*> children() const override { return data_.children(); }
    [[nodiscard]] const models::Document& document() const override { return data_.document(); }

private:
    NodeData data_;
};

class RealDirectoryNode : public models::DirectoryNode {
public:
    explicit RealDirectoryNode(NodeData data) : data_(std::move(data)) {}

    [[nodiscard]] std::string_view name() const override { return data_.name(); }
    [[nodiscard]] std::filesystem::path manifest_path() const override { return data_.manifest_path(); }
    [[nodiscard]] std::filesystem::path directory() const override { return data_.directory(); }
    [[nodiscard]] const models::ManifestNode* parent() const override { return data_.parent(); }
    [[nodiscard]] std::vector<const models::ManifestNode*> children() const override { return data_.children(); }
    [[nodiscard]] const models::Document& document() const override { return data_.document(); }

private:
    NodeData data_;
};

class RealDocNode : public models::DocNode {
public:
    RealDocNode(NodeData data, const mm::build::Target& target) : data_(std::move(data)) {
        // target.sources is already root relative for a kind:doc target
        // (mm::build joins each file: value with target.dir and normalizes
        // it before storing it), so files() reuses that rather than
        // re-deriving paths from document().values("file"), which holds
        // the unjoined values as written in the manifest.
        files_.reserve(target.sources.size());
        for (const auto& unit : target.sources) files_.emplace_back(unit.path);
    }

    [[nodiscard]] std::string_view name() const override { return data_.name(); }
    [[nodiscard]] std::filesystem::path manifest_path() const override { return data_.manifest_path(); }
    [[nodiscard]] std::filesystem::path directory() const override { return data_.directory(); }
    [[nodiscard]] const models::ManifestNode* parent() const override { return data_.parent(); }
    [[nodiscard]] std::vector<const models::ManifestNode*> children() const override { return data_.children(); }
    [[nodiscard]] const models::Document& document() const override { return data_.document(); }
    [[nodiscard]] std::vector<std::filesystem::path> files() const override { return files_; }

private:
    NodeData data_;
    std::vector<std::filesystem::path> files_;
};

class RealModuleNode : public models::ModuleNode {
public:
    RealModuleNode(NodeData data, const mm::build::Target& target)
        : data_(std::move(data)), buildable_(target), exported_module_name_(target.module_name) {}

    [[nodiscard]] std::string_view name() const override { return data_.name(); }
    [[nodiscard]] std::filesystem::path manifest_path() const override { return data_.manifest_path(); }
    [[nodiscard]] std::filesystem::path directory() const override { return data_.directory(); }
    [[nodiscard]] const models::ManifestNode* parent() const override { return data_.parent(); }
    [[nodiscard]] std::vector<const models::ManifestNode*> children() const override { return data_.children(); }
    [[nodiscard]] const models::Document& document() const override { return data_.document(); }
    [[nodiscard]] std::vector<const models::SourceUnit*> sources() const override { return buildable_.sources(); }
    [[nodiscard]] std::vector<std::string_view> uses() const override { return buildable_.uses(); }
    [[nodiscard]] std::string_view exported_module_name() const override { return exported_module_name_; }

private:
    NodeData data_;
    BuildableData buildable_;
    std::string exported_module_name_;
};

// The resolved counterpart to a ModuleNode's declaration: imports() holds
// actual Module pointers rather than declared_by().uses()'s plain text.
// Built in two passes (build_modules() below), since an import can name any
// module in the tree, including one constructed after this one.
class RealModule : public models::Module {
public:
    explicit RealModule(const models::ModuleNode& declared_by) : declared_by_(&declared_by) {}

    [[nodiscard]] std::string_view name() const override {
        return declared_by_->exported_module_name();
    }
    [[nodiscard]] const models::ModuleNode& declared_by() const override { return *declared_by_; }
    [[nodiscard]] std::vector<const models::Module*> imports() const override { return imports_; }

    void set_imports(std::vector<const models::Module*> imports) { imports_ = std::move(imports); }

private:
    const models::ModuleNode* declared_by_;
    std::vector<const models::Module*> imports_;
};

// ok is false if any module's use: entry does not resolve to another
// module in module_nodes: mm::build::order already rejects this at real
// build time, but Loaded::load() never calls order(), so nothing else
// re-derives that guarantee for resolved_modules() specifically. A dropped
// edge here would otherwise leave imports() silently shorter than
// declared_by().uses(), with no signal to a caller that anything was lost.
std::vector<std::unique_ptr<RealModule>> build_modules(
    const std::vector<std::unique_ptr<RealModuleNode>>& module_nodes, bool& ok) {
    ok = true;

    std::vector<std::unique_ptr<RealModule>> result;
    result.reserve(module_nodes.size());
    for (const auto& node : module_nodes) result.push_back(std::make_unique<RealModule>(*node));

    std::map<std::string_view, RealModule*> by_name;
    for (const auto& resolved : result) by_name[resolved->name()] = resolved.get();

    for (const auto& resolved : result) {
        std::vector<const models::Module*> imports;
        for (const auto used : resolved->declared_by().uses()) {
            const auto it = by_name.find(used);
            if (it == by_name.end()) {
                std::cerr << "mm.model: module " << resolved->name() << " uses unknown module "
                          << used << "\n";
                ok = false;
                continue;
            }
            imports.push_back(it->second);
        }
        resolved->set_imports(std::move(imports));
    }

    return result;
}

class RealAppNode : public models::AppNode {
public:
    RealAppNode(NodeData data, const mm::build::Target& target)
        : data_(std::move(data)), buildable_(target) {}

    [[nodiscard]] std::string_view name() const override { return data_.name(); }
    [[nodiscard]] std::filesystem::path manifest_path() const override { return data_.manifest_path(); }
    [[nodiscard]] std::filesystem::path directory() const override { return data_.directory(); }
    [[nodiscard]] const models::ManifestNode* parent() const override { return data_.parent(); }
    [[nodiscard]] std::vector<const models::ManifestNode*> children() const override { return data_.children(); }
    [[nodiscard]] const models::Document& document() const override { return data_.document(); }
    [[nodiscard]] std::vector<const models::SourceUnit*> sources() const override { return buildable_.sources(); }
    [[nodiscard]] std::vector<std::string_view> uses() const override { return buildable_.uses(); }

private:
    NodeData data_;
    BuildableData buildable_;
};

class RealTestNode : public models::TestNode {
public:
    RealTestNode(NodeData data, const mm::build::Target& target)
        : data_(std::move(data)), buildable_(target) {}

    [[nodiscard]] std::string_view name() const override { return data_.name(); }
    [[nodiscard]] std::filesystem::path manifest_path() const override { return data_.manifest_path(); }
    [[nodiscard]] std::filesystem::path directory() const override { return data_.directory(); }
    [[nodiscard]] const models::ManifestNode* parent() const override { return data_.parent(); }
    [[nodiscard]] std::vector<const models::ManifestNode*> children() const override { return data_.children(); }
    [[nodiscard]] const models::Document& document() const override { return data_.document(); }
    [[nodiscard]] std::vector<const models::SourceUnit*> sources() const override { return buildable_.sources(); }
    [[nodiscard]] std::vector<std::string_view> uses() const override { return buildable_.uses(); }

private:
    NodeData data_;
    BuildableData buildable_;
};

class RealRepository : public models::Repository {
public:
    RealRepository(const RealProjectNode* root, std::vector<const models::ModuleNode*> modules,
                    std::vector<const models::AppNode*> apps,
                    std::vector<const models::TestNode*> tests,
                    std::vector<const models::DocNode*> docs)
        : root_(root), modules_(std::move(modules)), apps_(std::move(apps)),
          tests_(std::move(tests)), docs_(std::move(docs)) {}

    [[nodiscard]] const models::ProjectNode& root() const override { return *root_; }
    [[nodiscard]] std::vector<const models::ModuleNode*> modules() const override { return modules_; }
    [[nodiscard]] std::vector<const models::AppNode*> apps() const override { return apps_; }
    [[nodiscard]] std::vector<const models::TestNode*> tests() const override { return tests_; }
    [[nodiscard]] std::vector<const models::DocNode*> docs() const override { return docs_; }

private:
    const RealProjectNode* root_;
    std::vector<const models::ModuleNode*> modules_;
    std::vector<const models::AppNode*> apps_;
    std::vector<const models::TestNode*> tests_;
    std::vector<const models::DocNode*> docs_;
};

// One models::Tool per kind:app manifest, matching what build.sh installs:
// every app is written to out/bin/<name>.
class RealTool : public models::Tool {
public:
    explicit RealTool(const models::AppNode& app)
        : name_(app.name()), invocation_(std::filesystem::path("out") / "bin" / name_),
          app_(&app) {}

    [[nodiscard]] std::string_view name() const override { return name_; }
    [[nodiscard]] models::Provenance provenance() const override { return models::Provenance::BuiltIn; }
    [[nodiscard]] std::filesystem::path invocation() const override { return invocation_; }
    [[nodiscard]] const models::AppNode* declared_by() const override { return app_; }

private:
    std::string name_;
    std::filesystem::path invocation_;
    const models::AppNode* app_;
};

// build0 and build1 (tools/build/main.cpp and bootstrap.sh's hand compiled
// build1; see docs/modules.mdy) have no RealAppNode a RealTool can point at,
// so this is a second, more general Tool: app is nullable, and invocation()
// is given directly rather than always being out/bin/<name>.
//
//   - build0: source is tools/build/main.cpp, which no manifest anywhere
//     declares. app is nullptr: there is nothing to point declared_by() at.
//   - build1: source is tools/build/build.cpp, the exact file
//     tools/build/mm.mdy declares under kind:app name:build. app is that
//     same AppNode, the one RealTool also builds a Tool for out/bin/build
//     from: build1 and out/bin/build are two Tools for one declared app,
//     one built by hand during bootstrap and one built by itself later.
//   - c++: the host compiler, Provenance::ThirdParty like cppcheck and
//     semgrep would be. bootstrap.sh's fallback branch (see
//     models.workflow) invokes it directly and repeatedly; that branch has
//     no Tool to point invokes() at without this entry.
class FixedTool : public models::Tool {
public:
    FixedTool(std::string name, std::filesystem::path invocation, const models::AppNode* app,
              models::Provenance provenance = models::Provenance::BuiltIn)
        : name_(std::move(name)), invocation_(std::move(invocation)), app_(app),
          provenance_(provenance) {}

    [[nodiscard]] std::string_view name() const override { return name_; }
    [[nodiscard]] models::Provenance provenance() const override { return provenance_; }
    [[nodiscard]] std::filesystem::path invocation() const override { return invocation_; }
    [[nodiscard]] const models::AppNode* declared_by() const override { return app_; }

private:
    std::string name_;
    std::filesystem::path invocation_;
    const models::AppNode* app_;
    models::Provenance provenance_;
};

std::vector<std::unique_ptr<models::Tool>> build_tools(const std::vector<const models::AppNode*>& apps) {
    std::vector<std::unique_ptr<models::Tool>> result;
    result.reserve(apps.size() + 3);
    for (const auto* app : apps) result.push_back(std::make_unique<RealTool>(*app));

    const models::AppNode* build_app = nullptr;
    for (const auto* app : apps)
        if (app->name() == "build") build_app = app;

    // build0, build1, and c++ are fixed facts about this repository's own
    // bootstrap.sh, not something Loaded::load() can derive for an
    // arbitrary tree: load() accepts any valid project (Repository and
    // RealTool above are genuinely general), but these three describe a
    // script that only exists here. Adding them unconditionally would make
    // Loaded report a build0/build1/c++ for a foreign project that has no
    // such thing, so they are gated on this tree actually declaring a
    // "build" app the way tools/build/mm.mdy does - the one real,
    // structural signal (not just a name check) that build1 shares an
    // identity with.
    if (build_app != nullptr) {
        result.push_back(std::make_unique<FixedTool>("build0", "out/build0", nullptr));
        result.push_back(std::make_unique<FixedTool>("build1", "out/build1", build_app));
        result.push_back(
            std::make_unique<FixedTool>("c++", "c++", nullptr, models::Provenance::ThirdParty));
    }

    return result;
}

const models::Tool* find_tool(const std::vector<std::unique_ptr<models::Tool>>& tools,
                              std::string_view name) {
    for (const auto& tool : tools)
        if (tool->name() == name) return tool.get();
    return nullptr;
}

// Fixed, hand authored data: the seven *.sh scripts and how they relate are
// not something any manifest declares, the same reasoning as build0/build1
// in build_tools(). invokes() is precomputed per branch rather than derived
// on demand, since it only ever needs to hand back what was given at
// construction.
class RealOperation : public models::Operation {
public:
    RealOperation(std::string name, std::filesystem::path script_path, models::Role role,
                 std::vector<std::vector<const models::Tool*>> branches,
                 std::vector<models::ArtifactKind> requires_artifacts,
                 std::vector<models::ArtifactKind> produces)
        : name_(std::move(name)), script_path_(std::move(script_path)), role_(role),
          branches_(std::move(branches)), requires_artifacts_(std::move(requires_artifacts)),
          produces_(std::move(produces)) {}

    [[nodiscard]] std::string_view name() const override { return name_; }
    [[nodiscard]] std::filesystem::path script_path() const override { return script_path_; }
    [[nodiscard]] models::Role role() const override { return role_; }
    [[nodiscard]] std::size_t branch_count() const override { return branches_.size(); }

    [[nodiscard]] std::vector<const models::Tool*> invokes(std::size_t branch) const override {
        return branches_.at(branch);
    }

    [[nodiscard]] std::vector<models::ArtifactKind> requires_artifacts() const override {
        return requires_artifacts_;
    }

    [[nodiscard]] std::vector<models::ArtifactKind> produces() const override { return produces_; }

private:
    std::string name_;
    std::filesystem::path script_path_;
    models::Role role_;
    std::vector<std::vector<const models::Tool*>> branches_;
    std::vector<models::ArtifactKind> requires_artifacts_;
    std::vector<models::ArtifactKind> produces_;
};

std::vector<std::unique_ptr<models::Operation>> build_operations(
    const std::vector<std::unique_ptr<models::Tool>>& tools) {
    const auto* cxx = find_tool(tools, "c++");
    const auto* build0 = find_tool(tools, "build0");
    const auto* build1 = find_tool(tools, "build1");
    const auto* build = find_tool(tools, "build");
    const auto* main_tool = find_tool(tools, "main");
    const auto* mdy = find_tool(tools, "mdy");
    const auto* test_runner = find_tool(tools, "test");
    const auto* check = find_tool(tools, "check");
    const auto* model = find_tool(tools, "model");

    // operations() is fixed data describing this repository's own seven
    // *.sh scripts (see the class comment above), not something meaningful
    // for an arbitrary tree Loaded::load() also accepts. find_tool()
    // returns nullptr for any of the above that a foreign project's tools()
    // does not happen to name, and a null Tool* has no honest place inside
    // invokes()/requires_artifacts()/produces() - not "no requirement",
    // which those already express as an empty vector, but a caller
    // dereferencing garbage. Rather than embed a null or invent a
    // placeholder Tool, this returns no operations at all unless every one
    // of the above resolved to a real Tool.
    if (cxx == nullptr || build0 == nullptr || build1 == nullptr || build == nullptr ||
        main_tool == nullptr || mdy == nullptr || test_runner == nullptr || check == nullptr ||
        model == nullptr)
        return {};

    // Every kind:app manifest's compiled/linked/installed output, the
    // common shape of "a full build happened": bootstrap.sh's final build1
    // invocation and build.sh both produce this.
    const std::vector<models::ArtifactKind> full_build = {
        models::ArtifactKind::ModuleObject,   models::ArtifactKind::AppObject,
        models::ArtifactKind::AppExecutable,  models::ArtifactKind::ToolObject,
        models::ArtifactKind::ToolExecutable, models::ArtifactKind::InstalledBinary,
    };

    std::vector<std::unique_ptr<models::Operation>> result;
    result.reserve(7);

    // bootstrap.sh: compile build0, then either build0 builds build1
    // (branch 0) or, only if that leaves no executable build1, the same
    // fixed steps run by hand instead (branch 1); either way the script
    // finishes by running build1 to build everything, "build" included.
    {
        std::vector<models::ArtifactKind> produces = {models::ArtifactKind::Staged};
        produces.insert(produces.end(), full_build.begin(), full_build.end());
        result.push_back(std::make_unique<RealOperation>(
            "bootstrap", "bootstrap.sh", models::Role::Required,
            std::vector<std::vector<const models::Tool*>>{
                {cxx, build0, build1},
                {cxx, build0, cxx, cxx, cxx, cxx, cxx, cxx, build1},
            },
            std::vector<models::ArtifactKind>{}, std::move(produces)));
    }

    // build.sh runs out/bin/build, not out/build1: since bootstrap.sh's own
    // last step now runs a full build too, build.sh is the installed tool
    // rebuilding itself on every subsequent change, not a second consumer
    // of the staged bootstrap artifacts.
    //
    // Optional rather than Required for the same reason: Role::Required
    // means nothing after it can meaningfully run without it, and
    // bootstrap.sh already produces every artifact build.sh would. Nothing
    // in the sequence after it (test, document, check, model) needs
    // build.sh to have run in this session; what they actually need is
    // stated by their own requires_artifacts().
    result.push_back(std::make_unique<RealOperation>(
        "build", "build.sh", models::Role::Optional,
        std::vector<std::vector<const models::Tool*>>{{build}},
        std::vector<models::ArtifactKind>{models::ArtifactKind::InstalledBinary}, full_build));

    result.push_back(std::make_unique<RealOperation>(
        "test", "test.sh", models::Role::Optional,
        std::vector<std::vector<const models::Tool*>>{
            {build0, build1, build, main_tool, mdy, test_runner, test_runner, test_runner,
             test_runner}},
        std::vector<models::ArtifactKind>{models::ArtifactKind::Staged,
                                          models::ArtifactKind::InstalledBinary},
        std::vector<models::ArtifactKind>{models::ArtifactKind::TestBuild}));

    result.push_back(std::make_unique<RealOperation>(
        "document", "document.sh", models::Role::Optional,
        std::vector<std::vector<const models::Tool*>>{{mdy}},
        std::vector<models::ArtifactKind>{models::ArtifactKind::InstalledBinary},
        std::vector<models::ArtifactKind>{models::ArtifactKind::Documentation}));

    result.push_back(std::make_unique<RealOperation>(
        "check", "check.sh", models::Role::Optional,
        std::vector<std::vector<const models::Tool*>>{{check}},
        std::vector<models::ArtifactKind>{models::ArtifactKind::InstalledBinary},
        std::vector<models::ArtifactKind>{}));

    result.push_back(std::make_unique<RealOperation>(
        "model", "model.sh", models::Role::Optional,
        std::vector<std::vector<const models::Tool*>>{{model}},
        std::vector<models::ArtifactKind>{models::ArtifactKind::InstalledBinary},
        std::vector<models::ArtifactKind>{}));

    result.push_back(std::make_unique<RealOperation>(
        "clean", "clean.sh", models::Role::UserInitiated,
        std::vector<std::vector<const models::Tool*>>{{}},
        std::vector<models::ArtifactKind>{}, std::vector<models::ArtifactKind>{}));

    return result;
}

// compiler()/compiler_flags()/linker_flags()/verbose() copy the live
// Toolchain rather than pointing into it, since default_configuration()
// hands the caller ownership and the Toolchain that built this is a local
// about to go out of scope. platform()/locale()/shell() return string_view
// into string literals, valid for the program's whole lifetime: they are
// fixed policy, not derived from anything with a shorter lifetime.
class FixedConfiguration final : public models::Configuration {
public:
    explicit FixedConfiguration(const mm::build::Toolchain& toolchain)
        : compiler_(toolchain.cxx),
          compiler_flags_(toolchain.cxxflags),
          linker_flags_(toolchain.ldflags),
          verbose_(toolchain.verbose) {}

    [[nodiscard]] std::string_view compiler() const override { return compiler_; }
    [[nodiscard]] std::string_view compiler_flags() const override { return compiler_flags_; }
    [[nodiscard]] std::string_view linker_flags() const override { return linker_flags_; }
    [[nodiscard]] bool verbose() const override { return verbose_; }

    [[nodiscard]] std::string_view platform() const override { return "POSIX"; }
    [[nodiscard]] std::string_view locale() const override { return "C"; }
    [[nodiscard]] std::string_view shell() const override { return "/bin/sh"; }

private:
    std::string compiler_;
    std::string compiler_flags_;
    std::string linker_flags_;
    bool verbose_;
};

}  // namespace

struct Loaded::Impl {
    // node index (into the combined load_nodes() result) -> pointer. Filled
    // in as each node below is constructed; every NodeData holds a pointer
    // to this vector rather than a copy, so parent()/children() see it
    // fully populated by the time a caller can observe Loaded at all.
    std::vector<const models::ManifestNode*> index;

    std::vector<std::unique_ptr<RealProjectNode>> projects;
    std::vector<std::unique_ptr<RealDirectoryNode>> directories;
    std::vector<std::unique_ptr<RealModuleNode>> modules;
    std::vector<std::unique_ptr<RealAppNode>> apps;
    std::vector<std::unique_ptr<RealTestNode>> tests;
    std::vector<std::unique_ptr<RealDocNode>> docs;

    std::unique_ptr<RealRepository> repository;
    std::vector<std::unique_ptr<models::Tool>> tools;
    std::vector<std::unique_ptr<models::Operation>> operations;
    std::vector<std::unique_ptr<RealModule>> resolved_modules;
};

Loaded::Loaded() = default;
Loaded::Loaded(Loaded&&) noexcept = default;
Loaded& Loaded::operator=(Loaded&&) noexcept = default;
Loaded::~Loaded() = default;

Loaded Loaded::load(const std::filesystem::path& root_dir, bool& ok) {
    Loaded loaded;
    ok = false;

    std::error_code ec;
    const auto previous = std::filesystem::current_path();
    std::filesystem::current_path(root_dir, ec);
    if (ec) return loaded;

    // One traversal: mm::build::load_project pairs each node with its own
    // parsed document and its target, so nothing here re-reads a manifest
    // or re-pairs the two views by directory.
    auto project = mm::build::load_project(".");
    if (!project.ok) {
        std::filesystem::current_path(previous, ec);
        return loaded;
    }
    const auto& nodes = project.nodes;

    if (nodes.empty() || nodes.front().kind != "project") {
        // Repository::root() is typed ProjectNode&: a subtree load whose
        // root is not kind:project (or an empty tree) cannot be represented
        // truthfully, so this is a load failure rather than a node this
        // adapter fabricates a project identity for.
        std::filesystem::current_path(previous, ec);
        return loaded;
    }

    auto impl = std::make_unique<Impl>();
    impl->index.assign(nodes.size(), nullptr);

    for (std::size_t i = 0; i < nodes.size(); ++i) {
        const auto& node = nodes[i];
        NodeData data(node.name, node.dir, node.parent, node.children, &impl->index,
                      project.documents[i]);

        // project.target[i] is this node's entry in the list its kind
        // selects, recorded by the same traversal that made the node, so a
        // buildable node without one means the walk contradicted itself.
        const auto target = project.target[i];
        const bool needs_target =
            node.kind == "module" || node.kind == "app" || node.kind == "test" || node.kind == "doc";
        if (needs_target && target == mm::build::no_target) {
            std::filesystem::current_path(previous, ec);
            return loaded;
        }

        if (node.kind == "project") {
            impl->projects.push_back(std::make_unique<RealProjectNode>(std::move(data)));
            impl->index[i] = impl->projects.back().get();
        } else if (node.kind == "dir") {
            impl->directories.push_back(std::make_unique<RealDirectoryNode>(std::move(data)));
            impl->index[i] = impl->directories.back().get();
        } else if (node.kind == "module") {
            impl->modules.push_back(
                std::make_unique<RealModuleNode>(std::move(data), project.targets[target]));
            impl->index[i] = impl->modules.back().get();
        } else if (node.kind == "app") {
            impl->apps.push_back(
                std::make_unique<RealAppNode>(std::move(data), project.targets[target]));
            impl->index[i] = impl->apps.back().get();
        } else if (node.kind == "test") {
            impl->tests.push_back(
                std::make_unique<RealTestNode>(std::move(data), project.tests[target]));
            impl->index[i] = impl->tests.back().get();
        } else if (node.kind == "doc") {
            impl->docs.push_back(
                std::make_unique<RealDocNode>(std::move(data), project.docs[target]));
            impl->index[i] = impl->docs.back().get();
        }
    }

    std::vector<const models::ModuleNode*> module_ptrs;
    for (const auto& m : impl->modules) module_ptrs.push_back(m.get());
    std::vector<const models::AppNode*> app_ptrs;
    for (const auto& a : impl->apps) app_ptrs.push_back(a.get());
    std::vector<const models::TestNode*> test_ptrs;
    for (const auto& t : impl->tests) test_ptrs.push_back(t.get());
    std::vector<const models::DocNode*> doc_ptrs;
    for (const auto& d : impl->docs) doc_ptrs.push_back(d.get());

    impl->repository = std::make_unique<RealRepository>(
        impl->projects.front().get(), module_ptrs, app_ptrs, test_ptrs, doc_ptrs);
    impl->tools = build_tools(app_ptrs);
    impl->operations = build_operations(impl->tools);

    bool modules_ok = false;
    impl->resolved_modules = build_modules(impl->modules, modules_ok);
    if (!modules_ok) {
        std::filesystem::current_path(previous, ec);
        return loaded;
    }

    loaded.impl_ = std::move(impl);

    std::filesystem::current_path(previous, ec);
    ok = true;
    return loaded;
}

const models::Repository& Loaded::repository() const { return *impl_->repository; }

std::vector<const models::Tool*> Loaded::tools() const {
    std::vector<const models::Tool*> result;
    result.reserve(impl_->tools.size());
    for (const auto& tool : impl_->tools) result.push_back(tool.get());
    return result;
}

std::vector<const models::Operation*> Loaded::operations() const {
    std::vector<const models::Operation*> result;
    result.reserve(impl_->operations.size());
    for (const auto& operation : impl_->operations) result.push_back(operation.get());
    return result;
}

std::vector<const models::Module*> Loaded::resolved_modules() const {
    std::vector<const models::Module*> result;
    result.reserve(impl_->resolved_modules.size());
    for (const auto& module : impl_->resolved_modules) result.push_back(module.get());
    return result;
}

std::unique_ptr<models::Configuration> default_configuration(bool verbose) {
    return std::make_unique<FixedConfiguration>(mm::build::default_toolchain(verbose));
}

std::vector<const models::Operation*> recommended_sequence(
    const std::vector<const models::Operation*>& operations) {
    static constexpr std::array<std::string_view, 7> order = {
        "clean", "bootstrap", "build", "test", "document", "check", "model",
    };

    std::vector<const models::Operation*> result;
    result.reserve(operations.size());
    for (const auto name : order)
        for (const auto* operation : operations)
            if (operation->name() == name) result.push_back(operation);

    return result;
}

}  // namespace mm::model
