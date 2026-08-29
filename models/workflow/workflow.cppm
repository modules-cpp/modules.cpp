// Abstract data model of a workflow step: one of the top level *.sh scripts
// and how it relates to the others, per docs/modules.mdy's "Build and
// development workflow". See models.manifest (models/manifest/manifest.cppm)
// for the note on why this is a design artifact rather than a build target.
//
// Operation is the script layer, above models.tool: bootstrap.sh, build.sh,
// test.sh, document.sh, check.sh, and clean.sh are each an Operation, and
// each but clean.sh drives a Tool. Modeling this separately from Tool keeps
// "what can run" (models.tool) apart from "in what order, and is it
// required" (this module): clean.sh is an Operation with no Tool, and
// check.sh is a Tool-driving Operation that happens to be optional.
//
// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
module;

#include <filesystem>
#include <string_view>
#include <vector>

export module models.workflow;

import models.tool;

export namespace models {

class Operation {
public:
    virtual ~Operation() = default;

    [[nodiscard]] virtual std::string_view name() const = 0;

    // Root relative, e.g. "build.sh".
    [[nodiscard]] virtual std::filesystem::path script_path() const = 0;

    // Other operations that must run first, e.g. build.sh depends on
    // bootstrap.sh. Empty for bootstrap.sh itself.
    [[nodiscard]] virtual std::vector<const Operation*> depends_on() const = 0;

    // True for an operation documented as optional and experimental, such
    // as check.sh: skipping it changes nothing else in the sequence.
    [[nodiscard]] virtual bool optional() const = 0;

    // The tool this script drives, or nullptr for a script with no
    // corresponding tool of its own, such as clean.sh, which only removes
    // generated files.
    [[nodiscard]] virtual const Tool* tool() const = 0;
};

}  // namespace models
