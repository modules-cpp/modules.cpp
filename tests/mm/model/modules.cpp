// Black box tests for mm.model::Loaded::resolved_modules(), run against
// this project's own real tree.

#include <string_view>
#include <vector>

import mm.model;
import mm.test;
import models.manifest;
import models.modules;
import models.repository;

namespace {

const models::Module* find_module(const std::vector<const models::Module*>& modules,
                                  std::string_view name) {
    for (const auto* module : modules)
        if (module->name() == name) return module;
    return nullptr;
}

void resolved_modules_match_declared_modules() {
    bool ok = false;
    auto loaded = mm::model::Loaded::load(".", ok);
    mm::test::expect(loaded.resolved_modules().size() == loaded.repository().modules().size(),
                     "expected one resolved Module per declared ModuleNode");
}

void mm_build_imports_are_resolved_to_real_modules() {
    bool ok = false;
    auto loaded = mm::model::Loaded::load(".", ok);
    const auto modules = loaded.resolved_modules();

    const auto* mm_build = find_module(modules, "mm.build");
    mm::test::expect(mm_build != nullptr, "expected a resolved Module named mm.build");
    if (mm_build == nullptr) return;

    // mm.build's own manifest declares use: mm.mdy.
    bool imports_mdy = false;
    for (const auto* imported : mm_build->imports())
        if (imported->name() == "mm.mdy") imports_mdy = true;
    mm::test::expect(imports_mdy, "expected mm.build's imports() to include the real mm.mdy Module");
}

void a_leaf_module_imports_nothing() {
    bool ok = false;
    auto loaded = mm::model::Loaded::load(".", ok);
    const auto modules = loaded.resolved_modules();

    // models.document declares no use: entries of its own; it is the most
    // foundational module in models/ (see docs/modules-model.mdy).
    const auto* document = find_module(modules, "models.document");
    mm::test::expect(document != nullptr, "expected a resolved Module named models.document");
    if (document == nullptr) return;

    mm::test::expect(document->imports().empty(),
                     "expected models.document to import nothing, matching its own use: list");
}

void every_resolved_module_agrees_with_its_declared_by() {
    bool ok = false;
    auto loaded = mm::model::Loaded::load(".", ok);

    for (const auto* module : loaded.resolved_modules())
        mm::test::expect(module->name() == module->declared_by().exported_module_name(),
                         "expected name() to match declared_by().exported_module_name()");
}

const mm::test::case_ cases[] = {
    { "resolved modules match declared modules",       &resolved_modules_match_declared_modules },
    { "mm.build imports are resolved to real modules", &mm_build_imports_are_resolved_to_real_modules },
    { "a leaf module imports nothing",                 &a_leaf_module_imports_nothing },
    { "every resolved module agrees with declared_by", &every_resolved_module_agrees_with_its_declared_by },
};

const mm::test::registrar reg{"mm.model modules", cases};

}  // namespace
