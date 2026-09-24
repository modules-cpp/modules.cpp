// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

import mm.shell;
import mm.shell.full;
import mm.test;

namespace mm::shell::full {
namespace {

using mm::test::expect;

struct Values {
    std::vector<VariableSlot> variables = std::vector<VariableSlot>(32);
    std::vector<char> variable_bytes = std::vector<char>(1024);
    std::vector<PositionalSlot> positionals =
        std::vector<PositionalSlot>(16);
    std::vector<char> positional_bytes = std::vector<char>(512);
    ShellState state;

    Values()
        : state{variables, variable_bytes, positionals, positional_bytes} {}
};

// Names only, so a pattern can be matched without a real directory.
struct FakeTree {
    std::vector<std::string> entries;

    [[nodiscard]] FileService service() {
        return {this, &FakeTree::list_callback, nullptr};
    }

    static ServiceStatus list_callback(void* context, std::string_view,
                                       std::vector<std::string>& out) {
        auto& self = *static_cast<FakeTree*>(context);
        for (const auto& entry : self.entries) out.push_back(entry);
        return ServiceStatus::Ok;
    }
};

// A substituter that returns a fixed capture, so expansion is tested without
// an evaluator.
struct FakeSubstituter {
    std::vector<std::string> sources;
    std::string reply = "captured";
    int status = 0;
    ServiceStatus service = ServiceStatus::Ok;

    [[nodiscard]] Substituter substituter() {
        return {this, &FakeSubstituter::run_callback};
    }

    static ServiceStatus run_callback(void* context, std::string_view source,
                                      std::string& out, int& status) {
        auto& self = *static_cast<FakeSubstituter*>(context);
        self.sources.emplace_back(source);
        if (self.service != ServiceStatus::Ok) return self.service;
        out = self.reply;
        status = self.status;
        return ServiceStatus::Ok;
    }
};

struct Fixture {
    Values values;
    FakeTree tree;
    FakeSubstituter substituter;

    [[nodiscard]] WordFields expand(std::string_view spelling,
                                    bool split = true,
                                    bool pathname = true) {
        return expand_word_full(spelling, values.state,
                                {split, pathname, "work"}, tree.service(),
                                substituter.substituter());
    }

