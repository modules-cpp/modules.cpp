// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
module;

#include <algorithm>
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <linux/input.h>
#include <span>
#include <string>
#include <sys/ioctl.h>
#include <unistd.h>
#include <vector>

export module platform.linux.touch;

import mm.touch;
import platform.linux.map;

namespace platform::linux::evdev_detail {

struct Contact {
    int x = 0;
    int y = 0;
    bool active = false;
};

}

namespace {

using Status = mm::touch::Status;
constexpr std::size_t bits_per_word = sizeof(unsigned long) * 8;

bool bit(const std::vector<unsigned long>& values, unsigned int number) {
    const auto word = number / bits_per_word;
    return word < values.size() &&
           (values[word] & (1UL << (number % bits_per_word))) != 0;
}

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

class LinuxTouch final : public mm::touch::Touch {
public:
    ~LinuxTouch() override {
        if (fd_ >= 0) ::close(fd_);
    }

    [[nodiscard]] mm::touch::Geometry geometry() const override {
        return {width_, height_, static_cast<unsigned int>(points_.size())};
    }

    [[nodiscard]] Status initialize() override {
        if (fd_ >= 0) { ::close(fd_); fd_ = -1; }
        const auto& resolved = platform::linux::resolve();
        if (resolved.status != platform::linux::MapStatus::Ok ||
            !resolved.map)
            return Status::BadArgument;
        entry_ = &resolved.map->touch;
        width_ = resolved.map->display.width;
        height_ = resolved.map->display.height;
        if (width_ == 0) width_ = 1;
        if (height_ == 0) height_ = 1;

        const bool explicit_path =
            entry_->device.kind != platform::linux::SelectorKind::Auto;
        std::string path;
        if (entry_->device.kind == platform::linux::SelectorKind::Name)
            path = entry_->device.name;
        else if (entry_->device.kind == platform::linux::SelectorKind::Index)
            path = "/dev/input/event" + std::to_string(entry_->device.index);

        if (explicit_path) {
            fd_ = ::open(path.c_str(), O_RDONLY | O_NONBLOCK | O_CLOEXEC);
            if (fd_ < 0) return failure(errno, true);
            if (!classify()) return Status::Unsupported;
        } else {
            for (unsigned int i = 0; i < 64; ++i) {
                path = "/dev/input/event" + std::to_string(i);
                fd_ = ::open(path.c_str(), O_RDONLY | O_NONBLOCK | O_CLOEXEC);
                if (fd_ < 0) continue;
                if (classify()) break;
                ::close(fd_);
                fd_ = -1;
            }
            if (fd_ < 0) return Status::Unsupported;
        }
        initialized_ = true;
        return Status::Ok;
    }

    [[nodiscard]] Status read(std::span<mm::touch::Point> output,
                              std::size_t& count) override {
        if (!initialized_) return Status::NotInitialized;
        count = 0;
        input_event events[32]{};
        while (true) {
            const auto bytes = ::read(fd_, events, sizeof(events));
            if (bytes < 0 && (errno == EAGAIN || errno == EWOULDBLOCK))
                break;
            if (bytes < 0 && errno == EINTR) continue;
            if (bytes <= 0) return failure(bytes == 0 ? ENODEV : errno, true);
            if (bytes % sizeof(input_event) != 0)
                return Status::TransportError;

            const auto num_events =
                static_cast<std::size_t>(bytes) / sizeof(input_event);
            for (std::size_t i = 0; i < num_events; ++i) {
                const auto& event = events[i];
                if (event.type == EV_SYN && event.code == SYN_DROPPED) {
                    dropped_ = true;
                    continue;
                }
                if (dropped_) {
                    if (event.type == EV_SYN && event.code == SYN_REPORT) {
                        dropped_ = false;
                        resync();
                    }
                    continue;
                }
                consume(event);
                if (event.type == EV_SYN && event.code == SYN_REPORT) {
                    publish(output, count);
                    return Status::Ok;
                }
            }
        }
        return Status::Ok;
    }

    [[nodiscard]] Status sleep() override {
        if (!initialized_) return Status::NotInitialized;
        if (fd_ >= 0) { ::close(fd_); fd_ = -1; }
        initialized_ = false;
        return Status::Ok;
    }

private:
    enum class Kind { None, TypeB, Single, Relative };
    using Contact = platform::linux::evdev_detail::Contact;

    bool classify() {
        std::vector<unsigned long> ev(
            (EV_MAX + bits_per_word) / bits_per_word);
        if (::ioctl(fd_, EVIOCGBIT(0, ev.size() * sizeof(unsigned long)),
                    ev.data()) < 0)
            return false;

        std::vector<unsigned long> abs(
            (ABS_MAX + bits_per_word) / bits_per_word);
        std::vector<unsigned long> rel(
            (REL_MAX + bits_per_word) / bits_per_word);
        std::vector<unsigned long> keys(
            (KEY_MAX + bits_per_word) / bits_per_word);
        ::ioctl(fd_, EVIOCGBIT(EV_ABS, abs.size() * sizeof(unsigned long)),
                abs.data());
        ::ioctl(fd_, EVIOCGBIT(EV_REL, rel.size() * sizeof(unsigned long)),
                rel.data());
        ::ioctl(fd_, EVIOCGBIT(EV_KEY, keys.size() * sizeof(unsigned long)),
                keys.data());

        if (bit(abs, ABS_MT_POSITION_X) && bit(abs, ABS_MT_POSITION_Y)) {
            if (!bit(abs, ABS_MT_SLOT)) return false;
            kind_ = Kind::TypeB;
            input_absinfo slots{};
            if (::ioctl(fd_, EVIOCGABS(ABS_MT_SLOT), &slots) < 0)
                return false;
            points_.assign(static_cast<std::size_t>(slots.maximum + 1), {});
            axis(ABS_MT_POSITION_X, x_min_, x_max_);
            axis(ABS_MT_POSITION_Y, y_min_, y_max_);
            return true;
        }
        if (bit(abs, ABS_X) && bit(abs, ABS_Y) && bit(keys, BTN_TOUCH)) {
            kind_ = Kind::Single;
            points_.assign(1, {});
            axis(ABS_X, x_min_, x_max_);
            axis(ABS_Y, y_min_, y_max_);
            return true;
        }
        if (bit(rel, REL_X) && bit(rel, REL_Y) && bit(keys, BTN_LEFT)) {
            kind_ = Kind::Relative;
            points_.assign(1, {});
            x_min_ = y_min_ = 0;
            x_max_ = width_ - 1;
            y_max_ = height_ - 1;
            return true;
        }
        return false;
    }

