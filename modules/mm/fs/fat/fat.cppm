// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
module;

#include <string_view>

export module mm.fs.fat;

import mm.fs;

export namespace mm::fs::fat {

struct Options {
    bool read_only = false;
};

// What format makes. Fat is FAT12 or FAT16, as FAT's own rules choose for
// the device's size; Automatic lets them choose FAT32 too.
enum class Format { Automatic, Fat, Fat32 };

// Mounts the FAT volume on device at prefix. Corrupt when the device holds
// none -- nothing is written; Unsupported with no provider or a device whose
// blocks are not 512 bytes. Nothing is left attached on failure.
[[nodiscard]] Status mount(std::string_view prefix, BlockDevice& device, Options options = {});

// Unmounts a volume this module mounted and detaches it. BadArgument when
// the volume at prefix was mounted by other means.
[[nodiscard]] Status unmount(std::string_view prefix);

// Writes a new, empty FAT volume over the whole device, in a partition as a
// PC expects. What was on the device is gone.
[[nodiscard]] Status format(BlockDevice& device, Format format = Format::Automatic);

// The seam. Every method answers Unsupported by default.
class Provider {
public:
    virtual ~Provider() = default;
    [[nodiscard]] virtual Status attach(BlockDevice&, const Options&, Volume*&) {
        return Status::Unsupported;
    }
    [[nodiscard]] virtual Status detach(Volume&) { return Status::Unsupported; }
    [[nodiscard]] virtual Status format(BlockDevice&, Format) { return Status::Unsupported; }
};

void set_provider(Provider& provider);
[[nodiscard]] Provider& selected_provider();

}
