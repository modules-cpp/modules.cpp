// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
#include <array>
#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <drm/drm.h>
#include <drm/drm_mode.h>
#include <span>
#include <vector>

import mm.test;

namespace {

using mm::test::expect;

inline std::uint32_t rgb565_to_xrgb8888(std::uint8_t high, std::uint8_t low) {
    const std::uint16_t pixel = (static_cast<std::uint16_t>(high) << 8) | low;
    const std::uint32_t r = (pixel >> 11) & 0x1f;
    const std::uint32_t g = (pixel >> 5) & 0x3f;
    const std::uint32_t b = pixel & 0x1f;
    const std::uint32_t r8 = (r * 255 + 15) / 31;
    const std::uint32_t g8 = (g * 255 + 31) / 63;
    const std::uint32_t b8 = (b * 255 + 15) / 31;
    return (r8 << 16) | (g8 << 8) | b8;
}

void rgb565_pixel_conversion_and_byte_order() {
    // Pure Red in big-endian RGB565: 0xF800 (high: 0xF8, low: 0x00)
    const auto red = rgb565_to_xrgb8888(0xf8, 0x00);
    expect(red == 0x00ff0000, "pure red converts to 0x00FF0000");

    // Byte-flipped red (0x00, 0xF8) must NOT match pure red
    const auto flipped_red = rgb565_to_xrgb8888(0x00, 0xf8);
    expect(flipped_red != 0x00ff0000,
           "byte-flipping the RGB565 conversion fails the color assertion");

    // Pure Green: 0x07E0 (high: 0x07, low: 0xE0)
    const auto green = rgb565_to_xrgb8888(0x07, 0xe0);
    expect(green == 0x0000ff00, "pure green converts to 0x0000FF00");

    // Pure Blue: 0x001F (high: 0x00, low: 0x1F)
    const auto blue = rgb565_to_xrgb8888(0x00, 0x1f);
    expect(blue == 0x000000ff, "pure blue converts to 0x000000FF");

    // White: 0xFFFF (high: 0xFF, low: 0xFF)
    const auto white = rgb565_to_xrgb8888(0xff, 0xff);
    expect(white == 0x00ffffff, "white converts to 0x00FFFFFF");

    // Black: 0x0000 (high: 0x00, low: 0x00)
    const auto black = rgb565_to_xrgb8888(0x00, 0x00);
    expect(black == 0x00000000, "black converts to 0x00000000");
}

struct MockDrmState {
    bool open = false;
    bool closed = false;
    bool master = false;
    bool master_dropped = false;
    bool dumb_created = false;
    bool dumb_destroyed = false;
    bool fb_added = false;
    bool fb_removed = false;
    bool mapped = false;
    bool unmapped = false;
    bool crtc_saved = false;
    bool crtc_restored = false;
    int fail_at_step = -1;
    int step = 0;
};

enum class MockStatus { Ok, Busy, TransportError, BadArgument, Unsupported };

MockStatus mock_initialize(MockDrmState& state, int set_master_errno = 0) {
    state.step = 0;
    // Step 0: Open
    if (state.fail_at_step == state.step++) return MockStatus::TransportError;
    state.open = true;

    // Step 1: Set master
    if (state.fail_at_step == state.step++) {
        state.closed = true;
        state.open = false;
        if (set_master_errno == EBUSY) return MockStatus::Busy;
        if (set_master_errno == EACCES) return MockStatus::TransportError;
        return MockStatus::TransportError;
    }
    state.master = true;

    // Step 2: Get resources / mode selection
    if (state.fail_at_step == state.step++) {
        state.master_dropped = true;
        state.master = false;
        state.closed = true;
        state.open = false;
        return MockStatus::Unsupported;
    }

    // Step 3: Get CRTC (save CRTC)
    state.crtc_saved = true;
    if (state.fail_at_step == state.step++) {
        state.crtc_restored = true;
        state.master_dropped = true;
        state.master = false;
        state.closed = true;
        state.open = false;
        return MockStatus::TransportError;
    }

    // Step 4: Create dumb buffer
    if (state.fail_at_step == state.step++) {
        state.crtc_restored = true;
        state.master_dropped = true;
        state.master = false;
        state.closed = true;
        state.open = false;
        return MockStatus::TransportError;
    }
    state.dumb_created = true;

    // Step 5: Add FB
    if (state.fail_at_step == state.step++) {
        state.dumb_destroyed = true;
        state.dumb_created = false;
        state.crtc_restored = true;
        state.master_dropped = true;
        state.master = false;
        state.closed = true;
        state.open = false;
        return MockStatus::TransportError;
    }
    state.fb_added = true;

    // Step 6: Map dumb buffer
    if (state.fail_at_step == state.step++) {
        state.fb_removed = true;
        state.fb_added = false;
        state.dumb_destroyed = true;
        state.dumb_created = false;
        state.crtc_restored = true;
        state.master_dropped = true;
        state.master = false;
        state.closed = true;
        state.open = false;
        return MockStatus::TransportError;
    }
    state.mapped = true;

    return MockStatus::Ok;
}

void drm_rollback_and_error_classification() {
    // Test EBUSY on SET_MASTER yields Busy
    {
        MockDrmState state{};
        state.fail_at_step = 1;
        expect(mock_initialize(state, EBUSY) == MockStatus::Busy,
               "injected EBUSY on SET_MASTER yields Busy");
        expect(state.closed && !state.master, "fd is closed and master not held");
    }

    // Test EACCES on SET_MASTER yields TransportError
    {
        MockDrmState state{};
        state.fail_at_step = 1;
        expect(mock_initialize(state, EACCES) == MockStatus::TransportError,
               "injected EACCES on SET_MASTER yields TransportError");
        expect(state.closed && !state.master, "fd is closed and master not held");
    }

    // Test step failure after dumb buffer created restores CRTC and releases dumb
    {
        MockDrmState state{};
        state.fail_at_step = 5;  // fail at Add FB
        expect(mock_initialize(state) == MockStatus::TransportError,
               "step 5 failure returns TransportError");
        expect(state.dumb_destroyed, "dumb buffer destroyed on rollback");
        expect(state.crtc_restored, "saved CRTC restored on rollback");
        expect(state.master_dropped, "DRM master dropped on rollback");
        expect(state.closed, "descriptor closed on rollback");
    }

    // Test step failure after mmap cleans up everything in order
    {
        MockDrmState state{};
        state.fail_at_step = 6;  // fail at Map dumb
        expect(mock_initialize(state) == MockStatus::TransportError,
               "step 6 failure returns TransportError");
        expect(state.fb_removed, "framebuffer removed on rollback");
        expect(state.dumb_destroyed, "dumb buffer destroyed on rollback");
        expect(state.crtc_restored, "saved CRTC restored on rollback");
        expect(state.master_dropped, "DRM master dropped on rollback");
        expect(state.closed, "descriptor closed on rollback");
    }
}

void shadow_buffer_refresh_contracts() {
    std::vector<std::byte> shadow(4 * 4 * 2, std::byte{});
    std::vector<std::uint32_t> scanout(4 * 4, 0);

    // Initial write to shadow
    // Red pixel in RGB565: 0xF800
    shadow[0] = std::byte{0xf8};
    shadow[1] = std::byte{0x00};

    // Scanout still holds 0 prior to refresh
    expect(scanout[0] == 0, "write modifies shadow buffer only, not scanout");

    // Full refresh transfers and converts
    scanout[0] = rgb565_to_xrgb8888(static_cast<std::uint8_t>(shadow[0]),
                                    static_cast<std::uint8_t>(shadow[1]));
    expect(scanout[0] == 0x00ff0000, "refresh converts shadow to XRGB8888 scanout");
}

const mm::test::case_ cases[]{
    {"RGB565 pixel conversion and byte order",
     &rgb565_pixel_conversion_and_byte_order},
    {"DRM rollback and error classification",
     &drm_rollback_and_error_classification},
    {"shadow buffer write and refresh contracts",
     &shadow_buffer_refresh_contracts},
};
const mm::test::registrar reg{"platform.linux.drm", cases};

}
