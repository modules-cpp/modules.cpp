// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
module;

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <span>
#include <string_view>
#include <system_error>

export module platform.linux.fs;

import mm.fs;
import mm.fs.local;
import mm.fs.native;
import platform.linux.map;

export namespace platform::linux::fs_testing {

// The Status a std::filesystem or stream error maps to: the table the
// provider uses for every failure the host reports.
[[nodiscard]] mm::fs::Status status_of(std::error_code error);

}  // namespace platform::linux::fs_testing

// A named, non-exported namespace rather than an unnamed one, as
// docs/modules-c++20.mdy requires of a provider's objects in an interface unit.
namespace platform::linux::fs_provider {

// A directory of the host's file system as an mm.fs volume.
class DirectoryVolume final : public mm::fs::Volume {
public:
    [[nodiscard]] bool in_use() const { return in_use_; }
    [[nodiscard]] mm::fs::Status attach(const std::filesystem::path& root, bool read_only,
                                        bool contain_symlinks);
    void detach();

    [[nodiscard]] mm::fs::Status open(std::string_view path, mm::fs::Access access,
                                      mm::fs::Disposition disposition,
                                      mm::fs::Handle& handle) override;
    [[nodiscard]] mm::fs::Status read(mm::fs::Handle handle, std::span<std::byte> into,
                                      std::size_t& count) override;
    [[nodiscard]] mm::fs::Status write(mm::fs::Handle handle, std::span<const std::byte> from,
                                       std::size_t& count) override;
    [[nodiscard]] mm::fs::Status seek(mm::fs::Handle handle, std::uint64_t offset) override;
    [[nodiscard]] mm::fs::Status tell(mm::fs::Handle handle, std::uint64_t& offset) override;
    [[nodiscard]] mm::fs::Status truncate(mm::fs::Handle handle) override;
    [[nodiscard]] mm::fs::Status sync(mm::fs::Handle handle) override;
    [[nodiscard]] mm::fs::Status file_stat(mm::fs::Handle handle, mm::fs::Stat& stat) override;
    [[nodiscard]] mm::fs::Status close(mm::fs::Handle handle) override;

    [[nodiscard]] mm::fs::Status open_directory(std::string_view path,
                                                mm::fs::Handle& handle) override;
    [[nodiscard]] mm::fs::Status next(mm::fs::Handle handle, std::span<char> name,
                                      std::size_t& length, mm::fs::Stat& stat,
                                      bool& done) override;
    [[nodiscard]] mm::fs::Status close_directory(mm::fs::Handle handle) override;

    [[nodiscard]] mm::fs::Status stat(std::string_view path, mm::fs::Stat& stat) override;
    [[nodiscard]] mm::fs::Status make_directory(std::string_view path) override;
    [[nodiscard]] mm::fs::Status remove(std::string_view path) override;
    [[nodiscard]] mm::fs::Status rename(std::string_view from, std::string_view to) override;
    [[nodiscard]] mm::fs::Status space(mm::fs::Space& space) override;
    [[nodiscard]] mm::fs::Status flush() override;

private:
    struct FileSlot {
        bool used = false;
        std::filebuf buffer;
        std::filesystem::path path;
        mm::fs::Access access = mm::fs::Access::Read;
        bool writing = false;    // the last transfer was a write
    };
    struct DirectorySlot {
        bool used = false;
        std::filesystem::path path;
        std::filesystem::directory_iterator iterator;
    };

    [[nodiscard]] mm::fs::Status locate(std::string_view relative,
                                        std::filesystem::path& full) const;
    [[nodiscard]] mm::fs::Status describe(const std::filesystem::path& full,
                                          mm::fs::Stat& stat) const;
    [[nodiscard]] bool open_on(const std::filesystem::path& full, bool writers_only) const;
    [[nodiscard]] bool open_beneath(const std::filesystem::path& full) const;
    [[nodiscard]] FileSlot* file(mm::fs::Handle handle);

    std::filesystem::path root_;
    bool read_only_ = false;
    bool contain_symlinks_ = true;
    bool in_use_ = false;
    std::array<FileSlot, 32> files_;
    std::array<DirectorySlot, 8> directories_;
};

// mm.fs.local: the device map's directory.0, or the working directory.
class LocalProvider final : public mm::fs::local::Provider {
public:
    [[nodiscard]] mm::fs::Status attach(const mm::fs::local::Options& options,
                                        mm::fs::Volume*& volume) override;
    [[nodiscard]] mm::fs::Status detach(mm::fs::Volume& volume) override;
};

// mm.fs.native: any absolute directory.
class NativeProvider final : public mm::fs::native::Provider {
public:
    [[nodiscard]] mm::fs::Status attach(std::string_view root,
                                        const mm::fs::native::Options& options,
                                        mm::fs::Volume*& volume) override;
    [[nodiscard]] mm::fs::Status detach(mm::fs::Volume& volume) override;
};

std::array<DirectoryVolume, 4> volumes;
LocalProvider local;
NativeProvider native;

struct Register {
    Register() {
        mm::fs::local::set_provider(local);
        mm::fs::native::set_provider(native);
    }
};

const Register registered;

}  // namespace platform::linux::fs_provider
