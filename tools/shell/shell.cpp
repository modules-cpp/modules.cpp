// modules.cpp shell tool
//
// Usage: shell [-v] [-h] [--project MANIFEST] [-e NAME=VALUE]... MODE
//
//   --check FILE...          parse each file and report the first diagnostic
//   --tokens FILE            dump the token stream
//   --dump-ast FILE          dump the syntax tree
//   --run FILE [-- ARG...]   execute FILE natively
//   -c TEXT [-- ARG...]      execute TEXT natively
//   --profile=embedded       run under the bounded embedded evaluator
//   --capabilities           list the capabilities of the active profile
//   --commands               list the installed commands
//   --legacy-sh              deprecated: delegate --run or -c to /bin/sh
//   [MANIFEST] COMMAND       deprecated alias for -c, using the native parser
//
// Scripts run through mm.shell.full over the mm.shell.posix host services.
// Nothing here calls system() and nothing reaches /bin/sh unless --legacy-sh
// is given explicitly: it is never selected after a native parse or execution
// failure.
//
// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
#include <cstdio>
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <span>
#include <string>
#include <string_view>
#include <vector>

import mm.app;
import mm.build;
import mm.shell;
import mm.shell.full;
import mm.shell.posix;

namespace {

namespace full = mm::shell::full;

constexpr std::string_view usage_text =
    "shell [-v] [-h] [--project MANIFEST] [-e NAME=VALUE]... "
    "(--check FILE... | --tokens FILE | --dump-ast FILE | "
    "--run FILE [-- ARG...] | -c TEXT [-- ARG...] | "
    "--capabilities | --commands) [--profile=embedded] [--legacy-sh]";

struct Assignment {
    std::string_view name;
    std::string_view value;
};

[[nodiscard]] Assignment split_assignment(std::string_view text) {
    const auto at = text.find('=');
    if (at == std::string_view::npos) return {};
    return {text.substr(0, at), text.substr(at + 1)};
}

[[nodiscard]] bool read_file(const std::string& path, std::string& out) {
    std::ifstream file{path, std::ios::binary};
    if (!file.good()) return false;
    out.assign(std::istreambuf_iterator<char>{file},
               std::istreambuf_iterator<char>{});
    return true;
}

[[nodiscard]] std::string_view describe(full::ParseStatus status) {
    switch (status) {
        case full::ParseStatus::Complete: return "complete";
        case full::ParseStatus::Incomplete: return "incomplete";
        case full::ParseStatus::Malformed: return "malformed";
        case full::ParseStatus::Unsupported: return "unsupported";
    }
    return "unknown";
}

[[nodiscard]] std::string_view describe(full::TokenKind kind) {
    switch (kind) {
        case full::TokenKind::Word: return "word";
        case full::TokenKind::IoNumber: return "io-number";
        case full::TokenKind::Newline: return "newline";
        case full::TokenKind::Semicolon: return "semicolon";
        case full::TokenKind::DoubleSemicolon: return "double-semicolon";
        case full::TokenKind::AndIf: return "and-if";
        case full::TokenKind::OrIf: return "or-if";
        case full::TokenKind::Pipe: return "pipe";
        case full::TokenKind::OpenParen: return "open-paren";
        case full::TokenKind::CloseParen: return "close-paren";
        case full::TokenKind::OpenBrace: return "open-brace";
        case full::TokenKind::CloseBrace: return "close-brace";
        case full::TokenKind::Input: return "input";
        case full::TokenKind::Output: return "output";
        case full::TokenKind::Append: return "append";
        case full::TokenKind::HereDocument: return "here-document";
        case full::TokenKind::DuplicateInput: return "duplicate-input";
        case full::TokenKind::DuplicateOutput: return "duplicate-output";
        case full::TokenKind::Bang: return "bang";
        case full::TokenKind::End: return "end";
    }
    return "unknown";
}

[[nodiscard]] std::string_view describe(full::NodeKind kind) {
    switch (kind) {
        case full::NodeKind::Program: return "program";
        case full::NodeKind::List: return "list";
        case full::NodeKind::AndOr: return "and-or";
        case full::NodeKind::Pipeline: return "pipeline";
        case full::NodeKind::Simple: return "simple";
        case full::NodeKind::If: return "if";
        case full::NodeKind::While: return "while";
        case full::NodeKind::For: return "for";
        case full::NodeKind::Case: return "case";
        case full::NodeKind::CaseItem: return "case-item";
        case full::NodeKind::Function: return "function";
        case full::NodeKind::BraceGroup: return "brace-group";
        case full::NodeKind::Subshell: return "subshell";
        case full::NodeKind::Negation: return "negation";
        case full::NodeKind::Redirection: return "redirection";
    }
    return "unknown";
}

void report(std::string_view path, const full::Diagnostic& diagnostic) {
    std::cerr << "shell: " << path << ": " << diagnostic.offset << ": "
              << describe(diagnostic.status) << ": " << diagnostic.message
              << "\n";
}

void dump_node(const full::FullScript& script, std::size_t node,
               unsigned int indent) {
    for (unsigned int i = 0; i < indent; ++i) std::cout << "  ";
    const auto& entry = script.nodes[node];
    std::cout << describe(entry.kind) << " tokens " << entry.first_token
              << ".." << entry.last_token << "\n";
    for (const auto child : entry.children) {
        dump_node(script, child.node, indent + 1);
    }
}

// The shell's own streams, borrowed rather than opened, so the tool never
// closes the descriptors it was started with.
struct HostStreams {
    mm::shell::posix::HostServices& host;
    full::Streams streams;

