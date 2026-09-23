// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <string_view>
#include <utility>

import mm.shell.full;
import mm.shell;
import mm.test;

namespace {

using mm::test::expect;
namespace full = mm::shell::full;

void owns_tokens_and_operators() {
    auto script = full::parse_full(
        "echo one | cat < input > output >> log\n(foo) && bar\n");
    expect(script.ok() && script.script.tokens.size() > 12 &&
               script.script.tokens[2].kind == full::TokenKind::Pipe &&
               script.script.tokens[4].kind == full::TokenKind::Input &&
               script.script.tokens[6].kind == full::TokenKind::Output &&
               script.script.tokens[8].kind == full::TokenKind::Append,
           "pipeline and redirection operators are distinct tokens");
    auto moved = std::move(script.script);
    expect(moved.text(moved.tokens[0].source) == "echo",
           "moving a full script preserves source-relative ranges");
    const auto numbered = full::parse_full(
        "echo 2 2>error 1>&2 <input\n");
    expect(numbered.ok() &&
               numbered.script.tokens[1].kind ==
                   full::TokenKind::Word &&
               numbered.script.tokens[2].kind ==
                   full::TokenKind::IoNumber &&
               numbered.script.tokens[5].kind ==
                   full::TokenKind::IoNumber,
           "only adjacent numeric redirection prefixes are IO numbers");
}

void here_documents() {
    constexpr std::string_view source =
        "cat <<PLAN\nhello $name\nPLAN\n"
        "cat <<'RAW'\n$name\nRAW\n";
    const auto parsed = full::parse_full(source);
    expect(parsed.ok() && parsed.script.documents.size() == 2 &&
               parsed.script.documents[0].delimiter == "PLAN" &&
               parsed.script.documents[0].expand &&
               parsed.script.text(parsed.script.documents[0].body) ==
                   "hello $name\n" &&
               parsed.script.documents[1].delimiter == "RAW" &&
               !parsed.script.documents[1].expand &&
               parsed.script.text(parsed.script.documents[1].body) ==
                   "$name\n",
           "here-doc bodies and delimiter quote policy are retained");
}

void redirection_planning() {
    const auto parsed = full::parse_full(
        "cat 2>>error 1>&2 <<'RAW'\n$name\nRAW\n");
    expect(parsed.ok(), "redirection planning fixture parses");
    std::size_t command = parsed.script.nodes.size();
    for (std::size_t i = 0; i < parsed.script.nodes.size(); ++i) {
        if (parsed.script.nodes[i].kind == full::NodeKind::Simple) {
            command = i;
            break;
        }
    }
    const auto planned = full::plan_redirections(
        parsed.script, command);
    expect(planned.ok() && planned.steps.size() == 3 &&
               planned.steps[0].kind ==
                   full::RedirectionKind::Append &&
               planned.steps[0].target == 2 &&
               planned.steps[0].operand == "error" &&
               planned.steps[1].kind ==
                   full::RedirectionKind::DuplicateOutput &&
               planned.steps[1].target == 1 &&
               planned.steps[1].operand == "2" &&
               planned.steps[2].kind ==
                   full::RedirectionKind::HereDocument &&
               planned.steps[2].body == "$name\n" &&
               !planned.steps[2].expand_body,
           "planning preserves order and quoted here-doc policy");
}

void refusals() {
    expect(full::parse_full("echo `date`\n").diagnostic.status ==
               full::ParseStatus::Unsupported &&
               full::parse_full("echo hi &\n").diagnostic.status ==
                   full::ParseStatus::Unsupported &&
               full::parse_full("cat <<-EOF\n").diagnostic.status ==
                   full::ParseStatus::Unsupported &&
               full::parse_full("echo 'open").diagnostic.status ==
                   full::ParseStatus::Incomplete &&
               full::parse_full("echo hi |\n").diagnostic.status ==
                   full::ParseStatus::Incomplete &&
               full::parse_full("cat >\n").diagnostic.status ==
                   full::ParseStatus::Malformed,
           "excluded and incomplete syntax has typed diagnostics");
    constexpr std::string_view excluded[]{
        "[[ x ]]\n", "(( x ))\n", "cat <(echo x)\n",
        "eval echo x\n", ". ./other.sh\n", "alias x=y\n",
        "jobs\n", "source other.sh\n", "function f { :; }\n",
        "x &\n", "cat <<< x\n", "cat <<-EOF\nEOF\n",
    };
    for (const auto source : excluded) {
        expect(full::parse_full(source).diagnostic.status ==
                   full::ParseStatus::Unsupported,
               "an excluded construct is refused, not reinterpreted");
    }
    constexpr std::string_view malformed[]{
        "if then echo yes; fi\n",
        "if true; then fi\n",
        "while do echo no; done\n",
        "for x in a; do done\n",
        "( )\n",
    };
    for (const auto source : malformed) {
        expect(full::parse_full(source).diagnostic.status ==
                   full::ParseStatus::Malformed,
               "empty compound bodies are syntax errors");
    }
}

void embedded_parser_agreement() {
    constexpr std::string_view fixtures[]{
        "echo one; echo two\n",
        "if true; then echo yes; else echo no; fi\n",
        "while false; do echo never; done\n",
    };
    for (const auto source : fixtures) {
        const auto full_script = full::parse_full(source);
        expect(full_script.ok(), "full grammar accepts embedded fixture");
        const auto needed = mm::shell::measure_embedded(
            mm::shell::SourceView{source});
        expect(needed.status == mm::shell::ParseStatus::Complete,
               "embedded grammar accepts the same fixture");
        mm::shell::ScriptToken tokens[64]{};
        mm::shell::WordFragment fragments[64]{};
        mm::shell::SyntaxNode nodes[64]{};
        mm::shell::SyntaxLink links[64]{};
        mm::shell::ParserFrame frames[16]{};
        mm::shell::EmbeddedScript embedded;
        const auto parsed = mm::shell::parse_embedded(
            mm::shell::SourceView{source},
            {tokens, fragments, nodes, links, frames}, embedded);
        expect(parsed.status == mm::shell::ParseStatus::Complete,
               "embedded fixture materializes");
        std::size_t full_simple = 0;
        for (const auto& node : full_script.script.nodes) {
            if (node.kind == full::NodeKind::Simple) ++full_simple;
        }
        std::size_t embedded_simple = 0;
        for (const auto& node : embedded.nodes) {
            if (node.kind == mm::shell::SyntaxKind::Simple) {
                ++embedded_simple;
            }
        }
        expect(full_simple == embedded_simple,
               "both grammars identify the same simple commands");
    }
}

void owning_child_state() {
    full::FullState parent;
    const std::string_view environment[]{"PATH=/bin", "EMPTY="};
    expect(parent.seed_environment(environment) ==
               mm::shell::Status::Ok &&
               parent.core().lookup("PATH").value == "/bin" &&
               parent.is_exported("EMPTY"),
           "initial environment is copied into owning state");
    expect(parent.core().assign("MODE", "parent").ok(),
           "parent value installs");
    const std::string_view args[]{"first", "second"};
    expect(parent.core().set_positionals("script", args).ok(),
           "parent arguments install");
    parent.set_directory("/project");
    parent.export_name("MODE");
    parent.set_trap(0, "echo parent");

    full::FullState child;
    expect(parent.fork_into(child) == mm::shell::Status::Ok &&
               child.core().lookup("MODE").value == "parent" &&
               child.core().positional(2).value == "second" &&
               child.directory() == "/project" &&
               child.is_exported("MODE") &&
               child.trap(0) == "echo parent",
           "child owns a complete initial host state");
    expect(child.core().assign("MODE", "child").ok(),
           "child value changes");
    const std::string_view new_args[]{"other"};
    expect(child.core().set_positionals("child", new_args).ok(),
           "child arguments change independently");
    child.set_directory("/other");
    child.set_trap(0, "echo child");
    expect(parent.core().lookup("MODE").value == "parent" &&
               parent.core().positional(2).value == "second" &&
               parent.directory() == "/project" &&
               parent.trap(0) == "echo parent",
           "child mutation never reaches the parent");
}

void full_expansion_helpers() {
    const auto short_prefix = full::trim_parameter(
        "abcabc", "a*", full::TrimMode::ShortestPrefix);
    const auto long_prefix = full::trim_parameter(
        "abcabc", "a*", full::TrimMode::LongestPrefix);
    const auto short_suffix = full::trim_parameter(
        "abcabc", "*c", full::TrimMode::ShortestSuffix);
    const auto long_suffix = full::trim_parameter(
        "abcabc", "*c", full::TrimMode::LongestSuffix);
    expect(short_prefix.status == mm::shell::Status::Ok &&
               short_prefix.value == "bcabc" &&
               long_prefix.value.empty() &&
               short_suffix.value == "abcab" &&
               long_suffix.value.empty(),
           "four positional pattern trims reuse the core matcher");

    mm::shell::ShellState state;
    const auto number = full::evaluate_full_arithmetic(
        "0x2a + 1", state);
    const auto too_large = full::evaluate_full_arithmetic(
        "0x8000000000000000", state);
    expect(number.ok() && number.value == 43 &&
               too_large.status == mm::shell::ArithmeticStatus::Range,
           "hex literals preserve signed 64-bit range checks");
}

void tracked_script_corpus() {
    constexpr std::string_view paths[]{
        "bootstrap.sh", "build.sh", "check.sh", "clean.sh",
        "configure.sh", "debug.sh", "document.sh", "flash.sh",
        "json.sh", "model.sh", "run.sh", "sketch.sh", "test.sh",
        "platforms/pico/install-sdk-tools.sh",
        "platforms/pico/sdk/pico-sdk/vendor.sh",
        "scripts/build-analog-smoke-pico.sh",
        "scripts/build-blink-linux.sh",
        "scripts/build-blink-pico.sh",
        "scripts/build-board-smoke-rp2350_touch_lcd_28.sh",
        "scripts/build-font-demo-pico-epaper.sh",
        "scripts/build-font-demo-rp2350_touch_lcd_28.sh",
        "scripts/build-gfx-demo-pico-epaper.sh",
        "scripts/build-gfx-demo-rp2350_touch_lcd_28.sh",
        "scripts/build-gpio-edge-smoke-pico.sh",
        "scripts/build-linux-board-smoke.sh",
        "scripts/build-linux-display-demo.sh",
        "scripts/build-linux-epaper-font-demo.sh",
        "scripts/build-linux-epaper-gfx-demo.sh",
        "scripts/build-linux-sdl-font-demo.sh",
        "scripts/build-linux-sdl-gfx-demo.sh",
        "scripts/build-linux-sdl.sh",
        "scripts/build-linux-smoke.sh",
        "scripts/build-pico.sh",
        "scripts/build-stdio-smoke-pico-sdk.sh",
        "scripts/configure-pico.sh", "scripts/release.sh",
    };
    for (const auto path : paths) {
        std::ifstream file{std::string(path)};
        expect(file.good(), path);
        const std::string source{std::istreambuf_iterator<char>{file},
                                 std::istreambuf_iterator<char>{}};
        const auto parsed = full::parse_full(source);
        if (!parsed.ok()) {
            std::cerr << path << ": " << parsed.diagnostic.offset
                      << ": " << parsed.diagnostic.message << "\n";
        }
        expect(parsed.ok(), path);
    }
}

const mm::test::case_ cases[]{
    {"owns tokens and operators", &owns_tokens_and_operators},
    {"here-documents", &here_documents},
    {"redirection planning", &redirection_planning},
    {"refusals", &refusals},
    {"embedded parser agreement", &embedded_parser_agreement},
    {"owning child state", &owning_child_state},
    {"full expansion helpers", &full_expansion_helpers},
    {"tracked script corpus", &tracked_script_corpus},
};

const mm::test::registrar reg{"mm.shell.full", cases};

}  // namespace
