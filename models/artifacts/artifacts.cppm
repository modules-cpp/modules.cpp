// Abstract data model of a generated artifact, per docs/modules.mdy's
// "Generated artifacts" section. See docs/modules-model.mdy for the full
// models/ picture.
//
// ArtifactKind is that section's bullet list, one variant each. produced_by()
// points at the Operation (models.workflow) whose script writes that kind of
// artifact, rather than at a Tool: docs/modules.mdy describes generation in
// terms of bootstrap.sh/build.sh/test.sh/document.sh, and models.workflow
// already models exactly that layer.
//
// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
module;

#include <filesystem>

export module models.artifacts;

import models.workflow;

export namespace models {

enum class ArtifactKind {
    ModuleObject,     // out/modules
    AppObject,        // out/apps
    ToolObject,       // out/tools
    InstalledBinary,  // out/bin
    TestBuild,        // out/tests
    Documentation,    // out/index.html and nested pages
    ModuleCache       // gcm.cache
};

class GeneratedArtifact {
public:
    virtual ~GeneratedArtifact() = default;

    [[nodiscard]] virtual ArtifactKind kind() const = 0;

    // Root relative, e.g. "out/bin/build" or "gcm.cache".
    [[nodiscard]] virtual std::filesystem::path path() const = 0;

    // The operation whose script writes this artifact. nullptr for
    // ModuleCache: every compiler invocation writes to gcm.cache as a side
    // effect, so no single operation is the one that produces it.
    [[nodiscard]] virtual const Operation* produced_by() const = 0;
};

}  // namespace models
