// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
//
// An in-memory volume that keeps every rule mm.fs's Volume documents: the
// reference driver the conformance checks are run against on the host, and
// the volume the mm.fs tests mount. Paths arrive volume-relative and
// normalised, "" for the root.
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <map>
#include <span>
#include <string>
#include <string_view>
#include <vector>

import mm.fs;

namespace {

using mm::fs::Access;
using mm::fs::Disposition;
using mm::fs::Handle;
using mm::fs::Kind;
using mm::fs::Stat;
using mm::fs::Status;

class MemoryVolume final : public mm::fs::Volume {
public:
    MemoryVolume() { reset(); }

    void reset() {
        nodes_.clear();
        nodes_[""] = Node{Kind::Directory, {}, 0};
        files_.assign(16, FileSlot{});
        directories_.assign(8, DirectorySlot{});
        capacity_ = 1u << 20;
        flushes_ = 0;
    }

    void set_capacity(std::uint64_t capacity) { capacity_ = capacity; }
    [[nodiscard]] unsigned int flushes() const { return flushes_; }
    [[nodiscard]] std::uint64_t modified(const std::string& path) const {
        const auto found = nodes_.find(path);
        return found == nodes_.end() ? 0 : found->second.modified;
    }

    Status open(std::string_view path, Access access, Disposition disposition,
                Handle& handle) override {
        const std::string key{path};
        if (key.empty()) return Status::IsDirectory;
        const auto parent_status = check_parent(key);
        if (parent_status != Status::Ok) return parent_status;
        const bool writable = access != Access::Read;
        auto found = nodes_.find(key);
        if (found != nodes_.end()) {
            if (found->second.kind == Kind::Directory) return Status::IsDirectory;
            if (disposition == Disposition::CreateNew) return Status::Exists;
            if (open_count(key) > 0 && (writable || writer_open(key))) return Status::Busy;
        } else if (disposition == Disposition::OpenExisting) {
            return Status::NotFound;
        }
        const auto slot = std::find_if(files_.begin(), files_.end(),
                                       [](const FileSlot& s) { return !s.used; });
        if (slot == files_.end()) return Status::TooMany;
        if (found == nodes_.end()) {
            found = nodes_.emplace(key, Node{Kind::File, {}, mm::fs::now()}).first;
        } else if (disposition == Disposition::CreateOrTruncate) {
            found->second.data.clear();
            found->second.modified = mm::fs::now();
        }
        *slot = FileSlot{true, key, access, 0};
        handle = static_cast<Handle>(slot - files_.begin());
        return Status::Ok;
    }

    Status read(Handle handle, std::span<std::byte> into, std::size_t& count) override {
        auto* slot = file(handle);
        if (slot == nullptr) return Status::BadArgument;
        const auto& data = nodes_[slot->path].data;
        const std::uint64_t available =
            slot->offset < data.size() ? data.size() - slot->offset : 0;
        const auto moved = static_cast<std::size_t>(
            std::min<std::uint64_t>(available, into.size()));
        std::copy_n(data.begin() + static_cast<std::ptrdiff_t>(slot->offset), moved,
                    into.begin());
        slot->offset += moved;
        count = moved;
        return Status::Ok;
    }

    Status write(Handle handle, std::span<const std::byte> from, std::size_t& count) override {
        auto* slot = file(handle);
        if (slot == nullptr) return Status::BadArgument;
        auto& node = nodes_[slot->path];
        auto& data = node.data;
        const std::uint64_t at = slot->access == Access::Append ? data.size() : slot->offset;
        const std::uint64_t gap = at > data.size() ? at - data.size() : 0;
        const std::uint64_t used = used_bytes();
        const std::uint64_t room = capacity_ > used ? capacity_ - used : 0;
        if (gap > room) {
            count = 0;
            return Status::NoSpace;
        }
        // Bytes past the current end cost space; overwriting does not.
        const std::uint64_t overlap = at < data.size() ? data.size() - at : 0;
        const std::uint64_t fits = overlap + (room - gap);
        const auto moved = static_cast<std::size_t>(std::min<std::uint64_t>(fits, from.size()));
        if (at + moved > data.size()) data.resize(static_cast<std::size_t>(at + moved));
        std::copy_n(from.begin(), moved, data.begin() + static_cast<std::ptrdiff_t>(at));
        slot->offset = at + moved;
        node.modified = mm::fs::now();
        count = moved;
        return moved < from.size() ? Status::NoSpace : Status::Ok;
    }

    Status seek(Handle handle, std::uint64_t offset) override {
        auto* slot = file(handle);
        if (slot == nullptr) return Status::BadArgument;
        slot->offset = offset;
        return Status::Ok;
    }

