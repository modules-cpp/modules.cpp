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

export namespace platform::linux::evdev_detail {

struct Contact {
    int x = 0;
    int y = 0;
    bool active = false;
};

enum class Kind { None, TypeB, Single, Relative };
enum class SyncAction { Consume, Publish, Discard, Resync };

void consume(Kind kind, int& slot, std::span<Contact> contacts,
             const input_event& event, unsigned int width,
             unsigned int height);
[[nodiscard]] unsigned int coordinate(int value, int minimum, int maximum,
                                      unsigned int extent);
[[nodiscard]] bool apply_type_b_snapshot(
    std::span<Contact> contacts, std::span<const int> tracking,
    std::span<const int> x, std::span<const int> y, int current_slot,
    int& slot);
[[nodiscard]] bool apply_single_snapshot(std::span<Contact> contacts,
                                         int x, int y, bool active);
[[nodiscard]] bool apply_relative_snapshot(std::span<Contact> contacts,
                                           bool active);
[[nodiscard]] SyncAction synchronization(bool& dropped,
                                         const input_event& event);
[[nodiscard]] std::size_t publish_contacts(
    std::span<const Contact> contacts, std::span<mm::touch::Point> output,
    int x_min, int x_max, int y_min, int y_max, unsigned int width,
    unsigned int height, bool invert_x, bool invert_y, bool swap_axes);

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
        initialized_ = false;
        kind_ = Kind::None;
        points_.clear();
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
            if (!classify()) {
                ::close(fd_);
                fd_ = -1;
                return Status::Unsupported;
            }
        } else {
            Status discovery_error = Status::Unsupported;
            for (unsigned int i = 0; i < 64; ++i) {
                path = "/dev/input/event" + std::to_string(i);
                fd_ = ::open(path.c_str(), O_RDONLY | O_NONBLOCK | O_CLOEXEC);
                if (fd_ < 0) {
                    const auto status = failure(errno, false);
                    if (status != Status::Unsupported) discovery_error = status;
                    continue;
                }
                if (classify()) break;
                ::close(fd_);
                fd_ = -1;
            }
            if (fd_ < 0) return discovery_error;
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
                const auto action =
                    platform::linux::evdev_detail::synchronization(
                        dropped_, event);
                if (action == platform::linux::evdev_detail::
                                  SyncAction::Discard)
                    continue;
                if (action == platform::linux::evdev_detail::
                                  SyncAction::Resync) {
                    resync();
                    publish(output, count);
                    return Status::Ok;
                }
                consume(event);
                if (action == platform::linux::evdev_detail::
                                  SyncAction::Publish) {
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
    using Kind = platform::linux::evdev_detail::Kind;
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
        platform::linux::evdev_detail::consume(kind_, slot_, points_, e,
                                                width_, height_);
    }

    unsigned int coordinate(int value, int minimum, int maximum,
                            unsigned int extent) const {
        return platform::linux::evdev_detail::coordinate(
            value, minimum, maximum, extent);
    }

    void publish(std::span<mm::touch::Point> output, std::size_t& count) {
        count = platform::linux::evdev_detail::publish_contacts(
            points_, output, x_min_, x_max_, y_min_, y_max_, width_, height_,
            entry_->invert_x, entry_->invert_y, entry_->swap_axes);
    }

    void resync() {
        if (kind_ == Kind::Relative) {
            std::vector<unsigned long> keys(
                (KEY_MAX + bits_per_word) / bits_per_word);
            if (::ioctl(fd_, EVIOCGKEY(keys.size() * sizeof(unsigned long)),
                        keys.data()) >= 0)
                (void)platform::linux::evdev_detail::apply_relative_snapshot(
                    points_, bit(keys, BTN_LEFT));
            return;
        }
        if (kind_ == Kind::Single) {
            input_absinfo x{}, y{};
            x.value = points_[0].x;
            y.value = points_[0].y;
            (void)::ioctl(fd_, EVIOCGABS(ABS_X), &x);
            (void)::ioctl(fd_, EVIOCGABS(ABS_Y), &y);
            std::vector<unsigned long> keys(
                (KEY_MAX + bits_per_word) / bits_per_word);
            if (::ioctl(fd_, EVIOCGKEY(keys.size() * sizeof(unsigned long)),
                        keys.data()) >= 0)
                (void)platform::linux::evdev_detail::apply_single_snapshot(
                    points_, x.value, y.value, bit(keys, BTN_TOUCH));
            return;
        }
        if (kind_ == Kind::TypeB) {
            auto slots = [&](unsigned int code, std::vector<int>& values) {
                values.assign(points_.size() + 1, 0);
                values[0] = static_cast<int>(code);
                return ::ioctl(fd_, EVIOCGMTSLOTS(
                    values.size() * sizeof(int)), values.data()) >= 0;
            };
            std::vector<int> tracking, x, y;
            const bool complete = slots(ABS_MT_TRACKING_ID, tracking) &&
                                  slots(ABS_MT_POSITION_X, x) &&
                                  slots(ABS_MT_POSITION_Y, y);
            input_absinfo current{};
            if (!complete ||
                ::ioctl(fd_, EVIOCGABS(ABS_MT_SLOT), &current) < 0 ||
                !platform::linux::evdev_detail::apply_type_b_snapshot(
                    points_, std::span{tracking}.subspan(1),
                    std::span{x}.subspan(1), std::span{y}.subspan(1),
                    current.value, slot_))
                for (auto& point : points_) point.active = false;
        }
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

namespace platform::linux::evdev_detail {

void consume(Kind kind, int& slot, std::span<Contact> contacts,
             const input_event& event, unsigned int width,
             unsigned int height) {
    if (contacts.empty()) return;
    if (kind == Kind::TypeB) {
        if (event.type == EV_ABS && event.code == ABS_MT_SLOT &&
            event.value >= 0 &&
            static_cast<std::size_t>(event.value) < contacts.size())
            slot = event.value;
        else if (event.type == EV_ABS && event.code == ABS_MT_TRACKING_ID)
            contacts[slot].active = event.value != -1;
        else if (event.type == EV_ABS && event.code == ABS_MT_POSITION_X)
            contacts[slot].x = event.value;
        else if (event.type == EV_ABS && event.code == ABS_MT_POSITION_Y)
            contacts[slot].y = event.value;
    } else if (kind == Kind::Single) {
        if (event.type == EV_ABS && event.code == ABS_X)
            contacts[0].x = event.value;
        else if (event.type == EV_ABS && event.code == ABS_Y)
            contacts[0].y = event.value;
        else if (event.type == EV_KEY && event.code == BTN_TOUCH)
            contacts[0].active = event.value != 0;
    } else if (kind == Kind::Relative) {
        if (event.type == EV_REL && event.code == REL_X)
            contacts[0].x = std::clamp(contacts[0].x + event.value, 0,
                                       static_cast<int>(width - 1));
        else if (event.type == EV_REL && event.code == REL_Y)
            contacts[0].y = std::clamp(contacts[0].y + event.value, 0,
                                       static_cast<int>(height - 1));
        else if (event.type == EV_KEY && event.code == BTN_LEFT)
            contacts[0].active = event.value != 0;
    }
}

unsigned int coordinate(int value, int minimum, int maximum,
                        unsigned int extent) {
    if (maximum <= minimum || extent == 0) return 0;
    const auto clipped = std::clamp(value, minimum, maximum);
    return static_cast<unsigned int>(
        (static_cast<long long>(clipped - minimum) * (extent - 1)) /
        (maximum - minimum));
}

bool apply_type_b_snapshot(std::span<Contact> contacts,
                           std::span<const int> tracking,
                           std::span<const int> x,
                           std::span<const int> y, int current_slot,
                           int& slot) {
    if (contacts.size() != tracking.size() || contacts.size() != x.size() ||
        contacts.size() != y.size() || current_slot < 0 ||
        static_cast<std::size_t>(current_slot) >= contacts.size())
        return false;
    for (std::size_t i = 0; i < contacts.size(); ++i) {
        contacts[i].active = tracking[i] != -1;
        contacts[i].x = x[i];
        contacts[i].y = y[i];
    }
    slot = current_slot;
    return true;
}

bool apply_single_snapshot(std::span<Contact> contacts, int x, int y,
                           bool active) {
    if (contacts.size() != 1) return false;
    contacts[0] = {x, y, active};
    return true;
}

bool apply_relative_snapshot(std::span<Contact> contacts, bool active) {
    if (contacts.size() != 1) return false;
    contacts[0].active = active;
    return true;
}

SyncAction synchronization(bool& dropped, const input_event& event) {
    if (event.type == EV_SYN && event.code == SYN_DROPPED) {
        dropped = true;
        return SyncAction::Discard;
    }
    if (!dropped)
        return event.type == EV_SYN && event.code == SYN_REPORT
                   ? SyncAction::Publish
                   : SyncAction::Consume;
    if (event.type != EV_SYN || event.code != SYN_REPORT)
        return SyncAction::Discard;
    dropped = false;
    return SyncAction::Resync;
}

std::size_t publish_contacts(
    std::span<const Contact> contacts, std::span<mm::touch::Point> output,
    int x_min, int x_max, int y_min, int y_max, unsigned int width,
    unsigned int height, bool invert_x, bool invert_y, bool swap_axes) {
    std::size_t count = 0;
    for (const auto& contact : contacts) {
        if (!contact.active) continue;
        auto x = coordinate(contact.x, x_min, x_max, width);
        auto y = coordinate(contact.y, y_min, y_max, height);
        if (invert_x) x = width - 1 - x;
        if (invert_y) y = height - 1 - y;
        if (swap_axes) std::swap(x, y);
        if (count < output.size()) output[count] = {x, y};
        ++count;
    }
    return std::min(count, output.size());
}

}
