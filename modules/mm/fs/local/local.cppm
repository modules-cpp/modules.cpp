// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
module;

#include <string_view>

export module mm.fs.local;

import mm.fs;

export namespace mm::fs::local {

struct Options {
    bool read_only = false;
    // On a platform whose storage can be blank, make an empty volume on first
    // mount rather than answering Corrupt. Linux's directory is never blank.
    bool format_if_blank = true;
};

// Mounts the board's own storage at prefix. NotFound when the platform has
// none configured; Unsupported with no provider; otherwise what attaching or
// mounting answered, with nothing left attached on failure.
[[nodiscard]] Status mount(std::string_view prefix, Options options = {});

// Unmounts a volume this module mounted and detaches it. BadArgument when
// the volume at prefix was mounted by other means; NotFound when nothing is
// mounted there; Busy while anything on it is open.
[[nodiscard]] Status unmount(std::string_view prefix);

// The seam. Every method answers Unsupported by default.
class Provider {
public:
    virtual ~Provider() = default;
    [[nodiscard]] virtual Status attach(const Options&, Volume*&) { return Status::Unsupported; }
    [[nodiscard]] virtual Status detach(Volume&) { return Status::Unsupported; }
};

void set_provider(Provider& provider);
[[nodiscard]] Provider& selected_provider();

}
