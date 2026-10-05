// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
//
// The shells over mm.fs: mm.shell.fs's command pack dispatched against the
// in-memory reference volume, and mm.fs.shell's services under the
// full-profile interpreter, its redirections, pathname expansion, and file
// tests all landing in the same volume.
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <span>
#include <string>
#include <string_view>
#include <vector>

import mm.fs;
import mm.fs.shell;
import mm.shell;
import mm.shell.fs;
import mm.shell.full;
import mm.test;

mm::fs::Volume& mm_test_fs_memory(unsigned int which);
void mm_test_fs_memory_reset();
void mm_test_fs_memory_capacity(unsigned int which, std::uint64_t capacity);

namespace {

using mm::fs::Status;
using mm::test::expect;

void clear() {
    static_cast<void>(mm::fs::unmount("/mem"));
    static_cast<void>(mm::fs::unmount("/two"));
    mm_test_fs_memory_reset();
}

std::string read_all(std::string_view path) {
    mm::fs::File file;
    if (mm::fs::open(path, mm::fs::Access::Read, mm::fs::Disposition::OpenExisting, file) !=
        Status::Ok)
        return "<absent>";
    std::string text;
    char buffer[64];
    while (true) {
        std::size_t count = 0;
        if (file.read(std::as_writable_bytes(std::span{buffer}), count) != Status::Ok) break;
        if (count == 0) break;
        text.append(buffer, count);
    }
    return text;
}

struct Pack {
    mm::shell::CommandDescriptor descriptors[12]{};
    mm::shell::Registry registry{descriptors};
    const std::string_view prefixes[2]{"/mem", "/two"};
    mm::shell::fs::FsBinding binding{prefixes};
    char out_bytes[512]{};
    char err_bytes[256]{};
    mm::shell::MemorySink out{out_bytes};
    mm::shell::MemorySink err{err_bytes};
    mm::shell::IoServices io{out.sink(), err.sink()};
    mm::shell::VariableSlot variables[4]{};
    char variable_text[64]{};
    mm::shell::PositionalSlot positionals[4]{};
    char positional_text[64]{};
    mm::shell::ShellState state{variables, variable_text, positionals, positional_text};
    mm::shell::CapabilitySet active;
    std::byte scratch[128]{};
    mm::shell::CommandContext context{io, state, active, scratch};

    Pack() {
        mm::shell::fs::enable(active);
        expect(mm::shell::fs::install_fs(registry, binding).ok(), "the pack installs");
    }

