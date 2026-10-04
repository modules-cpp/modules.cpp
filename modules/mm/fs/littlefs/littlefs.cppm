// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
module;

#include <cstdint>
#include <string_view>

export module mm.fs.littlefs;

import mm.fs;

export namespace mm::fs::littlefs {

struct Options {
    bool read_only = false;
    // Erases a metadata block takes before littlefs moves it on; littlefs
    // suggests 100 to 1000, and -1 turns block-level wear levelling off.
    std::int32_t block_cycles = 500;
};

// Mounts the littlefs volume on device at prefix. Corrupt when the device
// holds none -- nothing is erased; Unsupported with no provider or a device
// whose geometry the provider cannot use. Nothing is left attached on failure.
[[nodiscard]] Status mount(std::string_view prefix, FlashDevice& device, Options options = {});

// Unmounts a volume this module mounted and detaches it. BadArgument when
// the volume at prefix was mounted by other means.
[[nodiscard]] Status unmount(std::string_view prefix);

// Writes an empty littlefs volume over the whole device.
[[nodiscard]] Status format(FlashDevice& device);

// The seam. Every method answers Unsupported by default.
class Provider {
public:
    virtual ~Provider() = default;
    [[nodiscard]] virtual Status attach(FlashDevice&, const Options&, Volume*&) {
        return Status::Unsupported;
    }
    [[nodiscard]] virtual Status detach(Volume&) { return Status::Unsupported; }
    [[nodiscard]] virtual Status format(FlashDevice&) { return Status::Unsupported; }
};

void set_provider(Provider& provider);
[[nodiscard]] Provider& selected_provider();

}
