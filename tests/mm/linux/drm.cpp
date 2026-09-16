// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
#include <array>
#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <drm/drm.h>
#include <drm/drm_mode.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <string>
#include <vector>

import mm.display;
import mm.test;
import platform.linux.display;
import platform.linux.map;

namespace {

using mm::test::expect;

struct FakeDrm {
    int call = 0;
    int fail_at = -1;
    int failure = EIO;
    bool closed = false;
    bool master = false;
    bool handle = false;
    bool framebuffer = false;
    bool mapped = false;
    bool restored = false;
    std::vector<std::byte> memory;
};

FakeDrm fake;

bool fails() {
    if (fake.call++ != fake.fail_at) return false;
    errno = fake.failure;
    return true;
}

int fake_open(const char*, int) {
    if (fake.fail_at == 0) {
        ++fake.call;
        errno = fake.failure;
        return -1;
    }
    if (fails()) return -1;
    return 9;
}

int fake_ioctl(int, unsigned long request, void* argument) {
    if (fails()) return -1;
    if (request == DRM_IOCTL_SET_MASTER) fake.master = true;
    else if (request == DRM_IOCTL_DROP_MASTER) fake.master = false;
    else if (request == DRM_IOCTL_MODE_GETRESOURCES) {
        auto& value = *static_cast<drm_mode_card_res*>(argument);
        value.count_connectors = value.count_encoders = value.count_crtcs = 1;
        if (value.connector_id_ptr)
            *reinterpret_cast<std::uint32_t*>(value.connector_id_ptr) = 10;
        if (value.encoder_id_ptr)
            *reinterpret_cast<std::uint32_t*>(value.encoder_id_ptr) = 20;
        if (value.crtc_id_ptr)
            *reinterpret_cast<std::uint32_t*>(value.crtc_id_ptr) = 30;
    } else if (request == DRM_IOCTL_MODE_GETCONNECTOR) {
        auto& value = *static_cast<drm_mode_get_connector*>(argument);
        value.connection = 1;
        value.connector_type = DRM_MODE_CONNECTOR_HDMIA;
        value.connector_type_id = 1;
        value.encoder_id = 20;
        value.count_modes = 1;
        value.count_encoders = 1;
        if (value.modes_ptr) {
            auto& mode = *reinterpret_cast<drm_mode_modeinfo*>(value.modes_ptr);
            mode.hdisplay = 640;
            mode.vdisplay = 480;
            mode.type = DRM_MODE_TYPE_PREFERRED;
            std::strncpy(mode.name, "640x480", sizeof(mode.name) - 1);
        }
        if (value.encoders_ptr)
            *reinterpret_cast<std::uint32_t*>(value.encoders_ptr) = 20;
    } else if (request == DRM_IOCTL_MODE_GETENCODER) {
        auto& value = *static_cast<drm_mode_get_encoder*>(argument);
        value.crtc_id = 30;
        value.possible_crtcs = 1;
    } else if (request == DRM_IOCTL_MODE_GETCRTC) {
        auto& value = *static_cast<drm_mode_crtc*>(argument);
        value.fb_id = 88;
        value.mode_valid = 1;
        value.mode.hdisplay = 640;
        value.mode.vdisplay = 480;
    } else if (request == DRM_IOCTL_MODE_CREATE_DUMB) {
        auto& value = *static_cast<drm_mode_create_dumb*>(argument);
        value.handle = 40;
        value.pitch = 640 * 4;
        value.size = 640 * 480 * 4;
        fake.handle = true;
    } else if (request == DRM_IOCTL_MODE_ADDFB) {
        static_cast<drm_mode_fb_cmd*>(argument)->fb_id = 50;
        fake.framebuffer = true;
    } else if (request == DRM_IOCTL_MODE_MAP_DUMB) {
        static_cast<drm_mode_map_dumb*>(argument)->offset = 0;
    } else if (request == DRM_IOCTL_MODE_RMFB) {
        fake.framebuffer = false;
    } else if (request == DRM_IOCTL_MODE_DESTROY_DUMB) {
        fake.handle = false;
    } else if (request == DRM_IOCTL_MODE_SETCRTC) {
        const auto& value = *static_cast<drm_mode_crtc*>(argument);
        if (value.fb_id == 88) fake.restored = true;
    }
    return 0;
}

void* fake_map(void*, std::size_t size, int, int, int, std::int64_t) {
    if (fails()) return MAP_FAILED;
    fake.memory.assign(size, std::byte{});
    fake.mapped = true;
    return fake.memory.data();
}

int fake_unmap(void*, std::size_t) {
    fake.mapped = false;
    return 0;
}

int fake_close(int) {
    fake.closed = true;
    return 0;
}

const platform::linux::drm_detail::Operations operations{
    &fake_open, &fake_ioctl, &fake_map, &fake_unmap, &fake_close};

// The rollback case injects an open failure at step zero. An auto card
// selector would discover the next card and recover from it, so pin an
// explicit card before the map is first resolved: an open failure on a
// named card is terminal.
platform::linux::Map explicit_card_map;

void reset(int fail_at = -1, int failure = EIO) {
    fake = {};
    fake.fail_at = fail_at;
    fake.failure = failure;
    explicit_card_map.display.card =
        platform::linux::Selector{platform::linux::SelectorKind::Index, 0, {}};
    platform::linux::set_map(explicit_card_map);

    // resolve() caches in a function-local static, so only the first call in
    // this binary decides what every later one sees. Resolving here, under the
    // pin just registered, is what makes that first call ours rather than
    // whichever suite happens to run before this one. The assertion is the
    // point: if another suite resolves first, the pin is silently ignored and
    // the rollback case below would be testing auto-discovery instead.
    const auto& resolved = platform::linux::resolve();
    expect(resolved.status == platform::linux::MapStatus::Ok && resolved.map != nullptr &&
               resolved.map->display.card.kind == platform::linux::SelectorKind::Index,
           "the explicit card pin reached the resolved map");

    platform::linux::drm_detail::set_operations_for_testing(&operations);
    // A configured board may register its own display after this provider's
    // static registration. Reclaim the seam so this suite deterministically
    // exercises the production DRM provider.
    mm::display::set_display(platform::linux::drm_detail::display_for_testing());
}

void conversion_and_shadow_refresh() {
    expect(platform::linux::drm_detail::rgb565_to_xrgb8888(0xf8, 0) ==
               0x00ff0000,
           "production RGB565 conversion preserves red byte order");
    expect(platform::linux::drm_detail::rgb565_to_xrgb8888(0, 0xf8) !=
               0x00ff0000,
           "byte-flipping fails the production conversion assertion");

    reset();
    auto& display = mm::display::selected_display();
    expect(display.initialize() == mm::display::Status::Ok,
           "the production DRM provider initializes through the fake kernel");
    const std::array red{std::byte{0xf8}, std::byte{0}};
    expect(display.write({0, 0, 1, 1}, red) == mm::display::Status::Ok,
           "write updates the production shadow buffer");
    expect(fake.memory[0] == std::byte{},
           "write does not update scanout before refresh");
    expect(display.refresh(mm::display::Refresh::Partial) ==
               mm::display::Status::Unsupported,
           "partial refresh is refused");
    expect(display.refresh(mm::display::Refresh::Full) ==
               mm::display::Status::Ok && fake.memory[2] == std::byte{0xff},
           "full refresh publishes converted red to XRGB8888 scanout");
    expect(display.sleep() == mm::display::Status::Ok && fake.restored &&
               !fake.master,
           "sleep restores the original CRTC and drops master");
    reset(0);
    (void)display.initialize();
    platform::linux::drm_detail::set_operations_for_testing(nullptr);
}

void rollback_and_error_classification() {
    // Bind the reference after reset reclaims the registration seam, so it
    // names the DRM provider rather than whichever display another module
    // registered earlier in the binary.
    reset(0);
    auto& display = mm::display::selected_display();
    for (int step = 0; step <= 13; ++step) {
        if (step == 6) continue;  // current encoder may fall back compatibly
        reset(step);
        const auto status = display.initialize();
        expect(status != mm::display::Status::Ok,
               "injected initialization failure " + std::to_string(step) +
                   " is reported");
        expect(!fake.master && !fake.handle && !fake.framebuffer &&
                   !fake.mapped && (fake.closed || step == 0),
               "production rollback releases every acquired resource");
    }

    reset(1, EBUSY);
    expect(display.initialize() == mm::display::Status::Busy,
           "SET_MASTER EBUSY is Busy");
    reset(1, EACCES);
    expect(display.initialize() == mm::display::Status::TransportError,
           "SET_MASTER EACCES is TransportError");
    platform::linux::drm_detail::set_operations_for_testing(nullptr);
}

const mm::test::case_ cases[]{
    {"production DRM rollback and errors", &rollback_and_error_classification},
    {"production conversion and shadow refresh", &conversion_and_shadow_refresh},
};
const mm::test::registrar reg{"platform.linux.drm", cases};

}
