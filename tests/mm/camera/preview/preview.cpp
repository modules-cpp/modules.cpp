// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
// Host regression test written for modules.cpp, not vendor code.
#include <cmath>
#include <array>
#include <cstddef>
#include <cstdint>
import mm.camera.preview;
import mm.test;

namespace {
namespace preview = mm::camera::preview;
using mm::test::expect;
preview::Frame frame;
preview::Image image;

// Independent floating-point rectangle intersection reference. Test rounding
// on a nonuniform image at all destination coordinates, including the edges.
unsigned reference(unsigned x, unsigned y) {
    y = preview::height - 1 - y;
    const double left = double(x) * preview::sensor_width / preview::width;
    const double right = double(x + 1) * preview::sensor_width / preview::width;
    const double top = double(y) * preview::sensor_height / preview::height;
    const double bottom = double(y + 1) * preview::sensor_height / preview::height;
    double total = 0;
    for (unsigned sy = static_cast<unsigned>(top); sy < std::ceil(bottom); ++sy)
        for (unsigned sx = static_cast<unsigned>(left); sx < std::ceil(right); ++sx) {
            const double w = std::fmin(right, sx + 1) - std::fmax(left, sx);
            const double h = std::fmin(bottom, sy + 1) - std::fmax(top, sy);
            total += std::to_integer<unsigned>(frame[sy * preview::sensor_width + sx]) * w * h;
        }
    // Exact half-integer means round up; floating coordinate arithmetic can
    // put them just below the tie. Epsilon is much smaller than one integer
    // weight quantum in the production sampler.
    return static_cast<unsigned>(std::floor(total / ((right - left) * (bottom - top)) +
                                            0.5 + 1e-8));
}
void sampling() {
    for (unsigned y = 0; y < preview::sensor_height; ++y)
        for (unsigned x = 0; x < preview::sensor_width; ++x)
            frame[y * preview::sensor_width + x] = std::byte((x * 53 + y * 97) % 256);
    for (unsigned y = 0; y < preview::height; ++y)
        for (unsigned x = 0; x < preview::width; ++x) {
            expect(preview::sample(frame, x, y, preview::Mode::Crop) ==
                std::to_integer<unsigned>(frame[(134 - y) * 324 + x]),
                "crop follows Waveshare's reversed rows, including row zero");
            expect(preview::sample(frame, x, y, preview::Mode::Nearest) ==
                std::to_integer<unsigned>(frame[((preview::height - 1 - y) * preview::sensor_height / preview::height) *
                                                preview::sensor_width +
                                                x * preview::sensor_width / preview::width]), "preview::sample(frame, x, y, preview::Mode::Nearest) == std::to_integer<unsigned>(frame[((preview::height - 1 - y) * preview::sensor_height / preview::height) * preview::sensor_width + x * preview::sensor_width / preview::width])");
            expect(preview::sample(frame, x, y, preview::Mode::Area) == reference(x, y), "preview::sample(frame, x, y, preview::Mode::Area) == reference(x, y)");
        }
    // Black/white DC levels must survive every resizing mode unchanged.
    for (const unsigned level : {0u, 255u}) {
        frame.fill(std::byte(level));
        for (auto mode : {preview::Mode::Crop, preview::Mode::Nearest, preview::Mode::Area, preview::Mode::Temporal}) {
            preview::process(frame, image, mode, false);
            for (auto pixel : image) expect(pixel == level, "pixel == level");
        }
    }
    expect(preview::next(preview::next(preview::next(preview::next(preview::Mode::Crop)))) ==
           preview::Mode::Crop, "four mode changes return to the default crop");
}
void temporal() {
    frame.fill(std::byte{0});
    preview::process(frame, image, preview::Mode::Temporal, false);
    frame.fill(std::byte{200});
    preview::process(frame, image, preview::Mode::Temporal, true);
    for (auto pixel : image) expect(pixel == 100, "pixel == 100");
    preview::process(frame, image, preview::Mode::Temporal, true);
    for (auto pixel : image) expect(pixel == 150, "pixel == 150");
    // Reset on restart/mode change must not blend a stale frame.
    frame.fill(std::byte{20});
    preview::process(frame, image, preview::Mode::Temporal, false);
    for (auto pixel : image) expect(pixel == 20, "pixel == 20");
    frame.fill(std::byte{80});
    preview::process(frame, image, preview::Mode::Area, true);
    for (auto pixel : image) expect(pixel == 80, "pixel == 80");
}
void encoding() {
    preview::RgbImage pixels{};
    for (std::size_t i = 0; i < image.size(); ++i)
        image[i] = static_cast<std::uint8_t>(i % 256);
    preview::encode(image, pixels);
    for (std::size_t i = 0; i < image.size(); ++i) {
        const unsigned value = std::to_integer<unsigned>(pixels[i * 2]) * 256 +
                               std::to_integer<unsigned>(pixels[i * 2 + 1]);
        expect((value >> 11) == (image[i] >> 3) &&
               ((value >> 5) & 63) == (image[i] >> 2) &&
               (value & 31) == (image[i] >> 3),
               "each complete-frame RGB565 pixel has the expected channel values");
    }
}
void buttons() {
    preview::Button button;
    // Held at boot, then a bouncing release.
    expect(!button.update(false, true, 0), "!button.update(false, true, 0)");
    expect(!button.update(true, false, 1), "!button.update(true, false, 1)");
    expect(!button.update(false, true, 5), "!button.update(false, true, 5)");
    expect(!button.update(true, false, 10), "!button.update(true, false, 10)");
    expect(!button.update(true, false, 39), "!button.update(true, false, 39)");
    expect(!button.update(true, false, 40), "!button.update(true, false, 40)");
    expect(button.update(false, true, 41), "button.update(false, true, 41)");
    // Press/release bounce and a long hold must produce just one action.
    expect(!button.update(true, false, 42), "!button.update(true, false, 42)");
    expect(!button.update(false, true, 43), "!button.update(false, true, 43)");
    expect(!button.update(false, false, 1000), "!button.update(false, false, 1000)");
    expect(!button.update(true, false, 1001), "!button.update(true, false, 1001)");
    expect(!button.update(true, true, 1005), "!button.update(true, true, 1005)");
    expect(!button.update(true, false, 1010), "!button.update(true, false, 1010)");
    expect(!button.update(true, false, 1040), "!button.update(true, false, 1040)");
    // Complete short tap while capture blocks: latch set, level high again.
    expect(button.update(true, true, 1100), "button.update(true, true, 1100)");
    expect(!button.update(true, false, 1101), "!button.update(true, false, 1101)");
    expect(!button.update(true, false, 1131), "!button.update(true, false, 1131)");
    expect(button.update(false, true, 1200), "button.update(false, true, 1200)");
    // Release interval across 32-bit timer wrap.
    expect(!button.update(true, false, 0xfffffff0u), "!button.update(true, false, 0xfffffff0u)");
    expect(!button.update(true, false, 0xdu), "!button.update(true, false, 0xdu)");
    expect(!button.update(true, false, 0xeu), "!button.update(true, false, 0xeu)");
    expect(button.update(false, true, 0xfu), "button.update(false, true, 0xfu)");
    // The two buttons retain independent debounce state.
    preview::Button other;
    expect(!other.update(true, false, 0), "!other.update(true, false, 0)");
    expect(!other.update(true, false, 30), "!other.update(true, false, 30)");
    expect(other.update(false, true, 31), "other.update(false, true, 31)");
    expect(!button.update(false, true, 32), "!button.update(false, true, 32)");
    // A press between gpio_take and gpio_read first appears as low level;
    // its delayed edge must not generate a second action.
    expect(!other.update(true, false, 40), "!other.update(true, false, 40)");
    expect(!other.update(true, false, 70), "!other.update(true, false, 70)");
    expect(other.update(false, false, 71), "other.update(false, false, 71)");
    expect(!other.update(false, true, 72), "!other.update(false, true, 72)");
}
const mm::test::case_ cases[] = {
    {"sampling", &sampling},
    {"temporal history", &temporal},
    {"complete-frame RGB565 encoding", &encoding},
    {"button debounce and edge retention", &buttons},
};
const mm::test::registrar reg{"mm.camera.preview", cases};
}
