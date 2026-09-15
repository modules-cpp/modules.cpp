// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
module;

#include <algorithm>
#include <array>
#include <cerrno>
#include <charconv>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fcntl.h>
#include <fstream>
#include <optional>
#include <poll.h>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <unistd.h>
#include <vector>

export module platform.linux.imu;

import mm.imu;
import platform.linux.map;

export namespace platform::linux::iio_detail {

struct Channel {
    std::string name;
    int index = 0;
    bool little = true;
    bool signed_value = true;
    unsigned int realbits = 0;
    unsigned int storagebits = 0;
    unsigned int shift = 0;
    unsigned int repeat = 1;
    std::size_t offset = 0;
    long double scale = 0;
};

[[nodiscard]] bool parse_scan_type(std::string_view text, Channel& channel);
[[nodiscard]] std::optional<std::size_t> layout(
    std::span<Channel> channels);
[[nodiscard]] std::int64_t extract_value(
    std::span<const std::byte> record, const Channel& channel);
[[nodiscard]] std::optional<unsigned int> physical_range(
    const Channel& channel, bool acceleration);

}

namespace {

using Status = mm::imu::Status;
using Channel = platform::linux::iio_detail::Channel;

Status failure(int value, bool explicit_path) {
    if ((value == ENOENT || value == ENODEV) && !explicit_path)
        return Status::Unsupported;
    if ((value == ENOENT || value == ENODEV) && explicit_path)
        return Status::BadArgument;
    if (value == EACCES || value == EPERM) return Status::TransportError;
    if (value == EBUSY) return Status::Busy;
    if (value == ETIMEDOUT) return Status::Timeout;
    return Status::TransportError;
}

bool read_text(const std::string& path, std::string& value) {
    std::ifstream input(path);
    std::getline(input, value);
    return static_cast<bool>(input) || input.eof();
}

bool write_text(const std::string& path, std::string_view value) {
    std::ofstream output(path);
    output << value;
    return static_cast<bool>(output);
}

bool integer(std::string_view text, int& value) {
    const auto r = std::from_chars(text.data(), text.data() + text.size(), value);
    return r.ec == std::errc{} && r.ptr == text.data() + text.size();
}

bool scan_type(std::string_view text, Channel& c) {
    if (text.size() < 7) return false;
    c.little = text.starts_with("le:");
    if (!c.little && !text.starts_with("be:")) return false;
    text.remove_prefix(3);
    c.signed_value = (text.front() == 's');
    if (!c.signed_value && text.front() != 'u') return false;
    text.remove_prefix(1);

    auto slash = text.find('/');
    auto shift = text.find(">>");
    if (slash == text.npos || shift == text.npos || slash > shift)
        return false;

    unsigned long real = 0;
    unsigned long storage = 0;
    auto a = std::from_chars(text.data(), text.data() + slash, real);
    auto b = std::from_chars(text.data() + slash + 1, text.data() + shift,
                             storage);
    unsigned long amount = 0;
    auto end = text.find('X', shift);
    auto d = std::from_chars(text.data() + shift + 2,
                             text.data() + (end == text.npos ? text.size() : end),
                             amount);
    if (a.ec != std::errc{} || a.ptr != text.data() + slash ||
        b.ec != std::errc{} || b.ptr != text.data() + shift ||
        d.ec != std::errc{} ||
        d.ptr != text.data() + (end == text.npos ? text.size() : end) ||
        storage % 8 != 0 || real == 0 || real > storage || storage > 64)
        return false;

    c.realbits = real;
    c.storagebits = storage;
    c.shift = amount;
    if (end != text.npos) {
        unsigned long repeat = 0;
        auto e = std::from_chars(text.data() + end + 1,
                                 text.data() + text.size(), repeat);
        if (e.ec != std::errc{} || e.ptr != text.data() + text.size() ||
            repeat == 0)
            return false;
        c.repeat = repeat;
    }
    return true;
}

std::int64_t extract(std::span<const std::byte> record, const Channel& c) {
    std::uint64_t raw = 0;
    const auto bytes = c.storagebits / 8;
    for (unsigned int i = 0; i < bytes; ++i) {
        const auto source = c.little ? i : bytes - 1 - i;
        raw |= static_cast<std::uint64_t>(record[c.offset + source]) << (8 * i);
    }
    raw >>= c.shift;
    const std::uint64_t mask =
        (c.realbits == 64) ? ~0ULL : (1ULL << c.realbits) - 1;
    raw &= mask;
    if (c.signed_value && c.realbits < 64 && (raw & (1ULL << (c.realbits - 1))))
        raw |= ~mask;
    return static_cast<std::int64_t>(raw);
}

class LinuxImu final : public mm::imu::Imu {
public:
    ~LinuxImu() override { cleanup(); }

    [[nodiscard]] mm::imu::Scale scale() const override { return scale_; }

