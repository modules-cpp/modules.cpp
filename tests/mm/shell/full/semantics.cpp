// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
#include <algorithm>
#include <cstddef>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

import mm.shell;
import mm.shell.full;
import mm.test;

namespace mm::shell::full {
namespace {

using mm::test::expect;

// A fake tree: names only, because pathname expansion asks for nothing else.
struct FakeTree {
    struct Directory {
        std::string path;
        std::vector<std::string> entries;
    };

    std::vector<Directory> directories;
    std::size_t listings = 0;
    std::size_t queries = 0;
    ServiceStatus list_status = ServiceStatus::Ok;

    [[nodiscard]] FileService service() {
        return {this, &FakeTree::list_callback, &FakeTree::status_callback};
    }

    [[nodiscard]] const Directory* at(std::string_view path) const {
        for (const auto& directory : directories) {
            if (directory.path == path) return &directory;
        }
        return nullptr;
    }

    static ServiceStatus list_callback(void* context, std::string_view path,
                                       std::vector<std::string>& out) {
        auto& self = *static_cast<FakeTree*>(context);
        ++self.listings;
        if (self.list_status != ServiceStatus::Ok) return self.list_status;
        const auto* directory = self.at(path);
        if (directory == nullptr) return ServiceStatus::NotFound;
        for (const auto& entry : directory->entries) out.push_back(entry);
        return ServiceStatus::Ok;
    }

    static ServiceStatus status_callback(void* context, std::string_view path,
                                         bool& exists, bool& directory) {
        auto& self = *static_cast<FakeTree*>(context);
        ++self.queries;
        directory = self.at(path) != nullptr;
        exists = directory;
        if (exists) return ServiceStatus::Ok;
        // A name listed by its parent exists even when it is not a directory.
        const auto slash = path.rfind('/');
        const auto parent = slash == std::string_view::npos
                                ? std::string_view{}
                                : path.substr(0, slash);
        const auto leaf = slash == std::string_view::npos
                              ? path
                              : path.substr(slash + 1);
        const auto* holder = self.at(parent);
        if (holder == nullptr) return ServiceStatus::Ok;
        exists = std::find(holder->entries.begin(), holder->entries.end(),
                           leaf) != holder->entries.end();
        return ServiceStatus::Ok;
    }
};

[[nodiscard]] FakeTree sample_tree() {
    FakeTree tree;
    tree.directories.push_back(
        {"work", {"build.sh", "clean.sh", "notes.txt", ".hidden", "src"}});
    tree.directories.push_back({"work/src", {"main.cpp", "util.cpp"}});
    return tree;
}

void pathname_requests_only_names() {
    auto tree = sample_tree();
    const auto service = tree.service();

    const auto literal = expand_pathname("build.sh", "work", service);
    expect(literal.ok() && !literal.expanded && literal.matches.empty() &&
               tree.listings == 0,
           "a word with no pattern byte asks the service nothing");

    const auto scripts = expand_pathname("*.sh", "work", service);
    expect(scripts.ok() && scripts.expanded && scripts.matches.size() == 2 &&
               scripts.matches[0] == "build.sh" &&
               scripts.matches[1] == "clean.sh",
           "a pattern yields sorted matches");

    const auto hidden = expand_pathname("*", "work", service);
    expect(hidden.ok() && hidden.expanded &&
               std::find(hidden.matches.begin(), hidden.matches.end(),
                         ".hidden") == hidden.matches.end(),
           "a leading period is not matched by a bare star");

    const auto dotted = expand_pathname(".*", "work", service);
    expect(dotted.ok() && dotted.expanded && dotted.matches.size() == 1 &&
               dotted.matches[0] == ".hidden",
           "an explicit leading period matches a dot entry");

    const auto nested = expand_pathname("src/*.cpp", "work", service);
    expect(nested.ok() && nested.expanded && nested.matches.size() == 2 &&
               nested.matches[0] == "src/main.cpp" &&
               nested.matches[1] == "src/util.cpp",
           "a literal component needs no listing and keeps its spelling");

    const auto every = expand_pathname("*/*.cpp", "work", service);
    expect(every.ok() && every.expanded && every.matches.size() == 2 &&
               every.matches[0] == "src/main.cpp",
           "a pattern component walks into each matching directory");
}

void unmatched_patterns_stay_literal() {
    auto tree = sample_tree();
    const auto service = tree.service();

    const auto missing = expand_pathname("*.md", "work", service);
    expect(missing.ok() && !missing.expanded && missing.matches.empty(),
           "an unmatched pattern reports no expansion, not an error");

    const auto absent = expand_pathname("nowhere/*", "work", service);
    expect(absent.ok() && !absent.expanded,
           "an unreadable directory contributes no match");

    FakeTree denied;
    denied.list_status = ServiceStatus::PermissionDenied;
    const auto refused = expand_pathname("*", "work", denied.service());
    expect(refused.ok() && !refused.expanded,
           "a refused listing contributes no match either");

    FakeTree broken;
    broken.list_status = ServiceStatus::Failed;
    const auto failed = expand_pathname("*", "work", broken.service());
    expect(failed.service == ServiceStatus::Failed && !failed.expanded,
           "a failing service is reported rather than silently empty");

    const FileService none;
    const auto unavailable = expand_pathname("*", "work", none);
    expect(unavailable.service == ServiceStatus::Invalid,
           "an unwired service is invalid, not an empty match");
}

struct Values {
    std::vector<VariableSlot> variables = std::vector<VariableSlot>(16);
    std::vector<char> variable_bytes = std::vector<char>(512);
    std::vector<PositionalSlot> positionals =
        std::vector<PositionalSlot>(16);
    std::vector<char> positional_bytes = std::vector<char>(256);
    ShellState state;

