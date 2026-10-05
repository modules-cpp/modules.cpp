// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
module;

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

export module mm.fs:volume;

import :status;
import :types;

export namespace mm::fs {

// One mounted volume: the seam a file-system driver implements. Every method
// answers Unsupported by default, so a driver implements what it has.
//
// Handles are the driver's own numbers. Paths are volume-relative and already
// normalised by mm.fs: no leading '/', no "." or "..", "" for the volume's
// root. mm.fs has already refused a read or write the File's access forbids.
//
// Drivers keep these rules, which the conformance suite checks: a file open
// for writing cannot be opened again, removed, or renamed (Busy); a file open
// for reading can be opened again for reading; rename never replaces an
// existing target (Exists); close frees the handle even when it fails.
class Volume {
public:
    virtual ~Volume() = default;

    [[nodiscard]] virtual Status open(std::string_view, Access, Disposition, Handle&) {
        return Status::Unsupported;
    }
    [[nodiscard]] virtual Status read(Handle, std::span<std::byte>, std::size_t&) {
        return Status::Unsupported;
    }
    // On NoSpace the count says how much was written before the volume filled.
    [[nodiscard]] virtual Status write(Handle, std::span<const std::byte>, std::size_t&) {
        return Status::Unsupported;
    }
    [[nodiscard]] virtual Status seek(Handle, std::uint64_t) { return Status::Unsupported; }
    [[nodiscard]] virtual Status tell(Handle, std::uint64_t&) { return Status::Unsupported; }
    [[nodiscard]] virtual Status truncate(Handle) { return Status::Unsupported; }
    [[nodiscard]] virtual Status sync(Handle) { return Status::Unsupported; }
    [[nodiscard]] virtual Status file_stat(Handle, Stat&) { return Status::Unsupported; }
    [[nodiscard]] virtual Status close(Handle) { return Status::Unsupported; }

    [[nodiscard]] virtual Status open_directory(std::string_view, Handle&) {
        return Status::Unsupported;
    }
    [[nodiscard]] virtual Status next(Handle, std::span<char>, std::size_t&, Stat&, bool&) {
        return Status::Unsupported;
    }
    [[nodiscard]] virtual Status close_directory(Handle) { return Status::Unsupported; }

    [[nodiscard]] virtual Status stat(std::string_view, Stat&) { return Status::Unsupported; }
    [[nodiscard]] virtual Status make_directory(std::string_view) {
        return Status::Unsupported;
    }
    [[nodiscard]] virtual Status remove(std::string_view) { return Status::Unsupported; }
    [[nodiscard]] virtual Status rename(std::string_view, std::string_view) {
        return Status::Unsupported;
    }
    [[nodiscard]] virtual Status space(Space&) { return Status::Unsupported; }

    // unmount asks for this before detaching; Ok when nothing is pending.
    [[nodiscard]] virtual Status flush() { return Status::Ok; }
};

}
