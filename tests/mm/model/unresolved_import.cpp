// Black box test for mm.model::Loaded::load() against a module whose use:
// entry names a module nothing in the tree declares. Loaded::load() never
// calls mm::build::order(), which is the only place that guarantee is
// normally re-derived, so a prior version of build_modules() silently
// dropped the edge instead of representing it: imports() came back shorter
// than declared_by().uses(), with no signal to a caller that anything was
// lost. This pins the fixed behavior: load() fails outright instead.

#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>

import mm.model;
import mm.test;

namespace {

void a_module_using_an_unknown_module_fails_to_load() {
    const mm::test::scoped_tree tree{"unresolved_import"};
    tree.manifest("", "kind: project\nname: p\nfolder: a\n");
    tree.manifest("a", "kind: module\nname: a\nmodule: mm.a\nfile: a.cppm\nuse: mm.nonexistent\n");

    bool ok = false;
    auto loaded = mm::model::Loaded::load(tree.root(), ok);
    mm::test::expect(!ok, "expected a module using an unknown module to fail to load, "
                         "not silently drop the edge from imports()");
}

const mm::test::case_ cases[] = {
    { "a module using an unknown module fails to load", &a_module_using_an_unknown_module_fails_to_load },
};

const mm::test::registrar reg{"mm.model unresolved import", cases};

}  // namespace