    explicit HostStreams(mm::shell::posix::HostServices& services)
        : host(services) {
        streams.input = host.borrow_descriptor(0);
        streams.output = host.borrow_descriptor(1);
        streams.error = host.borrow_descriptor(2);
    }
};

[[nodiscard]] int run_native(std::string_view source,
                             std::string_view name,
                             std::span<const std::string> arguments,
                             std::span<const std::string> exports,
                             const std::string& directory,
                             const std::filesystem::path& project_root,
                             bool native_policy, bool verbose) {
    mm::shell::posix::HostServices host;
    if (native_policy &&
        !host.set_native_project_root(project_root.string())) {
        std::cerr << "shell: cannot resolve native project root\n";
        return mm::build::exit_manifest;
    }
    const HostStreams streams{host};
    const auto environment = mm::shell::snapshot_environment();
    std::vector<std::string_view> environment_views;
    environment_views.reserve(environment.size());
    std::size_t environment_bytes = 0;
    for (const auto& entry : environment) {
        environment_views.push_back(entry);
        environment_bytes += entry.size() + 1;
    }
    full::StateCapacity capacity;
    capacity.variable_slots = std::max(capacity.variable_slots,
                                       environment.size() + 64);
    capacity.variable_bytes = std::max(capacity.variable_bytes,
                                       environment_bytes + 8192);
    full::FullState state{capacity};
    if (state.seed_environment(environment_views) !=
        mm::shell::Status::Ok) {
        std::cerr << "shell: cannot import host environment\n";
        return mm::build::exit_run;
    }
    state.set_directory(directory);
    for (const auto& entry : exports) {
        const auto assignment = split_assignment(entry);
        if (!state.core().assign(assignment.name, assignment.value).ok()) {
            std::cerr << "shell: cannot set " << assignment.name << "\n";
            return mm::build::exit_run;
        }
        state.export_name(assignment.name);
    }

    full::Interpreter interpreter{state, host.all()};
    interpreter.set_streams(streams.streams);
    std::vector<std::string_view> views;
    views.reserve(arguments.size());
    for (const auto& argument : arguments) views.push_back(argument);
    if (interpreter.set_arguments(name, views) != mm::shell::Status::Ok) {
        std::cerr << "shell: too many arguments\n";
        return mm::build::exit_usage;
    }

    const auto outcome = interpreter.run_text(source);
    if (outcome.diagnostic.status != full::ParseStatus::Complete) {
        report(name, outcome.diagnostic);
        return mm::build::exit_usage;
    }
    if (outcome.service != full::ServiceStatus::Ok) {
        std::cerr << "shell: " << name << ": host service failure\n";
        return mm::build::exit_run;
    }
    if (verbose) {
        std::cerr << "shell: " << interpreter.spawn_count()
                  << " external command(s)\n";
    }
    return outcome.status;
}

// A byte sink over a stdio stream, so the embedded profile writes where the
// tool's own streams point.
struct StdioSink {
    std::FILE* stream = nullptr;

