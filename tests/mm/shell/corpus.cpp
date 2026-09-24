// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
//
// The shell script fixture suite under fixtures/: every y_ file parses and
// evaluates, every n_ file is refused by the parser or evaluator, and every
// i_ file -- where the minimal shell has an implementation-defined outcome
// or level boundary -- has the outcome this module chose, listed here so that
// a change to one is a change someone made on purpose.
#include <algorithm>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <span>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

import mm.shell;
import mm.test;

namespace {

using mm::shell::CommandContext;
using mm::shell::EmbeddedScript;
using mm::shell::Evaluator;
using mm::shell::EvaluatorStorage;
using mm::shell::FunctionLibrary;
using mm::shell::ParseStatus;
using mm::shell::SourceView;
using mm::shell::Status;
using mm::shell::Step;
using mm::test::expect;

const std::filesystem::path fixtures = "tests/mm/shell/fixtures";

[[nodiscard]] std::string read(const std::filesystem::path& file) {
    std::ifstream in(file, std::ios::binary);
    std::ostringstream text;
    text << in.rdbuf();
    return text.str();
}

struct Expected {
    std::string_view name;
    ParseStatus status;
};

// Pinned outcomes for implementation-defined / level-boundary fixtures.
// Under the embedded Level-1 profile, full Level-3 features (pipelines,
// redirections, subshells, here-docs) return Malformed because the embedded
// scanner rejects those tokens. Empty and whitespace-only files parse
// completely.
constexpr Expected i_outcomes[] = {
    {"i_arith_max_int64.sh", ParseStatus::Complete},
    {"i_arith_overflow.sh", ParseStatus::Complete},
    {"i_l3_heredoc.sh", ParseStatus::Malformed},
    {"i_l3_pipeline.sh", ParseStatus::Malformed},
    {"i_l3_redirection.sh", ParseStatus::Malformed},
    {"i_l3_subshell.sh", ParseStatus::Malformed},
    {"i_syntax_empty_file.sh", ParseStatus::Complete},
    {"i_syntax_no_trailing_nl.sh", ParseStatus::Complete},
    {"i_syntax_trailing_comment_no_nl.sh", ParseStatus::Complete},
    {"i_syntax_whitespace_only.sh", ParseStatus::Complete},
};

[[nodiscard]] const Expected* find(const Expected* table, std::size_t count,
                                   std::string_view name) {
    for (std::size_t i = 0; i < count; ++i) {
        if (table[i].name == name) return table + i;
    }
    return nullptr;
}

[[nodiscard]] std::vector<std::filesystem::path> files_with(
    std::string_view prefix) {
    std::vector<std::filesystem::path> result;
    std::error_code ec;
    for (const auto& entry :
         std::filesystem::directory_iterator(fixtures, ec)) {
        const auto name = entry.path().filename().string();
        if (name.starts_with(prefix) && name.ends_with(".sh")) {
            result.push_back(entry.path());
        }
    }
    std::sort(result.begin(), result.end());
    return result;
}

struct ParseScratch {
    std::vector<mm::shell::ScriptToken> tokens;
    std::vector<mm::shell::WordFragment> fragments;
    std::vector<mm::shell::SyntaxNode> nodes;
    std::vector<mm::shell::SyntaxLink> links;
    std::vector<mm::shell::ParserFrame> context;
};

[[nodiscard]] mm::shell::ParseOutcome parse_source(
    std::string_view source, EmbeddedScript& script,
    ParseScratch& scratch) {
    const auto measured = mm::shell::measure_embedded(SourceView{source});
    if (measured.status != ParseStatus::Complete) {
        return {.status = measured.status, .issue = measured.issue};
    }
    scratch.tokens.resize(
        std::max<std::size_t>(measured.required.tokens + 16, 64));
    scratch.fragments.resize(
        std::max<std::size_t>(measured.required.fragments + 16, 64));
    scratch.nodes.resize(
        std::max<std::size_t>(measured.required.nodes + 16, 64));
    scratch.links.resize(
        std::max<std::size_t>(measured.required.links + 16, 64));
    scratch.context.resize(
        std::max<std::size_t>(measured.required.context + 8, 16));
    return mm::shell::parse_embedded(
        SourceView{source},
        {scratch.tokens, scratch.fragments, scratch.nodes, scratch.links,
         scratch.context},
        script);
}

struct DiscardSink {
    [[nodiscard]] mm::shell::ByteSink sink() {
        return {this, &write_cb, &flush_cb, &failure_cb};
    }
    static mm::shell::SinkResult write_cb(void*, std::span<const char>) {
        return mm::shell::SinkResult::Accepted;
    }
    static mm::shell::SinkResult flush_cb(void*) {
        return mm::shell::SinkResult::Accepted;
    }
    static mm::shell::SinkFailure failure_cb(void*) {
        return {.error = Status::Ok};
    }
};

struct ExecScratch {
    std::vector<mm::shell::FieldPiece> pieces =
        std::vector<mm::shell::FieldPiece>(128);
    std::vector<char> generated = std::vector<char>(256);
    std::vector<char> field_text = std::vector<char>(256);
    std::vector<mm::shell::SourceSpan> fields =
        std::vector<mm::shell::SourceSpan>(64);
    std::vector<mm::shell::VariableSlot> shadow_variables =
        std::vector<mm::shell::VariableSlot>(16);
    std::vector<char> shadow_text = std::vector<char>(256);
    std::vector<std::string_view> arguments =
        std::vector<std::string_view>(64);
    std::vector<char> argument_text = std::vector<char>(512);
    std::vector<mm::shell::EvaluatorFrame> frames =
        std::vector<mm::shell::EvaluatorFrame>(32);
    std::vector<std::string_view> loop_items =
        std::vector<std::string_view>(32);
    std::vector<char> loop_text = std::vector<char>(256);
    std::vector<mm::shell::PatternByte> pattern =
        std::vector<mm::shell::PatternByte>(128);
    std::vector<mm::shell::VariableSlot> prefix_variables =
        std::vector<mm::shell::VariableSlot>(16);
    std::vector<char> prefix_variable_text = std::vector<char>(256);
    std::vector<char> staged_output = std::vector<char>(512);
    std::vector<char> staged_error = std::vector<char>(512);
    std::vector<mm::shell::PositionalSlot> call_positionals =
        std::vector<mm::shell::PositionalSlot>(64);
    std::vector<mm::shell::VariableSlot> variables =
        std::vector<mm::shell::VariableSlot>(64);
    std::vector<char> variable_text = std::vector<char>(2048);
    std::vector<mm::shell::PositionalSlot> positionals =
        std::vector<mm::shell::PositionalSlot>(64);
    std::vector<char> positional_text = std::vector<char>(1024);
    std::vector<mm::shell::CommandDescriptor> commands =
        std::vector<mm::shell::CommandDescriptor>(32);
    std::vector<std::byte> scratch = std::vector<std::byte>(512);

