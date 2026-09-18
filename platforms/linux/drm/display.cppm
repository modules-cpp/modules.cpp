// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
module;

#include <algorithm>
#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <drm/drm.h>
#include <drm/drm_mode.h>
#include <fcntl.h>
#include <span>
#include <string>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>
#include <vector>

export module platform.linux.display;

import mm.display;
import platform.linux.map;

export namespace platform::linux::drm_detail {

struct Operations {
    int (*open)(const char*, int);
    int (*ioctl)(int, unsigned long, void*);
    void* (*map)(void*, std::size_t, int, int, int, std::int64_t);
    int (*unmap)(void*, std::size_t);
    int (*close)(int);
};

[[nodiscard]] std::uint32_t rgb565_to_xrgb8888(std::uint8_t high,
                                               std::uint8_t low);
void set_operations_for_testing(const Operations* operations);
// Test seam: lets the production display suite select this provider
// deterministically when another provider's static registration may run
// after this one in the same binary.
mm::display::Display& display_for_testing();

}

// A named, non-exported namespace rather than an unnamed one: Clang emits an
// interface unit's unnamed-namespace objects again in every importer, and a
// provider object must exist exactly once.
namespace platform::linux::drm_provider {

using Status = mm::display::Status;
constexpr std::uint32_t connected_connector = 1;

int real_open(const char* path, int flags) { return ::open(path, flags); }
int real_ioctl(int fd, unsigned long request, void* argument) {
    return ::ioctl(fd, request, argument);
}
void* real_map(void* address, std::size_t size, int protection, int flags,
               int fd, std::int64_t offset) {
    return ::mmap(address, size, protection, flags, fd,
                  static_cast<off_t>(offset));
}
int real_unmap(void* address, std::size_t size) {
    return ::munmap(address, size);
}
int real_close(int fd) { return ::close(fd); }

const platform::linux::drm_detail::Operations real_operations{
    &real_open, &real_ioctl, &real_map, &real_unmap, &real_close};
const platform::linux::drm_detail::Operations* operations = &real_operations;

Status failure(int value, bool explicit_path = true) {
    if ((value == ENOENT || value == ENODEV) && !explicit_path)
        return Status::Unsupported;
    if ((value == ENOENT || value == ENODEV) && explicit_path)
        return Status::BadArgument;
    if (value == EACCES || value == EPERM) return Status::TransportError;
    if (value == EBUSY) return Status::Busy;
    if (value == ETIMEDOUT) return Status::Timeout;
    return Status::TransportError;
}

inline std::uint32_t rgb565_to_xrgb8888(std::uint8_t high, std::uint8_t low) {
    const std::uint16_t pixel =
        (static_cast<std::uint16_t>(high) << 8) | low;
    const std::uint32_t r = (pixel >> 11) & 0x1f;
    const std::uint32_t g = (pixel >> 5) & 0x3f;
    const std::uint32_t b = pixel & 0x1f;
    const std::uint32_t r8 = (r * 255 + 15) / 31;
    const std::uint32_t g8 = (g * 255 + 31) / 63;
    const std::uint32_t b8 = (b * 255 + 15) / 31;
    return (r8 << 16) | (g8 << 8) | b8;
}

std::string connector_name(const drm_mode_get_connector& connector) {
    const char* type = "Unknown";
    switch (connector.connector_type) {
        case DRM_MODE_CONNECTOR_VGA: type = "VGA"; break;
        case DRM_MODE_CONNECTOR_DVII: type = "DVI-I"; break;
        case DRM_MODE_CONNECTOR_DVID: type = "DVI-D"; break;
        case DRM_MODE_CONNECTOR_DVIA: type = "DVI-A"; break;
        case DRM_MODE_CONNECTOR_Composite: type = "Composite"; break;
        case DRM_MODE_CONNECTOR_SVIDEO: type = "SVIDEO"; break;
        case DRM_MODE_CONNECTOR_LVDS: type = "LVDS"; break;
        case DRM_MODE_CONNECTOR_Component: type = "Component"; break;
        case DRM_MODE_CONNECTOR_9PinDIN: type = "DIN"; break;
        case DRM_MODE_CONNECTOR_DisplayPort: type = "DP"; break;
        case DRM_MODE_CONNECTOR_HDMIA: type = "HDMI-A"; break;
        case DRM_MODE_CONNECTOR_HDMIB: type = "HDMI-B"; break;
        case DRM_MODE_CONNECTOR_TV: type = "TV"; break;
        case DRM_MODE_CONNECTOR_eDP: type = "eDP"; break;
        case DRM_MODE_CONNECTOR_VIRTUAL: type = "Virtual"; break;
        case DRM_MODE_CONNECTOR_DSI: type = "DSI"; break;
        case DRM_MODE_CONNECTOR_DPI: type = "DPI"; break;
        case DRM_MODE_CONNECTOR_WRITEBACK: type = "Writeback"; break;
        case DRM_MODE_CONNECTOR_SPI: type = "SPI"; break;
        case DRM_MODE_CONNECTOR_USB: type = "USB"; break;
    }
    return std::string(type) + "-" +
           std::to_string(connector.connector_type_id);
}

class LinuxDisplay final : public mm::display::Display {
public:
    ~LinuxDisplay() override { release(); }

