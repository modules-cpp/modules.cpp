// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
module;

#include <string_view>

export module mm.fs.native;

import mm.fs;

export namespace mm::fs::native {

struct Options {
    bool read_only = false;
    bool contain_symlinks = true;   // a link leading outside root is absent
};

// Mounts root, an absolute directory of the platform's file system, at
// prefix. BadArgument for a relative root; NotFound or NotDirectory for a
// root that is not a directory; Unsupported with no provider. Nothing is
// left attached on failure.
[[nodiscard]] Status mount(std::string_view prefix, std::string_view root,
                           Options options = {});

// Unmounts a volume this module mounted and detaches it. BadArgument when
// the volume at prefix was mounted by other means.
[[nodiscard]] Status unmount(std::string_view prefix);

// The seam. Every method answers Unsupported by default.
class Provider {
public:
    virtual ~Provider() = default;
    [[nodiscard]] virtual Status attach(std::string_view, const Options&, Volume*&) {
        return Status::Unsupported;
    }
    [[nodiscard]] virtual Status detach(Volume&) { return Status::Unsupported; }
};

void set_provider(Provider& provider);
[[nodiscard]] Provider& selected_provider();

}