    [[nodiscard]] Status initialize() override {
        cleanup();
        const auto& r = platform::linux::resolve();
        if (r.status != platform::linux::MapStatus::Ok || !r.map)
            return Status::BadArgument;
        entry_ = &r.map->imu;

        const bool explicit_selection =
            entry_->device.kind != platform::linux::SelectorKind::Auto;
        std::optional<unsigned int> selected;
        if (entry_->device.kind == platform::linux::SelectorKind::Index) {
            selected = entry_->device.index;
        } else if (entry_->device.kind == platform::linux::SelectorKind::Name) {
            for (unsigned int i = 0; i < 64; ++i) {
                std::string name;
                if (read_text("/sys/bus/iio/devices/iio:device" +
                                  std::to_string(i) + "/name", name) &&
                    name == entry_->device.name) {
                    selected = i;
                    break;
                }
            }
            if (!selected) return Status::BadArgument;
        }

        Status discovery_error = Status::Unsupported;
        const auto try_open = [&](unsigned int index) {
            device_ = "/dev/iio:device" + std::to_string(index);
            sysfs_ = "/sys/bus/iio/devices/iio:device" +
                     std::to_string(index);
            fd_ = ::open(device_.c_str(), O_RDONLY | O_NONBLOCK | O_CLOEXEC);
            if (fd_ >= 0) return true;
            const auto status = failure(errno, explicit_selection);
            if (status != Status::Unsupported) discovery_error = status;
            return false;
        };
        if (selected) {
            if (!try_open(*selected)) return discovery_error;
        } else {
            for (unsigned int i = 0; i < 64 && fd_ < 0; ++i) try_open(i);
            if (fd_ < 0) return discovery_error;
        }

        static constexpr std::array names{
            "in_accel_x", "in_accel_y", "in_accel_z",
            "in_anglvel_x", "in_anglvel_y", "in_anglvel_z"};
        channels_.clear();
        for (const auto* name : names) {
            Channel c;
            c.name = name;
            std::string value;
            if (!read_text(sysfs_ + "/scan_elements/" + c.name + "_index", value) ||
                !integer(value, c.index) ||
                !read_text(sysfs_ + "/scan_elements/" + c.name + "_type", value) ||
                !scan_type(value, c) ||
                !read_text(sysfs_ + "/" + c.name.substr(0, c.name.rfind('_')) +
                               "_scale",
                           value)) {
                cleanup();
                return Status::Unsupported;
            }
            char* end = nullptr;
            c.scale = std::strtold(value.c_str(), &end);
            if (end == value.c_str() || c.scale <= 0) {
                cleanup();
                return Status::Unsupported;
            }
            channels_.push_back(c);
        }

        const auto layout = platform::linux::iio_detail::layout(channels_);
        if (!layout) {
            cleanup();
            return Status::Unsupported;
        }
        record_size_ = *layout;

        const auto accel =
            platform::linux::iio_detail::physical_range(channels_[0], true);
        const auto gyro =
            platform::linux::iio_detail::physical_range(channels_[3], false);
        if (!accel || !gyro || channels_[0].realbits != channels_[3].realbits ||
            channels_[0].realbits > 32) {
            cleanup();
            return Status::Unsupported;
        }
        scale_ = {*accel, *gyro, static_cast<unsigned int>(
                                      1ULL << (channels_[0].realbits - 1))};

        std::string old;
        if (!read_text(sysfs_ + "/buffer/enable", old)) {
            cleanup();
            return Status::TransportError;
        }
        old_buffer_ = old;
        if (!write_text(sysfs_ + "/buffer/enable", "0")) {
            cleanup();
            return Status::TransportError;
        }

        const auto scan_elements = sysfs_ + "/scan_elements";
        std::error_code ec;
        for (std::filesystem::directory_iterator it(scan_elements, ec), end;
             !ec && it != end; it.increment(ec)) {
            const auto filename = it->path().filename().string();
            if (!filename.ends_with("_en")) continue;
            std::string state;
            const auto path = it->path().string();
            if (!read_text(path, state)) {
                cleanup();
                return Status::TransportError;
            }
            old_enable_.emplace_back(path, state);
            if (!write_text(path, "0")) {
                cleanup();
                return Status::TransportError;
            }
        }
        if (ec) {
            cleanup();
            return Status::TransportError;
        }
        for (const auto& c : channels_) {
            if (!write_text(sysfs_ + "/scan_elements/" + c.name + "_en", "1")) {
                cleanup();
                return Status::TransportError;
            }
        }

        const auto trigger_path = sysfs_ + "/trigger/current_trigger";
        if (!read_text(trigger_path, old_trigger_)) {
            cleanup();
            return Status::Unsupported;
        }
        trigger_saved_ = true;
        std::string trigger = old_trigger_;
        if (entry_->trigger.kind == platform::linux::SelectorKind::Name)
            trigger = entry_->trigger.name;
        if (trigger.empty()) {
            cleanup();
            return Status::Unsupported;
        }
        if (!write_text(trigger_path, trigger) ||
            !write_text(sysfs_ + "/buffer/length", "1") ||
            !write_text(sysfs_ + "/buffer/enable", "1")) {
            cleanup();
            return Status::TransportError;
        }
        initialized_ = true;
        return Status::Ok;
    }

