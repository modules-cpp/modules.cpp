// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
module;

#include <array>
#include <string_view>

module mm.fs.native;

import mm.fs;

namespace mm::fs::native {

namespace {

Provider unserved;
Provider* current = &unserved;

// The volumes this module attached and mounted, so unmount detaches only
// those. One per mount slot is enough: each occupies one.
std::array<Volume*, max_mounts> attached{};

}  // namespace

void set_provider(Provider& provider) { current = &provider; }

Provider& selected_provider() { return *current; }

Status mount(std::string_view prefix, std::string_view root, Options options) {
    Volume* volume = nullptr;
    const auto status = current->attach(root, options, volume);
    if (status != Status::Ok) return status;
    const auto mounted = mm::fs::mount(prefix, *volume);
    if (mounted != Status::Ok) {
        static_cast<void>(current->detach(*volume));
        return mounted;
    }
    for (auto& slot : attached) {
        if (slot == nullptr) {
            slot = volume;
            break;
        }
    }
    return Status::Ok;
}

Status unmount(std::string_view prefix) {
    Volume* volume = nullptr;
    const auto found = mm::fs::mounted(prefix, volume);
    if (found != Status::Ok) return found;
    Volume** slot = nullptr;
    for (auto& entry : attached)
        if (entry == volume) slot = &entry;
    if (slot == nullptr) return Status::BadArgument;
    const auto unmounted = mm::fs::unmount(prefix);
    if (unmounted != Status::Ok) return unmounted;
    *slot = nullptr;
    return current->detach(*volume);
}

}  // namespace mm::fs::native
