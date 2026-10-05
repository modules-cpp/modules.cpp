// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
module;

#include <array>
#include <cstddef>
#include <span>
#include <string>
#include <string_view>
#include <vector>

export module mm.fs.shell;

import mm.fs;
import mm.shell.full;

export namespace mm::fs::shell {

class FileServices {
public:
    // base serves every handle this object did not open, and pipes.
    explicit FileServices(mm::shell::full::IoService base = {});
    ~FileServices() = default;
    FileServices(const FileServices&) = delete;
    FileServices& operator=(const FileServices&) = delete;
    FileServices(FileServices&&) = delete;
    FileServices& operator=(FileServices&&) = delete;

    // Where a relative path resolves: an absolute mm.fs path. False, with
    // nothing changed, for one that is not.
    [[nodiscard]] bool set_directory(std::string_view directory);
    [[nodiscard]] const std::string& directory() const { return directory_; }

    [[nodiscard]] mm::shell::full::IoService io();
    [[nodiscard]] mm::shell::full::FileService file();
    // others with io and file replaced by this object's.
    [[nodiscard]] mm::shell::full::Services all(mm::shell::full::Services others = {});

    // Files still open, for a leak assertion after a failure.
    [[nodiscard]] std::size_t open_files() const;
    void close_all();

    // The handles this object hands out carry this bit, which no base
    // handle of the kind the shell uses does.
    static constexpr mm::shell::full::Handle own = 0x4000'0000ul;

private:
    static mm::shell::full::ServiceStatus open_callback(void*, std::string_view,
                                                        mm::shell::full::OpenMode,
                                                        mm::shell::full::Handle&);
    static mm::shell::full::ServiceStatus pipe_callback(void*, mm::shell::full::Handle&,
                                                        mm::shell::full::Handle&);
    static mm::shell::full::ServiceStatus read_callback(void*, mm::shell::full::Handle,
                                                        std::span<std::byte>, std::size_t&);
    static mm::shell::full::ServiceStatus write_callback(void*, mm::shell::full::Handle,
                                                         std::span<const std::byte>,
                                                         std::size_t&);
    static mm::shell::full::ServiceStatus close_callback(void*, mm::shell::full::Handle);
    static mm::shell::full::ServiceStatus list_callback(void*, std::string_view,
                                                        std::vector<std::string>&);
    static mm::shell::full::ServiceStatus status_callback(void*, std::string_view, bool&,
                                                          bool&);
    static mm::shell::full::ServiceStatus canonical_callback(void*, std::string_view,
                                                             std::string&);
    static mm::shell::full::ServiceStatus test_callback(void*, std::string_view,
                                                        mm::shell::full::FilePredicate, bool&);

    [[nodiscard]] bool resolve(std::string_view path, std::string& out) const;
    [[nodiscard]] mm::fs::File* owned(mm::shell::full::Handle handle);

    std::array<mm::fs::File, 8> files_;
    mm::shell::full::IoService base_;
    std::string directory_ = "/";
};

}  // namespace mm::fs::shell