    [[nodiscard]] mm::shell::ByteSink sink() {
        return {this, &StdioSink::write_callback,
                &StdioSink::flush_callback, &StdioSink::failure_callback};
    }

    static mm::shell::SinkResult write_callback(
        void* context, std::span<const char> bytes) {
        auto* self = static_cast<StdioSink*>(context);
        if (self == nullptr || self->stream == nullptr) {
            return mm::shell::SinkResult::Failed;
        }
        const auto moved = std::fwrite(bytes.data(), 1, bytes.size(),
                                       self->stream);
        return moved == bytes.size() ? mm::shell::SinkResult::Accepted
                                     : mm::shell::SinkResult::Failed;
    }

    static mm::shell::SinkResult flush_callback(void* context) {
        auto* self = static_cast<StdioSink*>(context);
        if (self == nullptr || self->stream == nullptr) {
            return mm::shell::SinkResult::Failed;
        }
        return std::fflush(self->stream) == 0
                   ? mm::shell::SinkResult::Accepted
                   : mm::shell::SinkResult::Failed;
    }

    static mm::shell::SinkFailure failure_callback(void*) {
        return {.error = mm::shell::Status::WriteError};
    }
};

// Storage for the embedded profile. The tool owns it on the heap because a
// host program may, while the shape stays the caller-declared one mm.shell
// requires.
struct EmbeddedStorage {
    std::vector<mm::shell::ScriptToken> tokens{256};
    std::vector<mm::shell::WordFragment> fragments{256};
    std::vector<mm::shell::SyntaxNode> nodes{256};
    std::vector<mm::shell::SyntaxLink> links{256};
    std::vector<mm::shell::ParserFrame> context{64};
    std::vector<mm::shell::VariableSlot> variables{64};
    std::vector<char> variable_text = std::vector<char>(4096);
    std::vector<mm::shell::PositionalSlot> positionals{64};
    std::vector<char> positional_text = std::vector<char>(4096);
    std::vector<mm::shell::FieldPiece> pieces{128};
    std::vector<char> generated = std::vector<char>(1024);
    std::vector<char> field_text = std::vector<char>(1024);
    std::vector<mm::shell::SourceSpan> fields{64};
    std::vector<mm::shell::VariableSlot> shadow_variables{64};
    std::vector<char> shadow_text = std::vector<char>(1024);
    std::vector<std::string_view> arguments{64};
    std::vector<char> argument_text = std::vector<char>(2048);
    std::vector<mm::shell::EvaluatorFrame> frames{64};
    std::vector<std::string_view> loop_items{64};
    std::vector<char> loop_text = std::vector<char>(1024);
    std::vector<mm::shell::PatternByte> pattern{256};
    std::vector<mm::shell::VariableSlot> prefix_variables{64};
    std::vector<char> prefix_variable_text = std::vector<char>(1024);
    std::vector<mm::shell::PositionalSlot> call_positionals{128};
    std::vector<char> staged_output = std::vector<char>(1024);
    std::vector<char> staged_error = std::vector<char>(1024);
    std::vector<mm::shell::CommandDescriptor> commands{64};
};

[[nodiscard]] int run_embedded(std::string_view source,
                               std::span<const std::string> arguments) {
    EmbeddedStorage storage;
    mm::shell::EmbeddedScript script;
    const auto parsed = mm::shell::parse_embedded(
        mm::shell::SourceView{source},
        {storage.tokens, storage.fragments, storage.nodes, storage.links,
         storage.context},
        script);
    if (parsed.status != mm::shell::ParseStatus::Complete) {
        std::cerr << "shell: embedded profile: parse failed at "
                  << parsed.issue.offset << "\n";
        return mm::build::exit_usage;
    }

    StdioSink out{stdout};
    StdioSink err{stderr};
    mm::shell::IoServices io{out.sink(), err.sink()};
    mm::shell::ShellState state{storage.variables, storage.variable_text,
                                storage.positionals,
                                storage.positional_text};
    std::vector<std::string_view> views;
    views.reserve(arguments.size());
    for (const auto& argument : arguments) views.push_back(argument);
    if (!state.set_positionals("shell", views).ok()) {
        std::cerr << "shell: embedded profile: too many arguments\n";
        return mm::build::exit_usage;
    }
    const auto capabilities = mm::shell::CapabilitySet::level1();
    std::vector<std::byte> scratch(256);
    mm::shell::CommandContext context{io, state, capabilities, scratch};
    mm::shell::Registry registry{storage.commands};
    mm::shell::ScriptLibrary scripts{{{}, {}, {}, {}, {}, {}}};
    mm::shell::Introspection binding{&registry, &scripts};
    if (!mm::shell::install_level1(registry, binding).ok()) {
        std::cerr << "shell: embedded profile: cannot install commands\n";
        return mm::build::exit_run;
    }

    mm::shell::EvaluatorStorage evaluator_storage{
        .expansion = {storage.pieces, storage.generated,
                      {storage.field_text, storage.fields},
                      storage.shadow_variables, storage.shadow_text},
        .arguments = storage.arguments,
        .argument_text = storage.argument_text,
        .frames = storage.frames,
        .loop_items = storage.loop_items,
        .loop_text = storage.loop_text,
        .pattern = storage.pattern,
        .prefix_variables = storage.prefix_variables,
        .prefix_variable_text = storage.prefix_variable_text,
        .staged_output = storage.staged_output,
        .staged_error = storage.staged_error,
    };
    evaluator_storage.call_positionals = storage.call_positionals;

    mm::shell::Evaluator evaluator;
    if (evaluator.begin(script, registry, context, evaluator_storage) !=
        mm::shell::Status::Ok) {
        std::cerr << "shell: embedded profile: cannot start\n";
        return mm::build::exit_run;
    }
    mm::shell::StepResult result;
    for (std::size_t step = 0; step < 1000000; ++step) {
        result = evaluator.step();
        if (result.step == mm::shell::Step::Running ||
            result.step == mm::shell::Step::Yielded) {
            continue;
        }
        break;
    }
    (void)out.sink().flush();
    (void)err.sink().flush();
    if (result.step == mm::shell::Step::Failed) {
        std::cerr << "shell: embedded profile: "
                  << mm::shell::describe(result.command.error) << "\n";
        return result.command.status == 0 ? 1 : result.command.status;
    }
    return result.command.status;
}

// The explicit one-release rollback. It reaches /bin/sh through the process
// service, never through system(), and is never selected automatically.
[[nodiscard]] int run_legacy(bool run_file, const std::string& operand,
                             std::span<const std::string> arguments) {
    std::cerr << "shell: --legacy-sh is deprecated and will be removed "
                 "after one migration release\n";
    mm::shell::posix::HostServices host;
    const auto process = host.process();
    std::vector<std::string> words;
    words.emplace_back("/bin/sh");
    if (run_file) {
        words.push_back(operand);
    } else {
        words.emplace_back("-c");
        words.push_back(operand);
        words.emplace_back("sh");
    }
    for (const auto& argument : arguments) words.push_back(argument);

    std::vector<std::string_view> views;
    views.reserve(words.size());
    for (const auto& word : words) views.push_back(word);
    full::ProcessRequest request;
    request.arguments = views;
    request.input = host.borrow_descriptor(0);
    request.output = host.borrow_descriptor(1);
    request.error = host.borrow_descriptor(2);

    full::Handle child = full::invalid_handle;
    const auto spawned = process.spawn(process.context, request, child);
    if (spawned != full::ServiceStatus::Ok) {
        std::cerr << "shell: --legacy-sh: cannot run /bin/sh\n";
        return mm::build::exit_run;
    }
    int status = 0;
    if (process.wait(process.context, child, status) !=
        full::ServiceStatus::Ok) {
        std::cerr << "shell: --legacy-sh: /bin/sh did not complete\n";
        return mm::build::exit_run;
    }
    return status;
}

void print_capabilities(bool embedded) {
    const auto set = embedded ? mm::shell::CapabilitySet::level1()
                              : mm::shell::CapabilitySet::level3();
    for (unsigned int i = 0; i < mm::shell::capability_count; ++i) {
        const auto capability = static_cast<mm::shell::Capability>(i);
        if (!set.has(capability)) continue;
        std::cout << mm::shell::name_of(capability) << "\n";
    }
}

void print_commands() {
    std::vector<mm::shell::CommandDescriptor> slots(64);
    mm::shell::Registry registry{slots};
    mm::shell::ScriptLibrary scripts{{{}, {}, {}, {}, {}, {}}};
    mm::shell::Introspection binding{&registry, &scripts};
    if (!mm::shell::install_level1(registry, binding).ok()) {
        std::cerr << "shell: cannot install commands\n";
        return;
    }
    for (const auto& descriptor : registry.descriptors()) {
        std::cout << descriptor.name << " " << descriptor.summary << "\n";
    }
}

}  // namespace

