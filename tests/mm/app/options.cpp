// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

import mm.app;
import mm.test;

namespace {

using mm::app::Cli;
using mm::app::Options;
using mm::test::expect;

// Options::parse takes argc and argv, so a case supplies its own vector. The
// program name occupies argv[0] exactly as it does on a real command line.
class Line {
public:
    explicit Line(std::vector<std::string> words)
        : words_(std::move(words)) {
        pointers_.reserve(words_.size());
        for (auto& word : words_) pointers_.push_back(word.data());
    }

    [[nodiscard]] int argc() const {
        return static_cast<int>(pointers_.size());
    }
    [[nodiscard]] char** argv() { return pointers_.data(); }

private:
    std::vector<std::string> words_;
    std::vector<char*> pointers_;
};

void flags_are_seen_and_counted() {
    Line line{{"tool", "--check", "--check", "--verbose"}};
    Options options{"tool"};
    options.flag("--check");
    options.flag("--other");
    expect(options.parse(line.argc(), line.argv()) == Cli::ok,
           "a flag line parses");
    expect(options.seen("--check") && options.count("--check") == 2,
           "a repeated flag is seen and counted");
    expect(!options.seen("--other") && options.count("--other") == 0,
           "an absent flag is neither seen nor counted");
    expect(options.verbose(), "the shared verbose flag is recognized");
}

// The behaviour this case pins is what a value-bearing option used to lack:
// seen() only answered for flag(), so a tool asking about an option always
// heard no and silently took its unset path.
void options_are_seen_and_counted() {
    Line line{{"tool", "-e", "A=1", "-e", "B=2", "--project", "dir"}};
    Options options{"tool"};
    options.option("-e", "a NAME=VALUE argument");
    options.option("--project", "a directory");
    options.option("--absent", "a value");
    expect(options.parse(line.argc(), line.argv()) == Cli::ok,
           "an option line parses");
    expect(options.seen("-e") && options.count("-e") == 2,
           "a repeated option is seen and counted like a flag");
    expect(options.count("-e") == options.values("-e").size(),
           "the count of a repeatable option matches its value count");
    expect(options.seen("--project") && options.count("--project") == 1,
           "a single-valued option is seen once");
    expect(options.value("--project") == "dir",
           "the value still reads back");
    expect(!options.seen("--absent") && options.count("--absent") == 0,
           "an option that was not given is not seen");
}

void an_empty_value_is_still_seen() {
    Line line{{"tool", "-c", ""}};
    Options options{"tool"};
    options.option("-c", "a command text");
    expect(options.parse(line.argc(), line.argv()) == Cli::ok,
           "an empty option value parses");
    // values() cannot express this: the vector holds one empty string, so a
    // caller testing emptiness of the value would read the option as absent.
    expect(options.seen("-c") && options.value("-c").empty(),
           "an option given an empty value is seen and reads back empty");
}

void assigned_prefixes_are_seen() {
    Line line{{"tool", "-o=out", "--profile=embedded"}};
    Options options{"tool"};
    options.assigned("-o=");
    options.assigned("--profile=");
    options.assigned("--unused=");
    expect(options.parse(line.argc(), line.argv()) == Cli::ok,
           "an assigned line parses");
    expect(options.seen("-o=") && options.value("-o=") == "out",
           "an assigned prefix is seen and carries its value");
    expect(options.seen("--profile=") &&
               options.value("--profile=") == "embedded",
           "a second assigned prefix is independent");
    expect(!options.seen("--unused="),
           "an assigned prefix that was not given is not seen");
}

void usage_failures_keep_their_shapes() {
    Line missing{{"tool", "--project"}};
    Options needs_value{"tool"};
    needs_value.option("--project", "a directory");
    expect(needs_value.parse(missing.argc(), missing.argv()) == Cli::usage,
           "an option with no value left is a usage error");
    expect(!needs_value.seen("--project"),
           "a rejected option is not recorded as seen");

    Line unknown{{"tool", "--nope"}};
    Options strict{"tool"};
    strict.flag("--check");
    expect(strict.parse(unknown.argc(), unknown.argv()) == Cli::usage,
           "an undeclared dashed argument is unknown");

    Line extra{{"tool", "one", "two"}};
    Options single{"tool"};
    expect(single.parse(extra.argc(), extra.argv()) == Cli::usage,
           "a second positional exceeds the default limit");

    Line helped{{"tool", "-h"}};
    Options documented{"tool"};
    documented.help("tool [options]");
    expect(documented.parse(helped.argc(), helped.argv()) == Cli::help,
           "help stops before any work");
}

void separator_and_positionals() {
    Line line{{"tool", "--run", "file", "--", "-e", "kept"}};
    Options options{"tool"};
    options.flag("--run");
    options.option("-e", "a value");
    options.separator();
    options.positional_limit(4);
    expect(options.parse(line.argc(), line.argv()) == Cli::ok,
           "a separated line parses");
    expect(options.seen("--run") && options.positional().size() == 1 &&
               options.positional()[0] == "file",
           "arguments before the separator are this tool's own");
    expect(!options.seen("-e") && options.trailing().size() == 2 &&
               options.trailing()[0] == "-e" &&
               options.trailing()[1] == "kept",
           "an option after the separator is passed through, not consumed");
}

const mm::test::case_ cases[]{
    {"flags are seen and counted", &flags_are_seen_and_counted},
    {"options are seen and counted", &options_are_seen_and_counted},
    {"an empty value is still seen", &an_empty_value_is_still_seen},
    {"assigned prefixes are seen", &assigned_prefixes_are_seen},
    {"usage failures", &usage_failures_keep_their_shapes},
    {"separator and positionals", &separator_and_positionals},
};

const mm::test::registrar reg{"mm.app options", cases};

}  // namespace
