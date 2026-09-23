// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
#include <cstddef>
#include <span>
#include <string_view>

import mm.shell;
import mm.test;

namespace {

using mm::shell::Capability;
using mm::shell::CapabilitySet;
using mm::shell::ScriptDescriptor;
using mm::shell::ScriptLibrary;
using mm::shell::SourceView;
using mm::shell::Status;
using mm::shell::StorageClass;
using mm::test::expect;

struct Arena {
    mm::shell::ScriptSlot slots[3]{};
    mm::shell::ScriptToken tokens[48]{};
    mm::shell::WordFragment fragments[48]{};
    mm::shell::SyntaxNode nodes[48]{};
    mm::shell::SyntaxLink links[48]{};
    mm::shell::ParserFrame context[16]{};

    [[nodiscard]] mm::shell::ScriptLibraryStorage storage() {
        return {slots, tokens, fragments, nodes, links, context};
    }
};

void noop_handler(void*, std::span<const std::string_view>,
                  mm::shell::CommandContext&, mm::shell::CommandResult&) {}

[[nodiscard]] ScriptDescriptor script(std::string_view name,
                                     std::string_view source,
                                     CapabilitySet required = {}) {
    return {name, "test script", required, SourceView{source}};
}

void installs_and_finds_scripts() {
    Arena arena;
    ScriptLibrary library{arena.storage()};
    expect(library.size() == 0 && library.find("blink") == nullptr,
           "a fresh library is empty");
    expect(library.install(script("blink", "echo on; echo off")).ok(),
           "a valid script installs");
    const auto* found = library.find("blink");
    expect(found != nullptr && found->descriptor.name == "blink" &&
               found->script.root < found->script.nodes.size() &&
               library.size() == 1,
           "the installed script is found with its parsed body");
    expect(library.install(script("blink", "echo again")).status ==
               Status::Duplicate && library.size() == 1,
           "a duplicate name is refused");
    expect(library.install(script("2bad", "echo hi")).status ==
               Status::BadArgument,
           "an invalid name is refused");
    expect(library.install(script("broken", "echo one ;; echo two"))
                   .status == Status::BadArgument &&
               library.size() == 1,
           "a malformed body is refused without publishing a slot");
    // An empty body is a valid no-op script, exactly as an empty script file
    // is for a real shell.
    expect(library.install(script("empty", "")).ok() &&
               library.find("empty") != nullptr,
           "an empty body installs as a no-op");
    library.reset();
    expect(library.size() == 0 && library.find("blink") == nullptr,
           "reset reclaims every slot and the whole arena");
}

void rejects_native_name_collisions() {
    Arena arena;
    ScriptLibrary library{arena.storage()};
    mm::shell::CommandDescriptor slots[2]{};
    mm::shell::Registry registry{slots};
    expect(registry.install({
               .name = "gpio",
               .summary = "native command",
               .command_class = mm::shell::CommandClass::Custom,
               .required_capabilities = {},
               .handler = &noop_handler,
               .context = nullptr,
           }).ok(),
           "native command installs");
    expect(library.install(script("gpio", "echo hi"), &registry).status ==
               Status::Duplicate && library.size() == 0,
           "a native name cannot be taken by a script");
    expect(library.install(script("gpio", "echo hi")).ok(),
           "without the registry the same name is admitted");
}

void preflights_every_arena() {
    Arena arena;
    ScriptLibrary library{arena.storage()};
    expect(library.install(script("one", "echo a")).ok(), "first installs");
    expect(library.install(script("two", "echo b")).ok(), "second installs");
    expect(library.install(script("three", "echo c")).ok(),
           "third fills the slots");
    const auto full = library.install(script("four", "echo d"));
    expect(full.status == Status::Overflow &&
               full.overflow.storage_class == StorageClass::Scripts &&
               full.overflow.required == 4 && library.size() == 3,
           "an exhausted slot span reports Scripts");

    mm::shell::ScriptSlot slots[2]{};
    mm::shell::ScriptToken tokens[4]{};
    mm::shell::WordFragment fragments[16]{};
    mm::shell::SyntaxNode nodes[16]{};
    mm::shell::SyntaxLink links[16]{};
    mm::shell::ParserFrame context[8]{};
    ScriptLibrary tight{
        {slots, tokens, fragments, nodes, links, context}};
    const auto narrow = tight.install(
        script("long", "echo one two three four five six"));
    expect(narrow.status == Status::Overflow &&
               narrow.overflow.storage_class == StorageClass::ScriptArena &&
               tight.size() == 0,
           "an exhausted parse arena reports ScriptArena");
}

void installs_a_pack_atomically() {
    Arena arena;
    ScriptLibrary library{arena.storage()};
    const ScriptDescriptor good[]{script("one", "echo a"),
                                  script("two", "echo b")};
    expect(library.install_pack(good).ok() && library.size() == 2,
           "a valid pack installs every entry");
    library.reset();

    const ScriptDescriptor duplicated[]{script("one", "echo a"),
                                        script("one", "echo b")};
    expect(library.install_pack(duplicated).status == Status::Duplicate &&
               library.size() == 0,
           "a pack with an internal duplicate installs nothing");

    const ScriptDescriptor partly_bad[]{script("one", "echo a"),
                                        script("two", "echo one ;; two")};
    expect(library.install_pack(partly_bad).status == Status::BadArgument &&
               library.size() == 0 && library.find("one") == nullptr,
           "a later failure rolls the whole pack back");
    expect(library.install_pack(good).ok() && library.size() == 2,
           "the rolled-back arena is reusable");
}

void capabilities_do_not_block_installation() {
    Arena arena;
    ScriptLibrary library{arena.storage()};
    CapabilitySet needs_gpio;
    needs_gpio.set(Capability::Gpio);
    expect(library.install(script("blink", "echo on", needs_gpio)).ok(),
           "an unserved capability does not prevent installation");
    const auto* found = library.find("blink");
    expect(found != nullptr &&
               found->descriptor.required_capabilities.has(Capability::Gpio),
           "the requirement is recorded for the invocation to check");
}

const mm::test::case_ cases[]{
    {"installs and finds scripts", &installs_and_finds_scripts},
    {"native name collisions", &rejects_native_name_collisions},
    {"preflights every arena", &preflights_every_arena},
    {"installs a pack atomically", &installs_a_pack_atomically},
    {"capabilities at invocation",
     &capabilities_do_not_block_installation},
};

const mm::test::registrar reg{"mm.shell script library", cases};

}  // namespace
