// Black box tests for mm.model::Loaded against a project that is not
// modules.cpp itself. Loaded::load() accepts any tree whose root manifest
// is kind:project (this is what makes it a general adapter rather than one
// hard-coded to this repository), but build0/build1/c++ in tools() and all
// of operations() are fixed data describing this repository's own
// bootstrap.sh specifically. A prior version added those unconditionally,
// so tools() reported a build0/build1/c++ that had nothing to do with a
// foreign project, and operations() embedded a null Tool* into invokes()
// for every one of the six named tools (build, main, mdy, test, check,
// model) a foreign project does not happen to declare. These cases pin the
// fixed correction: load() still succeeds generally, but the
// repository-specific fixed data is absent rather than null or fabricated.

#include <cstddef>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>

import mm.model;
import mm.test;
import models.repository;
import models.tool;
import models.workflow;

namespace {

// Writes the same foreign (not modules.cpp) project into tree: one
// kind:project with one kind:app named "widget" - deliberately none of the
// six names (build, main, mdy, test, check, model) operations() and the
// fixed part of tools() look for.
void write_foreign_project(const mm::test::scoped_tree& tree) {
    tree.manifest("", "kind: project\nname: unrelated-project\nfolder: a\n");
    tree.manifest("a", "kind: app\nname: widget\nfile: a.cpp\n");
}

void load_of_a_foreign_project_still_succeeds() {
    const mm::test::scoped_tree tree{"foreign_load"};
    write_foreign_project(tree);
    bool ok = false;
    auto loaded = mm::model::Loaded::load(tree.root(), ok);
    mm::test::expect(ok, "expected load() to accept any valid project, not only modules.cpp");
    mm::test::expect(loaded.repository().apps().size() == 1, "expected the one declared app");
}

void a_foreign_project_has_no_bootstrap_tools() {
    const mm::test::scoped_tree tree{"foreign_tools"};
    write_foreign_project(tree);
    bool ok = false;
    auto loaded = mm::model::Loaded::load(tree.root(), ok);
    if (!ok) return;

    const auto tools = loaded.tools();
    mm::test::expect(tools.size() == 1,
                     "expected only the declared \"widget\" tool, no build0/build1/c++");

    for (const auto* tool : tools) {
        mm::test::expect(tool->name() != "build0", "expected no build0 for a foreign project");
        mm::test::expect(tool->name() != "build1", "expected no build1 for a foreign project");
        mm::test::expect(tool->name() != "c++", "expected no c++ for a foreign project");
    }
}

void a_foreign_project_has_no_operations() {
    const mm::test::scoped_tree tree{"foreign_operations"};
    write_foreign_project(tree);
    bool ok = false;
    auto loaded = mm::model::Loaded::load(tree.root(), ok);
    if (!ok) return;

    mm::test::expect(loaded.operations().empty(),
                     "expected no Operations for a project that is not modules.cpp, "
                     "rather than Operations holding a null Tool*");
}

const mm::test::case_ cases[] = {
    { "load of a foreign project still succeeds",  &load_of_a_foreign_project_still_succeeds },
    { "a foreign project has no bootstrap tools",  &a_foreign_project_has_no_bootstrap_tools },
    { "a foreign project has no operations",       &a_foreign_project_has_no_operations },
};

const mm::test::registrar reg{"mm.model foreign project", cases};

}  // namespace
