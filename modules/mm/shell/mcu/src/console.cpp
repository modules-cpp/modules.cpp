// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
module;

#include <cstddef>
#include <cstring>
#include <span>

module mm.shell.mcu;

import mm.shell;
import mm.stdio;

namespace mm::shell::mcu {

mm::stdio::Status McuConsole::attach(
    mm::stdio::Console& driver, std::span<char> pending) {
    const auto status = driver.initialize();
    if (status != mm::stdio::Status::Ok) return status;
    driver_ = &driver;
    pending_ = pending;
    used_ = 0;
    failure_ = {};
    return status;
}

ByteSink McuConsole::sink() {
    return {.context = this,
            .write_fn = &write_callback,
            .flush_fn = &flush_callback,
            .failure_fn = &failure_callback};
}

SinkResult McuConsole::write_callback(
    void* context, std::span<const char> bytes) {
    auto* self = static_cast<McuConsole*>(context);
    if (self == nullptr || self->driver_ == nullptr) {
        return SinkResult::Failed;
    }
    if (bytes.size() > self->pending_.size() - self->used_) {
        return SinkResult::WouldBlock;
    }
    for (std::size_t i = 0; i < bytes.size(); ++i) {
        self->pending_[self->used_ + i] = bytes[i];
    }
    self->used_ += bytes.size();
    return SinkResult::Accepted;
}

mm::stdio::Status McuConsole::pump() {
    if (driver_ == nullptr) return mm::stdio::Status::NotInitialized;
    if (used_ == 0) return mm::stdio::Status::Ok;
    std::size_t written = 0;
    const auto bytes = std::as_bytes(
        std::span<const char>{pending_.data(), used_});
    const auto status = driver_->write(bytes, written);
    if (status != mm::stdio::Status::Ok) {
        failure_ = {.error = Status::WriteError};
        return status;
    }
    if (written > used_) {
        failure_ = {.error = Status::WriteError};
        return mm::stdio::Status::TransportError;
    }
    std::memmove(pending_.data(), pending_.data() + written,
                 used_ - written);
    used_ -= written;
    return status;
}

mm::stdio::Status McuConsole::flush() {
    const auto drained = pump();
    if (drained != mm::stdio::Status::Ok) return drained;
    if (used_ != 0) return mm::stdio::Status::Busy;
    return driver_->flush();
}

mm::stdio::Status McuConsole::connected(bool& value) const {
    if (driver_ == nullptr) return mm::stdio::Status::NotInitialized;
    return driver_->connected(value);
}

mm::stdio::Status McuConsole::read(
    std::span<std::byte> bytes, std::size_t& count) const {
    if (driver_ == nullptr) return mm::stdio::Status::NotInitialized;
    return driver_->read(bytes, count);
}

SinkResult McuConsole::flush_callback(void* context) {
    auto* self = static_cast<McuConsole*>(context);
    if (self == nullptr) return SinkResult::Failed;
    const auto status = self->flush();
    if (status == mm::stdio::Status::Ok) return SinkResult::Accepted;
    if (status == mm::stdio::Status::Busy) return SinkResult::WouldBlock;
    return SinkResult::Failed;
}

SinkFailure McuConsole::failure_callback(void* context) {
    auto* self = static_cast<McuConsole*>(context);
    return self == nullptr ? SinkFailure{.error = Status::WriteError}
                           : self->failure_;
}

}  // namespace mm::shell::mcu
