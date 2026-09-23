// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <span>
#include <string_view>

import mm.shell;
import mm.shell.mcu;
import mm.mcu;
import mm.stdio;
import mm.test;

namespace {

using mm::test::expect;

constexpr std::string_view blink_source =
    "gpio configure $1 out none\n"
    "gpio write $1 1 || return $?\n"
    "delay ms 100\n"
    "gpio write $1 0\n";
constexpr std::string_view sample_source =
    "adc configure $1 || return $?\n"
    "sample=$(adc read $1)\n"
    "echo $sample\n"
    "adc release $1\n";

struct RecordingMcu final : mm::mcu::Platform {
    mm::mcu::Status answer = mm::mcu::Status::Ok;
    std::size_t calls = 0;
    std::string_view last_uart;
    bool high = true;

    [[nodiscard]] mm::mcu::Capabilities capabilities() const override {
        return {.board = true, .gpio = true, .spi = true,
                .i2c = true, .uart = true, .timer = true,
                .adc = true, .pwm = true};
    }

    [[nodiscard]] mm::mcu::Board board() const override {
        static constexpr mm::mcu::Gpio gpios[]{{1, "one"}, {2, "two"}};
        return {.name = "test-board", .gpios = gpios,
                .led = mm::mcu::Led{"led", 1, true}};
    }

    [[nodiscard]] mm::mcu::Status gpio_configure(
        unsigned int, mm::mcu::Direction,
        mm::mcu::Pull) override { ++calls; return answer; }
    [[nodiscard]] mm::mcu::Status gpio_write(
        unsigned int, bool) override { ++calls; return answer; }
    [[nodiscard]] mm::mcu::Status gpio_read(
        unsigned int, bool& value) override {
        ++calls;
        if (answer == mm::mcu::Status::Ok) value = high;
        return answer;
    }
    [[nodiscard]] mm::mcu::Status gpio_watch(
        unsigned int, mm::mcu::Pull,
        mm::mcu::Edge) override { ++calls; return answer; }
    [[nodiscard]] mm::mcu::Status gpio_take(
        unsigned int, bool& value) override {
        ++calls;
        if (answer == mm::mcu::Status::Ok) value = high;
        return answer;
    }
    [[nodiscard]] mm::mcu::Status gpio_wait(
        unsigned int, unsigned long, bool& value) override {
        ++calls;
        if (answer == mm::mcu::Status::Ok) value = high;
        return answer;
    }
    [[nodiscard]] mm::mcu::Status gpio_unwatch(
        unsigned int) override { ++calls; return answer; }

    [[nodiscard]] mm::mcu::Status spi_configure(
        const mm::mcu::SpiConfiguration&) override {
        ++calls; return answer;
    }
    [[nodiscard]] mm::mcu::Status spi_write(
        unsigned int, std::span<const std::byte>) override {
        ++calls; return answer;
    }
    [[nodiscard]] mm::mcu::Status spi_transfer(
        unsigned int, std::span<const std::byte>,
        std::span<std::byte> received) override {
        ++calls;
        if (answer == mm::mcu::Status::Ok) {
            for (auto& value : received) value = std::byte{0xab};
        }
        return answer;
    }

    [[nodiscard]] mm::mcu::Status i2c_configure(
        const mm::mcu::I2cConfiguration&) override {
        ++calls; return answer;
    }
    [[nodiscard]] mm::mcu::Status i2c_write(
        unsigned int, unsigned int,
        std::span<const std::byte>) override {
        ++calls; return answer;
    }
    [[nodiscard]] mm::mcu::Status i2c_read(
        unsigned int, unsigned int,
        std::span<std::byte> received) override {
        ++calls;
        if (answer == mm::mcu::Status::Ok) {
            for (auto& value : received) value = std::byte{0xcd};
        }
        return answer;
    }
    [[nodiscard]] mm::mcu::Status i2c_write_read(
        unsigned int, unsigned int, std::span<const std::byte>,
        std::span<std::byte> received) override {
        return i2c_read(0, 0, received);
    }
    [[nodiscard]] mm::mcu::Status uart_write(
        unsigned int, const char* text) override {
        ++calls;
        if (answer == mm::mcu::Status::Ok) last_uart = text;
        return answer;
    }
    [[nodiscard]] mm::mcu::Status ticks_ms(
        unsigned long& value) override {
        ++calls;
        if (answer == mm::mcu::Status::Ok) value = 123;
        return answer;
    }
    [[nodiscard]] mm::mcu::Status ticks_us(
        unsigned long& value) override {
        ++calls;
        if (answer == mm::mcu::Status::Ok) value = 456;
        return answer;
    }
    [[nodiscard]] mm::mcu::Status delay_ms(
        unsigned long) override { ++calls; return answer; }
    [[nodiscard]] mm::mcu::Status delay_us(
        unsigned long) override { ++calls; return answer; }

