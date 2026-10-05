// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
module;

#include <algorithm>
#include <array>
#include <cstddef>
#include <span>
#include <string_view>

module mm.fs;

namespace mm::fs {

// Built in a local buffer and copied out at the end, so out changes only on
// Ok. The buffer holds "/a/b" for the root's children and nothing for the
// root itself until the end; ".." drops back to the previous '/'.
Status normalize(std::string_view path, std::span<char> out, std::size_t& length) {
    if (path.empty() || path.front() != '/') return Status::BadArgument;
    if (path.find('\0') != std::string_view::npos) return Status::BadArgument;
    const std::size_t limit = std::min(out.size(), max_path);
    if (limit == 0) return Status::NameTooLong;

    std::array<char, max_path> work{};
    std::size_t used = 0;
    std::size_t at = 0;
    while (at < path.size()) {
        while (at < path.size() && path[at] == '/') ++at;
        const std::size_t start = at;
        while (at < path.size() && path[at] != '/') ++at;
        const auto component = path.substr(start, at - start);
        if (component.empty() || component == ".") continue;
        if (component == "..") {
            while (used > 0 && work[used - 1] != '/') --used;
            if (used > 0) --used;
            continue;
        }
        if (component.size() > max_name) return Status::NameTooLong;
        if (used + 1 + component.size() > limit) return Status::NameTooLong;
        work[used++] = '/';
        std::copy(component.begin(), component.end(), work.begin() + used);
        used += component.size();
    }
    if (used == 0) work[used++] = '/';

    std::copy(work.begin(), work.begin() + used, out.begin());
    length = used;
    return Status::Ok;
}

}  // namespace mm::fs
