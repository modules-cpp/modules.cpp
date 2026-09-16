// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
#include <cstddef>
#include <span>

import mm.display;

namespace {

class StandDisplay : public mm::display::Display {
public:
    [[nodiscard]] mm::display::Geometry geometry() const override {
        return {128, 296, 1};
    }

    [[nodiscard]] mm::display::Status initialize() override {
        initialized = true;
        sleeping = false;
        return mm::display::Status::Ok;
    }

    [[nodiscard]] mm::display::Status clear(mm::display::Color requested) override {
        if (!initialized || sleeping) return mm::display::Status::NotInitialized;
        color = requested;
        return mm::display::Status::Ok;
    }

    [[nodiscard]] mm::display::Status write(
        mm::display::Rectangle requested, std::span<const std::byte> bytes) override {
        if (!initialized || sleeping) return mm::display::Status::NotInitialized;
        rectangle = requested;
        size = bytes.size();
        return mm::display::Status::Ok;
    }

    [[nodiscard]] mm::display::Status refresh(mm::display::Refresh requested) override {
        if (!initialized || sleeping) return mm::display::Status::NotInitialized;
        refresh_mode = requested;
        return requested == mm::display::Refresh::Partial
                   ? mm::display::Status::Unsupported
                   : mm::display::Status::Ok;
    }

    [[nodiscard]] mm::display::Status sleep() override {
        if (!initialized) return mm::display::Status::NotInitialized;
        sleeping = true;
        return mm::display::Status::Ok;
    }

    void reset();

    bool initialized = false;
    bool sleeping = false;
    mm::display::Color color = mm::display::Color::White;
    mm::display::Refresh refresh_mode = mm::display::Refresh::Full;
    mm::display::Rectangle rectangle;
    std::size_t size = 0;
};

StandDisplay stand;

// A configured board may inject its own display provider into this binary,
// and its static registration may run after ours. Reclaim the seam so every
// case deterministically exercises the portable interface through this
// stand-in.
void StandDisplay::reset() {
    mm::display::set_display(*this);
    initialized = false;
    sleeping = false;
    color = mm::display::Color::White;
    refresh_mode = mm::display::Refresh::Full;
    rectangle = {};
    size = 0;
}

struct Register {
    Register() { mm::display::set_display(stand); }
};

const Register registered;

}

void mm_test_display_reset() { stand.reset(); }
bool mm_test_display_initialized() { return stand.initialized; }
bool mm_test_display_sleeping() { return stand.sleeping; }
std::size_t mm_test_display_size() { return stand.size; }
