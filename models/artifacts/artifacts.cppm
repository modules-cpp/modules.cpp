// Abstract data model of a generated artifact, per docs/modules.mdy's
// "Generated artifacts" section. See docs/modules-model.mdy for the full
// models/ picture.
//
// ArtifactKind lives in models.workflow, not here: see that module's doc
// comment for why (models.artifacts depends on models.workflow for
// produced_by(), so the reverse dependency is not available). It starts
// from that section's bullet list, but splits AppObject and ToolObject each
// into an object-file variant and an executable variant: out/apps/main, for
// example, holds both main.cpp.o and the uninstalled main executable
// build.sh links before installing it to out/bin, and a single AppObject
// kind cannot tell those apart. Staged adds what the bullet list does not
// mention at all: out/build0 and out/build1, the pre-manifest executables
// bootstrap.sh produces directly from the compiler, named by neither a
// kind:app manifest nor any of the per-directory kinds below.
//
// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
module;

#include <filesystem>

export module models.artifacts;

import models.workflow;

export namespace models {

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
