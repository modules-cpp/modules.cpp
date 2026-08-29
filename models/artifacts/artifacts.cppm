// Abstract data model of a generated artifact, per docs/modules.mdy's
// "Generated artifacts" section. See docs/modules-model.mdy for the full
// models/ picture.
//
// ArtifactKind starts from that section's bullet list, but splits
// AppObject and ToolObject each into an object-file variant and an
// executable variant: out/apps/main, for example, holds both main.cpp.o
// and the uninstalled main executable build.sh links before installing it
// to out/bin, and a single AppObject kind cannot tell those apart. Staged
// adds what the bullet list does not mention at all: out/build0 and
// out/build1, the pre-manifest executables bootstrap.sh produces directly
// from the compiler, named by neither a kind:app manifest nor any of the
// per-directory kinds below. produced_by() points at the Operation
// (models.workflow) whose script writes that kind of artifact, rather than
// at a Tool: docs/modules.mdy describes generation in terms of
// bootstrap.sh/build.sh/test.sh/document.sh, and models.workflow already
// models exactly that layer.
//
// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
module;

#include <filesystem>

export module models.artifacts;

import models.workflow;

export namespace models {

enum class ArtifactKind {
    ModuleObject,      // out/modules/**/*.o
    AppObject,         // out/apps/**/*.o
    AppExecutable,     // out/apps/**/<name>, before install
    ToolObject,        // out/tools/**/*.o
    ToolExecutable,    // out/tools/**/<name>, before install
    Staged,            // out/build0, out/build1
    InstalledBinary,   // out/bin/<name>
    TestBuild,         // out/tests
    Documentation,     // out/index.html and nested pages
    ModuleCache        // gcm.cache
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
