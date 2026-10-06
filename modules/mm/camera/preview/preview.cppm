// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
// Project preview processing and controls; crop follows Waveshare main.c.
module;
#include <array>
#include <cstddef>
#include <cstdint>

export module mm.camera.preview;

export namespace mm::camera::preview {
inline constexpr unsigned sensor_width = 324, sensor_height = 324;
inline constexpr unsigned width = 240, height = 135;
using Frame = std::array<std::byte, sensor_width * sensor_height>;
using RgbImage = std::array<std::byte, width * height * 2>;
using Image = std::array<std::uint8_t, width * height>;
enum class Mode { Crop, Nearest, Area, Temporal };
[[nodiscard]] constexpr Mode next(Mode mode) {
    return mode == Mode::Crop ? Mode::Nearest :
           mode == Mode::Nearest ? Mode::Area :
           mode == Mode::Area ? Mode::Temporal : Mode::Crop;
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

// Crop uses Waveshare's x=0..239, y=134..0 window without resizing.
// Other modes resize the complete frame with the same vertical orientation.
// Coordinates must lie inside the fixed 240x135 output image.
[[nodiscard]] unsigned sample(const Frame& frame, unsigned x, unsigned y, Mode mode);
// Set history only when image contains the preceding output in the same mode.
void process(const Frame& frame, Image& image, Mode mode, bool history);
// Big-endian RGB565 bytes for a single complete LCD transfer.
void encode(const Image& image, RgbImage& pixels);
}