    [[nodiscard]] Status read(mm::imu::Axes& acceleration,
                              mm::imu::Axes& rotation) override {
        if (!initialized_) return Status::NotInitialized;
        pollfd p{fd_, POLLIN, 0};
        int ready;
        do ready = ::poll(&p, 1, entry_->timeout_ms);
        while (ready < 0 && errno == EINTR);
        if (ready == 0) return Status::Timeout;
        if (ready < 0) return failure(errno, true);

        std::vector<std::byte> record(record_size_);
        const auto got = ::read(fd_, record.data(), record.size());
        if (got < 0) return failure(errno, true);
        if (static_cast<std::size_t>(got) != record.size())
            return Status::TransportError;

        std::array<int, 6> values{};
        for (std::size_t i = 0; i < channels_.size(); ++i)
            values[i] = static_cast<int>(
                platform::linux::iio_detail::extract_value(record,
                                                           channels_[i]));
        acceleration = {values[0], values[1], values[2]};
        rotation = {values[3], values[4], values[5]};
        return Status::Ok;
    }

    [[nodiscard]] Status temperature(int& value) override {
        if (!initialized_) return Status::NotInitialized;
        std::string raw, scale, offset;
        if (!read_text(sysfs_ + "/in_temp_raw", raw) ||
            !read_text(sysfs_ + "/in_temp_scale", scale))
            return Status::Unsupported;
        read_text(sysfs_ + "/in_temp_offset", offset);
        const long double result =
            (std::strtold(raw.c_str(), nullptr) +
             std::strtold(offset.c_str(), nullptr)) *
            std::strtold(scale.c_str(), nullptr);
        value = static_cast<int>(std::llround(result * 100));
        return Status::Ok;
    }

    [[nodiscard]] Status sleep() override {
        if (!initialized_) return Status::NotInitialized;
        cleanup();
        return Status::Ok;
    }

private:
    void cleanup() {
        if (!sysfs_.empty()) {
            write_text(sysfs_ + "/buffer/enable", "0");
            for (const auto& saved : old_enable_)
                write_text(saved.first, saved.second);
            if (trigger_saved_)
                write_text(sysfs_ + "/trigger/current_trigger", old_trigger_);
            if (!old_buffer_.empty() && old_buffer_ != "0")
                write_text(sysfs_ + "/buffer/enable", old_buffer_);
        }
        if (fd_ >= 0) ::close(fd_);
        fd_ = -1;
        initialized_ = false;
        channels_.clear();
        old_enable_.clear();
        trigger_saved_ = false;
        old_trigger_.clear();
        sysfs_.clear();
    }

    const platform::linux::ImuEntry* entry_ = nullptr;
    int fd_ = -1;
    bool initialized_ = false;
    std::string device_;
    std::string sysfs_;
    std::string old_trigger_;
    bool trigger_saved_ = false;
    std::string old_buffer_;
    std::vector<std::pair<std::string, std::string>> old_enable_;
    std::vector<Channel> channels_;
    std::size_t record_size_ = 0;
    mm::imu::Scale scale_{};
};

LinuxImu imu;
struct Register { Register() { mm::imu::set_imu(imu); } };
const Register registered;

}

namespace platform::linux::iio_detail {

bool parse_scan_type(std::string_view text, Channel& channel) {
    return ::scan_type(text, channel);
}

std::optional<std::size_t> layout(std::span<Channel> channels) {
    std::vector<std::size_t> order(channels.size());
    for (std::size_t i = 0; i < order.size(); ++i) order[i] = i;
    std::sort(order.begin(), order.end(), [&](auto a, auto b) {
        return channels[a].index < channels[b].index;
    });
    for (std::size_t i = 1; i < order.size(); ++i)
        if (channels[order[i - 1]].index == channels[order[i]].index)
            return {};

    std::size_t size = 0;
    for (const auto position : order) {
        auto& channel = channels[position];
        const auto bytes = channel.storagebits / 8;
        if (bytes == 0) return {};
        size = (size + bytes - 1) / bytes * bytes;
        channel.offset = size;
        size += bytes * channel.repeat;
    }
    return size;
}

std::int64_t extract_value(std::span<const std::byte> record,
                           const Channel& channel) {
    return ::extract(record, channel);
}

std::optional<unsigned int> physical_range(const Channel& channel,
                                           bool acceleration) {
    const long double counts = std::ldexp(1.0L, channel.realbits - 1);
    const long double physical = channel.scale * counts /
        (acceleration ? 9.80665L :
                        (3.14159265358979323846L / 180.0L));
    const auto rounded = std::llround(physical);
    if (rounded <= 0 ||
        std::fabs((physical - rounded) / physical) > 0.0001L)
        return {};
    return static_cast<unsigned int>(rounded);
}

}
