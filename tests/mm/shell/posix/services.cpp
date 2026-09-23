// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
#include <algorithm>
#include <csignal>
#include <cstddef>
#include <fstream>
#include <span>
#include <string>
#include <string_view>
#include <vector>

import mm.shell.full;
import mm.shell.posix;
import mm.test;

namespace {

using mm::shell::full::Handle;
using mm::shell::full::OpenMode;
using mm::shell::full::ProcessRequest;
using mm::shell::full::ServiceStatus;
using mm::shell::full::invalid_handle;
using mm::shell::posix::HostServices;
using mm::test::expect;
using mm::test::scoped_tree;

[[nodiscard]] std::span<const std::byte> bytes_of(std::string_view text) {
    return {reinterpret_cast<const std::byte*>(text.data()), text.size()};
}

[[nodiscard]] std::string read_file(const std::string& path) {
    std::ifstream file{path, std::ios::binary};
    return std::string{std::istreambuf_iterator<char>{file},
                       std::istreambuf_iterator<char>{}};
}

void write_file(const std::string& path, std::string_view text) {
    std::ofstream file{path, std::ios::binary};
    file << text;
}

void descriptors_round_trip_and_balance() {
    scoped_tree tree{"posix_services_io"};
    const auto path = (tree.root() / "out.txt").string();
    HostServices host;
    const auto io = host.io();

    Handle sink = invalid_handle;
    expect(io.open(io.context, path, OpenMode::Truncate, sink) ==
               ServiceStatus::Ok && sink != invalid_handle,
           "a truncating open publishes a handle");
    std::size_t moved = 0;
    expect(io.write(io.context, sink, bytes_of("one\n"), moved) ==
               ServiceStatus::Ok && moved == 4,
           "a write reports the byte count it moved");
    expect(io.close(io.context, sink) == ServiceStatus::Ok &&
               host.open_descriptors() == 0,
           "closing a handle releases its slot");

    Handle appended = invalid_handle;
    expect(io.open(io.context, path, OpenMode::Append, appended) ==
               ServiceStatus::Ok,
           "an appending open succeeds");
    expect(io.write(io.context, appended, bytes_of("two\n"), moved) ==
               ServiceStatus::Ok &&
               io.close(io.context, appended) == ServiceStatus::Ok,
           "an append writes and closes");
    expect(read_file(path) == "one\ntwo\n",
           "truncate then append produced both lines in order");

    Handle source = invalid_handle;
    expect(io.open(io.context, path, OpenMode::Read, source) ==
               ServiceStatus::Ok,
           "a reading open succeeds");
    std::byte storage[32]{};
    const std::span<std::byte> buffer{storage, sizeof(storage)};
    expect(io.read(io.context, source, buffer, moved) ==
               ServiceStatus::Ok && moved == 8,
           "a read reports its count");
    expect(io.read(io.context, source, buffer, moved) ==
               ServiceStatus::Ok && moved == 0,
           "end of file is a zero count, not a failure");
    expect(io.close(io.context, source) == ServiceStatus::Ok,
           "the reading handle closes");

    Handle missing = invalid_handle;
    expect(io.open(io.context, (tree.root() / "absent").string(),
                   OpenMode::Read, missing) == ServiceStatus::NotFound &&
               missing == invalid_handle,
           "a missing path is NotFound and publishes no handle");
    expect(io.read(io.context, invalid_handle, buffer, moved) ==
               ServiceStatus::Invalid &&
               io.close(io.context, 0) == ServiceStatus::Invalid,
           "an unknown handle is Invalid rather than a host error");

    Handle read_end = invalid_handle;
    Handle write_end = invalid_handle;
    expect(io.pipe(io.context, read_end, write_end) == ServiceStatus::Ok &&
               host.open_descriptors() == 2,
           "a pipe publishes both ends");
    expect(io.write(io.context, write_end, bytes_of("piped"), moved) ==
               ServiceStatus::Ok && moved == 5 &&
               io.close(io.context, write_end) == ServiceStatus::Ok,
           "the write end carries bytes and closes");
    expect(io.read(io.context, read_end, buffer, moved) ==
               ServiceStatus::Ok && moved == 5 &&
               std::string_view{reinterpret_cast<const char*>(storage), 5} ==
                   "piped",
           "the read end sees what was written");
    expect(io.close(io.context, read_end) == ServiceStatus::Ok &&
               host.open_descriptors() == 0,
           "every descriptor is accounted for at the end");
}

void abandoned_descriptors_are_reclaimed() {
    scoped_tree tree{"posix_services_reclaim"};
    std::size_t leaked = 0;
    {
        HostServices host;
        const auto io = host.io();
        Handle first = invalid_handle;
        Handle second = invalid_handle;
        expect(io.open(io.context, (tree.root() / "a").string(),
                       OpenMode::Truncate, first) == ServiceStatus::Ok &&
                   io.pipe(io.context, second, second) == ServiceStatus::Ok,
               "two resources open");
        leaked = host.open_descriptors();
    }
    expect(leaked != 0,
           "the scope really did leave descriptors open to the destructor");
}

void directory_names_only() {
    scoped_tree tree{"posix_services_file"};
    const auto root = tree.root().string();
    write_file(root + "/build.sh", "");
    write_file(root + "/clean.sh", "");
    write_file(root + "/notes.txt", "");
    tree.manifest("src", "kind: module\nname: sample\n");

    HostServices host;
    const auto files = host.file();
    std::vector<std::string> names;
    expect(files.list(files.context, root, names) == ServiceStatus::Ok &&
               names.size() == 4 &&
               std::find(names.begin(), names.end(), "src") != names.end() &&
               std::find(names.begin(), names.end(), ".") == names.end() &&
               std::find(names.begin(), names.end(), "..") == names.end(),
           "a listing reports entries and omits dot and dot-dot");

    names.clear();
    expect(files.list(files.context, root + "/absent", names) ==
               ServiceStatus::NotFound && names.empty(),
           "listing a missing directory is NotFound");

    bool exists = false;
    bool directory = false;
    expect(files.status(files.context, root + "/src", exists, directory) ==
               ServiceStatus::Ok && exists && directory,
           "a directory reports as existing and a directory");
    expect(files.status(files.context, root + "/build.sh", exists,
                        directory) == ServiceStatus::Ok && exists &&
               !directory,
           "a file reports as existing and not a directory");
    expect(files.status(files.context, root + "/absent", exists,
                        directory) == ServiceStatus::Ok && !exists,
           "a missing name is an answer, not a service failure");
}

void pathname_expansion_over_the_host_tree() {
    scoped_tree tree{"posix_services_glob"};
    const auto root = tree.root().string();
    write_file(root + "/build.sh", "");
    write_file(root + "/clean.sh", "");
    write_file(root + "/notes.txt", "");
    write_file(root + "/.hidden", "");

    HostServices host;
    const auto matched = mm::shell::full::expand_pathname("*.sh", root,
                                                          host.file());
    expect(matched.ok() && matched.expanded && matched.matches.size() == 2 &&
               matched.matches[0] == "build.sh" &&
               matched.matches[1] == "clean.sh",
           "the abstract pathname request works over a real directory");
    const auto none = mm::shell::full::expand_pathname("*.md", root,
                                                       host.file());
    expect(none.ok() && !none.expanded,
           "an unmatched pattern over a real tree stays literal");
    const auto hidden = mm::shell::full::expand_pathname("*", root,
                                                          host.file());
    expect(hidden.expanded &&
               std::find(hidden.matches.begin(), hidden.matches.end(),
                         ".hidden") == hidden.matches.end(),
           "a real dot entry is still skipped by a bare star");
}

void spawn_distinguishes_its_failures() {
    scoped_tree tree{"posix_services_spawn"};
    const auto root = tree.root().string();
    const auto unexecutable = root + "/plain.txt";
    write_file(unexecutable, "not a program\n");

    HostServices host;
    const auto process = host.process();
    Handle child = invalid_handle;

    const std::string_view absent[]{"mm-no-such-program-exists"};
    ProcessRequest missing;
    missing.arguments = absent;
    expect(process.spawn(process.context, missing, child) ==
               ServiceStatus::NotFound && child == invalid_handle &&
               host.live_children() == 0,
           "a missing program is NotFound and reaps its own child");

    const std::string_view plain[]{unexecutable};
    ProcessRequest refused;
    refused.arguments = plain;
    expect(process.spawn(process.context, refused, child) ==
               ServiceStatus::PermissionDenied &&
               child == invalid_handle && host.live_children() == 0,
           "an unexecutable file is PermissionDenied, distinct from missing");

    const std::string_view nowhere[]{"true"};
    ProcessRequest bad_directory;
    bad_directory.arguments = nowhere;
    bad_directory.directory = root + "/absent";
    expect(process.spawn(process.context, bad_directory, child) ==
               ServiceStatus::Failed && child == invalid_handle &&
               host.live_children() == 0,
           "an infrastructure fault is Failed, distinct from both");

    ProcessRequest empty;
    expect(process.spawn(process.context, empty, child) ==
               ServiceStatus::Invalid,
           "an empty argument vector is Invalid");
}

void spawn_reports_child_status() {
    HostServices host;
    const auto process = host.process();

    const std::string_view ok[]{"true"};
    ProcessRequest succeeds;
    succeeds.arguments = ok;
    Handle child = invalid_handle;
    int status = -1;
    expect(process.spawn(process.context, succeeds, child) ==
               ServiceStatus::Ok && child != invalid_handle &&
               host.live_children() == 1,
           "a real program spawns and is tracked");
    expect(process.wait(process.context, child, status) ==
               ServiceStatus::Ok && status == 0 &&
               host.live_children() == 0,
           "waiting reports the status and releases the child");

    const std::string_view fails[]{"false"};
    ProcessRequest exits_one;
    exits_one.arguments = fails;
    expect(process.spawn(process.context, exits_one, child) ==
               ServiceStatus::Ok &&
               process.wait(process.context, child, status) ==
                   ServiceStatus::Ok && status == 1,
           "a nonzero exit is reported as itself");

    const std::string_view sleeps[]{"sleep", "30"};
    ProcessRequest blocked;
    blocked.arguments = sleeps;
    expect(process.spawn(process.context, blocked, child) ==
               ServiceStatus::Ok,
           "a long-running child spawns");
    expect(process.signal(process.context, child, SIGKILL) ==
               ServiceStatus::Ok &&
               process.wait(process.context, child, status) ==
                   ServiceStatus::Ok && status == 128 + SIGKILL,
           "a signalled child reports the shell's 128 plus signal form");
    expect(process.wait(process.context, child, status) ==
               ServiceStatus::Invalid,
           "a reaped handle is no longer valid");
}

void pipelines_run_real_processes() {
    scoped_tree tree{"posix_services_pipeline"};
    const auto path = (tree.root() / "piped.txt").string();
    HostServices host;
    const auto io = host.io();

    Handle sink = invalid_handle;
    expect(io.open(io.context, path, OpenMode::Truncate, sink) ==
               ServiceStatus::Ok,
           "the pipeline output file opens");

    const std::string_view first[]{"echo", "through"};
    const std::string_view second[]{"cat"};
    ProcessRequest stages[2];
    stages[0].arguments = first;
    stages[1].arguments = second;
    stages[1].output = sink;

    const auto outcome = mm::shell::full::run_pipeline(stages, host.all());
    expect(outcome.ok() && outcome.exit_status == 0 &&
               outcome.stages_started == 2,
           "a two-stage pipeline over real processes completes");
    expect(io.close(io.context, sink) == ServiceStatus::Ok &&
               read_file(path) == "through\n",
           "the last stage wrote what the first produced");
    expect(host.open_descriptors() == 0 && host.live_children() == 0,
           "the pipeline left no descriptor and no child behind");

    const std::string_view absent[]{"mm-no-such-program-exists"};
    ProcessRequest broken[2];
    broken[0].arguments = first;
    broken[1].arguments = absent;
    const auto failed = mm::shell::full::run_pipeline(broken, host.all());
    expect(!failed.ok() && host.open_descriptors() == 0 &&
               host.live_children() == 0,
           "a failed launch leaks no descriptor and no child handle");
}

void signal_dispositions_install_and_restore() {
    HostServices host;
    const auto signals = host.signal();
    int pending = -1;
    expect(signals.poll(signals.context, pending) == ServiceStatus::Ok &&
               pending == 0,
           "nothing is pending before anything is installed");

    expect(signals.install(signals.context, SIGTERM) ==
               ServiceStatus::Ok && host.installed_signals() == 1,
           "a disposition installs and is owned");
    expect(::raise(SIGTERM) == 0, "the process survives its own SIGTERM");
    expect(signals.poll(signals.context, pending) == ServiceStatus::Ok &&
               pending == SIGTERM,
           "the pending condition is reported");
    expect(signals.poll(signals.context, pending) == ServiceStatus::Ok &&
               pending == 0,
           "polling consumes the condition");

    expect(signals.install(signals.context, SIGTERM) ==
               ServiceStatus::Ok && host.installed_signals() == 1,
           "installing twice owns it once");
    expect(signals.restore(signals.context, SIGTERM) ==
               ServiceStatus::Ok && host.installed_signals() == 0,
           "restoring releases ownership");
    expect(signals.install(signals.context, 0) == ServiceStatus::Invalid &&
               signals.restore(signals.context, 4096) ==
                   ServiceStatus::Invalid,
           "an out-of-range condition is Invalid");
    // The destructor restores anything still installed, which the next case
    // relies on: a leaked SIGTERM handler would break an unrelated suite.
    expect(signals.install(signals.context, SIGTERM) == ServiceStatus::Ok,
           "a disposition is left installed for the destructor to restore");
}

void borrowed_descriptors_are_not_closed() {
    HostServices host;
    const auto io = host.io();
    const auto borrowed = host.borrow_descriptor(2);
    expect(borrowed != invalid_handle && host.open_descriptors() == 0,
           "a borrowed descriptor is usable but not owned");
    std::size_t moved = 0;
    expect(io.write(io.context, borrowed, bytes_of(""), moved) ==
               ServiceStatus::Ok,
           "a borrowed descriptor accepts a write");
    expect(io.close(io.context, borrowed) == ServiceStatus::Ok,
           "closing a borrowed handle releases only the slot");
    expect(host.borrow_descriptor(-1) == invalid_handle,
           "a negative descriptor cannot be borrowed");
}

const mm::test::case_ cases[]{
    {"descriptor round trip", &descriptors_round_trip_and_balance},
    {"abandoned descriptors", &abandoned_descriptors_are_reclaimed},
    {"directory names", &directory_names_only},
    {"pathname over host tree", &pathname_expansion_over_the_host_tree},
    {"spawn failure classes", &spawn_distinguishes_its_failures},
    {"child status", &spawn_reports_child_status},
    {"real pipelines", &pipelines_run_real_processes},
    {"signal dispositions", &signal_dispositions_install_and_restore},
    {"borrowed descriptors", &borrowed_descriptors_are_not_closed},
};

const mm::test::registrar reg{"mm.shell.posix services", cases};

}  // namespace