    mm::shell::CommandResult call(std::initializer_list<std::string_view> words) {
        out.reset();
        err.reset();
        const auto* command = registry.find(*words.begin());
        expect(command != nullptr, "the command is installed");
        return mm::shell::dispatch(*command, {words.begin(), words.size()}, context);
    }
};

void the_pack_installs_and_needs_files() {
    clear();
    Pack pack;
    expect(pack.registry.count() == mm::shell::fs::fs_builtin_count, "ten commands");
    for (const auto* name : {"ls", "cat", "stat", "write", "append", "rm", "mkdir", "mv", "df",
                             "mounts"})
        expect(pack.registry.find(name) != nullptr, name);
    pack.active.clear(mm::shell::Capability::Files);
    expect(pack.call({"ls", "/mem"}).status == 125, "without Files the commands are absent");

    mm::shell::CommandDescriptor short_slots[9]{};
    mm::shell::Registry short_registry{short_slots};
    const std::string_view none[1]{"/mem"};
    mm::shell::fs::FsBinding binding{none};
    expect(mm::shell::fs::install_fs(short_registry, binding).status ==
                   mm::shell::Status::Overflow &&
               short_registry.count() == 0,
           "a short registry installs none of the pack");
}

void files_are_written_read_and_listed() {
    clear();
    expect(mm::fs::mount("/mem", mm_test_fs_memory(0)) == Status::Ok, "a volume mounts");
    Pack pack;
    expect(pack.call({"write", "/mem/a.txt", "hello", "world"}).status == 0 &&
               read_all("/mem/a.txt") == "hello world\n",
           "write replaces a file with its words and a newline");
    expect(pack.call({"append", "/mem/a.txt", "again"}).status == 0 &&
               read_all("/mem/a.txt") == "hello world\nagain\n",
           "append adds a line");
    expect(pack.call({"cat", "/mem/a.txt"}).status == 0 &&
               pack.out.view() == "hello world\nagain\n" && pack.err.view().empty(),
           "cat prints it whole when it fits");
    expect(pack.call({"cat", "/mem/a.txt", "6"}).status == 0 &&
               pack.out.view() == "world\nagain\n",
           "and from an offset");
    expect(pack.call({"mkdir", "/mem/logs"}).status == 0 && pack.call({"ls", "/mem"}).status == 0,
           "a directory is made and the root listed");
    const auto listing = pack.out.view();
    expect(listing.find("a.txt\n") != std::string_view::npos &&
               listing.find("logs/\n") != std::string_view::npos,
           "files plainly, directories with a slash");
    expect(pack.call({"stat", "/mem/a.txt"}).status == 0 &&
               pack.out.view().starts_with("file 18 "),
           "stat gives the kind and size");
    expect(pack.call({"stat", "/mem/logs"}).status == 0 &&
               pack.out.view().starts_with("directory 0 "),
           "and for a directory");
    expect(pack.call({"mv", "/mem/a.txt", "/mem/logs/b.txt"}).status == 0 &&
               read_all("/mem/logs/b.txt") == "hello world\nagain\n",
           "mv renames within the volume");
    expect(pack.call({"rm", "/mem/logs"}).status == 1 &&
               pack.err.view() == "rm: directory not empty\n",
           "rm refuses a full directory with the file system's reason");
    expect(pack.call({"rm", "/mem/logs/b.txt"}).status == 0 &&
               pack.call({"rm", "/mem/logs"}).status == 0,
           "and removes a file, then the empty directory");
    expect(pack.call({"cat", "/mem/gone"}).status == 1 &&
               pack.err.view() == "cat: no such file or directory\n",
           "a missing file is status 1 with a reason");
    expect(pack.call({"cat", "relative"}).status == 2, "a relative path is a usage error");
    expect(pack.call({"write"}).status == 2 && pack.call({"mv", "/mem/x"}).status == 2,
           "missing operands are usage errors");
}

void long_output_pages() {
    clear();
    expect(mm::fs::mount("/mem", mm_test_fs_memory(0)) == Status::Ok, "a volume mounts");
    Pack pack;
    std::string big;
    for (int i = 0; i < 20; ++i) big += "0123456789";
    {
        mm::fs::File file;
        std::size_t count = 0;
        expect(mm::fs::open("/mem/big", mm::fs::Access::Write,
                            mm::fs::Disposition::CreateOrTruncate, file) == Status::Ok &&
                   file.write(std::as_bytes(std::span{big.data(), big.size()}), count) ==
                       Status::Ok,
               "a 200-byte file");
    }
    expect(pack.call({"cat", "/mem/big"}).status == 0 && pack.out.view() == big.substr(0, 128) &&
               pack.err.view() == "cat: more; cat /mem/big 128\n",
           "cat prints what the scratch holds and says where to go on");
    expect(pack.call({"cat", "/mem/big", "128"}).status == 0 &&
               pack.out.view() == big.substr(128) && pack.err.view().empty(),
           "and the rest from there");
    expect(pack.call({"cat", "/mem/big", "500"}).status == 0 && pack.out.view().empty(),
           "past the end prints nothing");

    for (int i = 0; i < 30; ++i) {
        const std::string name = "/mem/file-" + std::to_string(100 + i);
        mm::fs::File file;
        expect(mm::fs::open(name, mm::fs::Access::Write, mm::fs::Disposition::CreateNew, file) ==
                   Status::Ok,
               "many files");
    }
    std::string all;
    std::string skip = "0";
    unsigned int pages = 0;
    while (pages < 10) {
        expect(pack.call({"ls", "/mem", skip}).status == 0, "a page lists");
        all += pack.out.view();
        ++pages;
        const auto note = pack.err.view();
        if (note.empty()) break;
        expect(note.starts_with("ls: more; ls /mem "), "a note says how to go on");
        skip = std::string{note.substr(std::string_view{"ls: more; ls /mem "}.size())};
        skip.pop_back();    // the newline
    }
    std::size_t lines = 0;
    for (char c : all)
        if (c == '\n') ++lines;
    expect(pages > 1, "the listing outgrew the scratch and paged");
    expect(lines == 31, "the pages hold every entry once");
}

void space_and_mounts() {
    clear();
    mm_test_fs_memory_capacity(0, 4096);
    expect(mm::fs::mount("/mem", mm_test_fs_memory(0)) == Status::Ok, "a volume mounts");
    Pack pack;
    expect(pack.call({"mounts"}).status == 0 && pack.out.view() == "/mem\n",
           "mounts lists the declared prefixes that are mounted");
    expect(pack.call({"df"}).status == 0 && pack.out.view().starts_with("/mem 4096 "),
           "df gives each one's total and free bytes");
    expect(mm::fs::mount("/two", mm_test_fs_memory(1)) == Status::Ok &&
               pack.call({"mounts"}).status == 0 && pack.out.view() == "/mem\n/two\n",
           "a second volume appears");
    expect(pack.call({"df", "/two"}).status == 0 && pack.out.view().starts_with("/two "),
           "df takes one prefix");
    expect(pack.call({"df", "/none"}).status == 1, "and refuses one that is not mounted");
}

// A stream the full interpreter writes its output to, outside mm.fs.
struct Captured {
    std::string text;