    Status tell(Handle handle, std::uint64_t& offset) override {
        auto* slot = file(handle);
        if (slot == nullptr) return Status::BadArgument;
        offset = slot->offset;
        return Status::Ok;
    }

    Status truncate(Handle handle) override {
        auto* slot = file(handle);
        if (slot == nullptr) return Status::BadArgument;
        auto& data = nodes_[slot->path].data;
        if (slot->offset < data.size()) data.resize(static_cast<std::size_t>(slot->offset));
        return Status::Ok;
    }

    Status sync(Handle handle) override {
        return file(handle) == nullptr ? Status::BadArgument : Status::Ok;
    }

    Status file_stat(Handle handle, Stat& stat) override {
        auto* slot = file(handle);
        if (slot == nullptr) return Status::BadArgument;
        stat = describe(nodes_[slot->path]);
        return Status::Ok;
    }

    Status close(Handle handle) override {
        auto* slot = file(handle);
        if (slot == nullptr) return Status::BadArgument;
        *slot = FileSlot{};
        return Status::Ok;
    }

    Status open_directory(std::string_view path, Handle& handle) override {
        const std::string key{path};
        const auto found = nodes_.find(key);
        if (found == nodes_.end()) return Status::NotFound;
        if (found->second.kind != Kind::Directory) return Status::NotDirectory;
        const auto slot = std::find_if(directories_.begin(), directories_.end(),
                                       [](const DirectorySlot& s) { return !s.used; });
        if (slot == directories_.end()) return Status::TooMany;
        *slot = DirectorySlot{true, key, {}, 0};
        for (const auto& [name, node] : nodes_)
            if (!name.empty() && name != key && parent(name) == key)
                slot->names.push_back(leaf(name));
        handle = static_cast<Handle>(slot - directories_.begin());
        return Status::Ok;
    }

    Status next(Handle handle, std::span<char> name, std::size_t& length, Stat& stat,
                bool& done) override {
        if (handle >= directories_.size() || !directories_[handle].used)
            return Status::BadArgument;
        auto& slot = directories_[handle];
        while (slot.index < slot.names.size()) {
            const auto& entry = slot.names[slot.index];
            const auto found = nodes_.find(join(slot.path, entry));
            if (found == nodes_.end()) {   // removed since the listing began
                ++slot.index;
                continue;
            }
            if (entry.size() > name.size()) return Status::NameTooLong;
            std::copy(entry.begin(), entry.end(), name.begin());
            length = entry.size();
            stat = describe(found->second);
            done = false;
            ++slot.index;
            return Status::Ok;
        }
        done = true;
        return Status::Ok;
    }

    Status close_directory(Handle handle) override {
        if (handle >= directories_.size() || !directories_[handle].used)
            return Status::BadArgument;
        directories_[handle] = DirectorySlot{};
        return Status::Ok;
    }

    Status stat(std::string_view path, Stat& stat) override {
        const auto found = nodes_.find(std::string{path});
        if (found == nodes_.end()) return Status::NotFound;
        stat = describe(found->second);
        return Status::Ok;
    }

    Status make_directory(std::string_view path) override {
        const std::string key{path};
        if (nodes_.count(key) != 0) return Status::Exists;
        const auto parent_status = check_parent(key);
        if (parent_status != Status::Ok) return parent_status;
        nodes_[key] = Node{Kind::Directory, {}, mm::fs::now()};
        return Status::Ok;
    }

    Status remove(std::string_view path) override {
        const std::string key{path};
        const auto found = nodes_.find(key);
        if (found == nodes_.end()) return Status::NotFound;
        if (open_count(key) > 0 || directory_open(key)) return Status::Busy;
        if (found->second.kind == Kind::Directory && has_children(key)) return Status::NotEmpty;
        nodes_.erase(found);
        return Status::Ok;
    }

    Status rename(std::string_view from, std::string_view to) override {
        const std::string source{from};
        const std::string target{to};
        const auto found = nodes_.find(source);
        if (found == nodes_.end()) return Status::NotFound;
        if (nodes_.count(target) != 0) return Status::Exists;
        const auto parent_status = check_parent(target);
        if (parent_status != Status::Ok) return parent_status;
        if (target.size() > source.size() && target.compare(0, source.size(), source) == 0 &&
            target[source.size()] == '/')
            return Status::BadArgument;    // a directory into itself
        if (open_beneath(source)) return Status::Busy;
        std::vector<std::pair<std::string, Node>> moved;
        for (auto it = nodes_.begin(); it != nodes_.end();) {
            if (it->first == source || beneath(it->first, source)) {
                moved.emplace_back(target + it->first.substr(source.size()), it->second);
                it = nodes_.erase(it);
            } else {
                ++it;
            }
        }
        for (auto& [name, node] : moved) nodes_[name] = node;
        return Status::Ok;
    }