    void axis(unsigned int code, int& minimum, int& maximum) {
        input_absinfo info{};
        if (::ioctl(fd_, EVIOCGABS(code), &info) == 0) {
            minimum = info.minimum;
            maximum = info.maximum;
        } else {
            minimum = 0;
            maximum = 1;
        }
    }

    void consume(const input_event& e) {
        if (kind_ == Kind::TypeB) {
            if (e.type == EV_ABS && e.code == ABS_MT_SLOT &&
                e.value >= 0 && static_cast<std::size_t>(e.value) < points_.size())
                slot_ = e.value;
            else if (e.type == EV_ABS && e.code == ABS_MT_TRACKING_ID)
                points_[slot_].active = (e.value != -1);
            else if (e.type == EV_ABS && e.code == ABS_MT_POSITION_X)
                points_[slot_].x = e.value;
            else if (e.type == EV_ABS && e.code == ABS_MT_POSITION_Y)
                points_[slot_].y = e.value;
        } else if (kind_ == Kind::Single) {
            if (e.type == EV_ABS && e.code == ABS_X)
                points_[0].x = e.value;
            else if (e.type == EV_ABS && e.code == ABS_Y)
                points_[0].y = e.value;
            else if (e.type == EV_KEY && e.code == BTN_TOUCH)
                points_[0].active = (e.value != 0);
        } else if (kind_ == Kind::Relative) {
            if (e.type == EV_REL && e.code == REL_X)
                points_[0].x = std::clamp(points_[0].x + e.value, 0,
                                          static_cast<int>(width_ - 1));
            else if (e.type == EV_REL && e.code == REL_Y)
                points_[0].y = std::clamp(points_[0].y + e.value, 0,
                                          static_cast<int>(height_ - 1));
            else if (e.type == EV_KEY && e.code == BTN_LEFT)
                points_[0].active = (e.value != 0);
        }
    }

    unsigned int coordinate(int value, int minimum, int maximum,
                            unsigned int extent) const {
        if (maximum <= minimum || extent == 0) return 0;
        const auto clipped = std::clamp(value, minimum, maximum);
        return static_cast<unsigned int>(
            (static_cast<long long>(clipped - minimum) * (extent - 1)) /
            (maximum - minimum));
    }

    void publish(std::span<mm::touch::Point> output, std::size_t& count) {
        count = 0;
        for (const auto& contact : points_) {
            if (!contact.active) continue;
            auto x = coordinate(contact.x, x_min_, x_max_, width_);
            auto y = coordinate(contact.y, y_min_, y_max_, height_);
            if (entry_->invert_x) x = width_ - 1 - x;
            if (entry_->invert_y) y = height_ - 1 - y;
            if (entry_->swap_axes) std::swap(x, y);
            if (count < output.size()) output[count] = {x, y};
            ++count;
        }
        if (count > output.size()) count = output.size();
    }

    void resync() {
        if (kind_ == Kind::Relative) {
            std::vector<unsigned long> keys(
                (KEY_MAX + bits_per_word) / bits_per_word);
            if (::ioctl(fd_, EVIOCGKEY(keys.size() * sizeof(unsigned long)),
                        keys.data()) >= 0)
                points_[0].active = bit(keys, BTN_LEFT);
            return;
        }
        if (kind_ == Kind::Single) {
            input_absinfo x{}, y{};
            if (::ioctl(fd_, EVIOCGABS(ABS_X), &x) == 0) points_[0].x = x.value;
            if (::ioctl(fd_, EVIOCGABS(ABS_Y), &y) == 0) points_[0].y = y.value;
            std::vector<unsigned long> keys(
                (KEY_MAX + bits_per_word) / bits_per_word);
            if (::ioctl(fd_, EVIOCGKEY(keys.size() * sizeof(unsigned long)),
                        keys.data()) >= 0)
                points_[0].active = bit(keys, BTN_TOUCH);
            return;
        }
        for (auto& point : points_) point.active = false;
    }

    const platform::linux::TouchEntry* entry_ = nullptr;
    int fd_ = -1;
    bool initialized_ = false;
    bool dropped_ = false;
    Kind kind_ = Kind::None;
    unsigned int width_ = 1;
    unsigned int height_ = 1;
    int x_min_ = 0;
    int x_max_ = 1;
    int y_min_ = 0;
    int y_max_ = 1;
    int slot_ = 0;
    std::vector<Contact> points_;
};

LinuxTouch touch;
struct Register { Register() { mm::touch::set_touch(touch); } };
const Register registered;

}
