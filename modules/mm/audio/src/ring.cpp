// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
//
// The ring's two indices each have one writer. The producer publishes a write
// index with release after copying samples in, and the consumer acquires it
// before copying them out; the read index goes the other way. Nothing else is
// shared between the two sides except the loss counts, which likewise have one
// writer each.
module;

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>

module mm.audio;

namespace mm::audio {

namespace {

// The indices run to twice the capacity, so twice the capacity must fit them.
constexpr std::size_t largest_capacity = std::numeric_limits<std::uint32_t>::max() / 2;

}  // namespace

Status Ring::configure(std::span<std::int16_t> storage, Format format) {
    if (storage.empty() || storage.size() > largest_capacity || format.rate_hz == 0)
        return Status::BadArgument;
    if (producer_.load(std::memory_order_acquire) || consumer_.load(std::memory_order_acquire))
        return Status::Busy;
    storage_ = storage;
    capacity_ = storage.size();
    format_ = format;
    write_.store(0, std::memory_order_relaxed);
    read_.store(0, std::memory_order_relaxed);
    underrun_.store(0, std::memory_order_relaxed);
    overrun_.store(0, std::memory_order_release);
    return Status::Ok;
}

std::size_t Ring::fill(std::uint32_t write, std::uint32_t read) const {
    const std::size_t span = 2 * capacity_;
    return (write + span - read) % span;
}

std::size_t Ring::slot(std::uint32_t index) const {
    return index < capacity_ ? index : index - capacity_;
}

std::uint32_t Ring::advance(std::uint32_t index, std::size_t count) const {
    return static_cast<std::uint32_t>((index + count) % (2 * capacity_));
}

std::size_t Ring::readable() const {
    if (capacity_ == 0) return 0;
    return fill(write_.load(std::memory_order_acquire), read_.load(std::memory_order_acquire));
}

std::size_t Ring::writable() const {
    if (capacity_ == 0) return 0;
    return capacity_ - readable();
}

Status Ring::write(std::span<const std::int16_t> samples, std::size_t& count) {
    if (capacity_ == 0) return Status::NotInitialized;
    const auto write = write_.load(std::memory_order_relaxed);
    const auto read = read_.load(std::memory_order_acquire);
    const auto moved = std::min(samples.size(), capacity_ - fill(write, read));
    const auto start = slot(write);
    const auto first = std::min(moved, capacity_ - start);
    std::copy_n(samples.begin(), first, storage_.begin() + static_cast<std::ptrdiff_t>(start));
    std::copy_n(samples.begin() + static_cast<std::ptrdiff_t>(first), moved - first,
                storage_.begin());
    write_.store(advance(write, moved), std::memory_order_release);
    count = moved;
    return Status::Ok;
}

Status Ring::read(std::span<std::int16_t> samples, std::size_t& count) {
    if (capacity_ == 0) return Status::NotInitialized;
    const auto read = read_.load(std::memory_order_relaxed);
    const auto write = write_.load(std::memory_order_acquire);
    const auto moved = std::min(samples.size(), fill(write, read));
    const auto start = slot(read);
    const auto first = std::min(moved, capacity_ - start);
    std::copy_n(storage_.begin() + static_cast<std::ptrdiff_t>(start), first, samples.begin());
    std::copy_n(storage_.begin(), moved - first,
                samples.begin() + static_cast<std::ptrdiff_t>(first));
    read_.store(advance(read, moved), std::memory_order_release);
    count = moved;
    return Status::Ok;
}

std::span<std::int16_t> Ring::write_region() {
    if (capacity_ == 0) return {};
    const auto write = write_.load(std::memory_order_relaxed);
    const auto room = capacity_ - fill(write, read_.load(std::memory_order_acquire));
    const auto start = slot(write);
    return storage_.subspan(start, std::min(room, capacity_ - start));
}

Status Ring::commit_write(std::size_t count) {
    if (capacity_ == 0) return Status::NotInitialized;
    const auto write = write_.load(std::memory_order_relaxed);
    const auto room = capacity_ - fill(write, read_.load(std::memory_order_acquire));
    if (count > std::min(room, capacity_ - slot(write))) return Status::BadArgument;
    write_.store(advance(write, count), std::memory_order_release);
    return Status::Ok;
}

std::span<const std::int16_t> Ring::read_region() const {
    if (capacity_ == 0) return {};
    const auto read = read_.load(std::memory_order_relaxed);
    const auto waiting = fill(write_.load(std::memory_order_acquire), read);
    const auto start = slot(read);
    return std::span<const std::int16_t>{storage_}.subspan(
        start, std::min(waiting, capacity_ - start));
}

Status Ring::commit_read(std::size_t count) {
    if (capacity_ == 0) return Status::NotInitialized;
    const auto read = read_.load(std::memory_order_relaxed);
    const auto waiting = fill(write_.load(std::memory_order_acquire), read);
    if (count > std::min(waiting, capacity_ - slot(read))) return Status::BadArgument;
    read_.store(advance(read, count), std::memory_order_release);
    return Status::Ok;
}

Status Ring::reset() {
    if (producer_.load(std::memory_order_acquire) || consumer_.load(std::memory_order_acquire))
        return Status::Busy;
    write_.store(0, std::memory_order_relaxed);
    read_.store(0, std::memory_order_relaxed);
    underrun_.store(0, std::memory_order_relaxed);
    overrun_.store(0, std::memory_order_release);
    return Status::Ok;
}

Status Ring::claim(End end) {
    if (capacity_ == 0) return Status::NotInitialized;
    auto& held = end == End::Producer ? producer_ : consumer_;
    if (held.load(std::memory_order_acquire)) return Status::Busy;
    held.store(true, std::memory_order_release);
    return Status::Ok;
}

void Ring::release(End end) {
    auto& held = end == End::Producer ? producer_ : consumer_;
    held.store(false, std::memory_order_release);
}

void Ring::underran(std::size_t count) {
    underrun_.store(underrun_.load(std::memory_order_relaxed) +
                        static_cast<std::uint32_t>(count),
                    std::memory_order_release);
}

void Ring::overran(std::size_t count) {
    overrun_.store(overrun_.load(std::memory_order_relaxed) +
                       static_cast<std::uint32_t>(count),
                   std::memory_order_release);
}

Counts Ring::counts() const {
    return {underrun_.load(std::memory_order_acquire), overrun_.load(std::memory_order_acquire)};
}

}