    [[nodiscard]] mm::display::Geometry geometry() const override {
        return {width_, height_, 16};
    }

    [[nodiscard]] Status initialize() override {
        release();
        const auto& resolved = platform::linux::resolve();
        if (resolved.status != platform::linux::MapStatus::Ok ||
            !resolved.map)
            return Status::BadArgument;
        entry_ = &resolved.map->display;

        const bool explicit_path =
            entry_->card.kind != platform::linux::SelectorKind::Auto;
        if (explicit_path) {
            const auto path = card_path(entry_->card);
            fd_ = operations->open(path.c_str(), O_RDWR | O_CLOEXEC);
            if (fd_ < 0) return failure(errno, true);
        } else {
            Status discovery_error = Status::Unsupported;
            for (unsigned int i = 0; i < 64 && fd_ < 0; ++i) {
                const auto path = "/dev/dri/card" + std::to_string(i);
                fd_ = operations->open(path.c_str(), O_RDWR | O_CLOEXEC);
                if (fd_ < 0) {
                    const auto status = failure(errno, false);
                    if (status != Status::Unsupported) discovery_error = status;
                }
            }
            if (fd_ < 0) return discovery_error;
        }

        if (operations->ioctl(fd_, DRM_IOCTL_SET_MASTER, nullptr) < 0) {
            const auto result = failure(errno, explicit_path);
            release();
            return result;
        }
        master_ = true;

        drm_mode_card_res resources{};
        if (operations->ioctl(fd_, DRM_IOCTL_MODE_GETRESOURCES,
                              &resources) < 0)
            return failed();

        std::vector<std::uint32_t> connectors(resources.count_connectors);
        std::vector<std::uint32_t> encoders(resources.count_encoders);
        std::vector<std::uint32_t> crtcs(resources.count_crtcs);
        resources.connector_id_ptr =
            reinterpret_cast<std::uintptr_t>(connectors.data());
        resources.encoder_id_ptr =
            reinterpret_cast<std::uintptr_t>(encoders.data());
        resources.crtc_id_ptr =
            reinterpret_cast<std::uintptr_t>(crtcs.data());
        if (operations->ioctl(fd_, DRM_IOCTL_MODE_GETRESOURCES,
                              &resources) < 0)
            return failed();

        std::size_t connector_index = 0;
        if (entry_->connector.kind == platform::linux::SelectorKind::Index)
            connector_index = entry_->connector.index;

        bool found = false;
        drm_mode_get_connector connector{};
        std::vector<drm_mode_modeinfo> modes;
        std::vector<std::uint32_t> connector_encoders;
        for (std::size_t i = 0; i < connectors.size(); ++i) {
            if (entry_->connector.kind == platform::linux::SelectorKind::Index &&
                i != connector_index)
                continue;
            connector = {};
            connector.connector_id = connectors[i];
            if (operations->ioctl(fd_, DRM_IOCTL_MODE_GETCONNECTOR,
                                  &connector) < 0)
                continue;
            modes.resize(connector.count_modes);
            connector_encoders.resize(connector.count_encoders);
            connector.modes_ptr =
                reinterpret_cast<std::uintptr_t>(modes.data());
            connector.encoders_ptr =
                reinterpret_cast<std::uintptr_t>(connector_encoders.data());
            if (operations->ioctl(fd_, DRM_IOCTL_MODE_GETCONNECTOR,
                                  &connector) < 0)
                continue;
            const bool selected_name =
                entry_->connector.kind != platform::linux::SelectorKind::Name ||
                entry_->connector.name == connector_name(connector);
            if (selected_name && connector.connection == connected_connector &&
                connector.count_modes > 0) {
                connector_id_ = connector.connector_id;
                found = true;
                break;
            }
        }
        if (!found) { release(); return Status::Unsupported; }

        std::size_t mode_index = 0;
        if (entry_->mode.kind == platform::linux::SelectorKind::Index) {
            mode_index = entry_->mode.index;
        } else if (entry_->mode.kind == platform::linux::SelectorKind::Auto) {
            for (std::size_t i = 0; i < modes.size(); ++i) {
                if ((modes[i].type & DRM_MODE_TYPE_PREFERRED) != 0) {
                    mode_index = i;
                    break;
                }
            }
        }
        if (mode_index >= modes.size()) {
            release();
            return Status::BadArgument;
        }
        mode_ = modes[mode_index];

        if (entry_->mode.kind == platform::linux::SelectorKind::Name) {
            found = false;
            for (const auto& mode : modes) {
                if (entry_->mode.name == mode.name) {
                    mode_ = mode;
                    found = true;
                    break;
                }
            }
            if (!found) { release(); return Status::BadArgument; }
        }

        drm_mode_get_encoder encoder{};
        encoder.encoder_id = connector.encoder_id;
        if (encoder.encoder_id &&
            operations->ioctl(fd_, DRM_IOCTL_MODE_GETENCODER, &encoder) == 0)
            crtc_id_ = encoder.crtc_id;
        if (!crtc_id_) {
            for (const auto encoder_id : connector_encoders) {
                drm_mode_get_encoder candidate{};
                candidate.encoder_id = encoder_id;
                if (operations->ioctl(fd_, DRM_IOCTL_MODE_GETENCODER,
                                      &candidate) < 0)
                    continue;
                for (std::size_t i = 0; i < crtcs.size() && i < 32; ++i) {
                    if ((candidate.possible_crtcs & (1U << i)) != 0) {
                        crtc_id_ = crtcs[i];
                        break;
                    }
                }
                if (crtc_id_) break;
            }
        }
        if (!crtc_id_) { release(); return Status::Unsupported; }

        saved_ = {};
        saved_.crtc_id = crtc_id_;
        if (operations->ioctl(fd_, DRM_IOCTL_MODE_GETCRTC, &saved_) < 0)
            return failed();
        saved_valid_ = true;

        saved_connectors_.clear();
        for (const auto id : connectors) {
            drm_mode_get_connector item{};
            item.connector_id = id;
            if (operations->ioctl(fd_, DRM_IOCTL_MODE_GETCONNECTOR, &item) < 0)
                return failed();
            if (!item.encoder_id) continue;
            drm_mode_get_encoder enc{};
            enc.encoder_id = item.encoder_id;
            if (operations->ioctl(fd_, DRM_IOCTL_MODE_GETENCODER, &enc) < 0)
                return failed();
            if (enc.crtc_id == crtc_id_)
                saved_connectors_.push_back(id);
        }

        width_ = entry_->width ? entry_->width : mode_.hdisplay;
        height_ = entry_->height ? entry_->height : mode_.vdisplay;
        if (width_ > mode_.hdisplay || height_ > mode_.vdisplay) {
            release();
            return Status::BadArgument;
        }

        drm_mode_create_dumb create{};
        create.width = mode_.hdisplay;
        create.height = mode_.vdisplay;
        create.bpp = 32;
        if (operations->ioctl(fd_, DRM_IOCTL_MODE_CREATE_DUMB, &create) < 0)
            return failed();
        handle_ = create.handle;
        pitch_ = create.pitch;
        size_ = create.size;

        drm_mode_fb_cmd fb{};
        fb.width = mode_.hdisplay;
        fb.height = mode_.vdisplay;
        fb.pitch = pitch_;
        fb.bpp = 32;
        fb.depth = 24;
        fb.handle = handle_;
        if (operations->ioctl(fd_, DRM_IOCTL_MODE_ADDFB, &fb) < 0)
            return failed();
        fb_id_ = fb.fb_id;

        drm_mode_map_dumb map{};
        map.handle = handle_;
        if (operations->ioctl(fd_, DRM_IOCTL_MODE_MAP_DUMB, &map) < 0)
            return failed();
        mapping_ = operations->map(nullptr, size_, PROT_READ | PROT_WRITE,
                                   MAP_SHARED, fd_, map.offset);
        if (mapping_ == MAP_FAILED) {
            mapping_ = nullptr;
            return failed();
        }

        shadow_.assign(static_cast<std::size_t>(width_) * height_ * 2,
                       std::byte{});
        initialized_ = true;
        return Status::Ok;
    }