    Status space(mm::fs::Space& space) override {
        space.total = capacity_;
        space.free = capacity_ - std::min(capacity_, used_bytes());
        return Status::Ok;
    }

    Status flush() override {
        ++flushes_;
        return Status::Ok;
    }

private:
    struct Node {
        Kind kind = Kind::File;
        std::vector<std::byte> data;
        std::uint64_t modified = 0;
    };
    struct FileSlot {
        bool used = false;
        std::string path;
        Access access = Access::Read;
        std::uint64_t offset = 0;
    };
    struct DirectorySlot {
        bool used = false;
        std::string path;
        std::vector<std::string> names;
        std::size_t index = 0;
    };

    [[nodiscard]] static std::string parent(const std::string& path) {
        const auto slash = path.rfind('/');
        return slash == std::string::npos ? std::string{} : path.substr(0, slash);
    }
    [[nodiscard]] static std::string leaf(const std::string& path) {
        const auto slash = path.rfind('/');
        return slash == std::string::npos ? path : path.substr(slash + 1);
    }
    [[nodiscard]] static std::string join(const std::string& directory, const std::string& name) {
        return directory.empty() ? name : directory + "/" + name;
    }
    [[nodiscard]] static bool beneath(const std::string& path, const std::string& directory) {
        return path.size() > directory.size() &&
               path.compare(0, directory.size(), directory) == 0 &&
               path[directory.size()] == '/';
    }

    [[nodiscard]] Status check_parent(const std::string& path) const {
        const auto found = nodes_.find(parent(path));
        if (found == nodes_.end()) return Status::NotFound;
        if (found->second.kind != Kind::Directory) return Status::NotDirectory;
        return Status::Ok;
    }
    [[nodiscard]] bool has_children(const std::string& path) const {
        return std::any_of(nodes_.begin(), nodes_.end(), [&](const auto& entry) {
            return !entry.first.empty() && entry.first != path && parent(entry.first) == path;
        });
    }
    [[nodiscard]] unsigned int open_count(const std::string& path) const {
        return static_cast<unsigned int>(std::count_if(
            files_.begin(), files_.end(),
            [&](const FileSlot& s) { return s.used && s.path == path; }));
    }
    [[nodiscard]] bool writer_open(const std::string& path) const {
        return std::any_of(files_.begin(), files_.end(), [&](const FileSlot& s) {
            return s.used && s.path == path && s.access != Access::Read;
        });
    }
    [[nodiscard]] bool directory_open(const std::string& path) const {
        return std::any_of(directories_.begin(), directories_.end(),
                           [&](const DirectorySlot& s) { return s.used && s.path == path; });
    }
    [[nodiscard]] bool open_beneath(const std::string& path) const {
        const auto under = [&](const std::string& p) { return p == path || beneath(p, path); };
        return std::any_of(files_.begin(), files_.end(),
                           [&](const FileSlot& s) { return s.used && under(s.path); }) ||
               std::any_of(directories_.begin(), directories_.end(),
                           [&](const DirectorySlot& s) { return s.used && under(s.path); });
    }
    [[nodiscard]] std::uint64_t used_bytes() const {
        std::uint64_t used = 0;
        for (const auto& entry : nodes_) used += entry.second.data.size();
        return used;
    }
    [[nodiscard]] FileSlot* file(Handle handle) {
        return handle < files_.size() && files_[handle].used ? &files_[handle] : nullptr;
    }
    [[nodiscard]] static Stat describe(const Node& node) {
        return Stat{node.kind, node.kind == Kind::File ? node.data.size() : 0, node.modified,
                    false};
    }

    std::map<std::string, Node> nodes_;
    std::vector<FileSlot> files_;
    std::vector<DirectorySlot> directories_;
    std::uint64_t capacity_ = 0;
    unsigned int flushes_ = 0;
};

MemoryVolume first;
MemoryVolume second;

}  // namespace

mm::fs::Volume& mm_test_fs_memory(unsigned int which) { return which == 0 ? first : second; }
void mm_test_fs_memory_reset() {
    first.reset();
    second.reset();
}
void mm_test_fs_memory_capacity(unsigned int which, std::uint64_t capacity) {
    (which == 0 ? first : second).set_capacity(capacity);
}
unsigned int mm_test_fs_memory_flushes(unsigned int which) {
    return (which == 0 ? first : second).flushes();
}
std::uint64_t mm_test_fs_memory_modified(unsigned int which, const std::string& path) {
    return (which == 0 ? first : second).modified(path);
}