    static mm::shell::full::ServiceStatus write(void* context, mm::shell::full::Handle handle,
                                                std::span<const std::byte> bytes,
                                                std::size_t& count) {
        if (handle != 1) return mm::shell::full::ServiceStatus::Invalid;
        static_cast<Captured*>(context)->text.append(reinterpret_cast<const char*>(bytes.data()),
                                                     bytes.size());
        count = bytes.size();
        return mm::shell::full::ServiceStatus::Ok;
    }
    static mm::shell::full::ServiceStatus close(void*, mm::shell::full::Handle) {
        return mm::shell::full::ServiceStatus::Ok;
    }

    mm::shell::full::IoService io() {
        return {.context = this, .write = &write, .close = &close};
    }
};

struct Full {
    Captured captured;
    mm::fs::shell::FileServices files{captured.io()};
    mm::shell::full::FullState state;

    mm::shell::full::RunOutcome run(std::string_view source) {
        mm::shell::full::Interpreter interpreter{state, files.all()};
        interpreter.set_streams({mm::shell::full::invalid_handle, 1, 1});
        return interpreter.run_text(source);
    }
};

void the_full_shell_redirects_into_mm_fs() {
    clear();
    expect(mm::fs::mount("/mem", mm_test_fs_memory(0)) == Status::Ok, "a volume mounts");
    Full full;
    auto outcome = full.run("echo first > /mem/log.txt\n"
                            "echo second >> /mem/log.txt\n");
    expect(outcome.ok() && outcome.status == 0 && read_all("/mem/log.txt") == "first\nsecond\n",
           "> and >> write files on the volume");
    outcome = full.run("read line < /mem/log.txt\necho got $line\n");
    expect(outcome.ok() && full.captured.text == "got first\n",
           "< reads one back, and output goes to the base stream");
    expect(full.files.open_files() == 0, "every file the redirections opened is closed");
}

void the_full_shell_expands_and_tests_paths() {
    clear();
    expect(mm::fs::mount("/mem", mm_test_fs_memory(0)) == Status::Ok, "a volume mounts");
    for (const auto* name : {"/mem/a.txt", "/mem/b.txt", "/mem/c.log"}) {
        mm::fs::File file;
        std::size_t count = 0;
        expect(mm::fs::open(name, mm::fs::Access::Write, mm::fs::Disposition::CreateNew, file) ==
                       Status::Ok &&
                   file.write(std::as_bytes(std::span{"x", 1}), count) == Status::Ok,
               "files to match");
    }
    expect(mm::fs::make_directory("/mem/dir") == Status::Ok, "and a directory");
    Full full;
    auto outcome = full.run("for f in /mem/*.txt; do echo $f; done\n");
    expect(outcome.ok() && full.captured.text == "/mem/a.txt\n/mem/b.txt\n",
           "pathname expansion lists the volume, sorted");
    full.captured.text.clear();
    outcome = full.run("[ -f /mem/a.txt ] && echo file\n"
                       "[ -d /mem/dir ] && echo dir\n"
                       "[ -e /mem/none ] || echo none\n"
                       "[ -s /mem/c.log ] && echo nonempty\n");
    expect(outcome.ok() && full.captured.text == "file\ndir\nnone\nnonempty\n",
           "file tests answer from mm.fs's stat");
    expect(full.files.set_directory("/mem") && !full.files.set_directory("relative"),
           "the directory relative paths resolve in");
    full.captured.text.clear();
    outcome = full.run("echo here > note\n[ -f /mem/note ] && echo resolved\n");
    expect(outcome.ok() && full.captured.text == "resolved\n" && read_all("/mem/note") == "here\n",
           "a relative path resolves there");
}

const mm::test::case_ cases[]{
    {"mm.shell.fs installs and needs Files", the_pack_installs_and_needs_files},
    {"mm.shell.fs writes, reads, and lists", files_are_written_read_and_listed},
    {"mm.shell.fs pages long output", long_output_pages},
    {"mm.shell.fs space and mounts", space_and_mounts},
    {"mm.fs.shell redirections", the_full_shell_redirects_into_mm_fs},
    {"mm.fs.shell expansion and tests", the_full_shell_expands_and_tests_paths},
};
const mm::test::registrar registrar{"mm.shell over mm.fs", cases};

}  // namespace
