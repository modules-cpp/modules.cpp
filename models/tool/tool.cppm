// Abstract data model of an invocable tool: something a script can run,
// whether this project builds it or an installed third-party binary
// provides it. See models.manifest (models/manifest/manifest.cppm) for the
// note on why this is a design artifact rather than a build target.
//
// Distinct from models::AppNode (models.manifest): AppNode is a manifest's
// declaration that a binary should exist; Tool is the binary itself, which
// may or may not have a declaring manifest. build, test, check, mdy, and
// main are each both an AppNode and a Tool. build0 and build1
// (tools/build/main.cpp, the bootstrap-stage compiler-driven artifacts) are
// Tools with no AppNode: they exist before any manifest does, which is the
// whole point of bootstrap.sh. cppcheck and semgrep are Tools with no
// AppNode for the opposite reason: this project never builds them.
//
// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
module;

#include <filesystem>
#include <string_view>

export module models.tool;

import models.manifest;

export namespace models {

enum class Provenance { BuiltIn, ThirdParty };

class Tool {
public:
    virtual ~Tool() = default;

    [[nodiscard]] virtual std::string_view name() const = 0;
    [[nodiscard]] virtual Provenance provenance() const = 0;

    // Where this tool is invoked from: a root relative path such as
    // "out/bin/build" for a built-in tool, or the bare command name such as
    // "cppcheck" for a third-party tool resolved from $PATH.
    [[nodiscard]] virtual std::filesystem::path invocation() const = 0;

    // The kind:app manifest that produces this tool's binary. nullptr for a
    // ThirdParty tool, and also nullptr for a BuiltIn tool with no manifest
    // of its own, such as build0 and build1.
    [[nodiscard]] virtual const AppNode* declared_by() const = 0;
};

}  // namespace models
