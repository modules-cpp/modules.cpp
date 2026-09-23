// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
module;

#include <cstddef>
#include <vector>

module mm.shell.posix;

import :service;
import mm.shell.full;

namespace mm::shell::posix {

HostServices::~HostServices() {
    close_all();
    restore_all_signals();
}

full::IoService HostServices::io() {
    return {this, &HostServices::open_callback, &HostServices::pipe_callback,
            &HostServices::read_callback, &HostServices::write_callback,
            &HostServices::close_callback};
}

full::ProcessService HostServices::process() {
    return {this, &HostServices::spawn_callback,
            &HostServices::wait_callback, &HostServices::kill_callback};
}

full::SignalService HostServices::signal() {
    return {this, &HostServices::install_callback,
            &HostServices::restore_callback, &HostServices::poll_callback};
}

full::FileService HostServices::file() {
    return {this, &HostServices::list_callback,
            &HostServices::status_callback};
}

full::Services HostServices::all() {
    return {io(), process(), signal(), file()};
}

std::size_t HostServices::open_descriptors() const {
    std::size_t count = 0;
    for (const auto& entry : descriptors_) {
        if (entry.value >= 0 && entry.owned) ++count;
    }
    return count;
}

std::size_t HostServices::live_children() const {
    std::size_t count = 0;
    for (const auto child : children_) {
        if (child >= 0) ++count;
    }
    return count;
}

std::size_t HostServices::installed_signals() const {
    return signals_.size();
}

// A handle is one past the table index, so zero is never a valid handle and a
// default-initialized field cannot name a real resource.
full::Handle HostServices::publish_descriptor(long descriptor, bool owned) {
    for (std::size_t i = 0; i < descriptors_.size(); ++i) {
        if (descriptors_[i].value >= 0) continue;
        descriptors_[i] = {descriptor, owned};
        return static_cast<full::Handle>(i + 1);
    }
    descriptors_.push_back({descriptor, owned});
    return static_cast<full::Handle>(descriptors_.size());
}

long HostServices::descriptor_of(full::Handle handle) const {
    if (handle == 0 || handle > descriptors_.size()) return -1;
    return descriptors_[handle - 1].value;
}

void HostServices::release_descriptor(full::Handle handle) {
    if (handle == 0 || handle > descriptors_.size()) return;
    descriptors_[handle - 1] = {-1, true};
}

full::Handle HostServices::publish_child(long child) {
    for (std::size_t i = 0; i < children_.size(); ++i) {
        if (children_[i] >= 0) continue;
        children_[i] = child;
        return static_cast<full::Handle>(i + 1);
    }
    children_.push_back(child);
    return static_cast<full::Handle>(children_.size());
}

long HostServices::child_of(full::Handle handle) const {
    if (handle == 0 || handle > children_.size()) return -1;
    return children_[handle - 1];
}

void HostServices::release_child(full::Handle handle) {
    if (handle == 0 || handle > children_.size()) return;
    children_[handle - 1] = -1;
}

full::Handle HostServices::borrow_descriptor(int descriptor) {
    if (descriptor < 0) return full::invalid_handle;
    return publish_descriptor(descriptor, false);
}

}  // namespace mm::shell::posix
