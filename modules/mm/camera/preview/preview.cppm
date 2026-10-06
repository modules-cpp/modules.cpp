// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
// Preview processing and controls written for modules.cpp; not vendor code.
module;
#include <array>
#include <cstddef>
#include <cstdint>

export module mm.camera.preview;

export namespace mm::camera::preview {
inline constexpr unsigned sensor_width = 324, sensor_height = 244;
inline constexpr unsigned width = 240, height = 135;
using Frame = std::array<std::byte, sensor_width * sensor_height>;
using Image = std::array<std::uint8_t, width * height>;
enum class Mode { Nearest, Area, Temporal };
[[nodiscard]] constexpr Mode next(Mode mode) {
    return mode == Mode::Nearest ? Mode::Area :
           mode == Mode::Area ? Mode::Temporal : Mode::Nearest;
}

// Accept one press, then require 30 ms of observed release to rearm.
// Falling-edge latches retain short taps while capture/display blocks.
// A button held at startup must first be released.
struct Button {
    bool armed = false;
    bool releasing = false;
    std::uint32_t released_at = 0;
    [[nodiscard]] bool update(bool high, bool falling, std::uint32_t now);
};

// Coordinates must lie inside the fixed 240x135 output image.
[[nodiscard]] unsigned sample(const Frame& frame, unsigned x, unsigned y, Mode mode);
// Set history only when image contains the preceding output in the same mode.
void process(const Frame& frame, Image& image, Mode mode, bool history);
}
