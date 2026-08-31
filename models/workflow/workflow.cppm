// Abstract data model of a workflow step: one of the top level *.sh scripts
// and how it relates to the others, per docs/modules.mdy's "Build and
// development workflow". See docs/modules-model.mdy for the full models/
// picture.
//
// Operation is the script layer, above models.tool: bootstrap.sh, build.sh,
// test.sh, document.sh, check.sh, model.sh, and clean.sh are each an
// Operation. A single ordered invokes() sequence is not enough for all
// seven: bootstrap.sh tries build0 first and only falls back to driving the
// host compiler directly and repeatedly if that does not produce build1, so
// an Operation can have more than one branch — most have exactly one.
// role() replaces a plain optional bool: clean.sh is not "optional" the way
// check.sh is (skippable within the standard sequence, still recommended);
// it is something the user chooses to run outside that sequence entirely.
//
// requires_artifacts() and produces() are the input and output side of what
// used to be a single depends_on() "must run first" edge: build.sh's real
// requirement is that out/build1 (ArtifactKind::Staged) exists, not that
// bootstrap.sh executed in the current session, so an incremental workflow
// where build1 already exists from an earlier run is not something this
// model reports as invalid the way a strict "ran before me" edge would.
//
// ArtifactKind lives here rather than in models.artifacts because the
// dependency runs the other way: models.artifacts models a concrete
// generated artifact and needs to point at the Operation that produces it
// (produced_by() : const Operation*), so it already imports models.workflow;
// putting the kind vocabulary in models.artifacts instead would make the two
// modules depend on each other, which C++20 modules do not allow.
//
// Modeling this separately from Tool keeps "what can run" (models.tool)
// apart from "in what order, under what conditions, and is it required"
// (this module).
//
// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
module;

#include <cstddef>
#include <filesystem>
#include <string_view>
#include <vector>

export module models.workflow;

import models.tool;

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

// Required: part of the standard sequence and nothing after it can
// meaningfully run without it (bootstrap.sh, build.sh). Optional: part of
// the standard sequence and skippable without affecting the rest of it
// (test.sh, document.sh, check.sh, model.sh). UserInitiated: not part of
// the forward sequence at all; the user runs it deliberately, outside the
// normal order (clean.sh).
enum class Role { Required, Optional, UserInitiated };

class Operation {
public:
    virtual ~Operation() = default;

    [[nodiscard]] virtual std::string_view name() const = 0;

    // Root relative, e.g. "build.sh".
    [[nodiscard]] virtual std::filesystem::path script_path() const = 0;

    [[nodiscard]] virtual Role role() const = 0;

    // How many alternative sequences invokes() can describe. 1 for every
    // operation except bootstrap.sh, which has 2: branch 0 tries build0,
    // branch 1 is the raw-compiler fallback taken only if that does not
    // produce build1.
    [[nodiscard]] virtual std::size_t branch_count() const = 0;

    // The tools one branch drives, in the order it drives them. Empty for a
    // script with no tool of its own, such as clean.sh. branch must be less
    // than branch_count().
    [[nodiscard]] virtual std::vector<const Tool*> invokes(std::size_t branch) const = 0;

    // Artifacts that must already exist for this operation to succeed,
    // regardless of what produced them or when.
    [[nodiscard]] virtual std::vector<ArtifactKind> requires_artifacts() const = 0;

    // Artifacts this operation writes on success. The same regardless of
    // which invokes() branch actually ran: bootstrap.sh's two branches
    // reach the same produces(), just by different means.
    [[nodiscard]] virtual std::vector<ArtifactKind> produces() const = 0;
};

}  // namespace models