    [[nodiscard]] Status clear(mm::display::Color color) override {
        if (!initialized_) return Status::NotInitialized;
        std::uint16_t pixel = 0;
        if (color == mm::display::Color::White) pixel = 0xffff;
        else if (color == mm::display::Color::Red) pixel = 0xf800;
        const auto high = static_cast<std::byte>((pixel >> 8) & 0xff);
        const auto low = static_cast<std::byte>(pixel & 0xff);
        for (std::size_t i = 0; i < shadow_.size(); i += 2) {
            shadow_[i] = high;
            shadow_[i + 1] = low;
        }
        return Status::Ok;
    }

    [[nodiscard]] Status write(mm::display::Rectangle rectangle,
                               std::span<const std::byte> data) override {
        if (!initialized_) return Status::NotInitialized;
        if (rectangle.width == 0 || rectangle.height == 0 ||
            rectangle.x + rectangle.width > width_ ||
            rectangle.y + rectangle.height > height_ ||
            data.size() != static_cast<std::size_t>(rectangle.width) *
                               rectangle.height * 2)
            return Status::BadArgument;

        for (unsigned int row = 0; row < rectangle.height; ++row) {
            const auto source = data.subspan(
                static_cast<std::size_t>(row) * rectangle.width * 2,
                static_cast<std::size_t>(rectangle.width) * 2);
            auto target = std::span{shadow_}.subspan(
                (static_cast<std::size_t>(rectangle.y + row) * width_ +
                 rectangle.x) * 2,
                source.size());
            std::copy(source.begin(), source.end(), target.begin());
        }
        return Status::Ok;
    }

