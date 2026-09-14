// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
#include <array>
#include <cstddef>

import mm.display;
import mm.test;

void mm_test_display_reset();
bool mm_test_display_initialized();
bool mm_test_display_sleeping();
std::size_t mm_test_display_size();

namespace {

using mm::test::expect;

void describes_geometry() {
    mm_test_display_reset();
    const auto geometry = mm::display::selected_display().geometry();
    expect(geometry.width == 128 && geometry.height == 296 &&
               geometry.bits_per_pixel == 1,
           "the provider reports immutable display geometry");
}

void enforces_lifecycle_and_hands_off_a_packed_region() {
    mm_test_display_reset();
    auto& display = mm::display::selected_display();
    expect(display.clear(mm::display::Color::White) ==
               mm::display::Status::NotInitialized,
           "drawing before initialization is rejected");
    expect(display.initialize() == mm::display::Status::Ok &&
               mm_test_display_initialized(),
           "initialization reaches the provider");

    const std::array row{std::byte{0xaa}, std::byte{0x55}};
    expect(display.write({0, 0, 16, 1}, row) == mm::display::Status::Ok &&
               mm_test_display_size() == row.size(),
           "one packed row crosses the interface as one span");
    expect(display.refresh(mm::display::Refresh::Full) == mm::display::Status::Ok,
           "full refresh is the baseline mode");
    expect(display.refresh(mm::display::Refresh::Partial) ==
               mm::display::Status::Unsupported,
           "unsupported partial refresh is explicit");
    expect(display.sleep() == mm::display::Status::Ok && mm_test_display_sleeping(),
           "sleep leaves visible provider state");
}

void fallback_is_safe() {
    mm::display::Display bare;
    expect(bare.initialize() == mm::display::Status::Unsupported,
           "an unserved display is linkable and reports Unsupported");
    expect(bare.geometry().width == 0, "an unserved display has empty geometry");
}

const mm::test::case_ cases[] = {
    {"describes geometry", &describes_geometry},
    {"lifecycle and packed region", &enforces_lifecycle_and_hands_off_a_packed_region},
    {"fallback is safe", &fallback_is_safe},
};

const mm::test::registrar reg{"mm.display", cases};

}
