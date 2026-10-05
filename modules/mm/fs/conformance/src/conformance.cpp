// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
module;

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

module mm.fs.conformance;

import mm.fs;

namespace mm::fs::conformance {

namespace {

// A path built from pieces in fixed storage, so the checks run on a target
// without a heap. A result that would not fit is cut short, and the call
// that uses it then fails, which fails the check rather than overrunning.
class Path {
public:
    Path(std::string_view first, std::string_view second, std::string_view third = {}) {
        append(first);
        append("/");
        append(second);
        if (!third.empty()) {
            append("/");
            append(third);
        }
    }

    [[nodiscard]] std::string_view view() const { return {data_.data(), length_}; }

private:
    void append(std::string_view piece) {
        const auto room = data_.size() - length_;
        const auto count = std::min(room, piece.size());
        std::copy_n(piece.begin(), count, data_.begin() + length_);
        length_ += count;
    }

    std::array<char, max_path + 1> data_{};
    std::size_t length_ = 0;
};

[[nodiscard]] std::span<const std::byte> bytes(std::string_view text) {
    return std::as_bytes(std::span<const char>{text.data(), text.size()});
}

[[nodiscard]] bool write_file(std::string_view path, std::string_view text,
                              Disposition disposition = Disposition::CreateOrTruncate) {
    File file;
    if (open(path, Access::Write, disposition, file) != Status::Ok) return false;
    std::size_t written = 0;
    if (file.write(bytes(text), written) != Status::Ok || written != text.size()) return false;
    return file.close() == Status::Ok;
}

// Reads at most 64 bytes, which every check's file fits in.
struct Contents {
    std::array<std::byte, 64> data{};
    std::size_t size = 0;

