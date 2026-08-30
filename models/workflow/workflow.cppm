// Abstract data model of a workflow step: one of the top level *.sh scripts
// and how it relates to the others, per docs/modules.mdy's "Build and
// development workflow". See docs/modules-model.mdy for the full models/
// picture.
//
// Operation is the script layer, above models.tool: bootstrap.sh, build.sh,
// test.sh, document.sh, check.sh, model.sh, and clean.sh are each an
// Operation. How many Tools one drives varies: clean.sh drives none, it
// only removes generated files; build.sh, document.sh, check.sh, and
// model.sh each drive exactly one; test.sh drives several in sequence
// (build0, build1, main, mdy, and the test runner). bootstrap.sh drives the
// host compiler directly and repeatedly, producing two staged executables,
// build0 and build1, that have no declaring manifest of their own (see
// models.tool). The compiler itself is a Tool too, the same
// Provenance::ThirdParty way cppcheck and semgrep are; bootstrap.sh's
// tools() would include it, once mm.model represents a compiler as a Tool.
// Modeling this separately from Tool keeps "what can run" (models.tool)
// apart from "in what order, and is it required" (this module).
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

    // The tools this script drives, in the order it drives them. Empty for
    // a script with no tool of its own, such as clean.sh. Most operations
    // return exactly one; test.sh returns several; see the note above the
    // class.
    [[nodiscard]] virtual std::vector<const Tool*> tools() const = 0;
};

}  // namespace models
