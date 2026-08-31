// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
module;

#include <filesystem>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

module mm.model;

import mm.build;
import mm.mdy;
import models.document;
import models.manifest;
import models.tool;

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

// Every node re-parses its own mm.mdy: mm::build::Node and mm::build::Target
// both discard the MDYDocument they read, keeping only the fields each one
// cares about.
RealDocument parse_document(const std::filesystem::path& dir) {
    const auto path = dir / "mm.mdy";
    return RealDocument(path, mm::mdy::Parser::parse_file(path));
}

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
             const std::vector<const models::ManifestNode*>* index)
        : name_(std::move(name)), directory_(std::move(directory)), parent_index_(parent_index),
          children_indices_(std::move(children_indices)), index_(index),
          document_(parse_document(directory_)) {}

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
// every app is written to out/bin/<name>. See model.cppm for what this
// deliberately leaves out (build0, build1, third party tools).
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

std::vector<std::unique_ptr<RealTool>> build_tools(const std::vector<const models::AppNode*>& apps) {
    std::vector<std::unique_ptr<RealTool>> result;
    result.reserve(apps.size());
    for (const auto* app : apps) result.push_back(std::make_unique<RealTool>(*app));
    return result;
}

// Offsets every index a load_nodes() result carries by base, so a second
// tree's indices land past the first tree's when the two are appended into
// one combined vector. no_parent is a sentinel, not a real index, and must
// pass through unchanged.
void offset_indices(std::vector<mm::build::Node>& nodes, std::size_t base) {
    for (auto& node : nodes) {
        if (node.parent != mm::build::no_parent) node.parent += base;
        for (auto& child : node.children) child += base;
    }
}

using TargetIndex = std::map<std::filesystem::path, const mm::build::Target*>;

TargetIndex index_by_dir(const std::vector<mm::build::Target>& a,
                         const std::vector<mm::build::Target>& b) {
    TargetIndex index;
    for (const auto& target : a) index[target.dir] = &target;
    for (const auto& target : b) index[target.dir] = &target;
    return index;
}

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
    std::vector<std::unique_ptr<RealTool>> tools;
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

    auto tree = mm::build::load_tree(".");
    bool nodes_ok = false;
    auto nodes = mm::build::load_nodes(".", nodes_ok);
    if (!tree.ok || !nodes_ok) {
        std::filesystem::current_path(previous, ec);
        return loaded;
    }

    // The root manifest has no folder: tests entry (docs/modules.mdy), so the
    // tests/ subtree needs its own walk of both kinds, same as tools/check.
    mm::build::Tree test_tree;
    if (std::filesystem::exists("tests")) {
        test_tree = mm::build::load_tree("tests");
        bool test_nodes_ok = false;
        auto test_nodes = mm::build::load_nodes("tests", test_nodes_ok);
        if (!test_tree.ok || !test_nodes_ok) {
            std::filesystem::current_path(previous, ec);
            return loaded;
        }
        offset_indices(test_nodes, nodes.size());
        nodes.insert(nodes.end(), test_nodes.begin(), test_nodes.end());
    }

    if (nodes.empty() || nodes.front().kind != "project") {
        // Repository::root() is typed ProjectNode&: a subtree load whose
        // root is not kind:project (or an empty tree) cannot be represented
        // truthfully, so this is a load failure rather than a node this
        // adapter fabricates a project identity for.
        std::filesystem::current_path(previous, ec);
        return loaded;
    }

    const auto target_index = index_by_dir(tree.targets, test_tree.targets);
    const auto doc_index = index_by_dir(tree.docs, test_tree.docs);
    const auto test_index = index_by_dir(tree.tests, test_tree.tests);

    auto impl = std::make_unique<Impl>();
    impl->index.assign(nodes.size(), nullptr);

    for (std::size_t i = 0; i < nodes.size(); ++i) {
        const auto& node = nodes[i];
        NodeData data(node.name, node.dir, node.parent, node.children, &impl->index);

        if (node.kind == "project") {
            impl->projects.push_back(std::make_unique<RealProjectNode>(std::move(data)));
            impl->index[i] = impl->projects.back().get();
        } else if (node.kind == "dir") {
            impl->directories.push_back(std::make_unique<RealDirectoryNode>(std::move(data)));
            impl->index[i] = impl->directories.back().get();
        } else if (node.kind == "module") {
            const auto it = target_index.find(node.dir);
            if (it == target_index.end()) {
                std::filesystem::current_path(previous, ec);
                return loaded;
            }
            impl->modules.push_back(std::make_unique<RealModuleNode>(std::move(data), *it->second));
            impl->index[i] = impl->modules.back().get();
        } else if (node.kind == "app") {
            const auto it = target_index.find(node.dir);
            if (it == target_index.end()) {
                std::filesystem::current_path(previous, ec);
                return loaded;
            }
            impl->apps.push_back(std::make_unique<RealAppNode>(std::move(data), *it->second));
            impl->index[i] = impl->apps.back().get();
        } else if (node.kind == "test") {
            const auto it = test_index.find(node.dir);
            if (it == test_index.end()) {
                std::filesystem::current_path(previous, ec);
                return loaded;
            }
            impl->tests.push_back(std::make_unique<RealTestNode>(std::move(data), *it->second));
            impl->index[i] = impl->tests.back().get();
        } else if (node.kind == "doc") {
            const auto it = doc_index.find(node.dir);
            if (it == doc_index.end()) {
                std::filesystem::current_path(previous, ec);
                return loaded;
            }
            impl->docs.push_back(std::make_unique<RealDocNode>(std::move(data), *it->second));
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

}  // namespace mm::model