    [[nodiscard]] bool one(std::string_view spelling,
                           std::string_view expected) {
        const auto fields = expand(spelling);
        return fields.ok() && fields.fields.size() == 1 &&
               fields.fields[0] == expected;
    }
};

void quotes_and_escapes() {
    Fixture fixture;
    expect(fixture.one("plain", "plain"), "a bare word is itself");
    expect(fixture.one("'$notexpanded'", "$notexpanded"),
           "single quotes suppress expansion");
    expect(fixture.one("\"quoted text\"", "quoted text"),
           "double quotes keep one field");
    expect(fixture.one("a'b'\"c\"d", "abcd"),
           "adjacent quoting styles concatenate into one field");
    expect(fixture.one("\\$literal", "$literal"),
           "a backslash escapes a dollar");
    expect(fixture.one("\"a\\\"b\"", "a\"b"),
           "a backslash escapes a quote inside double quotes");
    expect(fixture.one("\"a\\qb\"", "a\\qb"),
           "a backslash before an ordinary byte stays literal when quoted");
    const auto empty = fixture.expand("\"\"");
    expect(empty.ok() && empty.fields.size() == 1 && empty.fields[0].empty(),
           "an empty quoted word is still one empty field");
    const auto unterminated = fixture.expand("'open");
    expect(unterminated.status == Status::BadArgument,
           "an unterminated quote is a bad argument");
    const auto backtick = fixture.expand("`date`");
    expect(backtick.status == Status::Unsupported,
           "the obsolete backtick form is refused");
}

void parameters_and_operators() {
    Fixture fixture;
    expect(fixture.values.state.assign("name", "value").ok(), "assigns");
    expect(fixture.values.state.assign("empty", "").ok(), "assigns empty");
    expect(fixture.one("$name", "value"), "a bare reference expands");
    expect(fixture.one("${name}", "value"), "a braced reference expands");
    expect(fixture.one("pre${name}post", "prevaluepost"),
           "a braced reference joins its neighbours");
    expect(fixture.one("${absent:-fallback}", "fallback"),
           "a colon default supplies a fallback");
    expect(fixture.one("${empty:-fallback}", "fallback"),
           "a colon default treats empty as unset");
    expect(fixture.one("\"${empty-kept}\"", ""),
           "the plain default form keeps an empty value");
    expect(fixture.one("${name:+present}", "present"),
           "an alternate fires on a set value");
    const auto alternate_unset = fixture.expand("${absent:+present}");
    expect(alternate_unset.ok() && alternate_unset.fields.empty(),
           "an unquoted alternate yields no field at all when unset");
    expect(fixture.one("\"${absent:+present}\"", ""),
           "quoting that same alternate yields one empty field");
    expect(fixture.one("${name}${name}", "valuevalue"),
           "two references concatenate");

    expect(fixture.one("${absent:=created}", "created") &&
               fixture.values.state.lookup("absent").value == "created",
           "an assignment operand both expands and assigns");

    expect(fixture.one("${#name}", "5"), "a length request counts bytes");
    expect(fixture.values.state.assign("path", "dir/file.sh").ok(),
           "assigns a path");
    expect(fixture.one("${path#*/}", "file.sh"),
           "a short prefix trim removes the least it can");
    expect(fixture.values.state.assign("deep", "a/b/c").ok(), "assigns");
    expect(fixture.one("${deep##*/}", "c"),
           "a long prefix trim removes the most it can");
    expect(fixture.one("${path%.sh}", "dir/file"),
           "a suffix trim removes a literal tail");
    expect(fixture.one("${deep%%/*}", "a"),
           "a long suffix trim removes the most it can");

    const auto missing = fixture.expand("${absent2:?}");
    expect(missing.status == Status::NotFound,
           "the error form reports an unset parameter");
    const auto malformed = fixture.expand("${}");
    expect(malformed.status == Status::BadArgument,
           "an empty reference body is malformed");
}

void nounset_and_specials() {
    Fixture fixture;
    const std::string_view arguments[]{"one", "two three"};
    expect(fixture.values.state.set_positionals("shell", arguments).ok(),
           "positionals install");
    fixture.values.state.last_status = 7;

    expect(fixture.one("$#", "2"), "the argument count expands");
    expect(fixture.one("$?", "7"), "the last status expands");
    expect(fixture.one("$1", "one"), "a positional expands");
    expect(fixture.one("\"${2}\"", "two three"),
           "a quoted braced positional keeps its bytes unsplit");

    const auto at_quoted = fixture.expand("\"$@\"");
    expect(at_quoted.ok() && at_quoted.fields.size() == 2 &&
               at_quoted.fields[0] == "one" &&
               at_quoted.fields[1] == "two three",
           "a quoted argument vector is one field per argument");
    const auto at_bare = fixture.expand("$@");
    expect(at_bare.ok() && at_bare.fields.size() == 3 &&
               at_bare.fields[2] == "three",
           "an unquoted argument vector splits on IFS");
    const auto star_quoted = fixture.expand("\"$*\"");
    expect(star_quoted.ok() && star_quoted.fields.size() == 1 &&
               star_quoted.fields[0] == "one two three",
           "a quoted star joins with one separator");

    const auto permitted = fixture.expand("$absent");
    expect(permitted.ok() && permitted.fields.empty(),
           "an unset parameter expands to nothing without set -u");
    fixture.values.state.nounset = true;
    const auto refused = fixture.expand("$absent");
    expect(refused.status == Status::NotFound,
           "set -u makes an unset parameter an error");
    const auto guarded = fixture.expand("${absent:-ok}");
    expect(guarded.ok() && guarded.fields.size() == 1,
           "a default operand stays exempt from set -u");
}

void splitting_and_arithmetic() {
    Fixture fixture;
    expect(fixture.values.state.assign("spaced", "a  b\tc").ok(), "assigns");
    const auto split = fixture.expand("$spaced");
    expect(split.ok() && split.fields.size() == 3 &&
               split.fields[0] == "a" && split.fields[2] == "c",
           "an unquoted expansion splits on IFS and drops runs");
    const auto joined = fixture.expand("\"$spaced\"");
    expect(joined.ok() && joined.fields.size() == 1 &&
               joined.fields[0] == "a  b\tc",
           "a quoted expansion never splits");
    const auto unsplit = fixture.expand("$spaced", false);
    expect(unsplit.ok() && unsplit.fields.size() == 1,
           "an assignment value is not split");

    expect(fixture.one("$((1 + 2))", "3"), "arithmetic expands");
    expect(fixture.one("$((0x10))", "16"),
           "hexadecimal reaches the full arithmetic path");
    expect(fixture.values.state.assign("n", "4").ok(), "assigns");
    expect(fixture.one("$((n * 2))", "8"),
           "arithmetic reads shell variables");
    const auto bad = fixture.expand("$((1 +))");
    expect(bad.status == Status::BadArgument,
           "a malformed expression is a bad argument");
}

void substitution_and_pathname() {
    Fixture fixture;
    fixture.substituter.reply = "head";
    expect(fixture.one("$(git rev-parse HEAD)", "head"),
           "a command substitution supplies its capture");
    expect(fixture.substituter.sources.size() == 1 &&
               fixture.substituter.sources[0] == "git rev-parse HEAD",
           "the inner source reaches the runner unchanged");
    fixture.substituter.status = 3;
    const auto carried = fixture.expand("$(false)");
    expect(carried.ok() && carried.substitution_status == 3,
           "the substitution status is reported to the caller");
    fixture.substituter.service = ServiceStatus::Failed;
    const auto broken = fixture.expand("$(anything)");
    expect(broken.service == ServiceStatus::Failed,
           "a failing runner fails the expansion");

    Fixture globbing;
    globbing.tree.entries = {"build.sh", "clean.sh", "notes.txt"};
    const auto matched = globbing.expand("*.sh");
    expect(matched.ok() && matched.fields.size() == 2 &&
               matched.fields[0] == "build.sh",
           "an unquoted pattern expands against the tree");
    const auto quoted = globbing.expand("\"*.sh\"");
    expect(quoted.ok() && quoted.fields.size() == 1 &&
               quoted.fields[0] == "*.sh",
           "a quoted pattern stays literal");
    const auto suppressed = globbing.expand("*.sh", true, false);
    expect(suppressed.ok() && suppressed.fields.size() == 1,
           "pathname expansion is skipped where POSIX says it does not apply");
    const auto unmatched = globbing.expand("*.md");
    expect(unmatched.ok() && unmatched.fields.size() == 1 &&
               unmatched.fields[0] == "*.md",
           "an unmatched pattern stays the literal word");
    expect(globbing.values.state.assign("pattern", "*.sh").ok(), "assigns");
    const auto expanded_pattern = globbing.expand("$pattern");
    expect(expanded_pattern.ok() && expanded_pattern.fields.size() == 2,
           "a pattern that came from an expansion still globs");
    const auto mixed = globbing.expand("\"*\"*");
    expect(mixed.ok() && mixed.fields.size() == 1 && mixed.fields[0] == "**",
           "a word mixing quoted and unquoted metacharacters stays literal");
}

const mm::test::case_ cases[]{
    {"quotes and escapes", &quotes_and_escapes},
    {"parameters and operators", &parameters_and_operators},
    {"nounset and specials", &nounset_and_specials},
    {"splitting and arithmetic", &splitting_and_arithmetic},
    {"substitution and pathname", &substitution_and_pathname},
};

const mm::test::registrar reg{"mm.shell.full words", cases};

}  // namespace
}  // namespace mm::shell::full
