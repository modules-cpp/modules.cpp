// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
module;

#include "../fat-cxx.h"
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

export module platform.linux.fs.fat;

import mm.fs;
import mm.fs.fat;

// A named, non-exported namespace, as docs/modules-c++20.mdy requires of a
// provider's objects in an interface unit.
namespace platform::linux::fat_provider {

// One attached FAT volume, seen through the adapter's ABI.
class FatVolume final : public mm::fs::Volume {
public:
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

    bool used = false;
    unsigned int index = 0;          // the adapter's volume number
    mm_linux_fat_device device{};     // must outlive the attachment
};

// Attaches FAT on device in a free volume object.
[[nodiscard]] mm::fs::Status attach_volume(mm::fs::BlockDevice& device, bool read_only,
                                           mm::fs::Volume*& volume);
[[nodiscard]] mm::fs::Status detach_volume(mm::fs::Volume& volume);
[[nodiscard]] mm_linux_fat_device device_record(mm::fs::BlockDevice& device);
[[nodiscard]] mm::fs::Status from(int code);
[[nodiscard]] int format_code(mm::fs::fat::Format format);

class FatProvider final : public mm::fs::fat::Provider {
public:
    [[nodiscard]] mm::fs::Status attach(mm::fs::BlockDevice& device,
                                        const mm::fs::fat::Options& options,
                                        mm::fs::Volume*& volume) override {
        return attach_volume(device, options.read_only, volume);
    }
    [[nodiscard]] mm::fs::Status detach(mm::fs::Volume& volume) override {
        return detach_volume(volume);
    }
    [[nodiscard]] mm::fs::Status format(mm::fs::BlockDevice& device,
                                        mm::fs::fat::Format format) override {
        const auto record = device_record(device);
        return from(mm_linux_fat_format(&record, format_code(format)));
    }
};

std::array<FatVolume, 2> volumes;
FatProvider fat;

void install_clock();

struct Register {
    Register() {
        install_clock();
        mm::fs::fat::set_provider(fat);
    }
};

const Register registered;

}  // namespace platform::linux::fat_provider