int main(int argc, char** argv) {
    mm::app::Options options("shell");
    options.flag("--check");
    options.flag("--tokens");
    options.flag("--dump-ast");
    options.flag("--run");
    options.flag("--capabilities");
    options.flag("--commands");
    options.flag("--legacy-sh");
    options.option("--project", "a manifest path");
    options.option("-c", "a command text");
    options.option("-e", "a NAME=VALUE argument");
    options.assigned("--profile=");
    options.separator();
    options.positional_limit(64);
    options.help(std::string(usage_text));

    const auto cli = options.parse(argc, argv);
    if (cli == mm::app::Cli::help) return mm::build::exit_ok;
    if (cli != mm::app::Cli::ok) return mm::build::exit_usage;

    const auto verbose = options.verbose();
    const auto exports = options.values("-e");
    for (const auto& entry : exports) {
        if (!split_assignment(entry).name.empty()) continue;
        std::cerr << "shell: malformed -e argument: " << entry << "\n";
        return mm::build::exit_usage;
    }

    const auto profile = options.value("--profile=");
    if (!profile.empty() && profile != "embedded") {
        std::cerr << "shell: unknown profile: " << profile << "\n";
        return mm::build::exit_usage;
    }
    const auto embedded = profile == "embedded";
    const auto legacy = options.seen("--legacy-sh");
    if (legacy && embedded) {
        std::cerr << "shell: --legacy-sh is host-only and cannot be "
                     "combined with --profile=embedded\n";
        return mm::build::exit_usage;
    }

    const auto& positional = options.positional();
    const auto command_text = options.value("-c");
    const auto has_command = options.seen("-c");
    const auto has_project = options.seen("--project");
    auto modes = 0;
    for (const auto flag : {"--check", "--tokens", "--dump-ast", "--run",
                            "--capabilities", "--commands"}) {
        if (options.seen(flag)) ++modes;
    }
    if (has_command) ++modes;
    if (modes > 1) {
        std::cerr << "shell: choose one mode\n";
        return mm::build::exit_usage;
    }

    // The project manifest is resolved before any script runs, so native
    // policy has a root. -c carries no implicit project provenance, and the
    // deprecated positional alias keeps the old manifest-first shape.
    std::filesystem::path manifest = has_project
                                         ? options.value("--project")
                                         : std::string{"mm.mdy"};
    auto alias_command = std::string{};
    if (modes == 0) {
        if (positional.empty() || positional.size() > 2) {
            std::cerr << "Usage: " << usage_text << "\n";
            return mm::build::exit_usage;
        }
        if (positional.size() == 2) manifest = positional[0];
        alias_command = positional.back();
        std::cerr << "shell: the [manifest] command form is deprecated; "
                     "use -c\n";
    }

    manifest = mm::build::resolve_manifest(manifest);
    std::filesystem::path root;
    if (const auto status = mm::app::open_manifest("shell", manifest, root,
                                                   true);
        status != mm::app::Cli::ok) {
        return status == mm::app::Cli::usage ? mm::build::exit_usage
                                             : mm::build::exit_manifest;
    }
    if (verbose) {
        std::cerr << "shell: root " << root.string() << "\n";
    }

    if (options.seen("--capabilities")) {
        print_capabilities(embedded);
        return mm::build::exit_ok;
    }
    if (options.seen("--commands")) {
        print_commands();
        return mm::build::exit_ok;
    }

    if (options.seen("--check")) {
        if (positional.empty()) {
            std::cerr << "shell: --check needs at least one file\n";
            return mm::build::exit_usage;
        }
        auto failures = 0;
        for (const auto& path : positional) {
            std::string source;
            if (!read_file(path, source)) {
                std::cerr << "shell: cannot read " << path << "\n";
                ++failures;
                continue;
            }
            const auto parsed = full::parse_full(source);
            if (parsed.ok()) {
                if (verbose) std::cout << path << ": complete\n";
                continue;
            }
            report(path, parsed.diagnostic);
            ++failures;
        }
        return failures == 0 ? mm::build::exit_ok : 1;
    }

    if (options.seen("--tokens") || options.seen("--dump-ast")) {
        if (positional.size() != 1) {
            std::cerr << "shell: that mode needs exactly one file\n";
            return mm::build::exit_usage;
        }
        std::string source;
        if (!read_file(positional[0], source)) {
            std::cerr << "shell: cannot read " << positional[0] << "\n";
            return mm::build::exit_usage;
        }
        const auto parsed = full::parse_full(source);
        if (options.seen("--tokens")) {
            for (std::size_t i = 0; i < parsed.script.tokens.size(); ++i) {
                const auto& token = parsed.script.tokens[i];
                std::cout << i << " " << describe(token.kind) << " "
                          << token.source.offset << "+"
                          << token.source.length;
                if (token.kind == full::TokenKind::Word) {
                    std::cout << " ["
                              << parsed.script.text(token.source) << "]";
                }
                std::cout << "\n";
            }
        } else if (parsed.script.root < parsed.script.nodes.size()) {
            dump_node(parsed.script, parsed.script.root, 0);
        }
        if (!parsed.ok()) {
            report(positional[0], parsed.diagnostic);
            return 1;
        }
        return mm::build::exit_ok;
    }

    std::vector<std::string> arguments = options.trailing();
    if (options.seen("--run")) {
        if (positional.size() != 1) {
            std::cerr << "shell: --run needs exactly one file\n";
            return mm::build::exit_usage;
        }
        const auto& path = positional[0];
        if (legacy) return run_legacy(true, path, arguments);
        std::string source;
        if (!read_file(path, source)) {
            std::cerr << "shell: cannot read " << path << "\n";
            return mm::build::exit_usage;
        }
        if (embedded) return run_embedded(source, arguments);
        return run_native(source, path, arguments, exports,
                          std::filesystem::current_path().string(),
                          root, true, verbose);
    }

    const auto text = has_command ? command_text : alias_command;
    if (legacy) return run_legacy(false, text, arguments);
    if (embedded) return run_embedded(text, arguments);
    return run_native(text, "sh", arguments, exports,
                      std::filesystem::current_path().string(),
                      root, has_project, verbose);
}
