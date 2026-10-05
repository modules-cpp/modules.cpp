// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
module;

#include "../lfs-cxx.h"
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

export module platform.pico.fs.littlefs;

import mm.fs;
import mm.fs.littlefs;
import mm.fs.local;

// A named, non-exported namespace, as docs/modules-c++20.mdy requires of a
// provider's objects in an interface unit.
namespace platform::pico::littlefs_provider {

// One attached littlefs volume, seen through the adapter's ABI.
class LittlefsVolume final : public mm::fs::Volume {
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
    mm_pico_lfs_device device{};     // must outlive the attachment
};

// Attaches littlefs on device in a free volume object.
[[nodiscard]] mm::fs::Status attach_volume(mm::fs::FlashDevice& device, bool read_only,
                                           bool format_if_blank, std::int32_t block_cycles,
                                           mm::fs::Volume*& volume);
[[nodiscard]] mm::fs::Status detach_volume(mm::fs::Volume& volume);
[[nodiscard]] mm_pico_lfs_device device_record(mm::fs::FlashDevice& device);
[[nodiscard]] mm::fs::Status from(int code);

class LittlefsProvider final : public mm::fs::littlefs::Provider {
public:
    [[nodiscard]] mm::fs::Status attach(mm::fs::FlashDevice& device,
                                        const mm::fs::littlefs::Options& options,
                                        mm::fs::Volume*& volume) override {
        return attach_volume(device, options.read_only, false, options.block_cycles, volume);
    }
    [[nodiscard]] mm::fs::Status detach(mm::fs::Volume& volume) override {
        return detach_volume(volume);
    }
    [[nodiscard]] mm::fs::Status format(mm::fs::FlashDevice& device) override {
        const auto record = device_record(device);
        return from(mm_pico_lfs_format(&record));
    }
};

// mm.fs.local: littlefs on the flash region.
class LocalProvider final : public mm::fs::local::Provider {
public:
    [[nodiscard]] mm::fs::Status attach(const mm::fs::local::Options& options,
                                        mm::fs::Volume*& volume) override {
        return attach_volume(region, options.read_only, options.format_if_blank, 500, volume);
    }
    [[nodiscard]] mm::fs::Status detach(mm::fs::Volume& volume) override {
        return detach_volume(volume);
    }

private:
    mm::fs::McuFlash region;
};

// One per volume the adapter can attach: the board table caps
// MM_BOARD_LFS_VOLUMES at four, and the adapter answers TooMany past its own.
std::array<LittlefsVolume, 4> volumes;
LittlefsProvider littlefs;
LocalProvider local;

void install_clock();

struct Register {
    Register() {
        install_clock();
        mm::fs::littlefs::set_provider(littlefs);
        mm::fs::local::set_provider(local);
    }
};

const Register registered;

}  // namespace platform::pico::littlefs_provider