    [[nodiscard]] bool equals(std::string_view text) const {
        if (size != text.size()) return false;
        for (std::size_t i = 0; i < size; ++i)
            if (data[i] != static_cast<std::byte>(text[i])) return false;
        return true;
    }
};

[[nodiscard]] bool read_file(std::string_view path, Contents& contents) {
    File file;
    if (open(path, Access::Read, Disposition::OpenExisting, file) != Status::Ok) return false;
    contents.size = 0;
    if (file.read(contents.data, contents.size) != Status::Ok) return false;
    return file.close() == Status::Ok;
}

[[nodiscard]] bool file_holds(std::string_view path, std::string_view text) {
    Contents contents;
    return read_file(path, contents) && contents.equals(text);
}

// Removes path and everything beneath it. One entry at a time, closing the
// directory before each removal, because a volume need not allow removing
// entries from a directory that is being listed.
Status remove_tree(std::string_view path) {
    Stat stat;
    const auto found = mm::fs::stat(path, stat);
    if (found != Status::Ok) return found;
    if (stat.kind == Kind::File) return remove(path);
    while (true) {
        std::array<char, max_name> name{};
        std::size_t length = 0;
        Stat entry;
        bool done = false;
        {
            Directory directory;
            const auto opened = open_directory(path, directory);
            if (opened != Status::Ok) return opened;
            const auto listed = directory.next(name, length, entry, done);
            if (listed != Status::Ok) return listed;
            static_cast<void>(directory.close());
        }
        if (done) break;
        const Path child{path, {name.data(), length}};
        const auto removed = remove_tree(child.view());
        if (removed != Status::Ok) return removed;
    }
    return remove(path);
}

using Check = bool (*)(std::string_view base);

bool create_new_creates(std::string_view base) {
    const Path path{base, "create-new"};
    File file;
    if (open(path.view(), Access::Write, Disposition::CreateNew, file) != Status::Ok)
        return false;
    if (file.close() != Status::Ok) return false;
    Stat stat;
    return mm::fs::stat(path.view(), stat) == Status::Ok && stat.kind == Kind::File &&
           stat.size == 0;
}

bool create_new_refuses_present(std::string_view base) {
    const Path path{base, "present"};
    if (!write_file(path.view(), "x")) return false;
    File file;
    return open(path.view(), Access::Write, Disposition::CreateNew, file) == Status::Exists &&
           !file.is_open() && file_holds(path.view(), "x");
}

bool open_existing_refuses_absent(std::string_view base) {
    const Path absent{base, "absent"};
    const Path orphan{base, "no-such-directory", "file"};
    File file;
    return open(absent.view(), Access::Read, Disposition::OpenExisting, file) ==
               Status::NotFound &&
           open(orphan.view(), Access::Write, Disposition::CreateNew, file) ==
               Status::NotFound &&
           !file.is_open();
}

bool write_reads_back(std::string_view base) {
    const Path path{base, "round-trip"};
    File file;
    if (open(path.view(), Access::ReadWrite, Disposition::CreateNew, file) != Status::Ok)
        return false;
    std::size_t written = 0;
    if (file.write(bytes("hello"), written) != Status::Ok || written != 5) return false;
    std::uint64_t offset = 0;
    if (file.tell(offset) != Status::Ok || offset != 5) return false;
    if (file.seek(0) != Status::Ok) return false;
    Contents contents;
    if (file.read(std::span{contents.data}.first(5), contents.size) != Status::Ok) return false;
    return contents.equals("hello") && file.close() == Status::Ok;
}

bool read_past_end_is_short(std::string_view base) {
    const Path path{base, "short"};
    if (!write_file(path.view(), "abc")) return false;
    File file;
    if (open(path.view(), Access::Read, Disposition::OpenExisting, file) != Status::Ok)
        return false;
    Contents contents;
    if (file.read(contents.data, contents.size) != Status::Ok || !contents.equals("abc"))
        return false;
    std::size_t again = 99;
    return file.read(contents.data, again) == Status::Ok && again == 0 &&
           file.close() == Status::Ok;
}

bool append_writes_at_end(std::string_view base) {
    const Path path{base, "append"};
    if (!write_file(path.view(), "hello")) return false;
    File file;
    if (open(path.view(), Access::Append, Disposition::OpenExisting, file) != Status::Ok)
        return false;
    if (file.seek(0) != Status::Ok) return false;
    std::size_t written = 0;
    if (file.write(bytes(" world"), written) != Status::Ok || written != 6) return false;
    if (file.close() != Status::Ok) return false;
    return file_holds(path.view(), "hello world");
}

bool write_past_end_extends(std::string_view base) {
    const Path path{base, "extend"};
    if (!write_file(path.view(), "ab")) return false;
    File file;
    if (open(path.view(), Access::ReadWrite, Disposition::OpenExisting, file) != Status::Ok)
        return false;
    if (file.seek(5) != Status::Ok) return false;
    std::size_t written = 0;
    if (file.write(bytes("z"), written) != Status::Ok || written != 1) return false;
    Stat stat;
    if (file.stat(stat) != Status::Ok || stat.size != 6) return false;
    if (file.close() != Status::Ok) return false;
    Contents contents;
    if (!read_file(path.view(), contents) || contents.size != 6) return false;
    return contents.data[0] == std::byte{'a'} && contents.data[1] == std::byte{'b'} &&
           contents.data[2] == std::byte{0} && contents.data[3] == std::byte{0} &&
           contents.data[4] == std::byte{0} && contents.data[5] == std::byte{'z'};
}

bool truncate_cuts_at_offset(std::string_view base) {
    const Path path{base, "truncate"};
    if (!write_file(path.view(), "abcdef")) return false;
    File file;
    if (open(path.view(), Access::ReadWrite, Disposition::OpenExisting, file) != Status::Ok)
        return false;
    if (file.seek(2) != Status::Ok || file.truncate() != Status::Ok) return false;
    Stat stat;
    if (file.stat(stat) != Status::Ok || stat.size != 2) return false;
    if (file.close() != Status::Ok) return false;
    return file_holds(path.view(), "ab");
}

bool create_or_truncate_empties(std::string_view base) {
    const Path path{base, "empties"};
    if (!write_file(path.view(), "something")) return false;
    File file;
    if (open(path.view(), Access::Write, Disposition::CreateOrTruncate, file) != Status::Ok)
        return false;
    if (file.close() != Status::Ok) return false;
    Stat stat;
    return mm::fs::stat(path.view(), stat) == Status::Ok && stat.size == 0;
}

bool open_or_create_creates_and_keeps(std::string_view base) {
    const Path path{base, "open-or-create"};
    if (!write_file(path.view(), "x", Disposition::OpenOrCreate)) return false;
    File file;
    if (open(path.view(), Access::ReadWrite, Disposition::OpenOrCreate, file) != Status::Ok)
        return false;
    Stat stat;
    if (file.stat(stat) != Status::Ok || stat.size != 1) return false;
    return file.close() == Status::Ok && file_holds(path.view(), "x");
}

bool synced_file_reads_back(std::string_view base) {
    const Path path{base, "synced"};
    File file;
    if (open(path.view(), Access::Write, Disposition::CreateNew, file) != Status::Ok)
        return false;
    std::size_t written = 0;
    if (file.write(bytes("kept"), written) != Status::Ok || file.sync() != Status::Ok)
        return false;
    Stat stat;
    if (file.stat(stat) != Status::Ok || stat.size != 4) return false;
    return file.close() == Status::Ok && file_holds(path.view(), "kept");
}

bool stat_reports_kind_and_size(std::string_view base) {
    const Path file{base, "sized"};
    const Path directory{base, "a-directory"};
    if (!write_file(file.view(), "12345")) return false;
    if (make_directory(directory.view()) != Status::Ok) return false;
    Stat of_file;
    Stat of_directory;
    Stat of_missing;
    const Path missing{base, "missing"};
    return mm::fs::stat(file.view(), of_file) == Status::Ok && of_file.kind == Kind::File &&
           of_file.size == 5 &&
           mm::fs::stat(directory.view(), of_directory) == Status::Ok &&
           of_directory.kind == Kind::Directory &&
           mm::fs::stat(missing.view(), of_missing) == Status::NotFound;
}

bool access_is_enforced(std::string_view base) {
    const Path path{base, "access"};
    if (!write_file(path.view(), "abc")) return false;
    std::size_t count = 0;
    {
        File reader;
        if (open(path.view(), Access::Read, Disposition::OpenExisting, reader) != Status::Ok)
            return false;
        if (reader.write(bytes("x"), count) != Status::BadArgument) return false;
        if (reader.truncate() != Status::BadArgument) return false;
        if (reader.close() != Status::Ok) return false;
    }
    File writer;
    if (open(path.view(), Access::Write, Disposition::OpenExisting, writer) != Status::Ok)
        return false;
    Contents contents;
    if (writer.read(contents.data, count) != Status::BadArgument) return false;
    return writer.close() == Status::Ok && file_holds(path.view(), "abc");
}

bool read_file_refuses_seek_past_end(std::string_view base) {
    const Path path{base, "read-seek"};
    if (!write_file(path.view(), "abc")) return false;
    File file;
    if (open(path.view(), Access::Read, Disposition::OpenExisting, file) != Status::Ok)
        return false;
    return file.seek(3) == Status::Ok && file.seek(4) == Status::BadArgument &&
           file.close() == Status::Ok;
}

bool kinds_are_refused(std::string_view base) {
    const Path directory{base, "kind-directory"};
    const Path file{base, "kind-file"};
    if (make_directory(directory.view()) != Status::Ok) return false;
    if (!write_file(file.view(), "x")) return false;
    File opened;
    Directory listed;
    return open(directory.view(), Access::Read, Disposition::OpenExisting, opened) ==
               Status::IsDirectory &&
           open_directory(file.view(), listed) == Status::NotDirectory && !opened.is_open() &&
           !listed.is_open();
}

bool directory_lists_entries(std::string_view base) {
    const Path directory{base, "listing"};
    if (make_directory(directory.view()) != Status::Ok) return false;
    const Path first{directory.view(), "first"};
    const Path second{directory.view(), "second"};
    const Path inner{directory.view(), "inner"};
    if (!write_file(first.view(), "1") || !write_file(second.view(), "22")) return false;
    if (make_directory(inner.view()) != Status::Ok) return false;

    Directory listing;
    if (open_directory(directory.view(), listing) != Status::Ok) return false;
    bool saw_first = false;
    bool saw_second = false;
    bool saw_inner = false;
    unsigned int entries = 0;
    while (true) {
        std::array<char, max_name> name{};
        std::size_t length = 0;
        Stat stat;
        bool done = false;
        if (listing.next(name, length, stat, done) != Status::Ok) return false;
        if (done) break;
        ++entries;
        const std::string_view seen{name.data(), length};
        if (seen == "first") saw_first = stat.kind == Kind::File && stat.size == 1;
        else if (seen == "second") saw_second = stat.kind == Kind::File && stat.size == 2;
        else if (seen == "inner") saw_inner = stat.kind == Kind::Directory;
        else return false;    // ".", "..", or something that was never made
        if (entries > 3) return false;
    }
    return listing.close() == Status::Ok && entries == 3 && saw_first && saw_second &&
           saw_inner;
}

bool small_name_buffer_retries(std::string_view base) {
    const Path directory{base, "retry"};
    if (make_directory(directory.view()) != Status::Ok) return false;
    const Path only{directory.view(), "a-longer-name"};
    if (!write_file(only.view(), "x")) return false;

    Directory listing;
    if (open_directory(directory.view(), listing) != Status::Ok) return false;
    std::array<char, 4> small{};
    std::size_t length = 0;
    Stat stat;
    bool done = false;
    if (listing.next(small, length, stat, done) != Status::NameTooLong) return false;
    std::array<char, max_name> large{};
    if (listing.next(large, length, stat, done) != Status::Ok || done) return false;
    if (std::string_view{large.data(), length} != "a-longer-name") return false;
    if (listing.next(large, length, stat, done) != Status::Ok || !done) return false;
    return listing.close() == Status::Ok;
}

bool make_directory_refuses(std::string_view base) {
    const Path directory{base, "made"};
    const Path orphan{base, "no-parent", "child"};
    return make_directory(directory.view()) == Status::Ok &&
           make_directory(directory.view()) == Status::Exists &&
           make_directory(orphan.view()) == Status::NotFound;
}

bool remove_refuses_and_removes(std::string_view base) {
    const Path directory{base, "full"};
    const Path inside{directory.view(), "inside"};
    const Path missing{base, "never-made"};
    if (make_directory(directory.view()) != Status::Ok) return false;
    if (!write_file(inside.view(), "x")) return false;
    if (remove(directory.view()) != Status::NotEmpty) return false;
    if (remove(missing.view()) != Status::NotFound) return false;
    if (remove(inside.view()) != Status::Ok || remove(directory.view()) != Status::Ok)
        return false;
    Stat stat;
    return mm::fs::stat(directory.view(), stat) == Status::NotFound;
}

bool rename_moves_and_refuses(std::string_view base) {
    const Path from{base, "rename-from"};
    const Path to{base, "rename-to"};
    const Path taken{base, "rename-taken"};
    if (!write_file(from.view(), "moved") || !write_file(taken.view(), "stays")) return false;
    if (rename(from.view(), taken.view()) != Status::Exists) return false;
    if (!file_holds(taken.view(), "stays") || !file_holds(from.view(), "moved")) return false;
    if (rename(from.view(), to.view()) != Status::Ok) return false;
    Stat stat;
    return mm::fs::stat(from.view(), stat) == Status::NotFound && file_holds(to.view(), "moved");
}

bool open_for_writing_is_exclusive(std::string_view base) {
    const Path path{base, "exclusive"};
    const Path elsewhere{base, "exclusive-moved"};
    if (!write_file(path.view(), "x")) return false;
    File writer;
    if (open(path.view(), Access::Write, Disposition::OpenExisting, writer) != Status::Ok)
        return false;
    File second;
    if (open(path.view(), Access::Read, Disposition::OpenExisting, second) != Status::Busy)
        return false;
    if (remove(path.view()) != Status::Busy) return false;
    if (rename(path.view(), elsewhere.view()) != Status::Busy) return false;
    if (writer.close() != Status::Ok) return false;
    return remove(path.view()) == Status::Ok;
}

bool open_for_reading_is_shared(std::string_view base) {
    const Path path{base, "shared"};
    if (!write_file(path.view(), "x")) return false;
    File first;
    File second;
    if (open(path.view(), Access::Read, Disposition::OpenExisting, first) != Status::Ok)
        return false;
    if (open(path.view(), Access::Read, Disposition::OpenExisting, second) != Status::Ok)
        return false;
    File writer;
    if (open(path.view(), Access::Write, Disposition::OpenExisting, writer) != Status::Busy)
        return false;
    return first.close() == Status::Ok && second.close() == Status::Ok;
}

bool dot_dot_reaches_same_file(std::string_view base) {
    const Path directory{base, "dots"};
    const Path target{base, "dots-target"};
    if (make_directory(directory.view()) != Status::Ok) return false;
    if (!write_file(target.view(), "found")) return false;
    const Path through{directory.view(), "../dots-target"};
    const Path dotted{directory.view(), "./.././dots-target"};
    return file_holds(through.view(), "found") && file_holds(dotted.view(), "found");
}

bool space_is_sane(std::string_view base) {
    Space space;
    const auto status = mm::fs::space(base, space);
    if (status == Status::Unsupported) return true;   // a volume may not know
    return status == Status::Ok && space.total >= space.free;
}

bool closed_file_is_refused(std::string_view base) {
    const Path path{base, "closed"};
    if (!write_file(path.view(), "x")) return false;
    File file;
    if (open(path.view(), Access::ReadWrite, Disposition::OpenExisting, file) != Status::Ok)
        return false;
    if (file.close() != Status::Ok || file.is_open()) return false;
    std::size_t count = 7;
    std::uint64_t offset = 7;
    Stat stat;
    Contents contents;
    return file.read(contents.data, count) == Status::BadArgument &&
           file.write(bytes("y"), count) == Status::BadArgument && count == 7 &&
           file.seek(0) == Status::BadArgument && file.tell(offset) == Status::BadArgument &&
           offset == 7 && file.truncate() == Status::BadArgument &&
           file.sync() == Status::BadArgument && file.stat(stat) == Status::BadArgument &&
           file.close() == Status::BadArgument;
}

struct Entry {
    std::string_view description;
    Check check;
};

constexpr Entry checks[] = {
    {"CreateNew creates an absent file", &create_new_creates},
    {"CreateNew refuses a present file with Exists", &create_new_refuses_present},
    {"OpenExisting refuses an absent file, and a missing parent, with NotFound",
     &open_existing_refuses_absent},
    {"a write reads back after a seek to the start", &write_reads_back},
    {"a read past the end is short, then zero", &read_past_end_is_short},
    {"Append writes at the end even after a seek", &append_writes_at_end},
    {"a write past the end extends the file with zeros", &write_past_end_extends},
    {"truncate cuts the file at the current offset", &truncate_cuts_at_offset},
    {"CreateOrTruncate empties a present file", &create_or_truncate_empties},
    {"OpenOrCreate creates an absent file and keeps a present one",
     &open_or_create_creates_and_keeps},
    {"a synced file reports its size and reads back after close", &synced_file_reads_back},
    {"stat reports a file's size, a directory's kind, and NotFound",
     &stat_reports_kind_and_size},
    {"a Read file refuses write and truncate, a Write file refuses read",
     &access_is_enforced},
    {"a Read file refuses a seek past its end", &read_file_refuses_seek_past_end},
    {"open of a directory is IsDirectory, open_directory of a file NotDirectory",
     &kinds_are_refused},
    {"a directory lists its entries without . or ..", &directory_lists_entries},
    {"next with a small buffer is NameTooLong and does not consume the entry",
     &small_name_buffer_retries},
    {"make_directory refuses a present name and a missing parent", &make_directory_refuses},
    {"remove refuses a non-empty directory and a missing name, and removes",
     &remove_refuses_and_removes},
    {"rename moves a file and refuses an existing target", &rename_moves_and_refuses},
    {"a file open for writing cannot be opened again, removed, or renamed",
     &open_for_writing_is_exclusive},
    {"a file open for reading can be opened for reading again, not for writing",
     &open_for_reading_is_shared},
    {"a path through . and .. reaches the same file", &dot_dot_reaches_same_file},
    {"space reports total no smaller than free", &space_is_sane},
    {"every call on a closed File is BadArgument", &closed_file_is_refused},
};

}  // namespace

Status run(std::string_view prefix, Report& report) {
    const Path base{prefix == "/" ? std::string_view{} : prefix, scratch_name};
    const auto cleared = remove_tree(base.view());
    if (cleared != Status::Ok && cleared != Status::NotFound) return cleared;
    const auto made = make_directory(base.view());
    if (made != Status::Ok) return made;

    Report result;
    for (const auto& entry : checks) {
        if (entry.check(base.view())) {
            ++result.passed;
        } else {
            ++result.failed;
            if (result.first_failure.empty()) result.first_failure = entry.description;
        }
    }
    static_cast<void>(remove_tree(base.view()));
    report = result;
    return Status::Ok;
}

}  // namespace mm::fs::conformance