    [[nodiscard]] Status refresh(mm::display::Refresh refresh) override {
        if (!initialized_) return Status::NotInitialized;
        if (refresh == mm::display::Refresh::Partial)
            return Status::Unsupported;

        const unsigned int offset_x =
            (mode_.hdisplay > width_) ? (mode_.hdisplay - width_) / 2 : 0;
        const unsigned int offset_y =
            (mode_.vdisplay > height_) ? (mode_.vdisplay - height_) / 2 : 0;
        auto* scanout = static_cast<std::uint8_t*>(mapping_);
        for (unsigned int y = 0; y < height_; ++y) {
            auto* row_dst = reinterpret_cast<std::uint32_t*>(
                scanout +
                static_cast<std::size_t>(offset_y + y) * pitch_ +
                static_cast<std::size_t>(offset_x) * 4);
            for (unsigned int x = 0; x < width_; ++x) {
                const std::size_t src_idx =
                    (static_cast<std::size_t>(y) * width_ + x) * 2;
                const auto high =
                    static_cast<std::uint8_t>(shadow_[src_idx]);
                const auto low =
                    static_cast<std::uint8_t>(shadow_[src_idx + 1]);
                row_dst[x] =
                    platform::linux::drm_detail::rgb565_to_xrgb8888(high, low);
            }
        }

        drm_mode_crtc command{};
        command.crtc_id = crtc_id_;
        command.fb_id = fb_id_;
        command.set_connectors_ptr =
            reinterpret_cast<std::uintptr_t>(&connector_id_);
        command.count_connectors = 1;
        command.mode = mode_;
        command.mode_valid = 1;
        if (operations->ioctl(fd_, DRM_IOCTL_MODE_SETCRTC, &command) < 0)
            return failure(errno);
        active_ = true;
        return Status::Ok;
    }

