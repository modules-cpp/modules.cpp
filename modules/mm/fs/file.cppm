// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
module;

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

export module mm.fs:file;

import :status;
import :types;
import :volume;

export namespace mm::fs {

// Attaches a volume at "/" or "/" followed by one component, such as "/usb".
// BadArgument for any other shape; Exists when the prefix is mounted; Busy
// when the volume is already mounted elsewhere; TooMany when all max_mounts
// slots are used. The caller owns the volume and keeps it alive until unmount.
[[nodiscard]] Status mount(std::string_view prefix, Volume& volume);

// Detaches it, after asking the volume to flush; a failed flush is returned
// and the volume stays mounted. Busy while any File or Directory on it is
// open; NotFound when nothing is mounted there.
[[nodiscard]] Status unmount(std::string_view prefix);

// The volume mounted at prefix, which a mount interface uses to find what it
// attached. NotFound when nothing is mounted there; volume changes only on Ok.
[[nodiscard]] Status mounted(std::string_view prefix, Volume*& volume);

class File;
class Directory;

// Opens path. BadArgument when file is already open: close it first.
[[nodiscard]] Status open(std::string_view path, Access access,
                          Disposition disposition, File& file);
[[nodiscard]] Status open_directory(std::string_view path, Directory& directory);

[[nodiscard]] Status stat(std::string_view path, Stat& stat);
[[nodiscard]] Status make_directory(std::string_view path);  // the parent must exist
[[nodiscard]] Status remove(std::string_view path);          // a file, or an empty directory
// Within one volume, CrossVolume otherwise; Exists rather than replacing a target.
[[nodiscard]] Status rename(std::string_view from, std::string_view to);
[[nodiscard]] Status space(std::string_view path, Space& space);

// An open file: a mount slot, the driver's handle, and the access it was
// opened with. Move-only; the destructor closes, ignoring the result, so a
// program that cares about a failed flush calls close and checks it. Every
// call on a closed File is BadArgument, and so is a read on a file opened
// Write or Append, or a write or truncate on one opened Read.
class File {
public:
    File() = default;
    File(File&& other) noexcept;
    File& operator=(File&& other) noexcept;
    File(const File&) = delete;
    File& operator=(const File&) = delete;
    ~File();

    [[nodiscard]] bool is_open() const;

    // Up to into.size() bytes from the current offset. A count short of the
    // request, including zero, means end of file.
    [[nodiscard]] Status read(std::span<std::byte> into, std::size_t& count);

    // Up to from.size() bytes at the current offset, or at the end for
    // Access::Append. A short count comes with NoSpace, and count is set then
    // too; otherwise counts and offsets change only on Ok.
    [[nodiscard]] Status write(std::span<const std::byte> from, std::size_t& count);

    // Absolute. Past the end is allowed for a writable file and extends it
    // with zeros on the next write; for a read-only one it is BadArgument.
    [[nodiscard]] Status seek(std::uint64_t offset);
    [[nodiscard]] Status tell(std::uint64_t& offset) const;

    [[nodiscard]] Status truncate();          // cut the file at the current offset
    [[nodiscard]] Status sync();              // make what was written durable
    [[nodiscard]] Status stat(Stat& stat) const;

    // Syncs and frees the handle. The File is closed afterwards whatever the
    // result, because the driver frees its handle either way.
    [[nodiscard]] Status close();

private:
    friend Status open(std::string_view, Access, Disposition, File&);

    void release();

    Volume* volume_ = nullptr;
    Handle handle_ = 0;
    unsigned int slot_ = 0;
    Access access_ = Access::Read;
};

// An open directory. Move-only and closed on destruction, as File.
class Directory {
public:
    Directory() = default;
    Directory(Directory&& other) noexcept;
    Directory& operator=(Directory&& other) noexcept;
    Directory(const Directory&) = delete;
    Directory& operator=(const Directory&) = delete;
    ~Directory();

    [[nodiscard]] bool is_open() const;

    // The next entry, never "." or "..", in the volume's order. Its name goes
    // into name, length is its size, and stat describes it. done is true,
    // with nothing else set, once the entries are exhausted. NameTooLong when
    // name is too small for the entry, which is then not consumed, so a
    // larger buffer can retry; max_name bytes always suffice.
    [[nodiscard]] Status next(std::span<char> name, std::size_t& length, Stat& stat,
                              bool& done);

    [[nodiscard]] Status close();

private:
    friend Status open_directory(std::string_view, Directory&);

    void release();

    Volume* volume_ = nullptr;
    Handle handle_ = 0;
    unsigned int slot_ = 0;
};

}