    [[nodiscard]] mm::mcu::AdcDescription adc_description()
        const override {
        static constexpr mm::mcu::AdcChannel channels[]{
            {0, "input", 1, 12, 3300}};
        return {channels};
    }
    [[nodiscard]] mm::mcu::Status adc_configure(
        unsigned int) override { ++calls; return answer; }
    [[nodiscard]] mm::mcu::Status adc_read(
        unsigned int, unsigned int& value) override {
        ++calls;
        if (answer == mm::mcu::Status::Ok) value = 2048;
        return answer;
    }
    [[nodiscard]] mm::mcu::Status adc_release(
        unsigned int) override { ++calls; return answer; }

    [[nodiscard]] mm::mcu::PwmDescription pwm_description()
        const override {
        static constexpr mm::mcu::PwmOutput outputs[]{
            {0, "output", 2, 0, 0, 100, 1000000}};
        return {outputs};
    }
    [[nodiscard]] mm::mcu::Status pwm_configure(
        unsigned int, std::uint64_t) override {
        ++calls; return answer;
    }
    [[nodiscard]] mm::mcu::Status pwm_period(
        unsigned int, std::uint64_t& value) override {
        ++calls;
        if (answer == mm::mcu::Status::Ok) value = 1000;
        return answer;
    }
    [[nodiscard]] mm::mcu::Status pwm_write(
        unsigned int, std::uint64_t) override {
        ++calls; return answer;
    }
    [[nodiscard]] mm::mcu::Status pwm_release(
        unsigned int) override { ++calls; return answer; }
};

struct RecordingConsole final : mm::stdio::Console {
    bool carrier = false;
    std::size_t write_limit = 64;
    char transmitted[64]{};
    std::size_t used = 0;

    [[nodiscard]] mm::stdio::Status initialize() override {
        return mm::stdio::Status::Ok;
    }
    [[nodiscard]] mm::stdio::Status connected(
        bool& value) override {
        value = carrier;
        return mm::stdio::Status::Ok;
    }
    [[nodiscard]] mm::stdio::Status flush() override {
        return mm::stdio::Status::Ok;
    }
    [[nodiscard]] mm::stdio::Status write(
        std::span<const std::byte> bytes,
        std::size_t& written) override {
        written = bytes.size() < write_limit ? bytes.size()
                                             : write_limit;
        for (std::size_t i = 0; i < written; ++i) {
            transmitted[used++] = static_cast<char>(
                std::to_integer<unsigned int>(bytes[i]));
        }
        return mm::stdio::Status::Ok;
    }
};

struct Fixture {
    RecordingMcu mcu;
    mm::mcu::Platform* previous = nullptr;
    mm::shell::CommandDescriptor descriptors[28]{};
    mm::shell::Registry registry{descriptors};
    mm::shell::Introspection introspection;
    mm::shell::mcu::Level2Binding binding;
    char out_bytes[512]{};
    char err_bytes[512]{};
    mm::shell::MemorySink out{out_bytes};
    mm::shell::MemorySink err{err_bytes};
    mm::shell::IoServices io{out.sink(), err.sink()};
    mm::shell::VariableSlot variables[16]{};
    char variable_text[256]{};
    mm::shell::PositionalSlot positionals[16]{};
    char positional_text[128]{};
    mm::shell::ShellState state{variables, variable_text,
                                positionals, positional_text};
    mm::shell::CapabilitySet active;
    std::byte scratch[256]{};
    mm::shell::CommandContext context{io, state, active, scratch};