    [[nodiscard]] Status sleep() override {
        if (!initialized_) return Status::NotInitialized;
        restore();
        initialized_ = false;
        return Status::Ok;
    }

private:
    std::string card_path(const platform::linux::Selector& selector) const {
        if (selector.kind == platform::linux::SelectorKind::Name)
            return selector.name;
        if (selector.kind == platform::linux::SelectorKind::Index)
            return "/dev/dri/card" + std::to_string(selector.index);
        return "/dev/dri/card0";
    }

    Status failed() {
        const auto value = errno;
        release();
        return failure(value);
    }

    void restore() {
        if (fd_ < 0) return;
        if (saved_valid_ && active_) {
            saved_.set_connectors_ptr =
                reinterpret_cast<std::uintptr_t>(saved_connectors_.data());
            saved_.count_connectors = saved_connectors_.size();
            operations->ioctl(fd_, DRM_IOCTL_MODE_SETCRTC, &saved_);
        }
        active_ = false;
        if (master_) {
            operations->ioctl(fd_, DRM_IOCTL_DROP_MASTER, nullptr);
            master_ = false;
        }
    }

    void release() {
        if (fd_ < 0) return;
        restore();
        if (mapping_) {
            operations->unmap(mapping_, size_);
            mapping_ = nullptr;
        }
        if (fb_id_) {
            std::uint32_t id = fb_id_;
            operations->ioctl(fd_, DRM_IOCTL_MODE_RMFB, &id);
            fb_id_ = 0;
        }
        if (handle_) {
            drm_mode_destroy_dumb destroy{};
            destroy.handle = handle_;
            operations->ioctl(fd_, DRM_IOCTL_MODE_DESTROY_DUMB, &destroy);
            handle_ = 0;
        }
        operations->close(fd_);
        fd_ = -1;
        initialized_ = false;
        shadow_.clear();
    }

    const platform::linux::DisplayEntry* entry_ = nullptr;
    int fd_ = -1;
    bool master_ = false;
    bool initialized_ = false;
    bool active_ = false;
    bool saved_valid_ = false;
    unsigned int width_ = 0;
    unsigned int height_ = 0;
    std::uint32_t connector_id_ = 0;
    std::uint32_t crtc_id_ = 0;
    std::uint32_t fb_id_ = 0;
    std::uint32_t handle_ = 0;
    std::uint32_t pitch_ = 0;
    std::uint64_t size_ = 0;
    void* mapping_ = nullptr;
    drm_mode_modeinfo mode_{};
    drm_mode_crtc saved_{};
    std::vector<std::uint32_t> saved_connectors_;
    std::vector<std::byte> shadow_;
};

LinuxDisplay display;
struct Register { Register() { mm::display::set_display(display); } };
const Register registered;

}

namespace platform::linux::drm_detail {

std::uint32_t rgb565_to_xrgb8888(std::uint8_t high, std::uint8_t low) {
    return drm_provider::rgb565_to_xrgb8888(high, low);
}

void set_operations_for_testing(const Operations* replacement) {
    drm_provider::operations = replacement ? replacement : &drm_provider::real_operations;
}

mm::display::Display& display_for_testing() { return drm_provider::display; }

}