    std::vector<char> capture_text = std::vector<char>(256);
    std::vector<std::string_view> capture_values =
        std::vector<std::string_view>(16);
    std::vector<mm::shell::VariableSlot> capture_variables =
        std::vector<mm::shell::VariableSlot>(16);
    std::vector<char> capture_variable_text = std::vector<char>(256);
    std::vector<mm::shell::ScriptToken> capture_tokens =
        std::vector<mm::shell::ScriptToken>(64);
    std::vector<mm::shell::WordFragment> capture_fragments =
        std::vector<mm::shell::WordFragment>(64);
    std::vector<mm::shell::SyntaxNode> capture_nodes =
        std::vector<mm::shell::SyntaxNode>(64);
    std::vector<mm::shell::SyntaxLink> capture_links =
        std::vector<mm::shell::SyntaxLink>(64);
    std::vector<mm::shell::ParserFrame> capture_parser_context =
        std::vector<mm::shell::ParserFrame>(16);

    std::vector<mm::shell::FunctionSlot> functions =
        std::vector<mm::shell::FunctionSlot>(16);
    std::vector<char> function_text = std::vector<char>(512);
    std::vector<mm::shell::ScriptToken> function_tokens =
        std::vector<mm::shell::ScriptToken>(128);
    std::vector<mm::shell::WordFragment> function_fragments =
        std::vector<mm::shell::WordFragment>(128);
    std::vector<mm::shell::SyntaxNode> function_nodes =
        std::vector<mm::shell::SyntaxNode>(128);
    std::vector<mm::shell::SyntaxLink> function_links =
        std::vector<mm::shell::SyntaxLink>(128);
    std::vector<mm::shell::ParserFrame> function_context =
        std::vector<mm::shell::ParserFrame>(32);
};

[[nodiscard]] int run_script(const EmbeddedScript& script, ExecScratch& s) {
    DiscardSink discard;
    mm::shell::IoServices io{discard.sink(), discard.sink()};
    mm::shell::ShellState state{s.variables, s.variable_text,
                                s.positionals, s.positional_text};
    const auto capabilities = mm::shell::CapabilitySet::level1();
    CommandContext context{io, state, capabilities, s.scratch};
    mm::shell::Registry registry{s.commands};
    mm::shell::ScriptLibrary scripts{{{}, {}, {}, {}, {}, {}}};
    mm::shell::Introspection binding{&registry, &scripts};
    if (!mm::shell::install_level1(registry, binding).ok()) return -1;

    FunctionLibrary func_lib{{s.functions, s.function_text,
                              s.function_tokens, s.function_fragments,
                              s.function_nodes, s.function_links,
                              s.function_context}};

    EvaluatorStorage storage{
        .expansion = {s.pieces, s.generated,
                      {s.field_text, s.fields},
                      s.shadow_variables, s.shadow_text},
        .arguments = s.arguments,
        .argument_text = s.argument_text,
        .frames = s.frames,
        .loop_items = s.loop_items,
        .loop_text = s.loop_text,
        .pattern = s.pattern,
        .prefix_variables = s.prefix_variables,
        .prefix_variable_text = s.prefix_variable_text,
        .staged_output = s.staged_output,
        .staged_error = s.staged_error,
        .functions = &func_lib,
        .scripts = &scripts,
        .call_positionals = s.call_positionals,
        .call_limit = 16,
        .capture_text = s.capture_text,
        .capture_values = s.capture_values,
        .capture_variables = s.capture_variables,
        .capture_variable_text = s.capture_variable_text,
        .capture_tokens = s.capture_tokens,
        .capture_fragments = s.capture_fragments,
        .capture_nodes = s.capture_nodes,
        .capture_links = s.capture_links,
        .capture_parser_context = s.capture_parser_context,
    };

    Evaluator evaluator;
    if (evaluator.begin(script, registry, context, storage) != Status::Ok) {
        return -2;
    }
    mm::shell::StepResult result;
    for (std::size_t step = 0; step < 100000; ++step) {
        result = evaluator.step();
        if (result.step == Step::Running || result.step == Step::Yielded) {
            continue;
        }
        break;
    }
    return result.command.status;
}

void accepted_scripts() {
    const auto files = files_with("y_");
    expect(files.size() == 39,
           "the suite's thirty-nine y_ files are present");
    for (const auto& file : files) {
        const auto name = file.filename().string();
        const auto text = read(file);
        ParseScratch parse_scratch;
        EmbeddedScript script;
        const auto outcome = parse_source(text, script, parse_scratch);
        expect(outcome.status == ParseStatus::Complete,
               name + " parses completely");
        if (outcome.status != ParseStatus::Complete) continue;

        ExecScratch exec_scratch;
        const auto exit_status = run_script(script, exec_scratch);
        expect(exit_status == 0, name + " evaluates to status 0");
    }
}

void refused_scripts() {
    const auto files = files_with("n_");
    expect(files.size() == 26,
           "the suite's twenty-six n_ files are present");
    for (const auto& file : files) {
        const auto name = file.filename().string();
        const auto text = read(file);
        ParseScratch parse_scratch;
        EmbeddedScript script;
        const auto outcome = parse_source(text, script, parse_scratch);
        if (outcome.status != ParseStatus::Complete) {
            expect(true, name + " is refused by the parser");
            continue;
        }
        ExecScratch exec_scratch;
        const auto exit_status = run_script(script, exec_scratch);
        expect(exit_status != 0, name + " is refused by the evaluator");
    }
}

void implementation_defined_scripts() {
    const auto files = files_with("i_");
    expect(files.size() == sizeof(i_outcomes) / sizeof(i_outcomes[0]),
           "every i_ file has a listed outcome");
    for (const auto& file : files) {
        const auto name = file.filename().string();
        const auto* expected = find(i_outcomes, std::size(i_outcomes), name);
        expect(expected != nullptr, name + " is in the outcome table");
        if (expected == nullptr) continue;
        const auto text = read(file);
        ParseScratch parse_scratch;
        EmbeddedScript script;
        const auto outcome = parse_source(text, script, parse_scratch);
        expect(outcome.status == expected->status,
               name + " matches its pinned status");
    }
}

const mm::test::case_ cases[] = {
    {"accepted scripts", &accepted_scripts},
    {"refused scripts", &refused_scripts},
    {"implementation-defined scripts", &implementation_defined_scripts},
};

const mm::test::registrar reg{"mm.shell corpus", cases};

}  // namespace