    Fixture() {
        previous = &mm::mcu::platform();
        mm::mcu::set_platform(mcu);
        active = mm::shell::mcu::capabilities();
        expect(mm::shell::mcu::install_level2(
                   registry, introspection, binding).ok(),
               "combined pack installs");
    }

    ~Fixture() { mm::mcu::set_platform(*previous); }

    [[nodiscard]] mm::shell::CommandResult call(
        std::initializer_list<std::string_view> words) {
        out.reset();
        err.reset();
        const auto args = std::span<const std::string_view>{
            words.begin(), words.size()};
        const auto* command = registry.find(*words.begin());
        expect(command != nullptr, "descriptor remains installed");
        return mm::shell::dispatch(*command, args, context);
    }
};

void vocabulary_and_capabilities() {
    Fixture fixture;
    expect(fixture.registry.count() == 28 &&
               fixture.registry.find("ticks") != nullptr &&
               fixture.registry.find("time") == nullptr,
           "level-2 pack has every command and no time alias");
    fixture.active.clear(mm::shell::Capability::Gpio);
    const auto before = fixture.mcu.calls;
    const auto absent = fixture.call({"gpio", "read", "1"});
    expect(absent.status == 125 && fixture.mcu.calls == before,
           "capability gate precedes provider access");
    expect(!mm::shell::mcu::capabilities().has(
               mm::shell::Capability::Console),
           "console capability requires attached driver");

    mm::shell::CommandDescriptor short_slots[27]{};
    mm::shell::Registry short_registry{short_slots};
    mm::shell::Introspection short_intro;
    mm::shell::mcu::Level2Binding short_binding;
    const auto install = mm::shell::mcu::install_level2(
        short_registry, short_intro, short_binding);
    expect(install.status == mm::shell::Status::Overflow &&
               install.overflow.required == 28 &&
               short_registry.count() == 0,
           "short combined pack does not install a partial level-1 pack");

    mm::shell::CommandDescriptor extra =
        *fixture.registry.find("board");
    extra.name = "forged";
    extra.command_class = mm::shell::CommandClass::SpecialBuiltin;
    mm::shell::CommandDescriptor enough_slots[28]{};
    mm::shell::CommandDescriptor scratch[19]{};
    mm::shell::Registry fresh{enough_slots};
    mm::shell::Introspection fresh_intro;
    const auto refused = mm::shell::install_level1_with(
        fresh, fresh_intro, std::span{&extra, 1}, scratch);
    expect(refused.status == mm::shell::Status::BadArgument &&
               fresh.count() == 0,
           "only the core pack may install special builtins");
}

void command_families() {
    Fixture fixture;
    expect(fixture.call({"board"}).status == 0 &&
               fixture.out.view() == "test-board\n",
           "board query prints stable name");
    expect(fixture.call({"gpio", "list"}).status == 0 &&
               fixture.out.view() == "1 one\n2 two\n",
           "gpio inventory prints stable lines");
    expect(fixture.call({"gpio", "configure", "1", "out",
                         "none"}).status == 0 &&
               fixture.call({"gpio", "read", "1"}).status == 0 &&
               fixture.out.view() == "1\n" &&
               fixture.call({"gpio", "write", "1", "1"}).status == 0 &&
               fixture.call({"gpio", "watch", "1", "up",
                             "rise"}).status == 0 &&
               fixture.call({"gpio", "take", "1"}).status == 0 &&
               fixture.out.view() == "1\n" &&
               fixture.call({"gpio", "wait", "1", "5"}).status == 0 &&
               fixture.call({"gpio", "unwatch", "1"}).status == 0,
           "GPIO command family reaches provider");
    expect(fixture.call({"adc", "list"}).status == 0 &&
               fixture.out.view() == "0 input\n" &&
               fixture.call({"adc", "configure", "0"}).status == 0 &&
               fixture.call({"adc", "read", "0"}).status == 0 &&
               fixture.out.view() == "2048\n" &&
               fixture.call({"adc", "release", "0"}).status == 0,
           "ADC command family formats count");
    expect(fixture.call({"pwm", "list"}).status == 0 &&
               fixture.out.view() == "0 output\n" &&
               fixture.call({"pwm", "configure", "0",
                             "1000"}).status == 0 &&
               fixture.call({"pwm", "period", "0"}).status == 0 &&
               fixture.out.view() == "1000\n" &&
               fixture.call({"pwm", "write", "0", "500"}).status == 0 &&
               fixture.call({"pwm", "release", "0"}).status == 0,
           "PWM command family formats period");
    expect(fixture.call({"ticks", "ms"}).status == 0 &&
               fixture.out.view() == "123\n" &&
               fixture.call({"ticks", "us"}).status == 0 &&
               fixture.out.view() == "456\n" &&
               fixture.call({"delay", "ms", "2"}).status == 0 &&
               fixture.call({"delay", "us", "2"}).status == 0,
           "timer commands reach selected provider");
}

void bus_commands_and_scratch() {
    Fixture fixture;
    expect(fixture.call({"spi", "configure", "0", "2", "3",
                         "-", "100000", "0", "msb"}).status == 0 &&
               fixture.call({"spi", "write", "0", "0x12"}).status == 0 &&
               fixture.call({"spi", "transfer", "0", "0x12",
                             "0x34"}).status == 0 &&
               fixture.out.view() == "ab ab\n",
           "SPI transfer formats bytes");
    expect(fixture.call({"i2c", "configure", "0", "2", "3",
                         "400000"}).status == 0 &&
               fixture.call({"i2c", "write", "0", "0x48",
                             "0x01"}).status == 0 &&
               fixture.call({"i2c", "read", "0", "0x48",
                             "2"}).status == 0 &&
               fixture.out.view() == "cd cd\n" &&
               fixture.call({"i2c", "write-read", "0", "0x48",
                             "1", "0x01"}).status == 0 &&
               fixture.out.view() == "cd\n",
           "I2C request and response use one transaction");
    expect(fixture.call({"uart", "write", "0", "hello",
                         "world"}).status == 0 &&
               fixture.mcu.last_uart == "hello world",
           "UART text joins arguments in bounded storage");

    fixture.context.scratch = std::span{fixture.scratch}.first(4);
    const auto before = fixture.mcu.calls;
    const auto short_result = fixture.call(
        {"spi", "transfer", "0", "0x12"});
    expect(short_result.error == mm::shell::Status::Overflow &&
               fixture.mcu.calls == before && fixture.out.view().empty(),
           "one-short scratch refuses before SPI provider call");

    fixture.context.scratch = std::span{fixture.scratch}.first(4);
    const auto short_i2c = fixture.call(
        {"i2c", "write-read", "0", "0x48", "1", "0x01"});
    expect(short_i2c.error == mm::shell::Status::Overflow &&
               short_i2c.overflow.required == 5 &&
               fixture.mcu.calls == before && fixture.out.view().empty(),
           "one-short I2C scratch refuses before provider call");

    fixture.context.scratch = fixture.scratch;
    const auto malformed = fixture.call(
        {"spi", "configure", "0", "2", "3", "-",
         "100000", "4", "msb"});
    expect(malformed.status == 2 && fixture.mcu.calls == before,
           "malformed arguments never reach the provider");
}

void status_mapping_and_console() {
    Fixture fixture;
    const auto absent = fixture.call({"console", "connected"});
    expect(absent.status == 125 && fixture.out.view().empty(),
           "unwired console is unavailable");
    constexpr mm::mcu::Status cases[]{
        mm::mcu::Status::BadArgument, mm::mcu::Status::Unsupported,
        mm::mcu::Status::Busy, mm::mcu::Status::Timeout,
        mm::mcu::Status::TransportError};
    constexpr int expected[]{2, 70, 75, 124, 74};
    for (std::size_t i = 0; i < 5; ++i) {
        fixture.mcu.answer = cases[i];
        const auto result = fixture.call({"gpio", "read", "1"});
        expect(result.status == expected[i] &&
                   fixture.out.view().empty() &&
                   !fixture.err.view().empty(),
               "provider status maps without stdout pollution");
    }
    fixture.mcu.answer = mm::mcu::Status::Ok;
    RecordingConsole driver;
    char pending[16]{};
    mm::shell::mcu::McuConsole console;
    expect(console.attach(driver, pending) == mm::stdio::Status::Ok,
           "console attaches after successful initialize");
    fixture.binding.console = &console;
    fixture.active = mm::shell::mcu::capabilities(&console);
    expect(fixture.call({"console", "connected"}).status == 0 &&
               fixture.out.view() == "0\n",
           "wired but disconnected console prints zero");
    driver.carrier = true;
    expect(fixture.call({"console", "connected"}).status == 0 &&
               fixture.out.view() == "1\n" &&
               fixture.call({"console", "flush"}).status == 0,
           "connected console is available");
    driver.write_limit = 2;
    expect(console.sink().write("abc") ==
               mm::shell::SinkResult::Accepted &&
               console.pump() == mm::stdio::Status::Ok &&
               console.pending_bytes() == 1 &&
               console.pump() == mm::stdio::Status::Ok &&
               console.pending_bytes() == 0 &&
               std::string_view{driver.transmitted, driver.used} == "abc",
           "partial physical write drains pending bytes later");
}

void firmware_script_sizes() {
    const auto a = mm::shell::measure_embedded(
        mm::shell::SourceView{blink_source});
    const auto b = mm::shell::measure_embedded(
        mm::shell::SourceView{sample_source});
    expect(a.status == mm::shell::ParseStatus::Complete &&
               b.status == mm::shell::ParseStatus::Complete &&
               a.required.tokens == 23 &&
               a.required.fragments == 18 &&
               a.required.nodes == 29 &&
               a.required.links == 28 &&
               a.required.context == 4 &&
               b.required.tokens == 16 &&
               b.required.fragments == 12 &&
               b.required.nodes == 22 &&
               b.required.links == 21 &&
               b.required.context == 8,
           "firmware script high-water counts stay fixed");
    Fixture fixture;
    mm::shell::ScriptSlot slots[2]{};
    mm::shell::ScriptToken tokens[39]{};
    mm::shell::WordFragment fragments[30]{};
    mm::shell::SyntaxNode nodes[51]{};
    mm::shell::SyntaxLink links[49]{};
    mm::shell::ParserFrame context[12]{};
    mm::shell::ScriptLibrary library{{slots, tokens, fragments,
                                      nodes, links, context}};
    const mm::shell::ScriptDescriptor scripts[]{
        {.name = "blink", .source = mm::shell::SourceView{blink_source}},
        {.name = "sample", .source = mm::shell::SourceView{sample_source}},
    };
    expect(library.install_pack(scripts, &fixture.registry).ok(),
           "both firmware scripts fit their combined exact-size arena");
}

struct ExecutionScratch {
    char source[128]{};
    mm::shell::ScriptToken tokens[64]{};
    mm::shell::WordFragment fragments[64]{};
    mm::shell::SyntaxNode nodes[80]{};
    mm::shell::SyntaxLink links[80]{};
    mm::shell::ParserFrame parser_context[16]{};
    mm::shell::ScriptSlot script_slots[2]{};
    mm::shell::ScriptToken script_tokens[64]{};
    mm::shell::WordFragment script_fragments[64]{};
    mm::shell::SyntaxNode script_nodes[64]{};
    mm::shell::SyntaxLink script_links[64]{};
    mm::shell::ParserFrame script_context[16]{};
    mm::shell::FieldPiece pieces[64]{};
    char generated[128]{};
    char field_text[256]{};
    mm::shell::SourceSpan fields[32]{};
    mm::shell::VariableSlot shadow_variables[16]{};
    char shadow_text[256]{};
    std::string_view arguments[32]{};
    char argument_text[256]{};
    mm::shell::EvaluatorFrame frames[24]{};
    std::string_view loop_items[16]{};
    char loop_text[128]{};
    mm::shell::PatternByte pattern[64]{};
    mm::shell::VariableSlot prefix_variables[16]{};
    char prefix_text[256]{};
    mm::shell::PositionalSlot call_positionals[32]{};
    char capture_text[128]{};
    std::string_view capture_values[8]{};
    mm::shell::VariableSlot capture_variables[16]{};
    char capture_variable_text[256]{};
    mm::shell::ScriptToken capture_tokens[32]{};
    mm::shell::WordFragment capture_fragments[32]{};
    mm::shell::SyntaxNode capture_nodes[32]{};
    mm::shell::SyntaxLink capture_links[32]{};
    mm::shell::ParserFrame capture_context[16]{};
    char staged_out[256]{};
    char staged_err[256]{};