    Values()
        : state{variables, variable_bytes, positionals, positional_bytes} {}
};

void here_document_bodies_are_byte_streams() {
    Values values;
    expect(values.state.assign("name", "world").ok(), "fixture assigns");
    const std::string_view arguments[]{"one", "two"};
    expect(values.state.set_positionals("shell", arguments).ok(),
           "fixture installs positionals");

    const auto quoted = expand_here_document("hello $name\n", false,
                                             values.state);
    expect(quoted.status == Status::Ok && !quoted.expanded &&
               quoted.segments.size() == 1 &&
               quoted.segments[0].text == "hello $name\n",
           "a quoted delimiter yields the body verbatim");

    const auto plain = expand_here_document("hello $name\n", true,
                                            values.state);
    expect(plain.status == Status::Ok && plain.expanded &&
               plain.segments.size() == 1 &&
               plain.segments[0].text == "hello world\n",
           "an unquoted delimiter expands a parameter");

    // Field splitting would turn this into two fields and pathname expansion
    // would replace the star. A byte stream does neither.
    expect(values.state.assign("spaced", "a  b").ok(), "fixture assigns");
    const auto unsplit = expand_here_document("[$spaced] *.sh\n", true,
                                              values.state);
    expect(unsplit.status == Status::Ok && unsplit.segments.size() == 1 &&
               unsplit.segments[0].text == "[a  b] *.sh\n",
           "no field splitting and no pathname expansion occur in a body");

    const auto braced = expand_here_document(
        "${name} ${missing:-fallback} ${missing:+set}\n", true,
        values.state);
    expect(braced.status == Status::Ok &&
               braced.segments[0].text == "world fallback \n",
           "braced forms and their operands expand");

    const auto counted = expand_here_document("$# $1 $@\n", true,
                                              values.state);
    expect(counted.status == Status::Ok &&
               counted.segments[0].text == "2 one one two\n",
           "positional forms join with one space and never split");

    const auto arithmetic = expand_here_document("total $((0x10 + 2))\n",
                                                 true, values.state);
    expect(arithmetic.status == Status::Ok &&
               arithmetic.segments[0].text == "total 18\n",
           "arithmetic expands, hexadecimal included");

    const auto escaped = expand_here_document("\\$name \\\\ \\q\n", true,
                                              values.state);
    expect(escaped.status == Status::Ok &&
               escaped.segments[0].text == "$name \\ \\q\n",
           "a backslash escapes only the bytes a here-document names");

    const auto substituted = expand_here_document(
        "commit $(git log -1) end\n", true, values.state);
    expect(substituted.status == Status::Ok &&
               substituted.segments.size() == 3 &&
               substituted.segments[0].text == "commit " &&
               substituted.segments[1].kind ==
                   HereSegmentKind::Substitution &&
               substituted.segments[1].text == "git log -1" &&
               substituted.segments[2].text == " end\n",
           "a command substitution is returned as a request, not executed");

    const auto backtick = expand_here_document("`date`\n", true,
                                               values.state);
    expect(backtick.status == Status::Unsupported && backtick.issue == 0,
           "the obsolete backtick form is refused inside a body too");

    const auto assign = expand_here_document("${name:=other}\n", true,
                                             values.state);
    expect(assign.status == Status::Unsupported,
           "an assignment operand cannot mutate the state a body reads");
}

void full_functions_replace_atomically() {
    FullFunctionLibrary library;
    expect(library.size() == 0 && library.find("greet") == nullptr,
           "a fresh library is empty");
    expect(library.define("greet", "echo hello $1\n").ok() &&
               library.size() == 1 && library.find("greet") != nullptr,
           "a valid body defines a function");
    expect(library.define("2bad", "echo hi\n").status ==
               Status::BadArgument &&
               library.define("while", "echo hi\n").status ==
                   Status::BadArgument,
           "an invalid or reserved name is refused");

    const auto first = library.body("greet");
    expect(first != nullptr && !first->nodes.empty(),
           "the handle carries an owning parsed body");

    const auto refused = library.define("greet", "echo one ;; echo two\n");
    expect(refused.status == Status::BadArgument &&
               library.body("greet") == first,
           "a failed redefinition leaves the previous body visible");
    const auto unsupported = library.define("greet", "echo `date`\n");
    expect(unsupported.status == Status::Unsupported &&
               library.body("greet") == first,
           "an excluded construct is reported as unsupported");

    expect(library.define("greet", "echo replaced\n").ok() &&
               library.size() == 1,
           "a valid redefinition replaces in place");
    const auto second = library.body("greet");
    expect(second != first && first.use_count() == 1 &&
               first->source.find("hello") != std::string::npos,
           "a body held by a running call keeps the text it started with");

    expect(library.remove("greet") && library.size() == 0 &&
               !library.remove("greet"),
           "removal is idempotent after the first call");
    library.reset();
    expect(library.size() == 0, "reset clears the table");
}

const mm::test::case_ cases[]{
    {"pathname requests", &pathname_requests_only_names},
    {"unmatched patterns", &unmatched_patterns_stay_literal},
    {"here-document bodies", &here_document_bodies_are_byte_streams},
    {"full functions", &full_functions_replace_atomically},
};

const mm::test::registrar reg{"mm.shell.full semantics", cases};

}  // namespace
}  // namespace mm::shell::full