    [[nodiscard]] mm::shell::ScriptLibraryStorage scripts() {
        return {script_slots, script_tokens, script_fragments,
                script_nodes, script_links, script_context};
    }

    [[nodiscard]] mm::shell::SessionStorage session(
        mm::shell::ScriptLibrary& scripts) {
        return {
            .source = source,
            .script = {tokens, fragments, nodes, links, parser_context},
            .evaluator = {
                .expansion = {pieces, generated,
                              {field_text, fields},
                              shadow_variables, shadow_text},
                .arguments = arguments,
                .argument_text = argument_text,
                .frames = frames,
                .loop_items = loop_items,
                .loop_text = loop_text,
                .pattern = pattern,
                .prefix_variables = prefix_variables,
                .prefix_variable_text = prefix_text,
                .staged_output = staged_out,
                .staged_error = staged_err,
                .scripts = &scripts,
                .call_positionals = call_positionals,
                .capture_text = capture_text,
                .capture_values = capture_values,
                .capture_variables = capture_variables,
                .capture_variable_text = capture_variable_text,
                .capture_tokens = capture_tokens,
                .capture_fragments = capture_fragments,
                .capture_nodes = capture_nodes,
                .capture_links = capture_links,
                .capture_parser_context = capture_context,
            },
        };
    }
};

void firmware_scripts_execute() {
    Fixture fixture;
    ExecutionScratch scratch;
    mm::shell::ScriptLibrary scripts{scratch.scripts()};
    fixture.introspection.scripts = &scripts;
    const mm::shell::ScriptDescriptor descriptors[]{
        {.name = "blink", .source =
             mm::shell::SourceView{blink_source}},
        {.name = "sample", .source =
             mm::shell::SourceView{sample_source}},
    };
    expect(scripts.install_pack(descriptors, &fixture.registry).ok(),
           "firmware scripts install beside the MCU command pack");
    mm::shell::Session session;
    expect(session.begin(fixture.registry, fixture.context,
                         scratch.session(scripts)) ==
               mm::shell::Status::Ok,
           "MCU session begins with caller-owned storage");

    constexpr std::string_view blink_line = "blink 1\n";
    expect(session.feed(std::span<const char>{
               blink_line.data(), blink_line.size()}).ok(),
           "blink command enters the evaluator");
    for (std::size_t i = 0; i < 128 && session.executing(); ++i) {
        expect(session.step().step != mm::shell::Step::Failed,
               "blink execution does not fail");
    }
    expect(!session.executing() && fixture.mcu.calls == 4 &&
               fixture.out.view().empty(),
           "blink configures, writes, delays, and clears GPIO");

    fixture.out.reset();
    constexpr std::string_view sample_line = "sample 0\n";
    expect(session.feed(std::span<const char>{
               sample_line.data(), sample_line.size()}).ok(),
           "sample command enters the evaluator");
    for (std::size_t i = 0; i < 128 && session.executing(); ++i) {
        expect(session.step().step != mm::shell::Step::Failed,
               "ADC execution does not fail");
    }
    expect(!session.executing() && fixture.mcu.calls == 7 &&
               fixture.out.view() == "2048\n",
           "sample captures ADC count and releases the channel");
}

const mm::test::case_ cases[]{
    {"vocabulary and capabilities", &vocabulary_and_capabilities},
    {"peripheral families", &command_families},
    {"bus commands and scratch", &bus_commands_and_scratch},
    {"status mapping and console", &status_mapping_and_console},
    {"firmware script sizes", &firmware_script_sizes},
    {"firmware scripts execute", &firmware_scripts_execute},
};

const mm::test::registrar reg{"mm.shell.mcu", cases};

}  // namespace
